#pragma once

#include "esphome.h"
#include "zigbee_serial.h"

using esphome::App;
using esphome::delay;
using esphome::millis;
using esphome::zigbee_gateway::ZigbeeSerialInterface;

namespace esphome {
namespace zigbee_gateway {

// -------- UART helpers --------
// Generic UART utilities used by both ZNP and BSL code paths.

// Read a single byte with a deadline (milliseconds).
// - Non-blocking polling: only calls read_byte() when data is buffered.
// - Returns true iff one byte was consumed into *out* before timeout_ms expires.
// - Side effect: consumes exactly 1 byte on success; consumes nothing on timeout.
static inline bool uart_read_byte_t(ZigbeeSerialInterface *uart, uint8_t *out, uint32_t timeout_ms)
{
  uint32_t start = millis();
  while (millis() - start < timeout_ms)
  {
    if (uart->available())
    {
      if (uart->read_byte(out))
        return true;
    }
  }
  return false;
}

// Consume all currently buffered RX bytes.
// - Never waits for new data, never logs, never sleeps.
// - Safe to call before starting a fresh exchange to clear stale bytes.
static inline void uart_drain(ZigbeeSerialInterface *uart)
{
  uint8_t b;
  while (uart->available())
  {
    if (!uart->read_byte(&b))
      break;
  }
}

// -------- Core stream primitives --------
// Minimal building blocks shared by ZNP and BSL:
// - uart_seek_byte(): consume bytes until a marker (e.g., ZNP SOF 0xFE, BSL ACK 0xCC)
// - uart_read_exact_t(): read exactly N bytes with a per-byte timeout

// Consume bytes until a target value is seen or the timeout elapses.
// - Returns true iff *value* was read (and consumed) before timeout_ms.
// - Useful for seeking ZNP SOF (0xFE) or BSL ACK (0xCC).
// - Does not sleep; scans as fast as the loop runs.
static inline bool uart_seek_byte(ZigbeeSerialInterface *uart, uint8_t value, uint32_t timeout_ms)
{
  uint8_t b;
  uint32_t start = millis();
  while (millis() - start < timeout_ms)
  {
    if (uart->read_byte(&b))
    {
      if (b == value)
        return true;
    }
  }
  return false;
}

// Read exactly *len* bytes into *buf*, honoring a per-byte timeout.
// - Returns true only if all *len* bytes were read; leaves already-read bytes consumed.
// - Built on uart_read_byte_t(): will not engage read_byte() when RX is empty.
static inline bool uart_read_exact_t(ZigbeeSerialInterface *uart, uint8_t *buf, size_t len, uint32_t per_byte_timeout_ms)
{
  for (size_t i = 0; i < len; i++)
  {
    if (!uart_read_byte_t(uart, &buf[i], per_byte_timeout_ms))
      return false;
  }
  return true;
}

// -------- ZNP helpers --------
//
// ZNP frame: SOF(0xFE) | LEN | CMD0 | CMD1 | DATA[LEN] | FCS
// LEN counts only DATA bytes (FCS is separate). FCS = XOR of LEN, CMD0, CMD1 and all DATA.
// Helpers below send and receive ZNP frames and validate FCS. All functions consume bytes on success.

// Compute ZNP frame FCS (XOR of LEN, CMD0, CMD1 and all DATA bytes).
static inline uint8_t znp_fcs(uint8_t len, uint8_t cmd0, uint8_t cmd1, const uint8_t *data)
{
  uint8_t x = len ^ cmd0 ^ cmd1;
  for (uint8_t i = 0; i < len; i++)
    x ^= data[i];
  return x;
}

// Send one ZNP frame: writes SOF, LEN, CMD0, CMD1, DATA (if any), then FCS. Flushes at the end.
static inline void znp_send(ZigbeeSerialInterface *uart, uint8_t cmd0, uint8_t cmd1,
                            const uint8_t *data, uint8_t len)
{
  const uint8_t fcs = znp_fcs(len, cmd0, cmd1, data);
  const uint8_t hdr[4] = {0xFE, len, cmd0, cmd1};
  uart->write_array(hdr, 4);
  if (len)
    uart->write_array(data, len);
  uart->write_byte(fcs);
  uart->flush();
}

// Receive a single ZNP frame.
// - Skips any preamble/junk until SOF 0xFE (within start_timeout_ms).
// - Reads header, DATA[len], and FCS with per-byte timeouts.
// - Verifies FCS; returns true and fills outputs on success.
// - Returns false on timeout, malformed frame, or FCS mismatch.
static inline bool znp_recv_once(ZigbeeSerialInterface *uart,
                                 uint8_t *cmd0, uint8_t *cmd1,
                                 uint8_t *data, size_t data_capacity, uint8_t *len,
                                 uint32_t start_timeout_ms, uint32_t byte_timeout_ms)
{
  if (!uart_seek_byte(uart, 0xFE, start_timeout_ms))
    return false;

  if (!uart_read_exact_t(uart, len, 1, byte_timeout_ms))
    return false;
  if (!uart_read_exact_t(uart, cmd0, 1, byte_timeout_ms))
    return false;
  if (!uart_read_exact_t(uart, cmd1, 1, byte_timeout_ms))
    return false;

  if (*len > data_capacity)
  {
    ESP_LOGW("znp", "Dropping oversized frame payload (len=%u, capacity=%u)",
             (unsigned) *len, (unsigned) data_capacity);
    uint8_t discarded = 0;
    for (uint16_t i = 0; i < static_cast<uint16_t>(*len) + 1; i++)
    {
      if (!uart_read_byte_t(uart, &discarded, byte_timeout_ms))
        break;
    }
    return false;
  }

  if (!uart_read_exact_t(uart, data, *len, byte_timeout_ms))
    return false;

  uint8_t fcs = 0;
  if (!uart_read_exact_t(uart, &fcs, 1, byte_timeout_ms))
    return false;

  // Validate
  const uint8_t calc = znp_fcs(*len, *cmd0, *cmd1, data);
  if (fcs != calc)
    return false;

  return true;
}

// Receive frames until overall_timeout_ms expires, returning the first that matches *pred*.
// - pred signature: bool(cmd0, cmd1, const uint8_t* data, uint8_t len)
// - Non-matching frames are consumed and discarded.
// - Returns true and sets *out_len* when a matching frame is seen.
template <typename Pred>
static inline bool znp_recv_until(ZigbeeSerialInterface *uart,
                                  Pred pred,
                                  uint8_t *buf, size_t buf_capacity, uint8_t *out_len,
                                  uint32_t start_timeout_ms,
                                  uint32_t byte_timeout_ms,
                                  uint32_t overall_timeout_ms)
{
  const uint32_t start = millis();
  uint8_t c0 = 0, c1 = 0, len = 0;
  while (millis() - start < overall_timeout_ms)
  {
    if (znp_recv_once(uart, &c0, &c1, buf, buf_capacity, &len, start_timeout_ms, byte_timeout_ms))
    {
      if (pred(c0, c1, buf, len))
      {
        *out_len = len;
        return true;
      }
    }
  }
  return false;
}

// Execute a ZNP request/response exchange with retries and invoke a result handler on success:
// Issue a ZNP SREQ and wait for a matching SRSP, invoking a result handler on success.
// - Pre/Inter-attempt drains to clear stale RX bytes
// - Send SREQ (optional payload)
// - Optional post-send gap (busy-wait; avoids delay())
// - Wait for matching SRSP using znp_recv_until()
// - Drain after success as well
//
// Parameters:
//   uart                : UART device
//   sreq_cmd0/cmd1      : SREQ command header to send
//   exp_cmd0/exp_cmd1   : Expected SRSP header to match
//   on_match(data,len)  : Callback invoked with DATA[len] of the SRSP (FCS excluded)
//   scratch             : Caller-provided buffer to hold DATA (must be large enough for response)
//   scratch_capacity    : Size of scratch; oversized replies are rejected safely
//   start_timeout_ms    : Time to find SOF before header (per frame)
//   byte_timeout_ms     : Per-byte timeout inside a frame
//   overall_timeout_ms  : Overall wait budget for each attempt
//   attempts            : Number of SREQ→SRSP attempts before giving up
//   post_send_gap_ms    : Optional small gap after send (busy-wait)
//   sreq_data/sreq_len  : Optional SREQ payload
//
// Returns true iff a matching SRSP was received and on_match() executed.
// Side effects: consumes UART bytes, including unrelated frames during the wait window.
template <typename Handler>
static inline bool znp_exec(ZigbeeSerialInterface *uart,
                            uint8_t sreq_cmd0, uint8_t sreq_cmd1,
                            uint8_t exp_cmd0, uint8_t exp_cmd1,
                            Handler on_match,
                            uint8_t *scratch, size_t scratch_capacity,
                            uint32_t start_timeout_ms,
                            uint32_t byte_timeout_ms,
                            uint32_t overall_timeout_ms,
                            uint8_t attempts,
                            uint32_t post_send_gap_ms,
                            const uint8_t *sreq_data = nullptr,
                            uint8_t sreq_len = 0)
{
  for (uint8_t attempt = 1; attempt <= attempts; ++attempt)
  {
    // Clear any stale RX before (re)starting exchange
    uart_drain(uart);

    // Send SREQ
    znp_send(uart, sreq_cmd0, sreq_cmd1, sreq_data, sreq_len);

    // Optional post-send gap
    if (post_send_gap_ms)
    {
      delay(post_send_gap_ms);
    }

    // Wait for matching SRSP
    uint8_t len = 0;
    if (znp_recv_until(uart, [=](uint8_t c0, uint8_t c1, const uint8_t *, uint8_t)
                       { return c0 == exp_cmd0 && c1 == exp_cmd1; },
                       scratch, scratch_capacity, &len,
                       start_timeout_ms, byte_timeout_ms, overall_timeout_ms))
    {
      // Success → allow caller to parse DATA and publish
      on_match(scratch, len);

      // Drain any leftovers (AREQs, etc.) before returning
      uart_drain(uart);
      return true;
    }

    // Attempt failed → post-attempt drain before retrying
    uart_drain(uart);
  }

  return false;
}

// -------- Endian helpers --------
// CC26xx/CC13xx ROM BSL uses big-endian (MSB-first) for all multi-byte fields on the wire.
// Use these helpers for any 16/32-bit values placed into or read from BSL frames.
static inline void encode_u32_be(uint32_t v, uint8_t out[4])
{
  out[0] = static_cast<uint8_t>((v >> 24) & 0xFF);
  out[1] = static_cast<uint8_t>((v >> 16) & 0xFF);
  out[2] = static_cast<uint8_t>((v >> 8) & 0xFF);
  out[3] = static_cast<uint8_t>(v & 0xFF);
}

static inline uint32_t decode_u32_be(const uint8_t in[4])
{
  return (static_cast<uint32_t>(in[0]) << 24) |
         (static_cast<uint32_t>(in[1]) << 16) |
         (static_cast<uint32_t>(in[2]) << 8) |
         (static_cast<uint32_t>(in[3]) << 0);
}

// -------- BSL helpers --------
// Send host ACK for a received BSL packet (two bytes: 0x00, 0xCC).
static inline void bsl_host_ack(ZigbeeSerialInterface *uart)
{
  const uint8_t a[2] = {0x00, 0xCC};
  uart->write_array(a, 2);
  uart->flush();
  ESP_LOGV("bsl", "host-ACK sent");
}

// Issue BSL GET_STATUS (0x23) and acknowledge its reply.
// Returns true if a well-formed status packet is received and ACKed; writes first status byte to *status (if non-null).
static inline bool bsl_get_status(ZigbeeSerialInterface *uart,
                                  uint32_t ack_timeout_ms,
                                  uint32_t header_timeout_ms,
                                  uint32_t payload_timeout_ms,
                                  uint8_t *status)
{
  ESP_LOGV("bsl", "GET_STATUS: send [03 23 23]");
  // Framed command: [SIZE=3][CHK=0x23] + {0x23}
  const uint8_t cmd[3] = {0x03, 0x23, 0x23};

  // Send command
  uart->write_array(cmd, sizeof(cmd));
  uart->flush();

  // Expect ACK (0xCC)
  if (!uart_seek_byte(uart, 0xCC, ack_timeout_ms))
  {
    ESP_LOGV("bsl", "GET_STATUS: ACK timeout (%u ms)", ack_timeout_ms);
    return false;
  }

  // Header: [packet_len][checksum]
  uint8_t hdr[2] = {0};
  if (!uart_read_exact_t(uart, hdr, 2, header_timeout_ms))
    return false;
  const uint8_t packet_len = hdr[0];
  const uint8_t chk = hdr[1];
  ESP_LOGV("bsl", "GET_STATUS: hdr len=%u chk=%02X", packet_len, chk);
  if (packet_len < 2)
    return false;

  // Payload
  const uint8_t payload_len = static_cast<uint8_t>(packet_len - 2);
  uint8_t buf[8] = {0}; // status is typically 1 byte; small buffer is enough
  if (payload_len > sizeof(buf))
    return false;
  if (!uart_read_exact_t(uart, buf, payload_len, payload_timeout_ms))
    return false;
  ESP_LOGV("bsl", "GET_STATUS: payload_len=%u val=%02X", payload_len, (payload_len ? buf[0] : 0));

  // Verify checksum
  uint32_t sum = 0;
  for (uint8_t i = 0; i < payload_len; i++)
    sum += buf[i];
  if (static_cast<uint8_t>(sum & 0xFF) != chk)
  {
    ESP_LOGV("bsl", "GET_STATUS: checksum mismatch");
    return false;
  }

  // Host ACK the status packet
  bsl_host_ack(uart);

  if (status && payload_len >= 1)
    *status = buf[0];

  ESP_LOGV("bsl", "GET_STATUS: ok status=%02X", (payload_len ? buf[0] : 0));
  return true;
}
// BSL protocol (CC26xx/CC13xx ROM bootloader):
// - ACK byte: 0xCC
// - Response packet: [packet_len][checksum] + PAYLOAD[packet_len - 2]
//   * packet_len includes the 2-byte header
//   * checksum is sum(payload) modulo 256

// Enter/align with BSL by sending two 0x55 bytes with a small gap.
// - Drains RX first to remove stale bytes.
// - Optionally waits for an ACK (0xCC) after the second 0x55.
static inline bool bsl_sync(ZigbeeSerialInterface *uart, uint32_t ack_timeout_ms, uint32_t sync_gap_ms)
{
  uart_drain(uart);
  uart->write_byte(0x55);
  uart->flush();

  // Inter-byte gap
  delay(sync_gap_ms);

  uart->write_byte(0x55);
  uart->flush();
  (void)uart_seek_byte(uart, 0xCC, ack_timeout_ms); // optional ACK
  return true;
}

// Execute a BSL command and process its reply.
// Sequence:
// - Write the command bytes as provided
// - Read one status byte and require ACK (0xCC); any other value fails
// - Read header: [packet_len][checksum]
// - Read payload: PAYLOAD[packet_len - 2]
// - Verify checksum: sum(payload) % 256 == checksum
// - On success, invoke on_reply(payload, payload_len) and return true
//
// Parameters:
//   uart                 : UART device
//   cmd/cmd_len          : Command bytes to send verbatim
//   on_reply(payload,len): Callback invoked with the validated payload
//   scratch/capacity     : Buffer for payload storage (must fit response)
//   ack_timeout_ms       : Timeout for the single status byte (ACK)
//   header_timeout_ms    : Per-byte timeout for the 2-byte header
//   payload_timeout_ms   : Per-byte timeout for the payload
//   attempts             : Number of send→reply attempts (default 1)
//   post_send_gap_ms     : Optional short pause after send (ms)
//
// Returns true iff the reply was received, validated, and on_reply() executed.
template <typename Handler>
static inline bool bsl_exec(ZigbeeSerialInterface *uart,
                            const uint8_t *cmd, uint8_t cmd_len,
                            Handler on_reply,
                            uint8_t *scratch, uint8_t scratch_capacity,
                            uint32_t ack_timeout_ms,
                            uint32_t header_timeout_ms,
                            uint32_t payload_timeout_ms,
                            uint8_t attempts = 1)
{
  for (uint8_t attempt = 1; attempt <= attempts; ++attempt)
  {
    {
      char tx[64];
      int n = 0;
      n += snprintf(tx + n, sizeof(tx) - n, "TX(%u): ", attempt);
      for (uint8_t i = 0; i < cmd_len && i < 12 && n < (int)sizeof(tx) - 3; i++)
        n += snprintf(tx + n, sizeof(tx) - n, "%02X ", cmd[i]);
      ESP_LOGV("bsl", "%s(len=%u)", tx, cmd_len);
    }
    // Send command
    uart->write_array(cmd, cmd_len);
    uart->flush();

    uint32_t t_ack = millis();
    // Expect an ACK (0xCC) somewhere next; skip anything else until it appears
    if (!uart_seek_byte(uart, 0xCC, ack_timeout_ms))
    {
      ESP_LOGV("bsl", "ACK timeout after %u ms", ack_timeout_ms);
      continue; // timeout → retry
    }
    ESP_LOGV("bsl", "ACK in %u ms", (unsigned)(millis() - t_ack));

    // Header: [packet_len][checksum]
    uint8_t hdr[2] = {0};
    if (!uart_read_exact_t(uart, hdr, 2, header_timeout_ms))
      continue;
    const uint8_t packet_len = hdr[0];
    const uint8_t chk = hdr[1];
    ESP_LOGV("bsl", "HDR: len=%u chk=%02X", packet_len, chk);
    if (packet_len < 2)
      continue;

    // Payload
    const uint8_t payload_len = static_cast<uint8_t>(packet_len - 2);
    if (payload_len == 0)
    {
      ESP_LOGV("bsl", "Empty payload");
      continue;
    }
    if (payload_len > scratch_capacity)
    {
      ESP_LOGW("bsl", "Dropping oversized reply payload (len=%u, capacity=%u)",
               (unsigned) payload_len, (unsigned) scratch_capacity);
      uint8_t discarded = 0;
      for (uint8_t i = 0; i < payload_len; i++)
      {
        if (!uart_read_byte_t(uart, &discarded, payload_timeout_ms))
          break;
      }
      continue;
    }
    if (!uart_read_exact_t(uart, scratch, payload_len, payload_timeout_ms))
      continue;
    ESP_LOGV("bsl", "Payload: %u bytes", payload_len);

    // Checksum: sum(payload) % 256 must equal chk
    uint32_t sum = 0;
    for (uint8_t i = 0; i < payload_len; i++)
      sum += scratch[i];
    const uint8_t calc = static_cast<uint8_t>(sum & 0xFF);
    if (calc != chk)
    {
      ESP_LOGV("bsl", "Checksum mismatch: got %02X calc %02X", chk, calc);
      continue;
    }

    // Acknowledge the received packet to the ROM
    bsl_host_ack(uart);
    ESP_LOGV("bsl", "RX packet ACKed");

    on_reply(scratch, payload_len);
    return true;
  }

  return false;
}

// Read memory via the BSL "MEMORY READ" command (opcode 0x2A).
// - Builds the framed request: [SIZE][CHK] + {0x2A, ADDR[4] BE, WIDTH, COUNT} and delegates to bsl_exec().
// - width is the access size in bytes: 1, 2, or 4.
// - count is the number of elements of that width to read (payload size = width * count).
// - On success, copies the returned payload into *out and writes its length to *out_len.
//
// Parameters:
//   uart                 : UART device
//   addr                 : Start address (little-endian in the command payload)
//   width                : Access width in bytes (1, 2, or 4)
//   count                : Number of elements to read (payload = width * count)
//   out/out_capacity     : Destination buffer and its capacity (must be large enough)
//   out_len              : Receives payload length on success
//   ack/header/payload   : Timeouts for the respective phases
//   attempts             : Number of attempts (default 1)
//
// Returns true iff a valid reply was received and copied into *out.
static inline bool bsl_mem_read(ZigbeeSerialInterface *uart,
                                uint32_t addr, uint8_t width, uint8_t count,
                                uint8_t *out, uint8_t out_capacity, uint8_t *out_len,
                                uint32_t ack_timeout_ms,
                                uint32_t header_timeout_ms,
                                uint32_t payload_timeout_ms,
                                uint8_t attempts = 1)
{
  ESP_LOGV("bsl", "MEM_READ addr=0x%08X width=%u count=%u", (unsigned)addr, width, count);
  // Data bytes for MEM_READ: { 0x2A, ADDR0,ADDR1,ADDR2,ADDR3, WIDTH, COUNT }
  uint8_t data[7];
  data[0] = 0x2A;
  encode_u32_be(addr, &data[1]);
  data[5] = width; // access width (protocol-specific: typically 1,2,4)
  data[6] = count; // number of elements

  // Frame header: SIZE (data bytes + 2), CHK (sum of data bytes % 256)
  uint8_t size = static_cast<uint8_t>(sizeof(data) + 2); // 7 + 2 = 9
  uint32_t sum = 0;
  for (uint8_t i = 0; i < sizeof(data); i++)
    sum += data[i];
  uint8_t chk = static_cast<uint8_t>(sum & 0xFF);

  // Build complete command buffer [SIZE][CHK] + data[7]
  uint8_t cmd[9];
  cmd[0] = size;
  cmd[1] = chk;
  for (uint8_t i = 0; i < sizeof(data); i++)
    cmd[2 + i] = data[i];
  {
    char tx[64];
    int n = snprintf(tx, sizeof(tx), "CMD: ");
    for (uint8_t i = 0; i < sizeof(cmd) && n < (int)sizeof(tx) - 3; i++)
      n += snprintf(tx + n, sizeof(tx) - n, "%02X ", cmd[i]);
    ESP_LOGV("bsl", "%s", tx);
  }

  uint8_t tmp_len = 0;
  bool ok = bsl_exec(
      uart,
      cmd, sizeof(cmd),
      [&](const uint8_t *payload, uint8_t len)
      {
        tmp_len = len;
        if (tmp_len > out_capacity)
          tmp_len = out_capacity;
        for (uint8_t i = 0; i < tmp_len; i++)
          out[i] = payload[i];
      },
      out, out_capacity,
      ack_timeout_ms, header_timeout_ms, payload_timeout_ms,
      attempts);

  if (!ok)
  {
    ESP_LOGV("bsl", "MEM_READ failed");
    return false;
  }
  if (out_len)
    *out_len = tmp_len;
  ESP_LOGV("bsl", "MEM_READ ok len=%u", (unsigned)tmp_len);
  return true;
}

// Read consecutive 32-bit words from memory using the BSL MEM_READ command.
// - words: number of 32-bit words to read (payload = words * 4 bytes).
// - out:   caller-provided buffer; must be at least words*4 bytes.
// - name:  optional tag for debug logging; may be nullptr to suppress logs.
// - Uses ack/header/payload timeouts as for bsl_mem_read() and a single attempt.
// Returns true on success (full words*4 bytes read), false on failure.
static inline bool bsl_read_words(ZigbeeSerialInterface *uart,
                                  uint32_t addr, uint8_t words,
                                  uint8_t *out, const char *name,
                                  uint32_t ack_timeout_ms,
                                  uint32_t header_timeout_ms,
                                  uint32_t payload_timeout_ms)
{
  uint8_t blen = 0;
  const uint8_t capacity = static_cast<uint8_t>(words * 4);
  bool ok = bsl_mem_read(
      uart, addr, /*width=*/1, /*count=*/words,
      out, capacity, &blen,
      ack_timeout_ms, header_timeout_ms, payload_timeout_ms,
      1);
  if (!ok || blen < capacity)
  {
    ESP_LOGW("bsl", "MEM_READ %s @0x%08X FAILED (len=%u need=%u)",
             (name ? name : ""), (unsigned)addr,
             (unsigned)blen, (unsigned)capacity);
    return false;
  }
  if (name)
  {
    ESP_LOGD("bsl",
             "MEM_READ %s @0x%08X -> %02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X %02X",
             name, (unsigned)addr,
             out[0], out[1], out[2], out[3],
             out[4], out[5], out[6], out[7],
             out[8], out[9], out[10], out[11],
             out[12], out[13], out[14], out[15]);
  }
  return true;
}

// Read an arbitrary byte range from memory using BSL MEM_READ, with a 248-byte chunk cap.
// - addr: start address.
// - n:    number of bytes to read.
// - out:  caller-provided buffer of at least n bytes.
// - name: optional tag for debug logging; may be nullptr to suppress logs.
// - Uses ack/header/payload timeouts as for bsl_mem_read() and a single attempt.
//   Internally performs multiple aligned MEM_READ calls with up to 63 words (252 bytes),
//   but caps each logical chunk at 248 bytes to match typical ROM BSL constraints.
// Returns true on success, false on any MEM_READ failure.
static inline bool bsl_read_bytes(ZigbeeSerialInterface *uart,
                                  uint32_t addr, uint32_t n,
                                  uint8_t *out, const char *name,
                                  uint32_t ack_timeout_ms,
                                  uint32_t header_timeout_ms,
                                  uint32_t payload_timeout_ms)
{
  if (n == 0)
    return true;

  const uint32_t CHUNK_MAX = 248; // keep under 63-word (252-byte) ROM BSL limit
  const uint8_t WORD_MAX = 63;
  uint32_t pos = 0;

  while (pos < n)
  {
    uint32_t cur_addr = addr + pos;
    uint32_t aligned = cur_addr & ~0x3u;  // 4-byte aligned base
    uint32_t prefix = cur_addr - aligned; // 0..3 bytes before requested start

    uint32_t hard_cap = WORD_MAX * 4;
    uint32_t room_cap = (hard_cap > prefix) ? (hard_cap - prefix) : 0;
    uint32_t room_user = (CHUNK_MAX > prefix) ? (CHUNK_MAX - prefix) : 0;
    uint32_t room = (room_cap < room_user) ? room_cap : room_user;

    uint32_t need = n - pos;
    uint32_t take = (need < room) ? need : room;

    uint32_t total = prefix + take;
    uint8_t words = static_cast<uint8_t>((total + 3) / 4);
    if (words > WORD_MAX)
      words = WORD_MAX;
    uint32_t max_bytes = static_cast<uint32_t>(words) * 4;

    uint8_t tmp[248];
    uint8_t blen = 0;
    bool ok = bsl_mem_read(
        uart, aligned, /*width=*/1, /*count=*/words,
        tmp, static_cast<uint8_t>(max_bytes), &blen,
        ack_timeout_ms, header_timeout_ms, payload_timeout_ms,
        1);
    if (!ok || blen < max_bytes)
    {
      ESP_LOGW("bsl", "MEM_READ bytes %s @0x%08X FAILED (len=%u need=%u)",
               (name ? name : ""), (unsigned)aligned,
               (unsigned)blen, (unsigned)max_bytes);
      return false;
    }

    for (uint32_t i = 0; i < take; i++)
      out[pos + i] = tmp[prefix + i];

    pos += take;
  }

  if (name)
  {
    if (n <= 16)
    {
      char line[96];
      size_t p = 0;
      line[0] = '\0';
      for (uint32_t i = 0; i < n && p + 3 < sizeof(line); i++)
      {
        p += snprintf(line + p, sizeof(line) - p, "%02X", out[i]);
        if (i + 1 < n)
          p += snprintf(line + p, sizeof(line) - p, ((i & 3) == 3) ? "  " : " ");
      }
      ESP_LOGV("bsl", "MEM_READ %s @0x%08X -> %s", name, (unsigned)addr, line);
    }
    else
    {
      uint8_t f[16], l[16];
      for (int i = 0; i < 16; i++)
      {
        f[i] = out[i];
        l[i] = out[n - 16 + i];
      }
      ESP_LOGV("bsl",
               "MEM_READ %s @0x%08X len=%u first16 -> %02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X %02X",
               name, (unsigned)addr, (unsigned)n,
               f[0], f[1], f[2], f[3],
               f[4], f[5], f[6], f[7],
               f[8], f[9], f[10], f[11],
               f[12], f[13], f[14], f[15]);
      ESP_LOGV("bsl",
               "MEM_READ %s @0x%08X len=%u last16  -> %02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X %02X",
               name, (unsigned)(addr + n - 16), (unsigned)n,
               l[0], l[1], l[2], l[3],
               l[4], l[5], l[6], l[7],
               l[8], l[9], l[10], l[11],
               l[12], l[13], l[14], l[15]);
    }
  }

  return true;
}

// -------- Zigbee NVOCMP (flash NV) backend helpers --------
//
// These helpers implement the low-level scan of the Zigbee NVOCMP region used by
// Z-Stack 3.x (Koenkk builds on CC13x2/CC26x2/CC26x2x7). YAML code should only
// describe *which* NV items are interesting and provide small per-item callbacks
// that interpret their payloads and publish sensors. All the heavy lifting
// (page discovery, item streaming, CRC) stays here.
//
// NVOCMP layout (Koenkk variant used by zigbee2mqtt routers/coordinators):
//
//   - Flash region is a contiguous range [nv_base .. nv_base+nv_size).
//   - It is subdivided into flash pages supplied by the detected chip/firmware
//     layout. The supported Koenkk x2 and x2x7 profiles both use 8 KiB
//     (0x2000), but the scanner does not infer or hardcode that geometry.
//   - Each page starts with a 4-byte page header followed by 3 compact headers,
//     then a data area where items are appended from the end of the page down
//     towards offset 0x0010.
//
// Page header (4 bytes at the start of each NV page):
//   [0] PST : Page state
//       0x7C = ACT   (active head page)
//       0x78 = FULL  (active, full)
//       0x70 = XSRC  (compaction source)
//       0x7E = RDY   (formatted/available)
//       0xFE = XDST  (compaction destination)
//       0xFF = NACT  (erased/unformatted)
//       0x00 = NDEF  (undefined)
//   [1] CYC : Cycle counter (monotonic; 0xFF when erased)
//   [2] VER : Version/flags (SDK-dependent). Informational only.
//   [3] SIG : Signature (expect 0x96)
//
// Compact headers (3 × 4 bytes immediately after page header):
//   struct { uint16_t pageOffset; uint8_t page; uint8_t signature; }
//   page = 0xF8 → SRC, 0xFE → DST, 0xFC → DONE, 0xFF with off=0xFFFF → empty slot.
//   page in 0..(N-1) → reference to a specific NV page.
//
// Page body layout:
//   [0x0000..0x0003]  Page Header (PST, CYC, VER, SIG=0x96)
//   [0x0004..0x000F]  Compact Headers (3 × {off[15:0], page[7:0], sig=0x96})
//   [0x0010.......]   NV items region (packed toward lower addresses)
//
// Items are appended from the end of the page towards 0x0010. Each item has:
//   [DATA ...][HEADER7]
// where `HEADER7` is a 7-byte header ending with 0x96, and DATA is a variable-
// length payload immediately preceding the header.
//
// Item header (7 bytes, non-LE bit packing; h[6] == 0x96):
//   Let h[0]..h[6] be the bytes of the header:
//     sysid  (6b) = (h[0] >> 2) & 0x3F              // subsystem (Z-Stack = 1)
//     item   (10b)= ((h[0] & 0x03) << 8) | h[1]     // base NV item id
//     subid  (10b)= (h[2] << 2) | ((h[3] >> 6) & 0x03)
//     len    (12b)= ((h[3] & 0x3F) << 6) | ((h[4] >> 2) & 0x3F)
//     crc8   ( 8b)= ((h[4] & 0x03) << 6) | ((h[5] >> 2) & 0x3F)
//     status ( 2b)=  h[5] & 0x03                    // bit1=ACTIVE; bit0 participates in CRC encoding
//     sig    ( 8b)=  h[6]                           // constant 0x96
//
// At runtime we apply three filters in sequence:
//   1) header sanity (sys/item/sub/len ranges, item fully inside page)
//   2) ACTIVE bit (status bit1) must be set
//   3) CRC-8 must match the on-flash header value
//
// Only items that pass all three are offered to the caller via a generic
// callback that receives (index in "wanted" list, header, payload buf, length).

struct NzgNvItemHeader
{
  uint8_t sys;
  uint16_t item;
  uint16_t sub;
  uint16_t len;
  uint8_t stats;
  uint8_t crc;
  uint8_t sig;
  uint8_t page_idx;
  uint32_t data_addr;
  uint32_t hdr_addr;
};

struct NzgNvWanted
{
  uint8_t sys;
  uint16_t item;
  uint16_t sub;
  const char *name;
  bool done;
};

struct NzgNvScanConfig
{
  uint32_t nv_base;            // start of NV region (flash address)
  uint32_t nv_size;            // size of NV region in bytes
  uint32_t page_size;          // NVOCMP page size selected from chip layout
  uint32_t ack_timeout_ms;     // BSL ACK phase timeout
  uint32_t header_timeout_ms;  // BSL header phase timeout
  uint32_t payload_timeout_ms; // BSL payload phase timeout
};

// TI NVOCMP CRC-8 table (poly=0x97, init=0x00).
// Feed model for one item: CRC over [payload bytes] + [header bytes 0..3] +
// then one synthetic length byte: ((len & 0x3F) << 2).
static const uint8_t NZG_NV_CRC8_TABLE[256] = {
    0x00, 0x97, 0xb9, 0x2e, 0xe5, 0x72, 0x5c, 0xcb, 0x5d, 0xca, 0xe4, 0x73, 0xb8, 0x2f, 0x01, 0x96,
    0xba, 0x2d, 0x03, 0x94, 0x5f, 0xc8, 0xe6, 0x71, 0xe7, 0x70, 0x5e, 0xc9, 0x02, 0x95, 0xbb, 0x2c,
    0xe3, 0x74, 0x5a, 0xcd, 0x06, 0x91, 0xbf, 0x28, 0xbe, 0x29, 0x07, 0x90, 0x5b, 0xcc, 0xe2, 0x75,
    0x59, 0xce, 0xe0, 0x77, 0xbc, 0x2b, 0x05, 0x92, 0x04, 0x93, 0xbd, 0x2a, 0xe1, 0x76, 0x58, 0xcf,
    0x51, 0xc6, 0xe8, 0x7f, 0xb4, 0x23, 0x0d, 0x9a, 0x0c, 0x9b, 0xb5, 0x22, 0xe9, 0x7e, 0x50, 0xc7,
    0xeb, 0x7c, 0x52, 0xc5, 0x0e, 0x99, 0xb7, 0x20, 0xb6, 0x21, 0x0f, 0x98, 0x53, 0xc4, 0xea, 0x7d,
    0xb2, 0x25, 0x0b, 0x9c, 0x57, 0xc0, 0xee, 0x79, 0xef, 0x78, 0x56, 0xc1, 0x0a, 0x9d, 0xb3, 0x24,
    0x08, 0x9f, 0xb1, 0x26, 0xed, 0x7a, 0x54, 0xc3, 0x55, 0xc2, 0xec, 0x7b, 0xb0, 0x27, 0x09, 0x9e,
    0xa2, 0x35, 0x1b, 0x8c, 0x47, 0xd0, 0xfe, 0x69, 0xff, 0x68, 0x46, 0xd1, 0x1a, 0x8d, 0xa3, 0x34,
    0x18, 0x8f, 0xa1, 0x36, 0xfd, 0x6a, 0x44, 0xd3, 0x45, 0xd2, 0xfc, 0x6b, 0xa0, 0x37, 0x19, 0x8e,
    0x41, 0xd6, 0xf8, 0x6f, 0xa4, 0x33, 0x1d, 0x8a, 0x1c, 0x8b, 0xa5, 0x32, 0xf9, 0x6e, 0x40, 0xd7,
    0xfb, 0x6c, 0x42, 0xd5, 0x1e, 0x89, 0xa7, 0x30, 0xa6, 0x31, 0x1f, 0x88, 0x43, 0xd4, 0xfa, 0x6d,
    0xf3, 0x64, 0x4a, 0xdd, 0x16, 0x81, 0xaf, 0x38, 0xae, 0x39, 0x17, 0x80, 0x4b, 0xdc, 0xf2, 0x65,
    0x49, 0xde, 0xf0, 0x67, 0xac, 0x3b, 0x15, 0x82, 0x14, 0x83, 0xad, 0x3a, 0xf1, 0x66, 0x48, 0xdf,
    0x10, 0x87, 0xa9, 0x3e, 0xf5, 0x62, 0x4c, 0xdb, 0x4d, 0xda, 0xf4, 0x63, 0xa8, 0x3f, 0x11, 0x86,
    0xaa, 0x3d, 0x13, 0x84, 0x4f, 0xd8, 0xf6, 0x61, 0xf7, 0x60, 0x4e, 0xd9, 0x12, 0x85, 0xab, 0x3c};

static inline uint8_t nzg_nv_crc8(const uint8_t *data, uint32_t len, uint8_t init)
{
  uint8_t crc = init;
  for (uint32_t i = 0; i < len; i++)
  {
    crc = NZG_NV_CRC8_TABLE[crc ^ data[i]];
  }
  return crc;
}

// Generic NV scan + dispatch:
//   - Scans NV pages from newest to oldest
//   - Walks items backwards inside each page
//   - Filters by ACTIVE bit and CRC-8
//   - For each matching item (by sys/item/sub) invokes cb(idx, hdr, buf_le, len)
//     where idx is the index into the "wanted" array.
//
// Parameters:
//   uart        : UART used for BSL MEM_READ access
//   cfg         : NV region base/size and BSL timeouts
//   wanted      : array of NzgNvWanted descriptors (mutated: .done is set on hits)
//   wanted_count: number of entries in wanted[]
//   cb          : frontend callback, signature:
//                   void(int idx, const NzgNvItemHeader&, const uint8_t *buf_le, uint16_t len)
//
// Behaviour:
//   - Stops early once all wanted[].done are true
//   - Logs basic page classification and active-page order
//   - Logs CRC mismatches and NOT FOUND items
//
template <typename FrontendCb>
static inline void nzg_nv_scan_and_dispatch(ZigbeeSerialInterface *uart,
                                            const NzgNvScanConfig &cfg,
                                            NzgNvWanted *wanted,
                                            int wanted_count,
                                            FrontendCb cb)
{
  const uint8_t NV_SIG = 0x96;

  if (cfg.page_size < 16 || cfg.nv_size == 0 ||
      cfg.nv_base % cfg.page_size != 0 ||
      cfg.nv_size % cfg.page_size != 0)
  {
    ESP_LOGE("bsl",
             "Invalid NV geometry: base=0x%06X size=0x%04X page=0x%04X",
             (unsigned)cfg.nv_base, (unsigned)cfg.nv_size,
             (unsigned)cfg.page_size);
    return;
  }

  // NVOCMP page states
  const uint8_t PST_ACT = 0x7C;  // active (head)
  const uint8_t PST_FULL = 0x78; // active, full
  const uint8_t PST_XSRC = 0x70; // compaction source
  const uint8_t PST_RDY = 0x7E;  // formatted/available
  const uint8_t PST_XDST = 0xFE; // compaction destination
  const uint8_t PST_NACT = 0xFF; // erased/unformatted
  const uint8_t PST_NDEF = 0x00; // undefined

  auto pst_name = [&](uint8_t pst) -> const char *
  {
    switch (pst)
    {
    case PST_ACT:
      return "ACT";
    case PST_FULL:
      return "FULL";
    case PST_XSRC:
      return "XSRC";
    case PST_RDY:
      return "RDY";
    case PST_XDST:
      return "XDST";
    case PST_NACT:
      return "NACT";
    case PST_NDEF:
      return "NDEF";
    default:
      return "UNK";
    }
  };

  // Helper to read a single NV item payload in natural on-flash order (LSB-first for
  // multi-byte values). We read at most max_copy bytes into buf_le.
  auto read_nv_payload = [&](uint32_t addr,
                             uint16_t len,
                             uint8_t *buf_le,
                             uint16_t max_copy,
                             uint16_t *out_len) -> bool
  {
    uint16_t copy_len = len;
    if (copy_len > max_copy)
      copy_len = max_copy;

    if (!bsl_read_bytes(uart,
                        addr,
                        copy_len,
                        buf_le,
                        "NVITEM_DATA",
                        cfg.ack_timeout_ms,
                        cfg.header_timeout_ms,
                        cfg.payload_timeout_ms))
    {
      return false;
    }

    *out_len = copy_len;
    return true;
  };

  const uint32_t pages = (cfg.nv_size / cfg.page_size);
  char summary[32] = {0};
  uint32_t sum_i = 0;

  struct NvPage
  {
    uint8_t idx;
    uint8_t pst;
    uint8_t cyc;
    uint32_t base;
  };

  NvPage active[8];
  uint8_t act_n = 0;

  // Phase 1: scan page headers and classify active pages
  for (uint32_t i = 0; i < pages; i++)
  {
    const uint32_t pg_base = cfg.nv_base + i * cfg.page_size;

    uint8_t hdr[16] = {0};
    if (!bsl_read_words(uart,
                        pg_base + 0,
                        /*words=*/4,
                        hdr,
                        "NVPG_HDR+COMPACT",
                        cfg.ack_timeout_ms,
                        cfg.header_timeout_ms,
                        cfg.payload_timeout_ms))
    {
      if (sum_i < sizeof(summary) - 1)
        summary[sum_i++] = '?';
      continue;
    }

    const uint8_t pst = hdr[0];
    const uint8_t cyc = hdr[1];
    const uint8_t verAll = hdr[2];
    const uint8_t sig = hdr[3];
    const uint8_t ver = (uint8_t)(verAll & 0x3F);

    char comp[96];
    size_t cp = 0;
    comp[0] = '\0';
    bool any_comp = false;
    for (int k = 0; k < 3; k++)
    {
      const int o = 4 + (k * 4);
      const uint16_t coff = (uint16_t)(hdr[o + 0] | (uint16_t)(hdr[o + 1] << 8));
      const uint8_t cpg = hdr[o + 2];

      // Empty slot convention: page==0xFF and offset==0xFFFF
      if (cpg == 0xFF && coff == 0xFFFF)
        continue;
      any_comp = true;

      cp += snprintf(comp + cp, sizeof(comp) - cp,
                     "%s[%d]=p%u@0x%04X",
                     (cp ? "; " : ""),
                     k,
                     (unsigned)cpg,
                     (unsigned)coff);
    }
    if (!any_comp)
      snprintf(comp, sizeof(comp), "comp=none");

    ESP_LOGD("bsl", "NV pg%u@0x%06X: %s c=0x%02X ver=0x%02X sig=%s | %s",
             (unsigned)i, (unsigned)pg_base, pst_name(pst), cyc, ver,
             (sig == NV_SIG ? "ok" : "bad"), comp);

    char cls_char = 'E';
    if (sig == NV_SIG)
    {
      if (pst == PST_ACT || pst == PST_FULL || pst == PST_XSRC)
        cls_char = 'A';
      else if (pst == PST_RDY || pst == PST_XDST || pst == PST_NACT || pst == PST_NDEF)
        cls_char = 'I';
    }
    if (sum_i < sizeof(summary) - 1)
      summary[sum_i++] = cls_char;

    if (sig == NV_SIG && (pst == PST_ACT || pst == PST_FULL || pst == PST_XSRC))
    {
      if (act_n < (uint8_t)(sizeof(active) / sizeof(active[0])))
      {
        active[act_n++] = NvPage{(uint8_t)i, pst, cyc, pg_base};
      }
    }
  }

  summary[sum_i] = '\0';
  ESP_LOGI("bsl", "NV pages: %u [%s]", (unsigned)pages, summary);

  if (act_n == 0)
  {
    ESP_LOGW("bsl", "No ACTIVE NV pages detected; skipping item scan.");
    ESP_LOGI("bsl", "NV wanted items recap:");
    for (int wi = 0; wi < wanted_count; wi++)
    {
      ESP_LOGW("bsl", "  %s: NOT FOUND", wanted[wi].name ? wanted[wi].name : "(unnamed)");
    }
    return;
  }

  auto pst_rank = [&](uint8_t pst) -> uint8_t
  {
    switch (pst)
    {
    case PST_ACT:
      return 3;
    case PST_FULL:
      return 2;
    case PST_XSRC:
      return 1;
    default:
      return 0;
    }
  };

  // Sort active pages from newest to oldest (by cycle counter, then by state rank, then by index).
  for (uint8_t a = 1; a < act_n; a++)
  {
    NvPage key = active[a];
    int j = (int)a - 1;
    while (j >= 0)
    {
      bool newer = (key.cyc > active[j].cyc) ||
                   ((key.cyc == active[j].cyc) && (pst_rank(key.pst) > pst_rank(active[j].pst))) ||
                   ((key.cyc == active[j].cyc) && (pst_rank(key.pst) == pst_rank(active[j].pst)) && (key.idx < active[j].idx));
      if (!newer)
        break;
      active[j + 1] = active[j];
      j--;
    }
    active[j + 1] = key;
  }

  char order[64];
  size_t p = 0;
  order[0] = '\0';
  for (uint8_t a = 0; a < act_n; a++)
  {
    p += snprintf(order + p, sizeof(order) - p, "%spg%u(%s,c=0x%02X)",
                  (a ? ", " : ""), (unsigned)active[a].idx,
                  pst_name(active[a].pst), active[a].cyc);
    if (p >= sizeof(order))
      break;
  }
  ESP_LOGI("bsl", "NV active page order: [%s]", order);

  // Helper: find index into wanted[] for a given header (skips .done items).
  auto match_wanted = [&](const NzgNvItemHeader &hdr) -> int
  {
    for (int wi = 0; wi < wanted_count; wi++)
    {
      if (wanted[wi].done)
        continue;
      if (hdr.sys == wanted[wi].sys &&
          hdr.item == wanted[wi].item &&
          hdr.sub == wanted[wi].sub)
      {
        return wi;
      }
    }
    return -1;
  };

  const uint32_t ITEM_HDR_LEN = 7;
  const uint8_t ITEM_SIG = NV_SIG;
  const uint16_t ITEM_LEN_MAX = 0x0FFF; // 12-bit length field limit

  int remaining = wanted_count;
  bool all_done = (remaining == 0);

  for (uint8_t a = 0; a < act_n && !all_done; a++)
  {
    const uint8_t pg_idx = active[a].idx;
    const uint32_t pg_base = active[a].base;
    const uint32_t data_floor = pg_base + 16;          // after page + compact headers
    const uint32_t data_ceil = pg_base + cfg.page_size; // one past last byte

    ESP_LOGI("bsl", "NV scan pg%u: data region [0x%06X..0x%06X)",
             (unsigned)pg_idx, (unsigned)data_floor, (unsigned)data_ceil);

    uint32_t scan_pos = data_ceil;
    const uint32_t WINDOW = 248; // chosen to match typical BSL chunking
    uint8_t window_buf[248];

    while (scan_pos > data_floor + ITEM_HDR_LEN - 1 && !all_done)
    {
      uint32_t window_end = scan_pos;
      uint32_t window_start = (window_end > WINDOW) ? (window_end - WINDOW) : data_floor;
      if (window_start < data_floor)
        window_start = data_floor;
      uint32_t window_len = window_end - window_start;
      if (window_len < ITEM_HDR_LEN)
      {
        scan_pos = window_start;
        continue;
      }

      if (!bsl_read_bytes(uart,
                          window_start,
                          window_len,
                          window_buf,
                          "NVPG_SCAN",
                          cfg.ack_timeout_ms,
                          cfg.header_timeout_ms,
                          cfg.payload_timeout_ms))
      {
        ESP_LOGW("bsl", "Aborting NV scan on pg%u due to MEM_READ error", (unsigned)pg_idx);
        break;
      }

      // Scan this window from the end backwards, looking for signature 0x96 as potential header end.
      for (int32_t rel = (int32_t)window_len - 1; rel >= 0 && !all_done; rel--)
      {
        if (window_buf[rel] != ITEM_SIG)
          continue;

        const uint32_t hdr_end_addr = window_start + (uint32_t)rel;
        const uint32_t hdr_start_addr = hdr_end_addr + 1 - ITEM_HDR_LEN; // inclusive

        if (hdr_start_addr < data_floor)
          continue; // header would overlap page header/compact area

        uint8_t h[ITEM_HDR_LEN];
        if (!bsl_read_bytes(uart,
                            hdr_start_addr,
                            ITEM_HDR_LEN,
                            h,
                            "NVITEM_HDR",
                            cfg.ack_timeout_ms,
                            cfg.header_timeout_ms,
                            cfg.payload_timeout_ms))
        {
          continue;
        }

        const uint8_t sysid = (uint8_t)((h[0] >> 2) & 0x3F);
        const uint16_t item = (uint16_t)(((uint16_t)(h[0] & 0x03) << 8) | h[1]);
        const uint16_t sub = (uint16_t)(((uint16_t)h[2] << 2) | ((h[3] >> 6) & 0x03));
        const uint16_t len = (uint16_t)((((uint16_t)(h[3] & 0x3F)) << 6) | ((h[4] >> 2) & 0x3F));
        const uint8_t hcrc = (uint8_t)(((h[4] & 0x03) << 6) | ((h[5] >> 2) & 0x3F));
        const uint8_t stats = (uint8_t)(h[5] & 0x03); // bit1=ACTIVE
        const uint8_t sig = h[6];

        if (sig != ITEM_SIG)
          continue;
        if (len == 0 || len > ITEM_LEN_MAX)
        {
          ESP_LOGD("bsl", "pg%u hdr@0x%06X: reject len=%u",
                   (unsigned)pg_idx, (unsigned)hdr_start_addr, (unsigned)len);
          continue;
        }
        if (sysid > 0x3F || item > 0x03FF || sub > 0x03FF)
        {
          ESP_LOGD("bsl", "pg%u hdr@0x%06X: reject field range sys=%u item=0x%03X sub=0x%03X",
                   (unsigned)pg_idx, (unsigned)hdr_start_addr,
                   (unsigned)sysid, (unsigned)item, (unsigned)sub);
          continue;
        }

        const uint32_t data_hi = hdr_start_addr - 1;
        const uint32_t data_lo = data_hi + 1 - len;
        if (data_lo < data_floor)
        {
          ESP_LOGD("bsl", "pg%u hdr@0x%06X: reject data underflow (data_lo=0x%06X floor=0x%06X)",
                   (unsigned)pg_idx, (unsigned)hdr_start_addr,
                   (unsigned)data_lo, (unsigned)data_floor);
          continue;
        }

        const bool active_item = (stats & 0x02) != 0;
        if (!active_item)
          continue;

        NzgNvItemHeader ih;
        ih.sys = sysid;
        ih.item = item;
        ih.sub = sub;
        ih.len = len;
        ih.stats = stats;
        ih.crc = hcrc;
        ih.sig = sig;
        ih.page_idx = pg_idx;
        ih.data_addr = data_lo;
        ih.hdr_addr = hdr_start_addr;

        int hit = match_wanted(ih);
        if (hit >= 0)
        {
          uint8_t buf_le[128] = {0};
          uint16_t got = 0;

          if (!read_nv_payload(ih.data_addr, ih.len, buf_le, sizeof(buf_le), &got))
          {
            ESP_LOGW("bsl", "Failed to read NV payload for %s (sys=%u item=0x%04X sub=0x%04X) pg%u data@0x%06X len=%u",
                     wanted[hit].name ? wanted[hit].name : "(unnamed)",
                     (unsigned)ih.sys, (unsigned)ih.item, (unsigned)ih.sub,
                     (unsigned)ih.page_idx, (unsigned)ih.data_addr,
                     (unsigned)ih.len);
          }
          else if (ih.len > got)
          {
            ESP_LOGW("bsl", "%s: payload shorter than len (len=%u got=%u); skipping",
                     wanted[hit].name ? wanted[hit].name : "(unnamed)",
                     (unsigned)ih.len, (unsigned)got);
          }
          else
          {
            // CRC-8 check using TI NVOCMP feed model:
            //   CRC over [data bytes] + [header bytes 0..3] + [synthetic len byte ((len & 0x3F) << 2)].
            uint8_t crc = nzg_nv_crc8(buf_le, ih.len, 0x00);
            crc = nzg_nv_crc8(h, 4, crc);
            uint8_t finalByte = (uint8_t)((ih.len & 0x3F) << 2);
            crc = nzg_nv_crc8(&finalByte, 1, crc);

            if (crc != ih.crc)
            {
              ESP_LOGW("bsl", "%s: CRC mismatch pg%u sys=%u item=0x%04X sub=0x%04X len=%u hcrc=0x%02X calc=0x%02X; skipping",
                       wanted[hit].name ? wanted[hit].name : "(unnamed)",
                       (unsigned)ih.page_idx,
                       (unsigned)ih.sys, (unsigned)ih.item, (unsigned)ih.sub,
                       (unsigned)ih.len,
                       (unsigned)ih.crc, (unsigned)crc);
            }
            else
            {
              // All generic checks passed; hand over to frontend.
              cb(hit, ih, buf_le, got);
              wanted[hit].done = true;
              if (--remaining <= 0)
                all_done = true;
            }
          }
        }

        ESP_LOGV("bsl", "NV item pg%u hdr@0x%06X data@0x%06X: sys=%u item=0x%04X sub=0x%04X len=%u stats=%c sig=%s",
                 (unsigned)pg_idx,
                 (unsigned)ih.hdr_addr,
                 (unsigned)ih.data_addr,
                 (unsigned)ih.sys,
                 (unsigned)ih.item,
                 (unsigned)ih.sub,
                 (unsigned)ih.len,
                 active_item ? 'A' : 'I',
                 (sig == ITEM_SIG ? "ok" : "bad"));
      } // end scan of this window

      scan_pos = window_start;
      App.feed_wdt(); // remain cooperative while scanning multiple NV pages
    } // end while page span
  } // end for over active pages

  ESP_LOGI("bsl", "NV wanted items recap:");
  for (int wi = 0; wi < wanted_count; wi++)
  {
    if (!wanted[wi].done)
    {
      ESP_LOGW("bsl", "  %s: NOT FOUND", wanted[wi].name ? wanted[wi].name : "(unnamed)");
    }
    else
    {
      ESP_LOGI("bsl", "  %s: FOUND", wanted[wi].name ? wanted[wi].name : "(unnamed)");
    }
  }
}

}  // namespace zigbee_gateway
}  // namespace esphome
