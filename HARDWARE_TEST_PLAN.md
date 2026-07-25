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
  the web server and native API become reachable, and TCP port 6638 starts
  listening only after the startup radio probe finishes.

### BAS-02 — Startup radio identification

- Status: `NOT RUN`
- Procedure: Capture complete logs from power-on through TCP-listener startup.
- Expected: BSL chip identification, flash-size detection, NVOCMP inspection,
  hardware identity, radio reset, and applicable ZNP firmware diagnostics
  complete without concurrent TCP UART access.

### BAS-03 — Diagnostic entity consistency

- Status: `NOT RUN`
- Procedure: Compare published hardware, flash size, firmware, role, IEEE,
  network state, PAN ID, channel, parent IEEE, and extended PAN ID with the
  radio firmware and Zigbee2MQTT information.
- Expected: Supported values agree; unavailable or inapplicable values are
  reported as unknown rather than fabricated.

### BAS-04 — ESP32 OTA update

- Status: `NOT RUN`
- Procedure: Perform an ESPHome OTA update with Zigbee2MQTT stopped, then repeat
  with it running after the ordinary TCP path has passed.
- Expected: ESP32 returns cleanly, startup diagnostics complete, and
  Zigbee2MQTT reconnects without radio network loss.

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

### MNT-06 — Parked Zigbee2MQTT behavior

- Status: `NOT RUN`
- Procedure: During a several-minute maintenance session, observe the original
  Zigbee2MQTT process and gateway connection counters.
- Expected: Its socket remains open while possible, incoming requests are
  drained and discarded, and it never reaches UART. When maintenance ends, it
  receives one abortive close and reconnects cleanly.

### MNT-07 — Aborted maintenance recovery

- Status: `NOT RUN`
- Procedure: Abort the flasher after entering BSL but before its normal reset.
- Expected: Maintenance disconnect triggers a hardware recovery reset, the
  parked socket is closed afterward, and Zigbee2MQTT can reconnect to whatever
  valid radio image remains.

### MNT-08 — BSL rendezvous timeout

- Status: `NOT RUN`
- Procedure: With no active client, send `/cmdZigBSL` but do not connect a
  flashing socket for more than 30 seconds.
- Expected: The rendezvous expires and the ESP32 resets the radio out of BSL.

### MNT-09 — Parked-client safety limit

- Status: `NOT RUN`
- Procedure: Hold a maintenance connection beyond the configured 10-minute
  parked-client limit.
- Expected: The old Zigbee2MQTT socket is closed at the safety limit while the
  active maintenance connection remains exclusive.

## Radio firmware and role changes

### FW-01 — Coordinator upgrade

- Status: `NOT RUN`
- Procedure: Flash a newer compatible coordinator image and reconnect
  Zigbee2MQTT.
- Expected: Flash and verification succeed, the existing Zigbee network is
  preserved when the image/storage format supports it, and diagnostics reflect
  the new firmware after a fresh identification pass.

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
  becomes Unknown until identified, and router rejoin control works.

### FW-04 — Router-to-coordinator conversion

- Status: `NOT RUN`
- Procedure: Flash a compatible coordinator image back onto the radio.
- Expected: The radio becomes usable by Zigbee2MQTT after appropriate
  coordinator configuration/restore; ESPHome does not retain the old Router
  role merely because it did not inspect the image stream.

## Physical controls, discovery, and LEDs

### HW-01 — LAN/USB mode and red LED

- Status: `NOT RUN`
- Procedure: Toggle LAN/USB mode from ESPHome and with the physical button.
- Expected: GPIO33 selects the intended path; the red mode LED matches the
  selected mode and survives reboot according to the configured restore
  behavior.

### HW-02 — Blue power LED

- Status: `NOT RUN`
- Procedure: Observe boot, normal operation, TCP connect/disconnect, and
  maintenance.
- Expected: Current ESPHome behavior is documented. Decide separately whether
  to reproduce XZG's connection-dependent blink/on behavior.

### HW-03 — Yellow Zigbee2MQTT connection LED

- Status: `PLANNED`
- Historical intent: UZG-01 documents the yellow/white LED as on when
  Zigbee2MQTT is connected. The original YAML exposed ZNP
  `UTIL_LED_CONTROL(LED1, OFF/ON)` frames as an internal UART switch, but no
  automation invoked it.
- Required implementation: issue the command through the UART owner/arbitration
  layer at a safe transaction boundary; never restore the old independent
  `platform: uart` writer.
- Expected after implementation: yellow LED turns on only for a confirmed
  normal Zigbee client and turns off after its session ends; pending,
  maintenance, and parked sockets must not falsely indicate a normal session.

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
