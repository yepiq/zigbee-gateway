# UZG-01 Hardware Test Plan

Bench acceptance checklist for the UZG-01 Zigbee Gateway firmware. Record the
device, software versions, and evidence for every test session.

## Status vocabulary

- `NOT RUN`: implemented but not yet exercised on hardware.
- `PASS`: observed result matches the expected result.
- `FAIL`: observed result does not match the expected result; retain evidence.
- `BLOCKED`: the test cannot proceed because a prerequisite is missing.
- `RETEST`: previously passed, but a later change affects this behavior.

## Test-session record

Record this information at the start of every hardware session:

- Date:
- Gateway commit:
- ESPHome version:
- UZG-01 hardware revision:
- Zigbee chip:
- Initial Zigbee role and firmware:
- Zigbee2MQTT version:
- ZigStarGW-MT legacy version:
- XZG-MT/current Multi Tool version:
- Host operating system:
- Network connection and power source:
- Notes/evidence location:

For firmware and role-changing tests, first save the currently installed radio
firmware identity and a Zigbee2MQTT coordinator backup. Do not run
coordinator-to-router tests on the only production coordinator without a
maintenance window and a recovery method.

## Baseline and diagnostics

### BAS-01 — ESP32 boot and Ethernet startup

- Status: `NOT RUN`
- Procedure: Power-cycle the UZG-01 with Ethernet connected.
- Expected: ESP32 boots without a watchdog reset, Ethernet obtains an address,
  physical identity and the last observed running image are restored without
  entering BSL, the last-known network snapshot reports `Cached`, the web
  server and native API become reachable, and TCP port 6638 starts listening
  without waiting for an intrusive startup radio probe.

### BAS-02 — Initial or invalid-cache radio identification

- Status: `NOT RUN`
- Procedure: On a device without a compatible metadata record, capture complete
  logs from power-on through TCP-listener startup.
- Expected: BSL chip identification, flash-size detection, NVOCMP inspection,
  hardware identity, radio reset, and applicable ZNP firmware diagnostics
  complete without concurrent TCP UART access. The detected family selects the
  correct `FLASH_SIZE` unit and NVOCMP layout. A verified record is committed
  before TCP starts.

### BAS-03 — Diagnostic entity consistency

- Status: `NOT RUN`
- Procedure: Compare published hardware, flash size, firmware, role, IEEE,
  network state, PAN ID, channel, parent IEEE, and extended PAN ID with the
  radio firmware and Zigbee2MQTT information.
- Expected: Supported values agree; unavailable or inapplicable values are
  reported as unknown rather than fabricated. Zigbee Metadata Status accurately
  reports `Restored`, `Refreshing`, `Verified`, `Observed`,
  `Awaiting Observation`, or `Unavailable`. Zigbee Network Information Status
  accurately reports `Cached`, `Cleared`, `Observed`, `Refreshed`, or
  `Unavailable`.

### BAS-04 — ESP32 OTA update

- Status: `NOT RUN`
- Procedure: Perform an ESPHome OTA update with Zigbee2MQTT stopped, then repeat
  with it running after the ordinary TCP path has passed.
- Expected: ESP32 returns cleanly, cached Zigbee information restores without
  toggling the radio pins, and Zigbee2MQTT reconnects without radio network
  loss.

### BAS-05 — USB-mode boot does not probe an uncached radio

- Status: `NOT RUN`
- Procedure: Clear or invalidate the physical-identity cache, save `USB
  Bridged`, reboot, then repeat with `USB Direct`.
- Expected: The selected GPIO33 path is applied before metadata setup. Neither
  USB mode enters BSL, sends a local ZNP request, or consumes USB/radio bytes.
  Metadata Status reports `Unavailable` until an explicit refresh is run in
  idle TCP mode.

## Chip and storage geometry

Run `tests/zigbee_chip_layout_test.cpp` after every change to chip-family
detection, flash-capacity calculation, or NVOCMP layout selection.

### GEO-01 — Host layout policy

- Status: `PASS`
- Procedure: Compile and run `tests/zigbee_chip_layout_test.cpp`.
- Expected: x2 uses a 4 KiB `FLASH_SIZE` unit and three 8 KiB NVOCMP pages;
  x2x7 uses an 8 KiB `FLASH_SIZE` unit and four 8 KiB NVOCMP pages; an unknown
  family has no layout.
- Evidence: Passed with the C++17 warning-enabled host command on 2026-07-26.

### GEO-02 — CC13x2/CC26x2 identification

- Status: `NOT RUN`
- Procedure: On a supported non-x7 radio with an empty physical-identity cache,
  capture the family, flash-capacity, CCFG-address, and NV-region logs.
- Expected: The family is x2; total flash is the register count multiplied by
  4 KiB; CCFG addresses are derived from that total; NV scanning uses base
  `0x50000`, size `0x6000`, and page size `0x2000`.

### GEO-03 — CC13x2x7/CC26x2x7 identification

- Status: `PASS`
- Procedure: On a supported x7 radio with an empty physical-identity cache,
  capture the family, flash-capacity, CCFG-address, and NV-region logs.
- Expected: The family is x2x7; total flash is the register count multiplied by
  8 KiB; CCFG addresses are derived from that total; NV scanning uses base
  `0xA6000`, size `0x8000`, and page size `0x2000`.
- Evidence: On 2026-08-01, firmware based on gateway commit `6b35e90`
  identified the UZG-01 radio as `cc13x2x7_cc26x2x7` / `CC2652P7`, read 88
  flash units of 8192 bytes for 720896 bytes total, and selected NV base
  `0xA6000`, size `0x8000`, and page size `0x2000`.

### GEO-04 — Unknown-family safety

- Status: `NOT RUN`
- Procedure: With a development fixture that produces ambiguous family
  detection, run an explicit information refresh.
- Expected: No fallback `FLASH_SIZE` multiplier, end-of-flash CCFG address, or
  NVOCMP range is used. Identification remains unverified, no partial physical
  cache is committed, the radio is reset out of BSL, and a later refresh may
  retry.

## Persistent radio metadata

### CACHE-01 — First identification creates a clean snapshot

- Status: `NOT RUN`
- Procedure: Start with no compatible Zigbee metadata preference, boot once,
  and retain the full log through listener startup.
- Expected: Status progresses from `Unavailable` through `Refreshing` to
  `Verified`; one complete local identification runs; the log reports a saved
  generation; all available entities contain the identified values.

### CACHE-02 — Clean reboot uses the fast restore path

- Status: `PASS`
- Procedure: After CACHE-01, power-cycle the ESP32 and capture the boot log and
  radio reset/BSL pins if test equipment is available.
- Expected: Status becomes `Restored`; the saved values publish immediately;
  logs explicitly say the startup probe was skipped; neither reset nor BSL is
  driven for identification.
- Evidence: On 2026-08-01, the fixed working-tree build restarted through the
  ESPHome control and reconnected its native API in 177 ms. Metadata Status
  reported `Restored`; Network Information Status reported `Cached`; the
  CC2652P7 identity, 720896-byte flash size, factory IEEE, Router role, PAN,
  channel, parent, and extended PAN ID were restored. TCP returned idle with
  no sockets, and the observed post-restart logs contained no BSL entry, radio
  reset, UART timeout, or intrusive metadata refresh. Zigbee2MQTT continued to
  report the commissioned Router alive with a last-seen age of only seconds.

### CACHE-03 — Manual refresh while idle

- Status: `PASS`
- Procedure: Stop Zigbee2MQTT and press the Refresh Zigbee Information
  diagnostic button.
- Expected: The component exclusively claims UART, marks the record dirty,
  identifies BSL/NV/ZNP information, resets the radio, saves the next clean
  generation, reports `Verified`, and releases UART for the next TCP client.
- Evidence: On 2026-08-01, the fixed working-tree build entered BSL, identified
  the CC2652P7 and its NV data, reset the Router, saved physical/image/network
  generation `1/3/3`, published metadata `Verified` and network information
  `Refreshed`, and returned the TCP transport to idle.

### CACHE-10 — Router refresh completes without Coordinator reset wait

- Status: `PASS`
- Procedure: With the cached or freshly detected role known to be Router, run
  Refresh Zigbee Information and capture the interval from the radio reset to
  completion.
- Expected: The radio is reset out of BSL without waiting for a
  Coordinator-only ZNP reset indication. The refresh produces no UART timeout
  errors and promptly releases ownership after the Router-specific settle
  period.
- Evidence: On 2026-08-01, firmware based on gateway commit `6b35e90` knew the
  role was Router before reset, but waited the full 5000 ms for
  `SYS_RESET_IND`, emitted repeated `Reading from UART timed out at byte 0`
  errors, and reported a 6853 ms component operation before skipping ZNP
  routines. With the subsequent working-tree fix, reset started at 11:01:43.340
  and the Router settle path completed at 11:01:43.456 without UART timeout
  errors or a missing-`SYS_RESET_IND` warning.

### CACHE-04 — BSL maintenance defers refresh to normal traffic

- Status: `NOT RUN`
- Procedure: Perform any successful remote BSL session, note metadata status
  and logs, close the maintenance connection, and let Zigbee2MQTT reconnect.
- Expected: The running-image record is committed pending immediately before
  BSL pin entry; physical identity remains valid and old network details remain
  `Cached`, but current network membership becomes unavailable during the
  opaque maintenance session. After the maintenance socket closes, the gateway
  does not re-enter BSL, scan NV, or delay the returning normal TCP client;
  running image status remains `Awaiting Observation` until normal ZNP startup
  traffic identifies it.

### CACHE-05 — Interrupted maintenance survives an ESP32 reboot

- Status: `NOT RUN`
- Procedure: Enter BSL through the TCP maintenance workflow without erasing or
  writing the radio, then power-cycle the whole gateway before completing the
  session.
- Expected: The persisted running-image marker survives, but the valid physical
  identity still permits a fast boot. Power-on returns the radio to its
  application; ESPHome does not enter BSL or scan NV and starts normal TCP with
  image status `Awaiting Observation`.

### CACHE-06 — Failed refresh retains last-known values

- Status: `NOT RUN`
- Procedure: With a known clean record and Zigbee2MQTT stopped, temporarily
  force BSL synchronization to fail, then invoke Refresh Zigbee Information.
- Expected: Physical identity is never invalidated. The failed candidate never
  replaces the running-image or network records; previous values remain
  published as last-known, image status is `Awaiting Observation`, and current
  network membership remains unavailable until a later normal client or manual
  refresh observes it again.

### CACHE-07 — Manual refresh cannot preempt a TCP client

- Status: `NOT RUN`
- Procedure: Keep Zigbee2MQTT connected and press Refresh Zigbee Information.
- Expected: The request is rejected with a clear log message; status and
  metadata do not change; no radio pin toggles and Zigbee2MQTT keeps UART.

### CACHE-08 — Incompatible record is rejected

- Status: `NOT RUN`
- Host check: Run `tests/zigbee_metadata_cache_test.cpp`.
- Hardware procedure: Install a development build with an intentionally newer
  cache schema after first creating a clean record with the preceding schema.
- Expected: Independent physical-identity, running-image, and network-snapshot
  magic, schema, size, known masks, Boolean domains, and string termination
  checks reject malformed records; hardware boot treats a schema mismatch as
  unavailable and performs the required identification.

### CACHE-09 — Persistence scopes invalidate independently

- Status: `NOT RUN`
- Procedure: Establish all three records, enter BSL without writing firmware,
  then reboot before completing maintenance.
- Expected: Physical hardware, flash capacity, and factory IEEE remain
  available and unchanged. Only the running-image record reports
  `Awaiting Observation`; the previous network values remain visible with
  Network Information Status `Cached`.

## Passive ZNP information observation

Run `tests/zigbee_znp_observer_test.cpp` after every change to the observed ZNP
command layouts. The fixture suite validates UNPI FCS handling and exact
payload boundaries for `SYS_VERSION`, `UTIL_GET_DEVICE_INFO`,
`ZDO_EXT_NWK_INFO`, and `ZDO_STATE_CHANGE_IND`.

### OBS-01 — Running image observed from normal startup

- Status: `NOT RUN`
- Procedure: Mark the running image pending through a maintenance session, then
  let Zigbee2MQTT reconnect while capturing verbose ZNP logs.
- Expected: The gateway only observes already-forwarded `SYS_VERSION` and
  `UTIL_GET_DEVICE_INFO` responses. Firmware build, stack, active IEEE, and
  role update; Metadata Status becomes `Observed`; factory IEEE and physical
  identity remain unchanged. No additional UART request is transmitted.

### OBS-02 — Full network snapshot observed

- Status: `NOT RUN`
- Procedure: Restart Zigbee2MQTT against a running coordinator and capture its
  `ZDO_EXT_NWK_INFO` exchange.
- Expected: PAN ID, channel, parent IEEE, extended PAN ID, and joined state
  match Zigbee2MQTT. Network Information Status becomes `Observed`; one
  last-known snapshot is persisted.

### OBS-03 — Live network state indication

- Status: `NOT RUN`
- Procedure: Cause a valid `ZDO_STATE_CHANGE_IND` while the normal client owns
  UART.
- Expected: Joined state updates from the indication without any local UART
  request. A partial state indication alone does not claim that an otherwise
  cached full network snapshot was freshly observed.

### OBS-04 — Invalid and out-of-scope frames are ignored

- Status: `NOT RUN`
- Procedure: Exercise malformed/short frames in the host fixture and observe a
  maintenance session on hardware.
- Expected: Bad-FCS, failed-status, short, and unrelated ZNP frames cannot
  update information. Maintenance traffic remains opaque, and local diagnostic
  exchanges do not enter the passive persistence path.

## Host transport state tests

Run `tests/zigbee_tcp_state_test.cpp` after every change that can alter TCP
client roles, pending/parked topology, maintenance takeover, or BSL rendezvous.
The deterministic suite currently covers:

- provisional-to-normal classification;
- the first-pending and third-client rejection policy;
- pending promotion after the normal owner disconnects;
- connection-first and command-first BSL maintenance;
- last-moment parking of the normal client;
- same-socket maintenance reset behavior;
- BSL rendezvous expiry and recovery intent.

The suite also covers the armed-owner-disconnect edge case: if the normal owner
leaves while BSL rendezvous is armed, the next maintenance socket must trigger
the deferred BSL entry rather than receiving an application-mode UART.

## Host stream and mode tests

Run `tests/zigbee_stream_pump_test.cpp` and
`tests/zigbee_transport_mode_test.cpp` after every change to common byte
forwarding, endpoint flow control, transport option ordering, or GPIO33 mode
mapping. Run `tests/zigbee_serial_owner_test.cpp` after every change to UART
ownership or passive-observation admission.

### STR-01 — Shared duplex stream pump

- Status: `PASS`
- Procedure: Compile and run `tests/zigbee_stream_pump_test.cpp`.
- Expected: The deterministic suite covers bidirectional forwarding, retained
  data across partial and temporarily blocked writes, quarantined TCP
  prebuffer injection and buffer reset, and left/right closure and error
  reporting.

### MOD-01 — Three-mode mapping

- Status: `PASS`
- Procedure: Compile and run `tests/zigbee_transport_mode_test.cpp`.
- Expected: The deterministic suite covers the exact `TCP`, `USB Bridged`, and
  `USB Direct` option ordering plus TCP-listener, software-bridge, and
  direct-GPIO mode mapping.

### OWN-01 — Passive-observer owner policy

- Status: `PASS`
- Procedure: Compile and run `tests/zigbee_serial_owner_test.cpp`.
- Expected: Only normal TCP and USB Bridged owners admit passive ZNP
  observations. No owner, local diagnostics, and TCP maintenance remain
  excluded.

## Transport diagnostics

All entities in this section must appear in Home Assistant under the diagnostic
category.

### DIAG-01 — Idle and normal transport state

- Status: `NOT RUN`
- Procedure: Boot without a TCP client, then connect and disconnect
  Zigbee2MQTT.
- Expected: Zigbee TCP State reports `idle`, briefly `provisional`, then
  `normal`, and returns to `idle`; Last Event follows the corresponding
  transitions; pending and parked sockets remain off.

### DIAG-02 — Pending and parked topology

- Status: `NOT RUN`
- Procedure: With Zigbee2MQTT normal, open a silent second socket, then promote
  it into a maintenance session.
- Expected: Pending Socket turns on while waiting, then turns off as Parked
  Socket turns on and State becomes `maintenance`; Socket Connections matches
  the actual active, pending, and parked socket count throughout.

### DIAG-03 — Rejection, timeout, and maintenance counters

- Status: `NOT RUN`
- Procedure: Cause one third-client rejection, one pending-socket timeout, and
  one successful maintenance takeover.
- Expected: Rejected Connections, Pending Timeouts, and Maintenance Sessions
  each increment exactly once. Repeating one event changes only its applicable
  boot-scoped counter.

### DIAG-04 — Recovery reset counter and last event

- Status: `NOT RUN`
- Procedure: Abort a maintenance session while the radio may still be in BSL,
  then separately exercise a command-first BSL rendezvous timeout.
- Expected: Recovery Resets increments only when the ESP32 actually performs
  the recovery reset. Last Event reports `Recovery Reset` after the reset side
  effect; no intrusive metadata refresh delays normal UART ownership.

## Ordinary TCP transport

### TCP-01 — Normal Zigbee2MQTT session

- Status: `NOT RUN`
- Procedure: Connect Zigbee2MQTT to `socket://<gateway>:6638` and exercise
  permit-join plus several device reads/commands.
- Expected: Exactly one normal client owns the UART; traffic is transparent and
  stable; Socket Status is on and Socket Connections is 1.

### TCP-02 — Normal disconnect and reconnect

- Status: `NOT RUN`
- Procedure: Stop Zigbee2MQTT, wait for disconnect detection, then start it.
- Expected: UART ownership is released, socket entities return to zero/off, and
  the next client becomes the normal owner without rebooting either chip.

### TCP-03 — First pending client timeout

- Status: `NOT RUN`
- Procedure: Keep Zigbee2MQTT active, open one additional TCP connection, do
  not send a maintenance command, and wait longer than 30 seconds.
- Expected: Zigbee2MQTT continues normally; the pending connection is reset at
  timeout; no radio pin is toggled.

### TCP-04 — First-pending policy and third-client rejection

- Status: `NOT RUN`
- Procedure: Keep Zigbee2MQTT active, open a pending connection, then attempt a
  third connection.
- Expected: The first pending connection is retained and the third is rejected
  immediately; the normal client remains unaffected.

### TCP-05 — Pending disconnect and replacement

- Status: `NOT RUN`
- Procedure: Open and close the first pending connection before timeout, then
  open another candidate.
- Expected: The slot is released and the replacement becomes the sole pending
  client.

### TCP-06 — Sustained bidirectional traffic

- Status: `NOT RUN`
- Procedure: Leave Zigbee2MQTT connected under normal network activity for at
  least 24 hours while monitoring ESP32 resets, UART errors, and device
  response latency.
- Expected: No corruption, unexplained reconnect loop, watchdog reset, or
  growing memory/resource loss.

## USB serial transports

### USB-01 — TCP to USB Bridged transition

- Status: `NOT RUN`
- Procedure: With Zigbee2MQTT connected over TCP, select `USB Bridged` and
  connect a USB serial client at 115200 baud.
- Expected: The TCP listener and all TCP sockets close before the USB bridge
  claims the radio UART. GPIO33 remains low, the red mode LED turns on, and
  USB-to-radio traffic becomes usable without an ESP32 reboot.

### USB-02 — USB Bridged transparent traffic

- Status: `NOT RUN`
- Procedure: Run Zigbee2MQTT or a ZNP exerciser through the UZG-01 USB serial
  device for at least 24 hours with ordinary bidirectional network activity.
- Expected: The shared duplex pump preserves every byte in both directions,
  including under bursts and partial host reads. There is no logger text,
  corruption, watchdog reset, or growing resource loss.

### USB-03 — USB Bridged exclusivity

- Status: `NOT RUN`
- Procedure: While USB Bridged is actively carrying ZNP traffic, attempt to
  connect TCP port 6638 and invoke Refresh Zigbee Information.
- Expected: TCP is not listening. Manual local refresh is rejected because USB
  host activity cannot be detected. Only the `USB_BRIDGE` owner can consume or
  write the radio UART.

### USB-04 — USB Direct hardware bypass

- Status: `NOT RUN`
- Procedure: Select `USB Direct`, verify GPIO33 electrically, then run a USB
  serial ZNP session while monitoring ESP32 UART activity and TCP port 6638.
- Expected: GPIO33 is high, the CH340 is physically connected to the Zigbee
  radio, the red mode LED is on, TCP is not listening, and the ESP32 neither
  reads nor writes the transparent stream.

### USB-05 — Mode cycling and persistence

- Status: `NOT RUN`
- Procedure: Cycle the physical mode button through all three options, reboot
  in each option, and repeat changes from Home Assistant.
- Expected: The order is `TCP`, `USB Bridged`, `USB Direct`. The selector,
  GPIO33, red LED, TCP listener, and software bridge agree after every change
  and reboot; there is never more than one external stream owner.

### USB-06 — USB Bridged BSL and reset baud changes

- Status: `NOT RUN`
- Procedure: In USB Bridged at 115200 baud, invoke Zigbee BSL Mode and flash or
  query the bootloader using the XZG-compatible 500000-baud workflow. Then
  invoke Restart Zigbee and continue with application ZNP at 115200.
- Expected: BSL entry changes both ESP32 UARTs to 500000 only after the bridge
  is the exclusive owner. Reset does not consume the radio's reset indication
  and restores the radio and CH340-facing UARTs to their configured normal
  baud rates.

### USB-07 — USB Direct reset and BSL control

- Status: `NOT RUN`
- Procedure: In USB Direct, use the ESPHome BSL and restart controls while a
  USB maintenance tool owns the direct serial path.
- Expected: The ESP32 keeps control of the radio BSL/reset pins without
  touching serial bytes. The external tool receives the bootloader and reset
  traffic directly through the CH340 path.

### USB-08 — Passive observation in USB Bridged only

- Status: `NOT RUN`
- Procedure: Exchange recognized `SYS_VERSION`, `UTIL_GET_DEVICE_INFO`, and
  network-information frames first through USB Bridged and then USB Direct.
- Expected: USB Bridged passively updates applicable cached information from
  already-forwarded valid frames. USB Direct produces no ESP32 observation and
  does not fabricate a refresh.

### USB-09 — USB Direct metadata provenance

- Status: `NOT RUN`
- Procedure: First obtain `Observed` running-image and network information over
  TCP, then select USB Direct. Repeat after entering BSL so the running image is
  pending, and reboot once with USB Direct persisted.
- Expected: Last-known values remain visible and physical identity remains
  unchanged. Ordinary USB Direct entry reports Metadata Status `Cached` and
  Network Information Status `Cached`; a pending running image remains
  `Awaiting Observation`. Missing physical identity remains `Unavailable`;
  missing running-image information with valid physical identity reports
  `Awaiting Observation`.

## Maintenance rendezvous and UART exclusion

### MNT-01 — Legacy command-first flashing with Zigbee2MQTT active

- Status: `NOT RUN`
- Procedure: Start a radio flash using the older ZigStarGW-MT sequence that
  sends `/cmdZigBSL` before opening its TCP socket.
- Expected: Zigbee2MQTT continues until the flasher socket exists; it is then
  parked, the flasher exclusively owns UART, and the transfer completes.

### MNT-02 — Current connection-first flashing with Zigbee2MQTT active

- Status: `NOT RUN`
- Procedure: Start a flash using the newer tool sequence that opens TCP before
  sending `/cmdZigBSL`.
- Expected: The new socket waits as the first pending client; BSL command parks
  Zigbee2MQTT at the last practical moment and promotes the pending client.

### MNT-03 — Command-first flashing without a normal client

- Status: `NOT RUN`
- Procedure: Stop Zigbee2MQTT, send `/cmdZigBSL`, then connect the flashing
  socket within 30 seconds.
- Expected: The radio enters BSL, the next socket becomes the maintenance owner,
  and no unrelated socket can take UART concurrently.

### MNT-04 — Connection-first flashing without a normal client

- Status: `NOT RUN`
- Procedure: Stop Zigbee2MQTT, open the tool socket, then send `/cmdZigBSL`.
- Expected: The silent provisional connection becomes the maintenance owner and
  receives an opaque BSL stream.

### MNT-05 — Same-socket reset and post-flash probe

- Status: `NOT RUN`
- Procedure: Using the current tool, finish flashing, send `/cmdZigRST`, and
  allow its version/ping check to continue on the same TCP connection.
- Expected: The ESP32 toggles radio reset without consuming
  `SYS_RESET_IND` or other UART bytes; the tool completes its check.
  ESPHome waits until that maintenance socket closes, then permits the next
  normal client without taking UART for an intrusive metadata refresh.

### MNT-06 — Parked Zigbee2MQTT behavior

- Status: `NOT RUN`
- Procedure: During a several-minute maintenance session, observe the original
  Zigbee2MQTT process and gateway connection counters.
- Expected: Its socket remains open while possible, incoming requests are
  drained and discarded, and it never reaches UART. When maintenance ends, it
  receives one abortive close and reconnects cleanly. Running-image diagnostics
  refresh only from ordinary ZNP traffic already exchanged by that client.

### MNT-07 — Aborted maintenance recovery

- Status: `NOT RUN`
- Procedure: Abort the flasher after entering BSL but before its normal reset.
- Expected: Maintenance disconnect triggers a hardware recovery reset, the
  parked socket is closed afterward, running-image information remains pending
  without an automatic BSL/NV pass, and Zigbee2MQTT can reconnect immediately
  to whatever valid radio image remains.

### MNT-08 — BSL rendezvous timeout

- Status: `NOT RUN`
- Procedure: With no active client, send `/cmdZigBSL` but do not connect a
  flashing socket for more than 30 seconds.
- Expected: The rendezvous expires, the ESP32 resets the radio out of BSL, and
  normal TCP resumes without a second BSL/NV pass. Physical identity remains
  valid while running-image information awaits passive observation.

### MNT-09 — Parked-client safety limit

- Status: `NOT RUN`
- Procedure: Hold a maintenance connection beyond the configured 10-minute
  parked-client limit.
- Expected: The old Zigbee2MQTT socket is closed at the safety limit while the
  active maintenance connection remains exclusive.

### MNT-10 — Armed normal owner disconnects before flasher arrival

- Status: `NOT RUN`
- Procedure: Keep Zigbee2MQTT active, invoke `/cmdZigBSL` so rendezvous is
  armed, stop Zigbee2MQTT before opening the flashing socket, then connect the
  flashing tool within the rendezvous timeout.
- Expected: Zigbee2MQTT gets maximum runtime and leaves normally; the accepted
  flashing socket becomes the exclusive maintenance owner; the ESP32 enters
  BSL at that moment and the tool receives the bootloader stream.

## Radio firmware and role changes

### STAGE-01 — Historical staging and write-path PoC

- Status: `PASS`
- Procedure: This test used the former **Simulate Zigbee Firmware Update**
  development action before the real BSL writer replaced it.
- Expected: The compatible raw image is downloaded directly into the 768 KiB
  `zigbee_fw` partition, read back with the same SHA-256, and marked ready.
  Simulated erase, write, and verification remain cooperative and consume the
  exact staged bytes. No radio UART bytes or radio control-pin changes occur,
  and progress reaches 100%.
- Evidence: On 2026-08-01, Router build `20250403` downloaded 720896 bytes in
  10837 ms. The staged readback, simulated write, and simulated verification
  all produced SHA-256
  `832db3da669ee1371084cc3256fb34aed4f0a2642a5b915bbb0188a8e469cabb`;
  progress reached 100% and the simulation completed in 108563 ms.

### STAGE-02 — Interrupted download remains invalid

- Status: `NOT RUN`
- Procedure: Stage one image successfully, make its upstream freshness check
  return HTTP 200, then remove power during the replacement download and
  reboot.
- Expected: The previous staging header was invalidated before replacement
  bytes were downloaded. Boot reports no verified staged image, and neither
  the old nor incomplete new payload can be used for a later radio update.

### STAGE-03 — Completed staged image survives reboot

- Status: `PASS`
- Procedure: Complete STAGE-01, reboot without starting another download, and
  retain the startup log.
- Expected: Boot reports the staged role, version, filename, image size, and
  ETag and SHA-256 from the verified header. The catalog remains limited to
  Zigbee coordinator and router images.
- Evidence: After the successful 2026-08-01 staging run and an ESP32 reboot,
  the live device reported `Staged: router 20250403`; the exact target restored
  as Router / `20250403`, and the catalog continued to expose only Coordinator
  and Router roles.

### STAGE-04 — Staging-path responsiveness

- Status: `PASS`
- Procedure: During staging and every historical simulated radio phase, repeatedly read
  native API entities and capture watchdog/component-blocking warnings.
- Expected: Home Assistant and logging remain responsive. Work advances in
  bounded chunks without an ESPHome watchdog reset. Repeat this test with
  Actual BSL responsiveness is covered separately by FW-00.
- Evidence: Throughout the 108563 ms simulation on 2026-08-01, HA received
  status and progress updates through every stage and the ESP32 remained
  responsive without a watchdog reset. One isolated API operation took 51 ms;
  no sustained event-loop stall was observed.

### STAGE-05 — Current staged image was reused by the PoC

- Status: `PASS`
- Procedure: Complete STAGE-01, keep the same role and version selected, then
  run the former simulation again with normal Internet access.
- Expected: A conditional HEAD request returns 304. The staged payload is read
  back and its SHA-256 verified before the simulated radio operations start;
  the firmware body is not downloaded again.
- Evidence: On 2026-08-01, the second Router `20250403` simulation received
  HTTP 304 in 594 ms, reused the staged image without a replacement download,
  and verified SHA-256
  `832db3da669ee1371084cc3256fb34aed4f0a2642a5b915bbb0188a8e469cabb`
  before completing the simulated radio phases.

### STAGE-06 — Freshness-check failure falls back to verified staging

- Status: `NOT RUN`
- Procedure: Stage a valid image, block or interrupt the firmware HEAD request,
  and run the same selection again.
- Expected: The request failure is logged as a warning. The complete staged
  payload is SHA-256 verified and used without a firmware download.

### STAGE-07 — Selection changes preserve unrelated staging

- Status: `NOT RUN`
- Procedure: Stage one coordinator image, change the version and role selects
  several times without running an update, then return to the staged selection
  and run it.
- Expected: Select changes do not erase or invalidate the staged header. The
  original exact role, version, and filename still match and follow the normal
  conditional reuse path.

### STAGE-08 — Manual staged-image invalidation

- Status: `NOT RUN`
- Procedure: Stage an image, press **Clear Staged Zigbee Firmware**, reboot,
  and run the same selection again.
- Expected: Only the 4 KiB staging header is erased. Boot reports no verified
  staged image and the next run downloads the firmware again; stale payload
  bytes are never treated as valid.

### STAGE-09 — Corrupt staged payload fails closed

- Status: `NOT RUN`
- Procedure: Stage an image, alter one payload byte with a development build,
  then run the same selection. Repeat once with Internet access and once
  without it.
- Expected: Full readback SHA-256 rejects the payload and invalidates its
  header. Online, a fresh download is attempted; offline, the update fails
  without entering radio erase or write.

### CATALOG-01 — Boot refresh precedes the first HA connection

- Status: `NOT RUN`
- Procedure: Reboot once with a cached catalog and once after clearing stored
  catalog data. Capture logs through the first native API connection.
- Expected: After the network connects, each boot performs one conditional
  catalog refresh. HA is released immediately after a successful 200/304 result or
  after the configured startup timeout; a missing cache remains unavailable
  if the refresh fails.

### CATALOG-02 — Scheduled and manual catalog checks

- Status: `NOT RUN`
- Procedure: Set the YAML refresh time a few minutes ahead, confirm HA time is
  valid, observe the scheduled check, then press Refresh Firmware Catalog.
- Expected: Both paths perform one conditional request. An unchanged catalog
  retains the existing selects without forcing an API reconnect; changed
  visible options cause one reconnect after the complete option list is ready.

### CATALOG-03 — Catalog-check failure diagnostic

- Status: `NOT RUN`
- Procedure: Observe the entity before the first completed check, then force an
  HTTPS or manifest-parse failure and finally restore a valid 200/304 response.
- Expected: Firmware Catalog Check Failed starts unknown, becomes on after the
  failed check, and returns off after the next successful check. No internal
  retry is scheduled.

### TARGET-01 — Exact target selection survives restart

- Status: `PASS`
- Procedure: Select a firmware role and version, note the exact catalog
  filename from the log, press **Restart ESP32**, and observe both selects
  after HA reconnects.
- Expected: The saved role, version, and exact filename are restored after the
  catalog is available. Restarting does not download, invalidate, or otherwise
  modify the staged firmware image.
- Evidence: On 2026-08-01, the saved target was Router / `20250403` /
  `CC1352P7_router_20250403.bin`. After reboot, the live selects restored Router
  / `20250403`, and the existing staged image remained available.

### TARGET-02 — Missing saved build is not substituted

- Status: `NOT RUN`
- Procedure: Save a target selection, then use a development manifest that
  retains its role but omits that exact version and filename. Restore the entry
  in a later catalog check.
- Expected: The role remains selected but the firmware select reports `Select
  firmware...`; another build with the same version label is not substituted.
  The saved identity remains intact and is restored if the exact entry returns,
  unless the user explicitly chooses another target first.

### TARGET-03 — Staged-image migration fallback

- Status: `NOT RUN`
- Procedure: Use a development build to leave a verified staged image without a
  target-selection preference, install the current build, then restart once
  more.
- Expected: On the first upgraded boot, the staged role, version, and filename
  become the initial saved target. Subsequent boots restore the preference
  directly; later staged-image changes do not overwrite user target intent.

### TARGET-04 — Current-radio-role fallback

- Status: `PASS`
- Procedure: Start without a saved target selection or verified staged image,
  but with the current radio role available from restored or refreshed Zigbee
  metadata. Reboot and observe both target selects after the catalog loads.
- Expected: Target Firmware Role follows the current radio role. Target
  Firmware Version reports `Select firmware...` because knowing the installed
  role does not imply knowing its exact catalog build. If the current role is
  unknown or unavailable in the catalog, the configured preferred role is
  used instead.
- Evidence: On 2026-08-01, the fixed working-tree build reported the current
  Zigbee role as `Router`; its live web state exposed Target Firmware Role as
  `Router` and Target Firmware Version as `Select firmware...` while no staged
  image was present.

### FW-00 — Router same-image local install

- Status: `PASS`
- Procedure: Install the real-writer ESP32 build, keep Router `20250403`
  selected and its verified staged image intact, then press **Install Zigbee
  Firmware** while capturing logs and HA responsiveness. Keep ESP32 serial
  recovery available, but do not interrupt the first run.
- Expected: The conditional request reuses the staged image, SHA-256 readback
  succeeds, and the external transport is stopped only immediately before BSL.
  The writer synchronizes at 500000 baud, bank-erases the CC2652P7, writes all
  720896 bytes in acknowledged chunks, and accepts only a matching ROM CRC32.
  The ROM bootloader acknowledges `CMD_RESET`, the gateway holds the UART
  through the application-startup interval, and normal transport resumes
  without cycling device power. The selected role/version are retained as
  locally installed while the remaining mutable metadata awaits observation.
  Bank erase publishes the network as disconnected and clears the previous
  PAN/channel/parent snapshot. The Router must be commissioned again. HA,
  Ethernet, and logging remain responsive during the worker task.
- Evidence: On 2026-08-01, the first real installation reused the staged Router
  `20250403` image after HTTP 304 and verified its staged SHA-256. It
  bank-erased the CC2652P7, wrote all 720896 bytes, and matched ROM CRC32
  `0x673D9A56`. Radio work took 23782 ms and the complete update took 27850 ms;
  HA, Ethernet, progress reporting, and TCP recovery remained responsive. The
  radio did not rejoin after the software reset or commissioning pulse, but a
  complete PoE power cycle started the image and it rejoined successfully.
  The tested build exposed two metadata defects: role/version remained unknown
  and the old cached network membership still appeared connected. Both are
  corrected in the next build.

  The repeated installation on 2026-08-01 reused the same staged image after a
  conditional HTTP 304 in 879 ms, matched the staged SHA-256, wrote all 720896
  bytes, and again matched ROM CRC32 `0x673D9A56`. The ROM `CMD_RESET` was
  acknowledged, the 50 ms GPIO fallback was not used, normal UART ownership
  resumed after the 500 ms startup interval, and TCP port 6638 reopened. The
  complete update took 28667 ms. The gateway published firmware `20250403`,
  role `Router`, network status Disconnected, and network information status
  `Cleared`. With permit-join already enabled, Zigbee2MQTT reported a current
  update from the router without a UZG-01 power cycle. CRC verification still
  emitted three misleading UART timeout log entries before completing
  successfully; log cleanup is outstanding and does not affect the verified
  result.

### FW-01 — Coordinator upgrade

- Status: `NOT RUN`
- Procedure: Flash a newer compatible coordinator image and reconnect
  Zigbee2MQTT.
- Expected: Flash and verification succeed. The current full-bank installer
  erases the coordinator network state, so the network must be restored from a
  controller backup or recreated. Diagnostics retain last-known values with
  awaiting-observation provenance until normal ZNP traffic identifies the new
  image.

### FW-02 — Coordinator downgrade

- Status: `NOT RUN`
- Procedure: Flash a known compatible older coordinator image and reconnect.
- Expected: Transport remains transparent and the result matches the selected
  image; any network-storage compatibility limitation is recorded separately
  from gateway transport behavior.

### FW-03 — Coordinator-to-router conversion

- Status: `NOT RUN`
- Procedure: In a controlled maintenance window, flash router firmware and
  commission it into another Zigbee network.
- Expected: The gateway does not assume the transferred image type; cached role
  remains last-known during transfer, is replaced only after applicable normal
  protocol traffic identifies it, and the router factory-reset control works.

### FW-04 — Router-to-coordinator conversion

- Status: `NOT RUN`
- Procedure: Flash a compatible coordinator image back onto the radio.
- Expected: The radio becomes usable by Zigbee2MQTT after appropriate
  coordinator configuration/restore; the automatic local identification does
  not retain the old Router role merely because ESPHome did not inspect the
  image stream.

### FW-05 — Failure before bank erase

- Status: `NOT RUN`
- Procedure: With an instrumented development build, force BSL synchronization
  to fail before the erase command.
- Expected: Installation fails with a bounded timeout and a specific status,
  the radio is reset, its previous image still runs, the staged image remains
  valid for retry, and the external transport resumes.

### FW-06 — Interrupted write and staged-image recovery

- Status: `NOT RUN`
- Procedure: After one successful real install, repeat with controlled power
  loss after bank erase and before CRC verification, then reboot and retry the
  same target.
- Expected: The incomplete radio image may not boot, but the ESP32 and verified
  staging header survive. The next install can re-enter ROM BSL without relying
  on the radio application, rewrite the complete image, verify CRC32, and
  recover normal operation.

### FW-07 — Transport ownership during local install

- Status: `NOT RUN`
- Procedure: Repeat a safe same-image install once with an active TCP client
  and once in USB Bridged mode. Separately press Install in USB Direct mode.
- Expected: TCP or USB Bridged is stopped only after staged SHA-256 validation,
  the local writer remains the sole radio-UART owner, and the prior transport
  resumes after reset. USB Direct rejects the request before BSL or erase.

### FW-08 — Local and opaque BSL metadata transitions

- Status: `PARTIAL PASS`
- Procedure: Repeat FW-00 with the corrected build, then enter BSL once through
  a transparent remote maintenance session without performing a local install.
- Expected: A confirmed local bank erase publishes Disconnected, clears PAN,
  channel, parent IEEE, and extended PAN ID, and reports network information as
  `Cleared`. Successful ROM verification records the selected role and firmware
  version while stack and active IEEE await observation. An opaque remote BSL
  session clears image-owned metadata and makes current network membership
  unavailable while retaining the remaining network fields only as cached
  history.
- Evidence: The repeated FW-00 run verified the local half: bank erase cleared
  PAN, channel, parent IEEE, and extended PAN ID; published Disconnected and
  `Cleared`; and recorded Router `20250403` with `Awaiting Observation`. The
  opaque remote-BSL half remains untested.

### FW-09 — Post-flash hardware-reset fallback

- Status: `NOT RUN`
- Procedure: In an instrumented build, suppress or discard the ROM
  `CMD_RESET` ACK after a verified same-image write.
- Expected: The update logs the missing ACK, deasserts BSL, pulses `RESET_N`
  for 50 ms, waits 500 ms for application startup, and restores normal
  transport without requiring a device power cycle. The verified installation
  remains successful and the fallback is distinguishable in production logs.

## Physical controls, discovery, and LEDs

### HW-01 — Zigbee serial transport and red LED

- Status: `NOT RUN`
- Procedure: Cycle `TCP`, `USB Bridged`, and `USB Direct` in the `Zigbee Serial
  Transport` selector and with the physical button.
- Expected: GPIO33 is low for TCP and USB Bridged, high only for USB Direct.
  The red mode LED preserves the original UZG-01 allocation by remaining off
  for TCP and on for both USB choices. The selection survives reboot.

### HW-02 — Blue power LED

- Status: `NOT RUN`
- Procedure: Observe boot, normal operation, TCP connect/disconnect, and
  maintenance.
- Expected: The blue power LED remains steadily on.

### HW-03 — Yellow Zigbee2MQTT connection LED

- Status: `NOT RUN`
- Procedure: Observe the LED while connecting a silent provisional socket,
  connecting and disconnecting Zigbee2MQTT, opening a pending socket, and
  performing a maintenance takeover. Repeat once with a clean ESP32 shutdown.
- Expected: The gateway issues XZG-compatible
  `UTIL_LED_CONTROL(LED1, OFF/ON)` only while it locally owns the UART. The LED
  stays off for idle, provisional, and pending-only states; turns on before a
  confirmed normal client's buffered bytes are forwarded; and turns off after
  that client releases the UART or during a clean shutdown. A maintenance
  takeover does not inject LED traffic into the live normal transaction; its
  radio reset clears the LED while the old socket is parked. Failure or an
  unsupported Router image is logged but never prevents TCP ownership.

### HW-04 — mDNS discovery

- Status: `NOT RUN`
- Procedure: Discover the `_uzg-01._tcp` service and connect using the advertised
  address/port.
- Expected: Port, radio type, and baud-rate metadata match the active server.

### HW-05 — Router factory reset and recommissioning

- Status: `PASS`
- Procedure: With known compatible router firmware, invoke Factory Reset
  Zigbee Router, enable joining on the coordinator, and recommission the radio.
- Expected: The router leaves its former network, clears its saved network
  association, restarts, and enters pairing.
- Evidence: On 2026-08-01, firmware based on gateway commit `6b35e90` logged
  `Requesting Zigbee router factory reset and pairing mode` at 10:38:13 and
  `Zigbee router factory reset pulse complete` at 10:38:14. The router then
  paired successfully, confirmed by the operator.

### HW-06 — Router factory-reset Coordinator guard

- Status: `NOT RUN`
- Procedure: With the radio role known to be Coordinator, invoke Factory Reset
  Zigbee Router while capturing logs and, where practical, the configuration
  pin level.
- Expected: The component reports that the operation only applies to Router
  firmware, skips it, and does not pulse the configuration pin.

### HW-07 — Router power-cycle and automatic rejoin

- Status: `PASS`
- Procedure: With the Router commissioned and recently seen by Zigbee2MQTT,
  power-cycle the complete UZG-01 without invoking factory reset or permit
  join.
- Expected: The ESP32 and radio start normally; the Router retains the same
  IEEE and network association, rejoins automatically, and resumes recent
  last-seen updates in Zigbee2MQTT. ESPHome restores cached metadata without an
  intrusive identification pass.
- Evidence: On 2026-08-01, the complete PoE-powered UZG-01 was power-cycled.
  ESPHome and Home Assistant reconnected normally. The restored state retained
  the CC2652P7 identity, Router role, 720896-byte flash size, factory and parent
  IEEE addresses, PAN ID 28030, and channel 11. Metadata Status reported
  `Restored`; Network Information Status reported `Cached`; TCP returned idle
  with no pending or parked socket. Zigbee2MQTT initially retained an old
  `last_seen` value while the Router was inactive, then immediately reported it
  `just now` after a TX-power command succeeded, confirming automatic rejoin
  and bidirectional communication without factory reset or permit join.

## References

- [UZG-01 LED behavior](https://uzg.zig-star.com/getting-started/#led-behaviour)
- [XZG LED behavior](https://xzg.xyzroe.cc/hardware/#led-indicators)
- [XZG CCTools LED1 frames](https://github.com/xyzroe/XZG/blob/e37f4065d016f26a5bb68e07a1dd52ff425466eb/lib/CCTools/src/CCTools.h#L543-L550)
