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

See [`yzg.yaml`](../../yzg.yaml) for a complete UZG-01 configuration. All entity
parameters accept the standard ESPHome options for their entity type.

## Hardware and transport

| Parameter | Required | Default | Description |
| --- | --- | --- | --- |
| `id` | No | Generated | Component ID |
| `uart_id` | Yes | — | UART connected to the Zigbee radio |
| `usb_uart_id` | Yes | — | CH340-facing UART used by USB Bridged |
| `reset_pin` | Yes | — | Zigbee reset output |
| `bsl_pin` | Yes | — | Zigbee BSL/router-rejoin output |
| `mode_pin` | Yes | — | Hardware selector; high enables USB Direct on UZG-01 |
| `mode_led_pin` | Yes | — | Transport-mode LED output |
| `serial_transport` | Yes | — | Select entity with `TCP`, `USB Bridged`, and `USB Direct` |
| `tcp_port` | No | `6638` | Raw TCP serial port |
| `pending_socket_timeout` | No | `30s` | Time allowed for a pending socket to request maintenance |
| `parked_socket_timeout` | No | `10min` | Maximum time to retain a quarantined normal socket |
| `ip_address` | No | — | Existing IP-address text sensor used in logged flashing instructions |

## Transport diagnostics

These entity parameters are required:

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

These entity parameters are required:

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

These button parameters are optional:

| Parameter | Action |
| --- | --- |
| `restart` | Restart Zigbee |
| `enter_bsl` | Enter Zigbee BSL mode |
| `router_rejoin` | Pulse the router-rejoin pin |
| `refresh_metadata` | Run local Zigbee identification |

`refresh_metadata` is intrusive. It runs only in TCP mode with no connected
socket and is rejected in both USB modes.
