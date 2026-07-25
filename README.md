# Yepiq Zigbee Gateway

ESPHome firmware for the ZigStar UZG-01 that exposes its TI Zigbee radio as an
exclusive TCP serial adapter while retaining local diagnostics, Home Assistant
entities, and remote radio-firmware maintenance.

The current source compiles with ESPHome 2026.7.2. The component and its host
tests are implemented, but the refactored UART and maintenance paths have not
yet been exercised on a physical UZG-01. Treat this as development firmware and
use the [hardware test plan](HARDWARE_TEST_PLAN.md) before deploying it on a
production coordinator.

This project is inspired by [xyzroe/XZG](https://github.com/xyzroe/XZG). Its
radio identification, storage-layout handling, discovery behavior, and remote
maintenance compatibility intentionally follow XZG and the original UZG
firmware where ESPHome permits. The Home Assistant diagnostics and the explicit
UART-ownership model are project-specific additions.

## What is implemented

- Ethernet TCP-to-UART transport for ZNP on configurable port 6638.
- Exclusive arbitration between local diagnostics, the normal TCP client, and
  a maintenance client.
- Compatibility HTTP endpoints `/cmdZigBSL` and `/cmdZigRST` for legacy
  ZigStarGW-MT and current XZG-MT-style workflows.
- Connection-first and command-first maintenance ordering.
- Remote BSL flashing with opaque pass-through of the flashing protocol.
- Persistent physical identity, running-image information, and last-known
  Zigbee network information.
- Passive observation of ordinary ZNP responses without consuming, delaying,
  or changing the client byte stream.
- Home Assistant controls and transport diagnostics.
- USB/TCP serial-path selection using the UZG-01 hardware switch.

The former `esphome-stream-server` dependency is no longer used.

## Hardware profile

The working configuration in [`yzg.yaml`](yzg.yaml) targets the UZG-01:

| Function | ESP32 pin | Notes |
| --- | --- | --- |
| Zigbee UART RX | GPIO36 | 115200 baud by default |
| Zigbee UART TX | GPIO4 | 115200 baud by default |
| Zigbee reset | GPIO16 | Inverted |
| Zigbee BSL/rejoin | GPIO32 | Inverted |
| USB/TCP serial selector | GPIO33 | `ON` selects USB serial |
| Mode button | GPIO35 | Toggles the serial selector |
| Red mode LED | GPIO12 | Strapping pin |
| Blue power LED | GPIO14 | Always on in the current configuration |
| LAN8720 MDC | GPIO23 |  |
| LAN8720 MDIO | GPIO18 |  |
| LAN8720 clock | GPIO17 | Clock output |
| LAN8720 power | GPIO5 | Strapping pin |

Board wiring, UART baud rate, TCP port, and TCP session-policy timeouts are
declared as substitutions at the start of `yzg.yaml`. ZNP/BSL parser timing and
the supported chip/storage layouts are implementation policy in the component,
not per-device YAML settings.

The detected chip family selects both forms of storage geometry independently:

| Family | `FLASH_SIZE` unit | NVOCMP region | NVOCMP page |
| --- | --- | --- | --- |
| CC13x2/CC26x2 | 4 KiB | `0x50000` + `0x6000` | 8 KiB (3 pages) |
| CC13x2x7/CC26x2x7 | 8 KiB | `0xA6000` + `0x8000` | 8 KiB (4 pages) |

The `FLASH_SIZE` multiplier is therefore never reused as the NVOCMP page size.
An unidentified family is not assigned a fallback layout: layout-dependent
CCFG and NV reads are skipped and the physical identity remains unverified.

### LED terminology and behavior

The names below describe the physical indicators rather than implying Zigbee
transport over a particular medium.

| Indicator | Original UZG behavior | XZG reference behavior | Current project behavior |
| --- | --- | --- | --- |
| Blue power LED | Constant while powered | Blinks without a network TCP client; constant with one | Constant while powered |
| Red mode LED | On for USB serial; off for Ethernet serial | On for USB, off for network, with additional status patterns | On for **USB**; off for **TCP** |
| Yellow/white Zigbee LED | On after Zigbee2MQTT connects | Controlled through the Zigbee radio | Radio `LED1` is turned on at normal TCP-session admission and off at disconnect |

The yellow/white indicator is connected to the Zigbee radio, not an ESP32 GPIO.
The component controls it using `UTIL_LED_CONTROL` only while local code owns
the UART. Router firmware may not provide that ZNP command; failure is logged
and never blocks TCP admission.

See the original [UZG LED description](https://uzg.zig-star.com/getting-started/#led-behaviour)
and [XZG LED behavior](https://xzg.xyzroe.cc/hardware/#led-indicators).

## UART ownership

Every consuming UART read and every UART write goes through one
`ZigbeeSerialInterface`. It permits exactly one owner:

| Owner | Purpose |
| --- | --- |
| Local | Chip identification, explicit diagnostics, reset handling, and the radio connection LED |
| TCP normal | Transparent Zigbee2MQTT or other ZNP traffic |
| TCP maintenance | Transparent BSL/flashing-tool traffic |

Local work has priority at startup and at explicitly protected session
boundaries. While local code owns the UART, the TCP server does not classify new
clients or touch active sockets. A manual information refresh does not preempt a
live TCP owner; it is rejected and can be retried after the client disconnects.

Passive ZNP observation is not another owner. The UART debugger sees bytes that
have already passed through the normal transparent stream, validates complete
UNPI frames, and queues recognized information for later publication. It never
sends a query or consumes a response.

## TCP and maintenance behavior

The listener uses one active socket, one pending socket, and, during takeover,
one temporarily parked former normal socket.

1. The first connection is provisional. Its first TCP payload classifies it as
   the normal transparent client and is forwarded only after the protected
   connection-LED transaction finishes.
2. While a normal client is active, the first additional connection is held as
   pending for `tcp_pending_socket_timeout_ms` (30 seconds by default).
3. Further connections are rejected while that pending slot is occupied. The
   first pending client is preserved.
4. If no maintenance command arrives, the pending client is closed and the
   normal client continues uninterrupted.
5. When `/cmdZigBSL` or `/cmdZigRST` identifies the pending client as the
   maintenance client, the normal socket is parked at the last practical
   moment, the pending client becomes the sole UART owner, and the requested
   pin operation is performed.
6. Bytes received from the parked client are discarded so Zigbee2MQTT remains
   connected but cannot interfere with maintenance. The parked-socket safety
   timeout defaults to ten minutes.
7. At maintenance completion, a radio that may remain in BSL is reset and the
   parked socket is closed abortively so Zigbee2MQTT can reconnect cleanly.

This also supports the two tool orderings:

- **Connection first:** the flashing tool opens the TCP socket, then calls
  `/cmdZigBSL` or `/cmdZigRST`.
- **Command first:** the tool calls `/cmdZigBSL` first and opens the TCP socket
  afterward. If a normal owner exists, it is kept running while the rendezvous
  is armed; BSL entry is deferred until the maintenance connection arrives or
  the normal owner leaves.

The radio flashing payload is deliberately opaque. ESPHome does not interpret
or modify BSL traffic, and therefore does not assume whether the resulting
image is a coordinator, router, upgrade, or downgrade.

## Persisted Zigbee information

Information is divided by its actual invalidation lifetime:

| Scope | Values | Refresh rule |
| --- | --- | --- |
| Physical identity | Chip/family information, flash capacity, factory IEEE | Kept indefinitely for the soldered radio; read again only if the cache is absent or incompatible |
| Running image | Firmware build, stack version, active ZNP IEEE, role, CCFG mode and BSL settings | Marked pending before BSL access; ordinary `SYS_VERSION` and `UTIL_GET_DEVICE_INFO` responses refresh the observable fields |
| Network snapshot | PAN ID, channel, joined state, parent IEEE, extended PAN ID | Restored as last-known data and refreshed from ordinary `ZDO_EXT_NWK_INFO` and `ZDO_STATE_CHANGE_IND` traffic |

The gateway does not query the UART at boot when valid physical identity exists.
It also does not run a BSL/NV scan after remote flashing. The returning normal
client gets maximum access time, and the gateway learns the new image and
network information from traffic that client already requested.

`Refresh Zigbee Information` remains available as a development and
troubleshooting control. It is intrusive, requires the TCP transport to be
idle, and is not part of normal metadata collection.

### Information status values

`Zigbee Metadata Status` describes physical/running-image information:

- `Restored`: valid cached information was published without touching UART.
- `Refreshing`: an explicit local identification pass is running.
- `Verified`: an explicit local identification pass completed.
- `Observed`: the running image was identified from normal ZNP traffic.
- `Awaiting Observation`: BSL access may have changed image-owned values.
- `Unavailable`: required information is not available.

`Zigbee Network Information Status` describes the separate network snapshot:

- `Cached`: last-known information was restored but not fully observed this
  boot.
- `Observed`: a complete network-information response was seen in normal
  traffic.
- `Refreshed`: an explicit local diagnostic pass obtained the snapshot.
- `Unavailable`: no valid network snapshot exists.

## Home Assistant surface

The example configuration exposes:

- Radio identity and state: hardware, flash size, firmware, stack, factory and
  active IEEE, role, TX power, PAN ID, channel, joined state, parent IEEE, and
  extended PAN ID.
- Transport state: active state, socket count, pending/parked flags, last event,
  rejected connections, pending timeouts, maintenance sessions, and recovery
  resets.
- Controls: restart Zigbee, enter BSL, router rejoin, temporary manual
  information refresh, and the **TCP** / **USB** options of the
  **Zigbee Serial Transport** selector.

Transport counters, cache provenance, and connection topology are diagnostic
entities.

## Build and validation

Install ESPHome, clone the repository, review the substitutions in `yzg.yaml`,
then validate and compile:

```sh
esphome -q config yzg.yaml
esphome -q compile yzg.yaml
```

The standalone logic tests require only a C++17 compiler:

```sh
c++ -std=c++17 -Wall -Wextra -pedantic -I. tests/zigbee_metadata_cache_test.cpp -o /tmp/zigbee_metadata_cache_test
/tmp/zigbee_metadata_cache_test

c++ -std=c++17 -Wall -Wextra -pedantic -I. tests/zigbee_tcp_state_test.cpp -o /tmp/zigbee_tcp_state_test
/tmp/zigbee_tcp_state_test

c++ -std=c++17 -Wall -Wextra -pedantic -I. tests/zigbee_znp_observer_test.cpp -o /tmp/zigbee_znp_observer_test
/tmp/zigbee_znp_observer_test

c++ -std=c++17 -Wall -Wextra -pedantic -I. tests/zigbee_chip_layout_test.cpp -o /tmp/zigbee_chip_layout_test
/tmp/zigbee_chip_layout_test
```

The host tests validate deterministic state and parser logic. They do not
replace the UZG-01 acceptance scenarios in `HARDWARE_TEST_PLAN.md`, including
legacy/current flashing-tool interoperability and Zigbee2MQTT recovery.

## Project layout

- [`yzg.yaml`](yzg.yaml): complete UZG-01 ESPHome configuration.
- [`components/zigbee_gateway`](components/zigbee_gateway): external component,
  UART arbitration, TCP transport, protocols, caches, and passive observer.
- [`components/zigbee_gateway/zigbee_chip_layout.h`](components/zigbee_gateway/zigbee_chip_layout.h):
  detected-family flash and NVOCMP geometry.
- [`tests`](tests): dependency-free host tests.
- [`HARDWARE_TEST_PLAN.md`](HARDWARE_TEST_PLAN.md): durable bench checklist and
  deferred feature tests.

## License

This repository is licensed under the [Apache License 2.0](LICENSE). XZG and
other referenced projects remain under their respective licenses.
