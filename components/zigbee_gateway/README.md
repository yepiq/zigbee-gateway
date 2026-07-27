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

## Firmware catalog and staged-update simulation

The optional `firmware_update` block retrieves compatible images from an online
manifest and provides persistent target selection. The current update action
downloads and verifies the real image, but simulates the radio erase, write,
and verification stages without changing the Zigbee radio.

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

| Entity parameter | Type | Purpose |
| --- | --- | --- |
| `target_firmware_role` | Select | Select coordinator or router firmware |
| `target_firmware_version` | Select | Select an exact compatible catalog build |
| `refresh_firmware_catalog` | Button | Check the catalog immediately |
| `simulate_firmware_update` | Button | Download, stage, verify, and simulate installing the selected image |
| `invalidate_staged_firmware` | Button | Discard the verified staged image |
| `firmware_catalog_status` | Text sensor | Catalog availability and last check result |
| `firmware_update_status` | Text sensor | Current or final update-simulation stage |
| `firmware_update_progress` | Sensor | Combined download and simulation progress |
| `firmware_catalog_check_failed` | Binary sensor | Whether the most recent catalog check failed |
