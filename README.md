# Yepiq Zigbee Gateway

ESPHome firmware for the ZigStar UZG-01. It exposes the TI Zigbee radio through
exclusive TCP, software-bridged USB, or direct USB serial transport and
publishes radio, network, and transport diagnostics to Home Assistant.

The firmware is inspired by [xyzroe/XZG](https://github.com/xyzroe/XZG) and
follows its UZG-01 pinout, radio identification, storage layouts, discovery,
and maintenance-tool conventions.

The source and host tests build with ESPHome 2026.7.2. The transport and
maintenance paths have not yet been verified on a physical UZG-01; see the
[hardware test plan](HARDWARE_TEST_PLAN.md) before production use.

## Features

- Transparent ZNP transport over Ethernet TCP, USB Bridged, or USB Direct.
- Exclusive UART ownership for local diagnostics, normal clients, and firmware
  maintenance.
- `/cmdZigBSL` and `/cmdZigRST` compatibility endpoints for ZigStarGW-MT and
  XZG-MT-style tools.
- Persistent physical identity, running-image metadata, and network state.
- Passive ZNP observation without additional UART requests.
- Home Assistant controls and transport diagnostics.
- ESPHome OTA, web interface, native API, Ethernet, and mDNS discovery.

The raw TCP stream and maintenance endpoints have no component-level
authentication. Use them only on a trusted or isolated network.

## Hardware

The supplied [`yzg.yaml`](yzg.yaml) targets the UZG-01:

| Function | ESP32 pin | Notes |
| --- | --- | --- |
| Zigbee UART RX | GPIO36 | 115200 baud by default |
| Zigbee UART TX | GPIO4 | 115200 baud by default |
| USB UART RX | GPIO3 | CH340-facing ESP32 UART0 |
| USB UART TX | GPIO1 | CH340-facing ESP32 UART0 |
| Zigbee reset | GPIO16 | Inverted |
| Zigbee BSL/rejoin | GPIO32 | Inverted |
| Direct USB selector | GPIO33 | High connects CH340 directly to the radio |
| Mode button | GPIO35 | Cycles all transport modes |
| Red mode LED | GPIO12 | Strapping pin |
| Blue power LED | GPIO14 | Steady on |
| LAN8720 MDC | GPIO23 |  |
| LAN8720 MDIO | GPIO18 |  |
| LAN8720 clock | GPIO17 | Clock output |
| LAN8720 power | GPIO5 | Strapping pin |

Pin assignments, UART settings, TCP port, and socket timeouts are substitutions
at the top of `yzg.yaml`.

Serial logging must remain disabled with `logger.baud_rate: 0`. UART0 carries
USB Bridged traffic, so logger output would corrupt the Zigbee stream.

## Architecture

### UART ownership

All radio UART reads and writes pass through one ownership gate:

| Owner | Purpose |
| --- | --- |
| Local | Chip identification, explicit diagnostics, reset parsing, and radio LED control |
| TCP normal | Zigbee2MQTT or another ZNP client |
| TCP maintenance | Transparent BSL/flashing traffic |
| USB bridge | CH340-facing software bridge |

Only one owner can access the radio UART. Local operations do not preempt a
connected client. Passive observation is not an owner: it inspects complete
frames already forwarded by normal TCP or USB Bridged and never sends or
consumes data.

### Serial transport

The **Zigbee Serial Transport** selector controls three mutually exclusive
modes:

| Mode | GPIO33 | Data path | TCP listener |
| --- | --- | --- | --- |
| `TCP` | Low | TCP client ↔ ESP32 ↔ radio | Enabled |
| `USB Bridged` | Low | CH340 ↔ ESP32 UART0 ↔ radio | Disabled |
| `USB Direct` | High | CH340 ↔ radio | Disabled |

TCP and USB Bridged share the same buffered full-duplex pump. Separate buffers
for each direction preserve partial non-blocking writes.

USB Bridged keeps the ESP32 in the data path. Entering BSL changes both UARTs to
500000 baud; resetting the radio restores their configured baud rates. USB
Direct bypasses both ESP32 UARTs, although the ESP32 still controls reset and
BSL pins.

Manual information refresh is rejected in both USB modes because the gateway
cannot detect whether a USB host is active.

### TCP maintenance

The server provides one active socket and one pending maintenance candidate:

1. The first socket is provisional until its first payload identifies a normal
   transparent client.
2. One additional socket may wait for `/cmdZigBSL` or `/cmdZigRST`; further
   sockets are rejected.
3. A maintenance command parks the normal socket only when takeover is ready.
4. Parked-client bytes are discarded, and only the maintenance client reaches
   the UART.
5. When maintenance ends, the radio is reset if necessary and the parked socket
   is closed so Zigbee2MQTT reconnects cleanly.

This supports tools that open TCP before sending the HTTP command and tools
that send the command before opening TCP. BSL payloads remain opaque; the
gateway does not inspect the transferred firmware.

### Persisted Zigbee information

Metadata is separated by invalidation lifetime:

| Scope | Values | Update policy |
| --- | --- | --- |
| Physical identity | Chip family, flash capacity, factory IEEE | Retained for the lifetime of the soldered radio |
| Running image | Firmware, stack, active IEEE, role, CCFG | Marked pending before BSL; ZNP fields update passively, CCFG through explicit refresh |
| Network snapshot | PAN ID, channel, joined state, parent IEEE, extended PAN ID | Retained as last-known state; updated from normal ZNP traffic or explicit refresh |

Valid physical identity allows startup without a BSL probe. After flashing, the
gateway gives the external client immediate UART access and learns mutable
metadata from traffic that client already requests.

USB Direct preserves cached values but reports mutable image and network
provenance as `Cached`. A BSL-pending image remains `Awaiting Observation`.

Metadata status values:

| Status | Meaning |
| --- | --- |
| `Restored` | Valid cached identity and image data loaded without UART access |
| `Cached` | Last-known mutable data is visible while USB Direct bypasses the ESP32 |
| `Refreshing` | Explicit local identification is running |
| `Verified` | Explicit local identification completed |
| `Observed` | Transparent ZNP traffic identified the running image |
| `Awaiting Observation` | BSL may have changed image-owned values |
| `Unavailable` | Required data is absent |

Network information uses `Cached`, `Observed`, `Refreshed`, and `Unavailable`
with the equivalent snapshot-specific meanings.

### Supported storage layouts

The detected chip family selects flash and NVOCMP geometry:

| Family | `FLASH_SIZE` unit | NVOCMP region | NVOCMP page |
| --- | --- | --- | --- |
| CC13x2/CC26x2 | 4 KiB | `0x50000` + `0x6000` | 8 KiB, 3 pages |
| CC13x2x7/CC26x2x7 | 8 KiB | `0xA6000` + `0x8000` | 8 KiB, 4 pages |

The flash-size unit and NVOCMP page size are independent. An unidentified
family receives no fallback layout; layout-dependent CCFG and NV reads are
skipped.

### LEDs

| Indicator | UZG-01 behavior | XZG behavior | This firmware |
| --- | --- | --- | --- |
| Blue power LED | Steady on | Blinks without a TCP client; steady with one | Steady on |
| Red mode LED | On for USB, off for Ethernet | On for USB, off for network, plus status patterns | On for either USB mode; off for TCP |
| Yellow/white Zigbee LED | On while Zigbee2MQTT is connected | Controlled through radio LED1 | On for an admitted normal TCP session |

The yellow/white indicator belongs to the Zigbee radio. The gateway sends
`UTIL_LED_CONTROL` only while local code owns the UART. Unsupported router
firmware or a command timeout never blocks TCP admission.

References:

- [UZG-01 LED behavior](https://uzg.zig-star.com/getting-started/#led-behaviour)
- [XZG LED behavior](https://xzg.xyzroe.cc/hardware/#led-indicators)

## Home Assistant

The example configuration exposes:

- Radio identity and state: hardware, flash size, firmware, stack, IEEE
  addresses, role, TX power, PAN ID, channel, joined state, and parent network.
- Transport diagnostics: state, socket count, pending/parked flags, last event,
  rejected connections, timeouts, maintenance sessions, and recovery resets.
- Controls: restart, enter BSL, router rejoin, information refresh, and serial
  transport selection.

## Build and test

Review the substitutions in `yzg.yaml`, then validate and compile:

```sh
esphome -q config yzg.yaml
esphome -q compile yzg.yaml
```

The dependency-free logic tests require a C++17 compiler:

```sh
for source in tests/*_test.cpp; do
  binary="/tmp/$(basename "${source%.cpp}")"
  c++ -std=c++17 -Wall -Wextra -pedantic -I. "$source" -o "$binary" &&
    "$binary"
done
```

Hardware behavior is tracked in [`HARDWARE_TEST_PLAN.md`](HARDWARE_TEST_PLAN.md).

## Project layout

- [`yzg.yaml`](yzg.yaml): UZG-01 ESPHome configuration.
- [`components/zigbee_gateway`](components/zigbee_gateway): external component.
- [`tests`](tests): dependency-free state, parser, and transport tests.
- [`HARDWARE_TEST_PLAN.md`](HARDWARE_TEST_PLAN.md): bench acceptance checklist.

## License

This repository is licensed under the [Apache License 2.0](LICENSE). XZG and
other referenced projects remain under their respective licenses.
