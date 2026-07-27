# Yepiq Zigbee Gateway

Yepiq Zigbee Gateway (YZG) is ESPHome firmware for the ZigStar UZG-01. It lets
Zigbee2MQTT use the built-in TI Zigbee radio over Ethernet or USB, while
Home Assistant provides transport selection, diagnostics, radio controls, and
ESP32 OTA updates.

YZG is inspired by [xyzroe/XZG](https://github.com/xyzroe/XZG) and supports the
same UZG-01 hardware and common Zigbee firmware-update tools.

TCP, USB, and firmware-update behavior still requires testing on a physical
UZG-01. Follow the [hardware test plan](HARDWARE_TEST_PLAN.md) before relying on
YZG in production.

## Install

### ESPHome Device Builder

Create a device configuration in ESPHome Device Builder and use YZG as a remote
package:

```yaml
substitutions:
  name: my-zigbee-gateway
  friendly_name: My Zigbee Gateway

packages:
  yzg: github://yepiq/zigbee-gateway/yzg-package.yaml@main
```

Choose a unique `name`, set the displayed `friendly_name`, then validate and
install the device from ESPHome Device Builder.

Alternatively, download [`yzg-package.yaml`](yzg-package.yaml), add the `name`
and `friendly_name` substitutions shown above, and import it into ESPHome
Device Builder. Cloning the repository is not required.

A device already running YZG advertises its configuration for Dashboard import.
When adopting it, ESPHome allows the device name and friendly name to be
changed.

The supplied configuration targets the UZG-01 Ethernet interface. Most users
only need to change:

| Setting | Default | Purpose |
| --- | --- | --- |
| `name` | `yzg` | Network and ESPHome device name |
| `friendly_name` | `Yepiq Zigbee Gateway` | Name shown in Home Assistant |
| `tcp_serial_port` | `6638` | Port used by Zigbee2MQTT in TCP mode |

The Zigbee TCP connection and firmware-update interface do not provide their
own authentication. Use YZG only on a trusted or isolated network.

## Choose a Zigbee connection

YZG provides three mutually exclusive ways to connect Zigbee2MQTT:

| Mode | Connection | Key difference |
| --- | --- | --- |
| `TCP` | TCP → ESP32 → TI Zigbee | Zigbee2MQTT connects over the network |
| `USB Bridged` | USB → ESP32 → TI Zigbee | Zigbee2MQTT connects by USB while YZG can still observe and manage the radio |
| `USB Direct` | USB → TI Zigbee | USB bypasses the ESP32; Zigbee information remains at its last known values |

Select the mode with **Zigbee Serial Transport** in Home Assistant or the YZG
web interface. The button on the gateway cycles through the same choices.
Selecting either USB mode closes the Zigbee TCP port.

### TCP

Configure Zigbee2MQTT with the YZG IP address and configured TCP port:

```yaml
serial:
  port: tcp://192.168.1.12:6638
  adapter: zstack
```

TCP is the normal choice when Zigbee2MQTT and YZG communicate over the network.
Only one Zigbee2MQTT connection can use the radio at a time.

### USB Bridged

Configure Zigbee2MQTT with the UZG-01 USB serial device and
`adapter: zstack`.

Choose USB Bridged when Zigbee2MQTT is connected locally by USB but you still
want YZG to observe Zigbee communication and retain control of the radio.

### USB Direct

Configure Zigbee2MQTT with the same USB settings as USB Bridged.

Choose USB Direct when software-controlled bridging is undesirable. The USB
serial adapter talks directly to the TI radio. YZG remains online in ESPHome,
but it cannot observe Zigbee communication or update displayed Zigbee
information until another mode is selected.

## Use YZG

YZG exposes these controls in Home Assistant and its web interface:

| Control | Result |
| --- | --- |
| **Zigbee Serial Transport** | Chooses TCP, USB Bridged, or USB Direct |
| **Restart Zigbee** | Restarts the TI Zigbee radio without restarting the ESP32 |
| **Zigbee BSL Mode** | Prepares the TI radio for a firmware update |
| **Factory Reset Zigbee Router** | Clears a compatible router's network association and starts pairing |
| **Refresh Zigbee Information** | Updates the displayed radio and Zigbee network information |
| **Target Firmware Role** | Chooses coordinator or router firmware |
| **Target Firmware Version** | Chooses a compatible build from the current online catalog |
| **Refresh Firmware Catalog** | Checks the catalog immediately instead of waiting for the scheduled check |
| **Simulate Zigbee Firmware Update** | Downloads and verifies the selected image, then tests the update workflow without changing the TI radio |
| **Clear Staged Zigbee Firmware** | Discards the downloaded image so the next simulation downloads it again |

Refreshing Zigbee information temporarily uses the radio connection. YZG allows
it only in TCP mode while Zigbee2MQTT is disconnected.

YZG reports:

- TI radio model, flash size, firmware, Zigbee stack, role, and IEEE addresses;
- Zigbee network, channel, PAN, joined state, and coordinator information;
- selected transport, TCP connection state, firmware-update sessions, rejected
  connections, timeouts, and recovery attempts.

Saved information is restored after an ESP32 restart. In USB Direct, values
remain visible but are identified as cached because YZG cannot see changes made
through the direct USB connection.

## Update the TI Zigbee firmware

YZG retrieves the compatible CC2652P7 coordinator and router builds from the
XZG firmware catalog. It checks the catalog at startup and on schedule, and
keeps the target role and exact selected build across ESP32 restarts.

The built-in update workflow is currently a simulation. It downloads the real
selected image, stores it on the ESP32, verifies its SHA-256, and exercises the
remaining update stages without writing to or resetting the TI radio. A
verified download is reused when the same selected image is still current.

Actual TI radio updates currently require an external firmware-update tool.
YZG supports ZigStarGW-MT and XZG-MT-style firmware-update tools over TCP and
USB.

When updating over TCP, Zigbee2MQTT continues running until the update tool is
ready. YZG then gives the tool exclusive access to the radio. After the update,
Zigbee2MQTT is disconnected so that it reconnects cleanly.

USB Bridged automatically uses the firmware-update speed expected by compatible
tools. In USB Direct, the update tool communicates directly with the TI radio.

YZG does not restrict the firmware image. An update can upgrade or downgrade
the radio, or change it between coordinator and router firmware. Back up the
Zigbee2MQTT coordinator before changing coordinator firmware or radio role.

## LEDs

| Indicator | Meaning |
| --- | --- |
| Blue power LED | YZG is powered |
| Red mode LED | USB Bridged or USB Direct is selected |
| Yellow/white Zigbee LED | A Zigbee client is connected |

The yellow/white LED is controlled by the TI radio. YZG can track a TCP
connection exactly. The UZG-01 hardware does not tell YZG when a USB serial
client opens or closes the port, so the same indication is not yet available in
USB modes.

## What happens when access competes

- A connected Zigbee2MQTT client is not interrupted by a manual information
  refresh.
- A firmware-update tool takes control only after it is ready, allowing
  Zigbee2MQTT to keep running until the last possible moment.
- Only one Zigbee client or firmware-update tool can communicate with the radio
  at a time.

## Customize YZG

The complete configuration is in [`yzg.yaml`](yzg.yaml). Its substitutions
cover the device name, TCP timeouts, UART settings, and UZG-01 pin assignments.

The [`zigbee_gateway` component reference](components/zigbee_gateway/README.md)
documents the component parameters and every published entity for users
building a different ESPHome configuration.

Serial logging must remain disabled in customized configurations:

```yaml
logger:
  # UART0 carries USB Bridged Zigbee data. Logger output would corrupt it.
  baud_rate: 0
```

## References and license

- [UZG-01 documentation](https://uzg.zig-star.com/)
- [XZG documentation](https://xzg.xyzroe.cc/)

This repository is licensed under the [Apache License 2.0](LICENSE). XZG and
other referenced projects remain under their respective licenses.
