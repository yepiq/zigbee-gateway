# UZG-01 Hardware Test Plan

This is the durable acceptance checklist for the Zigbee Gateway project. Add or
revise cases whenever behavior changes; do not rely on an old chat transcript
when the UZG-01 becomes available again.

## Status vocabulary

- `NOT RUN`: implemented but not yet exercised on hardware.
- `PASS`: observed result matches the expected result.
- `FAIL`: observed result does not match the expected result; retain evidence.
- `BLOCKED`: the test cannot proceed because a prerequisite is missing.
- `RETEST`: previously passed, but a later change affects this behavior.
- `PLANNED`: desired behavior is recorded but not implemented yet.

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
  complete without concurrent TCP UART access. A verified record is committed
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
  accurately reports `Cached`, `Observed`, `Refreshed`, or `Unavailable`.

### BAS-04 — ESP32 OTA update

- Status: `NOT RUN`
- Procedure: Perform an ESPHome OTA update with Zigbee2MQTT stopped, then repeat
  with it running after the ordinary TCP path has passed.
- Expected: ESP32 returns cleanly, cached Zigbee information restores without
  toggling the radio pins, and Zigbee2MQTT reconnects without radio network
  loss.

## Persistent radio metadata

### CACHE-01 — First identification creates a clean snapshot

- Status: `NOT RUN`
- Procedure: Start with no compatible Zigbee metadata preference, boot once,
  and retain the full log through listener startup.
- Expected: Status progresses from `Unavailable` through `Refreshing` to
  `Verified`; one complete local identification runs; the log reports a saved
  generation; all available entities contain the identified values.

### CACHE-02 — Clean reboot uses the fast restore path

- Status: `NOT RUN`
- Procedure: After CACHE-01, power-cycle the ESP32 and capture the boot log and
  radio reset/BSL pins if test equipment is available.
- Expected: Status becomes `Restored`; the saved values publish immediately;
  logs explicitly say the startup probe was skipped; neither reset nor BSL is
  driven for identification.

### CACHE-03 — Manual refresh while idle

- Status: `NOT RUN`
- Procedure: Stop Zigbee2MQTT and press the Refresh Zigbee Information
  diagnostic button.
- Expected: The component exclusively claims UART, marks the record dirty,
  identifies BSL/NV/ZNP information, resets the radio, saves the next clean
  generation, reports `Verified`, and releases UART for the next TCP client.

### CACHE-04 — BSL maintenance defers refresh to normal traffic

- Status: `NOT RUN`
- Procedure: Perform any successful remote BSL session, note metadata status
  and logs, close the maintenance connection, and let Zigbee2MQTT reconnect.
- Expected: The running-image record is committed pending immediately before BSL pin entry;
  physical identity remains valid and old network values remain `Cached`
  during maintenance. After the maintenance socket closes, the gateway does
  not re-enter BSL, scan NV, or delay the returning normal TCP client; running
  image status remains `Awaiting Observation` until normal ZNP startup traffic
  identifies it.

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
  published as last-known, image status is `Awaiting Observation`, and network
  status remains `Cached`. A later normal client may refresh them passively.

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
  effect; metadata refresh still completes before normal UART ownership.

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
  ESPHome waits until that maintenance socket closes before taking UART for its
  own metadata refresh.

### MNT-06 — Parked Zigbee2MQTT behavior

- Status: `NOT RUN`
- Procedure: During a several-minute maintenance session, observe the original
  Zigbee2MQTT process and gateway connection counters.
- Expected: Its socket remains open while possible, incoming requests are
  drained and discarded, and it never reaches UART. When maintenance ends, it
  receives one abortive close; local metadata refresh completes; it then
  reconnects cleanly.

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

### FW-01 — Coordinator upgrade

- Status: `NOT RUN`
- Procedure: Flash a newer compatible coordinator image and reconnect
  Zigbee2MQTT.
- Expected: Flash and verification succeed, the existing Zigbee network is
  preserved when the image/storage format supports it, and diagnostics reflect
  the new firmware after the automatic post-maintenance identification pass.

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
  remains stale during transfer, is replaced only after local identification,
  and router rejoin control works.

### FW-04 — Router-to-coordinator conversion

- Status: `NOT RUN`
- Procedure: Flash a compatible coordinator image back onto the radio.
- Expected: The radio becomes usable by Zigbee2MQTT after appropriate
  coordinator configuration/restore; the automatic local identification does
  not retain the old Router role merely because ESPHome did not inspect the
  image stream.

## Physical controls, discovery, and LEDs

### HW-01 — Zigbee serial transport and red LED

- Status: `NOT RUN`
- Procedure: Toggle between `Zigbee TCP Serial` and `Zigbee USB Serial` from
  ESPHome and with the physical button.
- Expected: GPIO33 selects the intended serial path; the red mode LED preserves
  the original UZG-01 allocation and matches the selected transport; the
  selection survives reboot according to the configured restore behavior.

### HW-02 — Blue power LED

- Status: `NOT RUN`
- Procedure: Observe boot, normal operation, TCP connect/disconnect, and
  maintenance.
- Expected: Current ESPHome behavior is documented. Decide separately whether
  to reproduce XZG's connection-dependent blink/on behavior.

### HW-03 — Yellow Zigbee2MQTT connection LED

- Status: `NOT RUN`
- Historical intent: UZG-01 documents the yellow/white LED as on when
  Zigbee2MQTT is connected. The original YAML exposed ZNP
  `UTIL_LED_CONTROL(LED1, OFF/ON)` frames as an internal UART switch, but no
  automation invoked it.
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

### HW-05 — Router rejoin pulse

- Status: `NOT RUN`
- Procedure: With known router firmware, invoke Router Rejoin and recommission
  the radio.
- Expected: BSL/rejoin pin timing produces pairing behavior without a competing
  TCP UART owner.

## Deferred optimization tests

Add cases here when the protocol-aware preemption parser is implemented:

- Detect ZNP transaction boundaries without modifying the opaque stream.
- Prefer takeover between transactions, but enforce a strict maximum wait.
- Confirm malformed or partial frames cannot block maintenance indefinitely.
- Repeat every maintenance test above with busy Zigbee traffic.

## Historical LED references

- [UZG-01 LED behavior](https://uzg.zig-star.com/getting-started/#led-behaviour)
- [XZG LED behavior](https://xzg.xyzroe.cc/hardware/#led-indicators)
- [XZG CCTools LED1 frames](https://github.com/xyzroe/XZG/blob/e37f4065d016f26a5bb68e07a1dd52ff425466eb/lib/CCTools/src/CCTools.h#L543-L550)
