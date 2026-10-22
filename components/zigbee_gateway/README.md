# `zigbee_gateway` component

ESPHome external component for the UZG-01 Zigbee radio, TCP server, USB serial
paths, maintenance controls, and diagnostics.

## Requirements

- ESP32 with ESP-IDF.
- ESPHome `network` and two `uart` components.
- Serial logger disabled with `logger.baud_rate: 0`.
- USB-facing UART declared before the radio UART so it receives UART0.

Install from GitHub:

```yaml
external_components:
  - source: github://yepiq/zigbee-gateway@main
    components: [zigbee_gateway]
```

See [`yzg.yaml`](../../yzg.yaml) for a complete UZG-01 configuration that
exposes every available entity. All entity parameters are optional: omitting
one keeps that function inside the component without exposing it to Home
Assistant or the web interface. When an entity is declared, the component
supplies its applicable category, device class, units, state class, accuracy,
and icon. Standard ESPHome entity options may override those defaults.

## Hardware and transport

| Parameter | Required | Default | Description |
| --- | --- | --- | --- |
| `id` | No | Generated | Component ID |
| `uart_id` | Yes | — | UART connected to the Zigbee radio |
| `usb_uart_id` | Yes | — | CH340-facing UART used by USB Bridged |
| `reset_pin` | Yes | — | Zigbee reset output |
| `bsl_pin` | Yes | — | TI radio bootloader/configuration input control |
| `mode_pin` | Yes | — | Hardware selector; high enables USB Direct on UZG-01 |
| `mode_led_pin` | Yes | — | Transport-mode LED output |
| `serial_transport` | No | Not exposed | Select entity with `TCP`, `USB Bridged`, and `USB Direct` |
| `tcp_port` | No | `6638` | Raw TCP serial port |
| `pending_socket_timeout` | No | `30s` | Time allowed for a pending socket to request maintenance |
| `parked_socket_timeout` | No | `10min` | Maximum time to retain a quarantined normal socket |
| `ip_address` | No | — | Existing IP-address text sensor used in logged flashing instructions |

## Transport diagnostics

Each optional parameter exposes one diagnostic entity:

| Parameter | Type | Published value |
| --- | --- | --- |
| `socket_connected` | Binary sensor | At least one active, pending, or parked TCP socket exists |
| `connection_count` | Sensor | Total active, pending, and parked TCP sockets |
| `transport_state` | Text sensor | `idle`, `provisional`, `normal`, or `maintenance` |
| `pending_socket` | Binary sensor | A maintenance candidate occupies the pending slot |
| `parked_socket` | Binary sensor | The previous normal client is quarantined |
| `last_transport_event` | Text sensor | Most recent TCP state transition |
| `rejected_connections` | Sensor | Connections rejected since boot |
| `pending_timeouts` | Sensor | Pending sockets timed out since boot |
| `maintenance_sessions` | Sensor | Maintenance sessions started since boot |
| `recovery_resets` | Sensor | Post-maintenance recovery resets since boot |

## Radio and network information

Each optional parameter exposes one information entity:

| Parameter | Type | Published value |
| --- | --- | --- |
| `hardware` | Text sensor | Detected TI radio family and variant |
| `flash_size` | Sensor | Radio flash capacity in bytes |
| `firmware` | Text sensor | ZNP firmware revision |
| `stack` | Text sensor | Z-Stack version |
| `factory_ieee` | Text sensor | Factory EUI-64 |
| `self_ieee` | Text sensor | Active IEEE address reported by ZNP |
| `role` | Text sensor | Coordinator, router, end device, or unknown |
| `tx_power` | Sensor | Passively observed ZNP transmit-power setting |
| `pan_id` | Sensor | Zigbee PAN ID |
| `channel` | Sensor | Zigbee channel |
| `on_network` | Binary sensor | Joined network state |
| `parent_ieee` | Text sensor | Parent/coordinator IEEE address |
| `extended_pan_id` | Text sensor | Extended PAN ID |
| `metadata_status` | Text sensor | Physical/running-image information provenance |
| `network_information_status` | Text sensor | Network-snapshot provenance |

Factory-resetting a Router clears its previous network snapshot. Joined state
remains unavailable until a later information refresh or passive observation
confirms the new network; YZG does not assume that commissioning succeeded.

## Controls

Each optional parameter exposes one control:

| Parameter | Type | Action |
| --- | --- | --- |
| `restart` | Button | Restart the TI radio |
| `enter_bsl` | Button | Prepare the TI radio for a firmware update |
| `router_factory_reset` | Button | Clear a compatible router's network association and start pairing |
| `refresh_metadata` | Button | Run local Zigbee identification |

`refresh_metadata` is intrusive. It runs only in TCP mode with no connected
socket and is rejected in both USB modes.

## Firmware catalog and radio update

The optional `firmware_update` block retrieves compatible images from an online
manifest and provides persistent target selection. Before installation, the
selected image is downloaded into ESP32 flash and verified by SHA-256. The
image must exactly match the detected radio flash size and retain a usable TI
ROM bootloader configuration for the device wiring.

Installation takes exclusive ownership from TCP or USB Bridged, writes the
complete image through the TI ROM bootloader, has the ROM verify the expected
CRC32 as the image is committed, and resets the radio. USB Direct is rejected
because the ESP32 has no UART access in that mode.

The current installer performs a full bank erase. This also erases the radio's
Zigbee network state. Back up a coordinator before updating it, and expect a
router to require commissioning again.

Enabling this block requires ESP-IDF and reserves a 768 KiB `zigbee_fw`
partition in the ESP32 flash. Its entities are independently optional.

| Parameter | Required | Default | Description |
| --- | --- | --- | --- |
| `id` | No | Generated | Firmware manager ID |
| `manifest_url` | Yes | — | HTTPS URL of the XZG-compatible firmware manifest |
| `chip` | Yes | — | TI radio model used to filter the manifest, such as `CC2652P7` |
| `preferred_role` | No | `coordinator` | Initially selected role when no saved target exists |
| `startup_timeout` | Yes | — | Maximum wait for the startup catalog check before allowing the native API to connect |
| `http_timeout` | Yes | — | Timeout for each catalog or firmware HTTP request |
| `max_manifest_size` | Yes | — | Largest manifest accepted in memory |
| `bootloader_backdoor_dio` | Yes | — | TI radio DIO driven to request the ROM bootloader |
| `bootloader_backdoor_active_high` | Yes | — | Active level of that TI radio DIO |

| Entity parameter | Type | Purpose |
| --- | --- | --- |
| `target_firmware_role` | Select | Select coordinator or router firmware |
| `target_firmware_version` | Select | Select an exact compatible catalog build |
| `refresh_firmware_catalog` | Button | Check the catalog immediately |
| `install_firmware` | Button | Download, stage, verify, and install the selected image |
| `invalidate_staged_firmware` | Button | Discard the verified staged image |
| `firmware_catalog_status` | Text sensor | Catalog availability and last check result |
| `firmware_update_status` | Text sensor | Current or final update stage |
| `firmware_update_progress` | Sensor | Combined download and radio-update progress |
| `firmware_catalog_check_failed` | Binary sensor | Whether the most recent catalog check failed |

## ESPHome 2026.10 improvement backlog

Reviewed on 2026-10-23 against ESPHome 2026.10.0 and the current UZG-01
implementation. These are opportunities to evaluate, not implemented gateway
features or measured performance gains. `esphome config yzg.yaml` passed with
2026.10.0; this review did not include a firmware build or hardware testing.

### Priority component changes

1. **Make mDNS discovery follow TCP availability.** Use the new ESP32 runtime
   service enable/disable API to advertise `_uzg-01._tcp` only while the Zigbee
   TCP listener is available. The current YAML advertises it in USB modes too.
   Request support through `mdns.request_service_enable_disable()` during code
   generation, then use `MDNSComponent::set_service_enabled()` once mDNS is
   ready. Verify discovery after boot in each transport, mode changes, and
   Ethernet reconnection; keep ESPHome management discovery available.
2. **Use the ESP-IDF UART input-flush helper.** Evaluate replacing byte-by-byte
   discard loops in `ZigbeeSerialInterface::drain()` and
   `ZigbeeUartStreamEndpoint::drain()` with `IDFUARTComponent::flush_input()`.
   The helper clears both the peek cache and driver RX buffer. Preserve the
   ownership checks and distinguish RX discard from waiting for TX completion.
   Retest transport changes, passive observation, BSL entry, and maintenance
   recovery so stale bytes cannot cross an ownership handoff.

Both APIs are listed in the
[2026.10 changelog](https://esphome.io/changelog/2026.10.0/).
Use the [hardware test plan](../../HARDWARE_TEST_PLAN.md), particularly discovery,
USB serial transports, and maintenance/UART exclusion, to record validation.

### Build and resource opportunities

- **Compressed ESP32 OTA:** automatically negotiated when the uploader and
  running firmware support it. Upstream reports roughly one-third fewer bytes
  transferred; measure the benefit on our Ethernet gateway. The first update
  from older firmware remains uncompressed. This applies to ESPHome host
  firmware, not the custom TI-radio installer.
- **RAM and state-publication improvements:** rebuilding picks up upstream
  changes such as static select options in flash and web-server state events
  without heap allocation. Compare free heap and responsiveness during catalog
  refresh, downloads, and diagnostic publication. Runtime firmware-catalog
  select options still need their dynamic storage; the existing
  `set_options(FixedVector<...>)` overload remains supported in 2026.10.0.
- **Faster ESP-IDF builds:** upstream build improvements are automatic.
  Evaluate `--skip-bootloader` for development builds intended only for OTA;
  keep full builds for factory/USB installation and release validation.
- **Flash-vendor selection:** consider `esp32.framework.advanced.flash_chip`
  after identifying the fitted flash chip and checking supported UZG-01 board
  revisions. It removes unused vendor drivers; measure actual image/RAM savings
  rather than applying the advertised ESP32-S3 octal-flash figures to this board.

See the [release overview](https://esphome.io/blog/2026/10/21/esphome-2026-10/)
and [select API migration notes](https://developers.esphome.io/blog/2026/10/05/select-options-and-light-effects-move-to-constvector/).

### Transport alternatives and hardware limits

- **Native `uart_tcp`:** offers a UART/TCP bridge, connection diagnostics, and
  IPv4 allowlists. It requires exclusive UART use and serves one TCP connection
  at a time. Adoption would need to preserve our maintenance rendezvous, parked
  clients, USB switching, and passive ZNP observation. Retain the current
  transport state machine for now; evaluate upstream primitives individually.
  See the [UART TCP documentation](https://esphome.io/components/uart_tcp/).
- **Native USB bridge and `uart_mux`:** the new CDC-ACM bridge and its mux support
  ESP32-S2/S3/P4 native USB. They cannot replace the classic ESP32/CH340 path on
  UZG-01. Revisit for a future board with suitable native USB wiring. See the
  [CDC-ACM bridge documentation](https://esphome.io/components/bridge/cdc_acm_uart/).
