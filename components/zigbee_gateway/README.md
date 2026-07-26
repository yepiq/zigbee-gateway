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

See [`yzg.yaml`](../../yzg.yaml) for a complete UZG-01 configuration. The
component creates its controls and information entities automatically. Add an
entity parameter only to override standard ESPHome options such as `name`,
`icon`, `internal`, or `disabled_by_default`.

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
| `serial_transport` | No | `Zigbee Serial Transport` | Select entity with `TCP`, `USB Bridged`, and `USB Direct` |
| `tcp_port` | No | `6638` | Raw TCP serial port |
| `pending_socket_timeout` | No | `30s` | Time allowed for a pending socket to request maintenance |
| `parked_socket_timeout` | No | `10min` | Maximum time to retain a quarantined normal socket |
| `ip_address` | No | — | Existing IP-address text sensor used in logged flashing instructions |

## Transport diagnostics

These diagnostic entities are created automatically:

| Parameter | Default entity | Published value |
| --- | --- | --- |
| `socket_connected` | Socket Status | At least one active, pending, or parked TCP socket exists |
| `connection_count` | Socket Connections | Total active, pending, and parked TCP sockets |
| `transport_state` | Zigbee TCP State | `idle`, `provisional`, `normal`, or `maintenance` |
| `pending_socket` | Zigbee TCP Pending Socket | A maintenance candidate occupies the pending slot |
| `parked_socket` | Zigbee TCP Parked Socket | The previous normal client is quarantined |
| `last_transport_event` | Zigbee TCP Last Event | Most recent TCP state transition |
| `rejected_connections` | Zigbee TCP Rejected Connections | Connections rejected since boot |
| `pending_timeouts` | Zigbee TCP Pending Timeouts | Pending sockets timed out since boot |
| `maintenance_sessions` | Zigbee TCP Maintenance Sessions | Maintenance sessions started since boot |
| `recovery_resets` | Zigbee TCP Recovery Resets | Post-maintenance recovery resets since boot |

## Radio and network information

These information entities are created automatically:

| Parameter | Default entity | Published value |
| --- | --- | --- |
| `hardware` | Zigbee Hardware | Detected TI radio family and variant |
| `flash_size` | Zigbee Flash Size | Radio flash capacity in bytes |
| `firmware` | Zigbee Firmware | ZNP firmware revision |
| `stack` | Zigbee Stack | Z-Stack version |
| `factory_ieee` | Zigbee Factory IEEE Address | Factory EUI-64 |
| `self_ieee` | Zigbee Active IEEE Address | Active IEEE address reported by ZNP |
| `role` | Zigbee Role | Coordinator, router, end device, or unknown |
| `tx_power` | Zigbee TX Power | Passively observed ZNP transmit-power setting |
| `pan_id` | Zigbee PAN ID | Zigbee PAN ID |
| `channel` | Zigbee Channel | Zigbee channel |
| `on_network` | Zigbee Network Status | Joined network state |
| `parent_ieee` | Zigbee Parent IEEE Address | Parent/coordinator IEEE address |
| `extended_pan_id` | Zigbee Extended PAN ID | Extended PAN ID |
| `metadata_status` | Zigbee Metadata Status | Physical/running-image information provenance |
| `network_information_status` | Zigbee Network Information Status | Network-snapshot provenance |

## Controls

These controls are created automatically:

| Parameter | Default entity | Action |
| --- | --- | --- |
| `restart` | Restart Zigbee | Restart the TI radio |
| `enter_bsl` | Zigbee BSL Mode | Prepare the TI radio for a firmware update |
| `router_rejoin` | Zigbee Router Rejoin | Allow router firmware to join or rejoin a network |
| `refresh_metadata` | Refresh Zigbee Information | Run local Zigbee identification |

`refresh_metadata` is intrusive. It runs only in TCP mode with no connected
socket and is rejected in both USB modes.
