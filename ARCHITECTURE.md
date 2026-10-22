# Zigbee Gateway Architecture

## Document purpose

This document defines the architecture of the Zigbee Gateway product family.
It is developed from the highest-level product model down to implementable
interfaces, configuration schemas, state machines, and validation requirements.

The document is intentionally incomplete while design discussions are active.
Decisions are recorded here before lower-level design proceeds.

Decision labels:

- **AGREED** — accepted project direction or existing required behavior.
- **PROPOSED** — recommended direction awaiting explicit agreement.
- **OPEN** — a question that must be resolved.
- **DEFERRED** — intentionally outside the current design stage.

## 1. Product vision

### 1.1 Product family

**AGREED:** The project will evolve from firmware for one UZG-01 configuration
into a reusable ESPHome Zigbee gateway product family.

The product family should support:

- hosts and boards supported by ESPHome;
- serially connected Zigbee radios;
- composition with unrelated ESPHome functions such as Bluetooth proxy,
  sensors, controls, and automations;
- known hardware products with fixed, ready-to-use profiles;
- future host, board, and radio implementations without duplicating gateway
  policy or user-facing behavior.

Support is limited by the capabilities exposed by ESPHome and the underlying
platform SDK.

### 1.2 Current and future protocol scope

**AGREED:** Zigbee coordinator and router use is the active scope.

**AGREED:** One ESPHome node contains one Zigbee gateway assembly controlling
one Zigbee radio. Supporting two Zigbee radios in one node is not a useful
current product requirement.

**AGREED:** The Zigbee stack runs on an external network coprocessor (NCP).
The ESPHome host transports NCP traffic and provides gateway, maintenance,
diagnostic, update, and unrelated ESPHome functions. It does not run the
Zigbee protocol stack.

**DEFERRED:** RCP, native-radio, Thread, and Thread Border Router support are
outside this architecture. If pursued later, they require a separate scope
decision rather than speculative host-stack abstractions in the current
external-NCP design.

### 1.3 User experience

The architecture must serve two audiences:

1. A user installing firmware on a known supported product should select that
   product and provide only normal device settings.
2. An advanced user or developer should be able to compose available host,
   radio, and feature implementations without copying an existing device
   configuration.

**AGREED:** Known-product profiles and advanced composition use the
same underlying packages and C++ implementations. A known-product profile is
an assembly with fixed choices, not a separate implementation.

**AGREED:** A known-product profile is itself assembled from reusable packages
for the host board, network interface, controls, indicators, Zigbee assembly,
radio wiring, and optional features. It fixes the correct selection and wiring
for a product; it must not duplicate those component definitions in one
monolithic YAML file.

## 2. Goals and non-goals

### 2.1 Goals

- Present one coherent gateway product across supported hardware.
- Keep hardware and radio differences behind unified interfaces.
- Reuse transport, ownership, diagnostics, catalog, and update logic.
- Express compile-time product variation through ESPHome configuration.
- Keep ready-to-use device configurations simple.
- Leave host resources available for useful ESPHome functions outside the
  Zigbee gateway.
- Allow optional Home Assistant entities without removing internal
  functionality.
- Reject invalid or impossible hardware/feature combinations during ESPHome
  configuration validation.
- Make platform-independent logic host-testable.
- Keep hardware-specific behavior isolated and explicitly testable.
- Preserve safe UART ownership and recovery behavior.

### 2.2 Non-goals

- Supporting every theoretical host/radio combination without validation.
- Translating between Zigbee host protocols such as ZNP and Ember.
- Selecting the MCU platform or physical pinout at runtime.
- Making YAML implement protocol parsers, flash algorithms, or ownership state
  machines.
- Exposing every internal capability as a Home Assistant entity.
- Implementing Thread during the Zigbee architecture phase.
- Running the Zigbee or Thread protocol stack on the ESPHome host.
- Supporting RCP or native-radio topologies.
- Supporting multiple Zigbee radios in one ESPHome node.

## 3. Architectural vocabulary

The following terms have distinct meanings throughout the design:

### Host platform

The MCU family, framework, and SDK environment running ESPHome, such as an
ESP32 variant using ESP-IDF.

### Host board

A concrete ESPHome board target and its host-side hardware resources. It may
define networking hardware, flash layout constraints, native USB facilities,
and available UARTs.

### Radio

The external NCP hardware running the Zigbee protocol stack and exposing a
host command protocol.

### Radio family

The radio silicon family, such as a Texas Instruments CC13x2 or CC26x2 device.
It constrains identification, memory layout, supported image formats, and
available bootloader mechanisms, but does not by itself describe the installed
bootloader, recovery wiring, or running application.

### Radio link

The physical and logical duplex byte connection between the ESPHome host and
the external NCP. It is normally UART. The gateway owns and arbitrates this
link without interpreting it as a prerequisite for transparent transport.

### Firmware build

A particular image compiled for a compatible radio target. Its application
type, role, source lineage, SDK, build options, patches, serial speed, and
version determine the behavior compiled into the image.

### Host protocol

The application protocol and command surface exposed by the running NCP
firmware, such as ZNP, Ember, or ZBOSS. This is separate from the radio family:
a firmware build determines which protocol and commands are present and may
change them during an update.

Coordinator and router images must not be assumed to expose the same host
protocol merely because they target the same radio. For example, current
Koenkk Z-Stack coordinator images are built from TI `znp_*` projects while
router images are built from separate `zr_genericapp_*` projects.

### Radio backend

The implementation of lifecycle and firmware operations for a compatible radio
silicon family and bootloader model, such as bootloader entry, chip
identification, flash layout, erase, write, verification, and reset. A radio
family may require more than one backend when its bootloader or recovery model
differs.

### Host-protocol backend

The implementation of commands, responses, passive observation, and metadata
interpretation for a compatible running firmware protocol. Its compatibility
is principally a property of the firmware build, not of the ESPHome host.

### External NCP client

The external application that owns the Zigbee network through the NCP.
Zigbee2MQTT and Home Assistant ZHA are the current controller families in
scope. Exactly one external client may own a Coordinator NCP at a time.

The gateway does not identify whether the client is Zigbee2MQTT, ZHA, or
another compatible application. Its host-protocol backend performs bounded
local operations and passive observation while preserving the client's byte
stream; it does not implement the Zigbee network stack or replace the external
client.

### Current TI firmware evidence

The current Koenkk Z-Stack build instructions build coordinator images from TI
`znp_*` projects and router images from `zr_genericapp_*` projects. Their
shared patch also contains coordinator- and router-specific compile sections.
The resulting firmware build, not merely the CC26x2 radio family, determines
which host-protocol functions exist.

The XZG TI catalog is an aggregator. Its CC2652P7 entries include Koenkk
coordinator and router builds as well as builds or patches from other
maintainers. Therefore chip, role, and version alone are not a sufficient
general description of protocol compatibility; source lineage and build
characteristics may matter.

References:

- [Koenkk Z-Stack compilation](https://github.com/Koenkk/Z-Stack-firmware/blob/master/coordinator/Z-Stack_3.x.0/COMPILE.md)
- [Koenkk coordinator changes](https://github.com/Koenkk/Z-Stack-firmware/blob/master/coordinator/Z-Stack_3.x.0/CHANGELOG.md)
- [Koenkk router changes](https://github.com/Koenkk/Z-Stack-firmware/blob/master/router/Z-Stack_3.x.0/CHANGELOG.md)
- [XZG TI firmware catalog](https://raw.githubusercontent.com/xyzroe/XZG/zb_fws/ti/manifest.json)

### Radio role

The role of the installed Zigbee firmware, currently coordinator or router.
Role is runtime radio metadata and a firmware-selection input; it is not a host
or backend type.

### Transport

A user-selectable path through which an external Zigbee client accesses the
radio, such as TCP, USB Bridged, or USB Direct.

### Capability

A function made possible by the selected hardware and compiled
implementations, such as TCP streaming, passive frame observation, direct USB
switching, or radio flashing.

### Device profile

A declarative assembly describing a known physical product. It composes
reusable host-board, network, UART, control, indicator, radio, transport, and
feature packages, then fixes their resources and appropriate defaults.

### Feature package

A reusable ESPHome package that contributes an optional capability,
integration, or user interface.

### Entity exposure

The decision to publish a control, sensor, binary sensor, text sensor, select,
or similar entity to Home Assistant. Entity exposure is independent of whether
the underlying C++ functionality exists.

## 4. Compile-time and runtime boundaries

ESPHome configuration generation and C++ runtime behavior have different
responsibilities.

| Concern | Compile-time YAML/Python schema | Runtime C++ |
| --- | --- | --- |
| MCU platform and framework | Select and validate | Use compiled platform |
| Physical product and wiring | Select profile and supply resources | Operate supplied resources |
| Radio backend | Select compatible implementation | Identify and operate silicon |
| Radio link | Configure serial and physical resources | Operate and arbitrate the NCP connection |
| Expected NCP firmware protocol | Select compatible implementation or policy | Probe and operate running NCP firmware |
| Firmware command surface | Declare known expectations where available | Detect and use supported commands |
| Optional feature inclusion | Include or omit component graph | Run included feature |
| Home Assistant entities | Explicitly declare exposure | Publish state and handle commands |
| UART ownership | Configure policy and resources | Enforce exclusive ownership |
| TCP and USB streaming | Enable supported transports | Move data and manage sessions |
| Protocol observation | Enable compatible observer | Parse live frames |
| Firmware catalog | Configure source and policy | Fetch, cache, and select |
| Firmware update | Include compatible updater | Download, verify, flash, and recover |
| Selected transport | Define available options | Change path safely |
| Target role/version | Define controls and defaults | Persist and apply selection |

**AGREED:** YAML selects and assembles the compiled product. C++ implements
runtime behavior through typed interfaces.

**AGREED:** Runtime state machines must not be duplicated in hardware profiles
or feature packages.

## 5. Top-level layered model

The intended dependency direction is:

```text
User device configuration
        ↓
Known device profile or advanced assembly
        ↓
Capability and feature packages
        ↓
ESPHome component schemas and code generation
        ↓
        Gateway application services and policies
        ↓
Host-platform, radio, and NCP host-protocol backends
        ↓
ESPHome, platform SDK, UART/USB, network, storage, and GPIO
```

### Layer 1: User device configuration

Owns device identity, user choices, credentials, and explicit entity exposure.
For known devices it should remain a thin configuration.

### Layer 2: Device profile or advanced assembly

A device profile owns fixed facts about a known product. Advanced assembly
provides the same inputs explicitly.

The profile is composed from reusable packages for resources such as Ethernet,
buttons, indicators, transport switching, and radio control. Physical serial
parameters are supplied directly to the Zigbee assembly that owns the UARTs;
they are not first declared as independent public resources and then attached
to it. Neither configuration path should implement gateway algorithms or copy
the definitions of those reusable resources.

### Layer 3: Capability and feature packages

Uses ESPHome package composition to include only relevant configuration and
integrations. It adapts profile data into component configuration but does not
replace C++ abstractions.

### Layer 4: ESPHome component schemas

Provide typed configuration validation, declare optional entities, reject
incompatible combinations, and construct the selected C++ implementations.

### Layer 5: Gateway services and policies

Implement behavior shared by all products: ownership, transport policy,
metadata lifecycle, catalog policy, update orchestration, persistence, and
diagnostics.

### Layer 6: Platform, radio, and protocol backends

Host-platform, radio, and NCP host-protocol backends implement their specific
operations behind common C++ interfaces. They do not own product-level policy.
The Zigbee protocol stack remains in the external NCP and controller client,
not in these backends.

## 6. ESPHome composition model

The design may rely on the following current ESPHome capabilities:

- substitutions can contain mappings, lists, booleans, numbers, and strings;
- `!include` accepts scoped `vars`;
- included packages can define `defaults`;
- packages can be selected conditionally;
- direct include filenames may contain substitution expressions;
- package mappings merge key by key;
- component lists with IDs merge by ID;
- `!extend` augments an existing ID;
- `!remove` removes an existing ID;
- remote package files can receive separate `vars` objects;
- an external component schema can make every entity optional while supplying
  its default metadata.

References:

- [ESPHome substitutions](https://esphome.io/components/substitutions/)
- [ESPHome packages](https://esphome.io/components/packages/)

### 6.1 Namespaced package inputs

Reusable packages should receive one namespaced mapping rather than a growing
set of global substitutions.

Illustrative shape only:

```yaml
packages:
  - !include
    file: packages/zigbee-gateway.yaml
    vars:
      gateway:
        features:
          tcp: true
          usb_bridge: true
        ui:
          diagnostics: true
```

The final public schema is not yet decided.

Package-owned `defaults` should normalize nested values using guarded lookups.
Global substitutions remain appropriate for device identity and intentionally
global ESPHome settings.

### 6.2 Parameterized resource templates

A reusable resource include owns configuration logic and schema adaptation. It
must not own the physical values of one device.

**AGREED:** Resource templates follow these rules:

- every physical fact is supplied through one namespaced `vars` mapping;
- required facts have no guessed pin or chip defaults;
- defaults are limited to safe behavioral values;
- one template represents one reusable configuration schema or algorithm, not
  one product pinout;
- a profile passes a native ESPHome configuration mapping intact where the
  owning ESPHome component already provides the required schema;
- a wrapper adds only project behavior, validation, or internal wiring that the
  native component does not provide;
- internal IDs are not public profile variables;
- a physical resource exclusively consumed by one project component is
  configured through that component instead of being exposed as an independent
  public resource;
- profiles and advanced configurations instantiate the same templates.

For example, a UZG-01 profile may instantiate a generic Ethernet template:

```yaml
packages:
  - !include
    file: ../resources/network/ethernet.yaml
    vars:
      ethernet:
        type: LAN8720
        mdc_pin:
          number: GPIO23
        mdio_pin:
          number: GPIO18
        clk:
          pin:
            number: GPIO17
          mode: CLK_OUT
        phy_addr: 0
        power_pin:
          number: GPIO5
          ignore_strapping_warning: true
```

The resource template contains no LAN8720 pinout and does not reproduce
ESPHome's separate RMII and SPI Ethernet schemas. It forwards the complete
native ESPHome mapping:

```yaml
defaults:
  ethernet: ${ ethernet | default({}) }

ethernet: ${ethernet}
```

ESPHome remains authoritative for fields supported by each Ethernet type,
including pin and bus differences. The project package may add shared network
policy or diagnostics, but should not mirror the native Ethernet schema.

The current UZG-01 external-NCP assembly follows a different ownership rule.
Both serial paths are exclusive implementation details of the gateway, so the
profile passes their physical parameters directly to that component:

```yaml
packages:
  - !include
    file: ../features/zigbee-gateway.yaml
    vars:
      zigbee:
        radio_uart:
          tx_pin:
            number: GPIO4
          rx_pin:
            number: GPIO36
          baud_rate: 115200
          rx_buffer_size: 2048
        usb_bridge_uart:
          tx_pin:
            number: GPIO1
          rx_pin:
            number: GPIO3
          baud_rate: 115200
          rx_buffer_size: 2048
```

The package forwards those mappings into the Zigbee component rather than
creating public `uart` components:

```yaml
defaults:
  zigbee: ${ zigbee | default({}) }

zigbee_gateway:
  id: zigbee_gateway
  radio_uart: ${zigbee["radio_uart"]}
  usb_bridge_uart: ${zigbee.get("usb_bridge_uart", {})}
```

The Zigbee component's schema validates and constructs these UART resources.
It owns their lifetime and their private C++ references. No public UART ID is
needed. Omitting `usb_bridge_uart` is valid only when the selected transport
features do not require USB Bridged operation. Other profiles may supply a
different private link or USB byte endpoint through their selected packages.

#### Radio-backend configuration

The physical connection to a radio is not adequately described by one
universal set of reset and bootloader fields.

Representative implementations differ materially:

- TI CC13x2/CC26x2 devices can use a ROM serial bootloader. Forced entry
  depends on bootloader-backdoor configuration stored in the radio image and
  the configured DIO level during reset.
- Silicon Labs NCP designs commonly provide `nRESET` and an optional Bootload
  signal. Bootloader entry may also be requested through the running EZSP
  protocol. A Gecko standalone UART bootloader transfers GBL images with
  XMODEM, and its UART and flow-control behavior are build-time choices.
- Espressif H2/C6 devices use reset and boot-mode strapping for ROM download.
  This project supports them only when a compatible Zigbee NCP application is
  installed.

References:

- [TI CC13x2/CC26x2 bootloader configuration](https://software-dl.ti.com/simplelink/esd/simplelink_cc13x2_26x2_sdk/3.30.00.03/exports/docs/driverlib_cc13xx_cc26xx/cc13x2_cc26x2/register_descriptions/CPU_MMAP/CCFG.html)
- [Silicon Labs EZSP-UART physical interface](https://docs.silabs.com/zigbee/9.0.1/emberznet-serial-protocol-uart-host-interfacing-guide/02-ezsp-uart-physical-interfaces)
- [Silicon Labs standalone UART bootloader](https://docs.silabs.com/zigbee/latest/using-gecko-bootloader-with-zigbee/02-using-the-gecko-standalone-bootloaders)
- [ESP32-H2 download-mode control](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32h2/download-guidelines.html)
- [ESP Zigbee radio modes](https://docs.espressif.com/projects/esp-zigbee-sdk/en/latest/esp32h2/api-reference/esp_zigbee_sdk.html)

**AGREED:** The generic Zigbee gateway owns common policy and coordinates the
NCP radio link, but delegates lifecycle and firmware operations to a selected
radio backend. The common C++ contract expresses semantic operations and
capabilities, such as:

- identify the radio;
- reset into its running application;
- determine whether forced recovery is available;
- enter a supported bootloader;
- validate the selected image format, layout, and operational prerequisites;
- erase, write, verify, and recover;
- report which operations are unavailable.

It does not expose universal `reset_pin`, `bootloader_pin`, or flashing-protocol
fields.

**AGREED:** A device profile selects one backend-specific `zigbee_radio`
package and provides a namespaced `vars` object defined by that package. The
variable schema may differ completely between backends; it is not coerced into
a universal superset.

A TI package may request reset and BSL wiring. A Silicon Labs package may
request reset, Bootload, and bootloader-variant information. An Espressif
package may request enable, boot-strapping, and download-transport information.
Required fields are determined by the selected backend and the capabilities
the profile intends to provide. Required serial settings may be declared by a
backend descriptor, but the backend does not own or directly configure the
physical NCP UART.

**AGREED:** The backend-specific radio package path is the compile-time
backend selector. The profile does not repeat that selection in a `type`
field. The `zigbee_radio` input is a backend-specific mapping containing its
radio-family control and lifecycle resources. The physical NCP byte link is
configured separately beneath `zigbee_gateway`.

Conceptual UZG-01 radio assembly:

```yaml
packages:
  - !include
    file: ../zigbee/radios/ti-cc13x2-cc26x2.yaml
    vars:
      zigbee_radio:
        # Interpreted only by the selected TI radio package.
        backend:
          expected_chip: CC2652P7
          reset_pin:
            number: GPIO16
            inverted: true
          bsl_pin:
            number: GPIO32
            inverted: true

          # The UZG-01 client indicator is TI radio LED1, controlled through
          # the running ZNP firmware rather than an ESP32 GPIO.
          client_connection_indicator:
            znp_led: 1
```

The TI radio package conceptually produces:

```yaml
zigbee_radio:
  id: zigbee_radio
  backend:
    type: ti_cc13x2_cc26x2
    config: ${zigbee_radio["backend"]}
```

The fixed `type` is private package output, not a profile choice duplicated in
the input. Another backend package may accept completely different keys under
`zigbee_radio.backend`. Only the C++ interface presented by the constructed
backend to the generic radio and gateway is uniform.

A radio package also includes its compatible NCP host-protocol handlers
internally. For the current TI/XZG radio assembly this includes ZNP support.
The device profile and end user do not include or select `znp.yaml`.

Radio lifecycle and host-protocol handling remain separate C++ interfaces even
though their YAML composition is hidden behind one radio package. If one radio
backend later supports multiple incompatible firmware ecosystems that cannot
reasonably be compiled together, any necessary selection belongs beneath
`zigbee_radio.firmware` or in distinct radio-package variants. It must not be a
standalone package choice beside the radio.

Both UZG-01 serial endpoints belong to the gateway assembly. The gateway owns
the NCP byte-link arbiter around the radio-facing UART and the USB-facing
endpoint used by USB Bridged:

```yaml
packages:
  - !include
    file: ../zigbee/gateway.yaml
    vars:
      zigbee_gateway:
        radio_uart:
          tx_pin:
            number: GPIO4
          rx_pin:
            number: GPIO36
          baud_rate: 115200
          rx_buffer_size: 2048

        tcp:
          port: ${tcp_serial_port}
          pending_socket_timeout: ${tcp_pending_socket_timeout_ms}ms
          parked_socket_timeout: ${tcp_parked_socket_timeout_ms}ms

        usb_bridge:
          uart:
            tx_pin:
              number: GPIO1
            rx_pin:
              number: GPIO3
            baud_rate: 115200
            rx_buffer_size: 2048

        usb_direct:
          gpio_switch:
            pin:
              number: GPIO33

logger:
  # UART0 is the USB Bridged endpoint; serial logging would corrupt its data.
  baud_rate: 0
```

The gateway package binds its fixed internal `zigbee_radio` reference, creates
and owns the physical radio UART and its lease-enforcing NCP byte link, owns
the USB-facing byte endpoint, and constructs the topology-compatible transport
policy. The gateway is the orchestration authority: HTTP endpoints, TCP
maintenance, local operations, and firmware installation request access
through it. The radio backend receives only access authorized by that
orchestration and never owns or bypasses the UART. The profile does not pass
resource IDs.

**AGREED:** Private transport endpoint implementations are selected by nested,
mutually exclusive mappings beneath `zigbee_gateway`. For example,
`usb_bridge.uart` selects a hardware-UART endpoint while
`usb_bridge.usb_cdc_acm` selects native USB CDC. Likewise,
`usb_direct.gpio_switch` and `usb_direct.dedicated_radio_usb` describe
different complete direct-route strategies. The profile does not include
separate endpoint packages or connect them to the gateway with public IDs.
Component validation requires exactly one supported implementation for every
configured transport.

Firmware catalog policy and Home Assistant entity exposure remain separate
feature inputs. They are not physical radio-backend parameters.

A known-device profile includes the correct backend-specific radio package and
supplies its physical values, so its end user never selects a radio family. An
advanced configuration explicitly includes that same radio package.

#### Indicator resources

**AGREED:** Indicators are named by their function, not by color or a generic
ordinal. A profile supplies the complete physical configuration for each
function present on the device. Omitting a function creates no corresponding
output.

The UZG-01 has two ESP32-controlled indicators:

```yaml
indicators:
  power:
    pin:
      number: GPIO14
      inverted: false

  usb_transport:
    pin:
      number: GPIO12
      inverted: false
      ignore_strapping_warning: true
```

The Zigbee-client indicator is different: it is connected to the TI radio and
controlled through the running ZNP firmware. Its device-specific binding
therefore belongs to the selected TI backend:

```yaml
zigbee_radio:
  backend:
    client_connection_indicator:
      znp_led: 1
```

The gateway requests a semantic client-indicator state through the radio
interface. The TI backend translates that request into ZNP LED1 control.
Another backend may implement the operation differently or report it
unsupported. Color may be documented as a physical product fact, but it is not
a configuration identity or logical signal name.

The same parameterized-template rule applies to buttons, mode controls,
radio-reset and bootloader wiring, storage resources, and other physical
elements.

Additional logical signal names, precedence rules, shared-LED behavior, and
support for non-binary indicators remain open. Those decisions belong to the
indicator behavior design, not to a product profile.

The examples in this section describe the intended configuration contract.
Their exact syntax must be validated with `esphome config` before it becomes a
public API.

### 6.3 Public package boundary

**AGREED:** Public configuration is divided by responsibility:

| Configuration layer | Owns |
| --- | --- |
| Device configuration | Device identity, credentials, user policy overrides, and selected UI exposure |
| Known-device profile | Fixed host, network, physical wiring, backend-specific radio package, supported transports, controls, indicators, and the profile's default feature set |
| Radio package | Backend-specific control and lifecycle configuration, radio-backend construction, and private compatible host-protocol handlers |
| Gateway package | Physical NCP byte link and lease enforcement, radio-access orchestration, TCP endpoint, USB-facing byte endpoint, USB Direct route control, and transport state machines |
| Gateway feature package | Optional Zigbee-gateway behavior such as catalog retrieval, staged firmware update, or future extensions |
| Host feature package | Independent ESPHome behavior such as Bluetooth proxy, sensors, relays, displays, or automations |
| UI package | Optional Home Assistant entity declarations and presentation overrides |

The stable public package tree is:

```text
profiles/
  uzg-01.yaml
  esp-thread-border-router.yaml

hosts/
  esp32-idf.yaml
  esp32-s3-idf.yaml

network/
  ethernet.yaml
  wifi.yaml

storage/
  esp32-firmware-staging.yaml

zigbee/
  gateway.yaml
  radios/
    ti-cc13x2-cc26x2.yaml
    espressif-h2-zboss.yaml

catalogs/
  xzg-ti.yaml
  esp-coordinator-h2.yaml

features/
  firmware-update/
    manager.yaml
    xzg-ti.yaml

controls/
  transport-mode-button.yaml

indicators/
  zigbee-gateway.yaml

integrations/
  esphome-management.yaml

ui/
  core.yaml
  firmware-update.yaml
  diagnostics.yaml
  all.yaml
```

Entries for unimplemented experimental functionality describe their intended
stable placement, not a claim that the package already exists or is supported.
`features/firmware-update/xzg-ti.yaml` is the convenience composition of the
XZG TI catalog package and the generic manager. Advanced assemblies may include
`catalogs/xzg-ti.yaml` and `features/firmware-update/manager.yaml` separately.

A known-device profile remains an assembly of reusable packages. It is not a
second implementation and does not copy their logic. Advanced users may include
the same radio, gateway, network, control, indicator, and feature packages
directly.

Host feature packages are peers of the Zigbee assembly, not children of it.
The gateway must not implement Bluetooth proxy or product sensor logic merely
to make those features available. A profile or advanced assembly may include
them when the host supports their requirements.

ESPHome schema validation must reject known conflicts in pins, UARTs, USB
instances, network choices, partitions, or mutually exclusive platform
features. Supported-profile qualification also covers practical shared
resources such as RAM, task latency, socket count, and RF coexistence. Merely
compiling BLE proxy beside the gateway is not evidence that their combined
runtime behavior is supported.

The ready-to-use UZG-01 device configuration should remain thin:

```yaml
substitutions:
  name: yzg
  friendly_name: Yepiq Zigbee Gateway

  tcp_serial_port: 6638
  tcp_pending_socket_timeout_ms: 30000
  tcp_parked_socket_timeout_ms: 600000

  firmware_catalog_url: https://raw.githubusercontent.com/xyzroe/XZG/zb_fws/ti/manifest.json
  firmware_catalog_refresh_hour: "4"
  firmware_catalog_refresh_minute: "0"
  firmware_catalog_refresh_days: MON-SUN

packages:
  - !include profiles/uzg-01.yaml

  # The demonstration configuration exposes every available entity.
  - !include ui/all.yaml
```

The known-device profile performs the physical and functional assembly:

```yaml
packages:
  - !include
    file: ../hosts/esp32-idf.yaml
    vars:
      host:
        esp32:
          board: esp32dev
          framework:
            type: esp-idf
            sdkconfig_options:
              CONFIG_FREERTOS_UNICORE: y
              CONFIG_ESP_TASK_WDT_TIMEOUT_S: "10"

  - !include
    file: ../network/ethernet.yaml
    vars:
      ethernet:
        # Complete native ESPHome LAN8720 configuration.
        type: LAN8720
        # Product pin mapping omitted here; see the Ethernet example above.

  - !include
    file: ../zigbee/radios/ti-cc13x2-cc26x2.yaml
    vars:
      zigbee_radio:
        # UZG-01 TI control and lifecycle values shown above.

  - !include
    file: ../zigbee/gateway.yaml
    vars:
      zigbee_gateway:
        # UZG-01 radio UART, TCP, USB Bridged, and USB Direct values
        # shown above.

  - !include
    file: ../controls/transport-mode-button.yaml
    vars:
      transport_mode_button:
        pin:
          number: GPIO35
          inverted: true
          ignore_strapping_warning: false

  - !include
    file: ../indicators/zigbee-gateway.yaml
    vars:
      indicators:
        power:
          pin:
            number: GPIO14
            inverted: false

        usb_transport:
          pin:
            number: GPIO12
            inverted: false
            ignore_strapping_warning: true

  - !include ../features/firmware-update/xzg-ti.yaml
```

The comments standing in for mappings make this an ownership diagram, not
valid ESPHome input. The real profile contains the complete physical values.

The firmware-update feature is separate from the radio package because the
radio must remain usable without online catalog or update support. The radio
backend supplies identification and format-specific flashing operations. The
YAML catalog parser selects and normalizes entries for the configured device;
the feature supplies catalog retrieval, selection, caching, staging, and update
orchestration. A catalog-source-specific package may provide sensible defaults
such as the XZG manifest URL while allowing the device configuration to
override policy values.

**AGREED:** The UZG-01 profile includes firmware-update functionality by
default. Catalog retrieval, persistence, staging, and update orchestration are
therefore configured and compiled independently of Home Assistant exposure.

The UI package contributes only optional entity definitions. Omitting it does
not remove internal gateway functions. Component schemas remain authoritative
for default entity metadata, and a caller may override that metadata in YAML.
The demonstration configuration includes `ui/all.yaml`; smaller UI bundles may
expose only core controls, firmware controls, or diagnostics.

### 6.4 Conditional packages

Optional capabilities should contribute no ESPHome configuration when absent.

Illustrative pattern:

```yaml
defaults:
  tcp_package: !include features/tcp.yaml
  has_tcp: ${ gateway.get("features", {}).get("tcp", false) }

packages:
  - ${ tcp_package if has_tcp else {} }
```

Conditional packages are suitable for selecting feature configuration and
compile-time implementations. They are not a substitute for runtime
polymorphism.

### 6.5 Stable IDs and extension hooks

Stable IDs allow device configurations and feature packages to augment
well-defined hooks through `!extend`.

Suitable uses include:

- LEDs and user feedback;
- physical buttons;
- user automations;
- presentation extensions;
- optional lifecycle callbacks.

Protocol handling, flashing, ownership, and streaming must remain C++ behavior.

**AGREED:** Reusable gateway packages own deterministic IDs for public
extension hooks and component references; profiles and users do not override
them. Objects used only inside the Zigbee component, including its owned radio
links and USB endpoints, remain private C++ references and need no YAML IDs.

**AGREED:** The current architecture supports one Zigbee gateway and one
Zigbee radio within an ESPHome node.

**DEFERRED:** If a future Zigbee-plus-Thread product needs another radio
subsystem, its namespacing and cross-protocol coordination will be designed
explicitly for that requirement. It is a composite multi-protocol device, not
a multi-Zigbee-gateway use case.

### 6.6 Known templating constraints

- Package variable mappings do not provide a native typed public API by
  themselves. The component schema must perform authoritative validation.
- Nested defaults require guarded lookups; deep default merging must not be
  assumed.
- Key-presence checks are required when an explicit `false` differs from a
  missing value.
- Schema-sensitive mappings generated as large Jinja expressions are difficult
  to understand and have shown compatibility problems.
- Complex nested dynamic `!include` and `vars` arrangements require validation
  against concrete configurations.
- Stable IDs used by `!extend` become part of the package compatibility
  contract.
- Conditional YAML inclusion does not by itself prove that unused C++ source
  has been excluded from the resulting binary.

Every new templating technique used in public infrastructure must be validated
with representative resolved configurations before adoption.

## 7. Capability-driven design

Product behavior must be derived from capabilities, not scattered checks for
product names.

### 7.1 Capability, compatibility, and availability

**AGREED:** The architecture distinguishes three independent layers:

| Layer | Meaning | Authority |
| --- | --- | --- |
| Capability | The compiled configuration contains the resources and implementations that could perform an operation | ESPHome schema validation and code generation |
| Compatibility | The attached radio, bootloader, or installed firmware matches an available implementation | Backend identification and protocol probing |
| Availability | Current runtime state permits the operation now | Ownership, transport, maintenance, network, and update state machines |

A capability does not imply compatibility or current availability.
Compatibility may change when radio firmware is replaced. Availability is
ephemeral and must not be treated as a persisted hardware fact.

Examples:

- Passive ZNP observation is capable when the handler is compiled, compatible
  when the running firmware speaks understood ZNP, and available only while
  the ESP can observe the radio UART.
- Radio flashing is capable when the configured parser, flasher, bootloader
  control, staging storage, and update services exist. The parser supplies
  eligible catalog entries, and flashing becomes available after the required
  ownership and updater prerequisites are satisfied.
- USB Direct is capable when a complete external route and a matching
  host-withdrawal strategy are configured. The route may be selected by a
  hardware mux, as on UZG-01, or be a dedicated radio USB endpoint while the
  ESP releases its own radio link. While it is active, local radio operations
  are unavailable because the host no longer owns or observes the application
  path.

Features require capabilities during configuration validation. Runtime
operations additionally require compatibility and availability. A failed
requirement must produce a truthful unavailable state or a safe rejection; it
must not fall through to a guessed implementation.

### 7.2 Capability derivation

**AGREED:** Capabilities are never asserted through a public YAML
`capabilities:` block. YAML declares resources and selects implementations;
ESPHome schema validation derives the resulting compile-time capabilities,
and runtime state machines determine their current availability.

Initial capability examples include:

- network TCP service;
- USB Bridged transport;
- direct radio USB transport;
- software-controlled direct/bridged selection;
- passive host-protocol observation;
- local radio commands;
- reset control;
- bootloader entry;
- radio-family identification;
- host-protocol identification;
- firmware-build identification;
- host-protocol command discovery or compatibility matching;
- firmware catalog support;
- staged image storage;
- radio flashing;
- metadata persistence;
- network-information persistence.

This list identifies architectural dimensions. Internally, code generation
may represent the derived result as typed facets, enums, or a typed capability
set. It must not become a user-maintained string registry.

**AGREED:** Configurations declare physical resources and select
implementations; they do not assert the resulting capabilities directly.
Radio backends declare silicon and bootloader support. NCP host-protocol
backends declare compatible firmware protocols and understood commands.
ESPHome schema validation derives and checks compile-time capabilities.
Runtime probing establishes compatibility, while gateway state machines
determine current availability.

For example, a profile must not offer USB Direct unless it provides the
complete direct route and a controller capable of withdrawing the ESP host
from the conflicting path. A firmware updater must not be selected unless its
radio backend, storage requirements, image format, and host platform services
are available.

Users may request or disable optional features, but enabling a feature does not
manufacture its prerequisites. A requested feature whose required resources or
implementations are absent must fail configuration validation.

Initial direct derivations include:

| Configuration input | Derived capability |
| --- | --- |
| TCP configuration and network/socket services | TCP transport |
| UART or USB CDC bridge endpoint | USB Bridged transport |
| Complete direct route plus host-withdrawal implementation | USB Direct transport |
| Radio reset control implementation | Radio reset |
| Radio bootloader control implementation | Bootloader entry |
| Compatible private NCP handler | Its local commands and passive observation |
| Catalog plus staging store | Firmware download and staging |
| Catalog, staging store, and radio updater | Firmware installation |

Missing required resources are configuration errors rather than unavailable
placeholder implementations.

The installed firmware is authoritative for its exposed host-protocol
functions. Radio silicon remains authoritative for firmware compatibility,
bootloader access, flash geometry, and other physical mechanisms.

**AGREED:** The Zigbee radio assembly selected by a device profile determines
the compatible host-protocol handlers to compile; handlers are not independent
user-facing package choices. At runtime, the gateway probes the installed
firmware and selects a compatible handler. Firmware may be changed
independently of the ESPHome host, including between coordinator and router
applications, without rebuilding the host firmware.

If the installed firmware is unknown, transparent serial transports remain
available. Local diagnostics, commands, and observations that require an
understood host protocol become unavailable rather than guessing or preventing
serial access.

### 7.3 External-client neutrality

The gateway has no Zigbee2MQTT/ZHA concept at runtime. A normal TCP or USB
session is an opaque external client holding the NCP byte stream. Admission,
ownership, forwarding, observation eligibility, transport transitions, and
maintenance takeover use the same state machines for every client.

Different clients may produce different valid command sequences, timing,
connection lifetimes, and reconnect behavior. Those are inputs to the same
protocol-neutral transport and must not create client-specific branches.
Passive and active local operations depend on the detected NCP protocol, not
on which external application uses it.

No controller selector, identifier, capability, or persisted controller state
belongs in the ESPHome component. Interoperability with particular external
applications may be recorded as qualification evidence and user documentation,
but it is not a gateway runtime abstraction.

Router firmware does not expose a Coordinator NCP for an external controller
to own; its supported local operations and transparent maintenance paths are
evaluated separately.

### 7.4 UZG-01 capability inputs

The UZG-01 profile currently supplies these resources and implementations:

| Input | Current UZG-01 provider |
| --- | --- |
| Host platform | ESP32 with ESP-IDF |
| Network and sockets | ESPHome network and socket services over LAN8720 Ethernet |
| HTTPS, TLS, JSON, and hashing | ESP-IDF HTTP client, certificate bundle, ArduinoJson, and mbedTLS SHA-256 |
| Persistent preferences | ESPHome preferences |
| Firmware staging | 768 KiB `zigbee_fw` ESP32 partition |
| Radio serial link | Owned UART using GPIO4 TX, GPIO36 RX, 115200 baud, and a 2048-byte RX buffer |
| USB-bridge serial link | Owned UART using GPIO1 TX, GPIO3 RX, 115200 baud, and a 2048-byte RX buffer |
| USB Direct path | UZG-01 hardware path selected by GPIO33 |
| Radio reset control | Inverted GPIO16 output |
| Radio BSL/rejoin control | Inverted GPIO32 output |
| Radio backend | TI CC13x2/CC26x2 identification, ROM BSL, flash-layout, CCFG, and NV implementation |
| Host-protocol handlers | ZNP parser, local commands, and passive observer compiled privately by the radio package |
| Firmware catalog provider | XZG TI manifest adapter |
| Firmware manager | Catalog cache, persistent target selection, staged download, and simulated update workflow |
| Optional compatibility HTTP API | ESPHome web server handlers for `/cmdZigBSL` and `/cmdZigRST` |
| Scheduled catalog trigger | Home Assistant time source and configured schedule |

These are inputs to derivation, not user-asserted capabilities. For example,
GPIO33 alone does not create USB Direct support; the UZG-01 profile declares
the complete, known hardware path controlled by that signal.

### 7.5 Initial UZG-01 capability matrix

The following identifiers are architectural working names. They do not become
stable public compatibility contracts until Decision 5 is resolved.

| Capability | Compile-time derivation | Runtime compatibility | Runtime availability |
| --- | --- | --- | --- |
| `transport.tcp` | Network, sockets, radio UART, serial arbiter, stream pump, and TCP server | Byte-transparent; no understood radio protocol required | TCP selected, network connected, and no local operation owns the radio |
| `transport.usb_bridge` | Radio UART, USB UART, serial arbiter, stream pump, and USB bridge | Byte-transparent; no understood radio protocol required | USB Bridged selected and both UARTs initialized |
| `transport.usb_direct` | Declared UZG-01 direct path and switch control | Guaranteed by a supported hardware profile | USB Direct selected and the hardware switch applied |
| `maintenance.tcp_takeover` | TCP transport, reset, bootloader entry, pending/parked socket state machine, and maintenance command integration | External tool follows the supported `/cmdZigBSL` or `/cmdZigRST` workflow | TCP mode; maintenance request can be coordinated with the active or pending client |
| `maintenance.http_commands` | Web server plus TCP maintenance integration | Compatible endpoint semantics expected by the maintenance tool | Network and web server available |
| `radio.reset` | TI backend plus reset control | Reset wiring matches the attached radio | No conflicting local operation; an external owner is preserved and coordinated |
| `radio.bootloader_entry` | TI backend, reset control, and BSL control | Attached image permits the configured TI bootloader-entry mechanism | No conflicting local operation; active transparent owner receives the bootloader stream |
| `radio.identification` | Radio UART, serial arbiter, TI BSL implementation, bootloader entry, chip/layout readers, and NV scanner | BSL responds with a supported TI family and layout | TCP mode, no TCP client, and local UART ownership acquired |
| `radio.router_factory_reset` | TI backend and the product's BSL/rejoin control | Installed image uses the supported router rejoin behavior | No other radio operation; current implementation permits Router or unknown role |
| `protocol.passive_observation` | Radio UART byte tap plus the privately compiled ZNP observer | Running application emits understood ZNP frames | Serial owner is TCP Normal or USB Bridge; unavailable for Local, TCP Maintenance, and USB Direct |
| `protocol.local_commands` | Radio UART, serial arbiter, and privately compiled ZNP command handler | Running firmware supports the requested ZNP command | Local ownership can be acquired without stealing an external session |
| `metadata.physical_identity` | Radio identification plus physical-identity cache | TI backend recognizes the silicon and immutable factory fields | Fresh identification requires local access; cached values remain available indefinitely |
| `metadata.running_image` | TI CCFG reader, compatible application handler, passive observer, and running-image cache | Installed image exposes an understood source for each field | Fresh values require local access or observed traffic; cached values remain visible and become awaiting-observation after BSL |
| `metadata.network_snapshot` | TI NV reader, compatible passive observer, and network-snapshot cache | Installed firmware or stored NV layout exposes understood network fields | Fresh values require local access or observed traffic; the last known snapshot remains available with cached provenance |
| `metadata.persistence` | Preference storage plus physical-identity, running-image, and network-snapshot codecs | Cache schema is recognized | Available without radio access; provenance reports whether values are verified, observed, awaiting observation, or cached |
| `firmware.catalog` | Network, HTTPS/TLS, JSON, preferences, XZG TI provider, and firmware manager | Manifest can be normalized for the configured or detected chip | Network connected and no conflicting catalog/update task |
| `firmware.catalog_schedule` | Firmware catalog plus a configured time source and schedule | Not radio-dependent | Clock valid, scheduled time reached, and catalog manager idle |
| `firmware.staging` | HTTPS download, SHA-256, staging partition, and staging-header codec | Selected image matches role/version metadata and size limits | Partition available and no conflicting download/update task |
| `firmware.update_simulation` | Catalog, staging, and cooperative simulated erase/write/verify state machine | Selected or staged image metadata is valid | Firmware manager idle; no radio ownership is required because radio operations are simulated |
| `firmware.install` | Actual backend flasher, catalog, staging, image validation, ownership orchestration, and recovery | Radio, bootloader, and selected image are mutually compatible | **Not currently provided.** It becomes available only after safe radio takeover and preconditions are satisfied |

The matrix deliberately treats transparent transports as protocol-independent.
An unknown coordinator or router image may still be used by an external
client. Only local protocol-dependent functions lose compatibility.

### 7.6 Initial derivation and validation rules

The first profile must enforce these rules:

1. A feature or UI declaration may require a capability but cannot create one.
2. Missing compile-time prerequisites cause a precise configuration error; they
   are not deferred to runtime.
3. The transport selector contains only derived transport capabilities. Its
   restored/default value must be one of those options.
4. USB Bridged requires an owned USB-facing duplex byte endpoint and exclusive
   use of that endpoint. It may be a hardware UART or native USB CDC. On
   UZG-01, UART0 serial logging must remain disabled.
5. USB Direct requires a profile declaration of the complete physical route
   and a route-control strategy that prevents simultaneous host traffic. The
   strategy may drive a hardware mux or quiesce and release a separate host
   link; an arbitrary GPIO is insufficient.
6. TCP transport requires network and socket services but never requires a
   recognized host protocol.
7. TCP maintenance takeover requires both reset and forced bootloader-entry
   capabilities. The HTTP compatibility endpoints additionally require a web
   server implementation.
8. Intrusive local identification and protocol commands require exclusive
   local radio ownership. They must not query the UART while a normal external
   client owns it.
9. Passive protocol observation requires a compatible handler and a
   transparent host-visible stream. It is disabled for local diagnostics,
   maintenance payloads, and USB Direct.
10. Metadata persistence remains usable when radio access, compatibility, or
    current availability is lost. Published provenance must describe that
    distinction.
11. Firmware catalog support requires a catalog provider and host HTTP,
    parsing, and persistence services. Scheduled refresh additionally requires
    a valid time source; startup and manual refresh do not.
12. Firmware staging requires a validated partition large enough for the
    staging header and maximum accepted image.
13. A catalog entry has already passed the source-specific YAML parser.
    Reusing a staged image additionally requires matching identity, role,
    version, size, freshness, and digest metadata.
14. Actual installation must not be derived until a real backend flasher,
    ownership workflow, image validator, verification path, and recovery path
    are present. The existing simulation does not satisfy this rule.
15. Transparent TCP and USB transports require a compatible external-NCP byte
    stream. RCP and native-radio configurations are outside the schema.
16. Optional Home Assistant entities are valid only when their parent
    functionality exists. Omitting every entity does not remove the underlying
    capability.

Compatibility must eventually distinguish at least **unknown**,
**compatible**, and **incompatible**; lack of access is not evidence of
incompatibility. Availability should carry a reason such as busy, blocked by
transport, waiting for network, or unavailable, rather than only a boolean.
The exact C++ status types remain part of the next design stage.

### 7.7 Feature and UI requirements

Feature and entity packages use the following initial requirement mapping:

| Configuration request | Required capability |
| --- | --- |
| Transport selection | Every offered option requires its corresponding `transport.*` capability; a selector is useful only when at least two options exist |
| TCP connection and maintenance diagnostics | `transport.tcp`; maintenance-only fields additionally require `maintenance.tcp_takeover` |
| Restart Zigbee control | `radio.reset` |
| Zigbee BSL Mode control | `radio.bootloader_entry` |
| Factory Reset Zigbee Router control | `radio.router_factory_reset` |
| Refresh Zigbee Information control | `radio.identification` |
| Hardware, flash size, and factory IEEE entities | `metadata.physical_identity` |
| Firmware, stack, active IEEE, and role entities | `metadata.running_image` |
| PAN, channel, joined state, parent IEEE, and extended PAN entities | `metadata.network_snapshot` |
| Transmit-power entity | `protocol.passive_observation`; the current value is observed live and is not part of the persisted network snapshot |
| Metadata provenance entities | Corresponding metadata capability plus `metadata.persistence` |
| Target firmware role/version and catalog status | `firmware.catalog` |
| Refresh Firmware Catalog control | `firmware.catalog` |
| Clear Staged Zigbee Firmware control | `firmware.staging` |
| Simulate Zigbee Firmware Update control and progress | `firmware.update_simulation` |
| Future Install Zigbee Firmware control and progress | `firmware.install` |

Entity declarations remain optional after these requirements are satisfied.
The internal services publish to zero or more attached entities and do not use
entity presence to decide whether their functionality exists.

## 8. Runtime service and ownership model

**AGREED:** This section defines the runtime boundaries. The names are
descriptive working names, not yet stable C++ API names. The behavioral
invariants and separation of authority are requirements; the exact class and
method names remain subject to the interface design.

### 8.1 Separate data, control, and orchestration planes

Radio access is not one indivisible operation. The architecture distinguishes:

| Plane | Responsibility | Examples |
| --- | --- | --- |
| Radio data plane | Connects one admitted external client or local operation to the NCP radio link | Zigbee2MQTT/ZHA TCP stream, USB Bridged stream, local ZNP transaction, TI BSL transaction |
| Radio control plane | Changes electrical or boot state without itself consuming the byte stream | Reset, forced bootloader entry, router commissioning/rejoin signal |
| Orchestration plane | Decides which operation may run and coordinates transitions | Transport change, maintenance takeover, local diagnostic admission, firmware installation |
| Observation plane | Receives a read-only copy of eligible NCP bytes | Passive ZNP/ZBOSS metadata observation |

This separation is required by the supported maintenance workflows. When a TCP
or USB client requests bootloader entry, that client must retain the data path
so it receives the resulting bootloader stream, while the radio backend drives
the reset and BSL controls. USB Direct similarly bypasses the ESP for bytes but
does not necessarily remove the ESP's ability to operate the radio control
signals.

A control-plane action therefore does not automatically acquire or replace a
data-plane owner. Its caller supplies the intended stream recipient and the
orchestrator verifies that the current path is consistent with that intent.

### 8.2 Runtime services

The target architecture has the following major services:

| Service | Owns | Must not own |
| --- | --- | --- |
| Gateway coordinator | Product-level operation requests, availability decisions, and coordination among radio, transport, metadata, and firmware services | Vendor protocol details, sockets, UART bytes, GPIO implementation |
| Transport coordinator | Selected transport, transitions among TCP, USB Bridged, and USB Direct, and admission policy during transitions | Radio-family commands, metadata parsing, firmware catalogs |
| NCP byte-link arbiter | NCP serial link, exclusive leases, guarded reads and writes, baud changes, and eligible read-only observation taps | Reset/BSL pins, socket lifecycle, product policy |
| Radio backend | Radio-family identification, reset and bootloader controls, flash geometry, bootloader protocol, and eventual erase/write/verify implementation | TCP policy, Home Assistant entities, product-specific UI |
| NCP host-protocol registry | Privately compiled NCP handlers and runtime selection of a compatible handler | Physical pins, transport selection, support claims |
| Host-protocol handler | Application-protocol transactions and passive parsing for one compatible firmware protocol | UART ownership decisions, bootloader protocol, entity construction |
| Metadata repository | Physical identity, running-image metadata, network snapshot, provenance, cache schema, and persistence | Active probing policy, transport changes, UI requirements |
| TCP transport | Listener and socket lifecycle, provisional/pending/active/parked clients, and the duplex stream endpoint | Direct UART ownership mutation, radio GPIO access, TI/ZNP implementation |
| USB bridge transport | The USB-facing duplex byte endpoint and shared stream pump | Radio GPIO access, protocol interpretation, transport policy |
| USB Direct controller | The declared direct route, host-withdrawal mechanism, and applied state | NCP byte reads or writes |
| Firmware manager | Catalog, target selection, staged-image metadata, download, validation, and update workflow | Direct UART or GPIO access, entity-dependent behavior |
| Platform services | Network, HTTP, storage, time, task scheduling, hashing, and partition access behind portable contracts | Zigbee policy |
| RF coexistence controller | Optional coordination signals and policy required by a specific host/radio/network combination | Protocol-stack behavior or product UI |
| Indicator controller | Logical gateway events, indicator-slot policy, patterns, and global suppression | Transport or radio-state authority |
| Entity adapters | Optional publication of state and user requests into the internal services | Capability creation, operational policy, stored source of truth |

The gateway coordinator is the façade used by ESPHome automations and optional
entities. It does not become a second owner of the resources delegated to the
services.

The radio backend and compatible NCP protocol handlers are privately assembled
by the selected `zigbee_radio` package. A TI/ZNP package may provide one TI
radio backend and compatible ZNP handlers; another NCP family may supply
different implementations without changing product-level policy.

### 8.3 Exclusive authorities

Each mutable resource or state machine has one authority:

1. Only the NCP byte-link arbiter grants, validates, and releases radio
   data-plane access.
2. Only the radio backend drives reset, bootloader, rejoin, and other
   radio-family control signals.
3. Only the transport coordinator changes the selected transport or the
   physical USB Direct path.
4. Only a selected compatible NCP host-protocol handler sends or interprets
   local application-protocol commands.
5. Only the metadata repository mutates persisted radio metadata and
   provenance.
6. Only the firmware manager mutates catalog state, target selection,
   staged-image metadata, and update progress.
7. Only the indicator controller drives logical indicator output.
8. Only the RF coexistence controller drives configured coexistence signals.
9. Entity adapters call public operations and publish returned state; they
   never reproduce any of the decisions above.

Services request work from one another through narrow interfaces. They do not
reach into another service's pins, UART, sockets, persistence blob, or state
enum.

### 8.4 NCP data-plane leases

The current owner enum is a useful behavioral seed, but direct `set_owner()`
calls are not the target contract. The arbiter should issue an exclusive lease
or token. Reads, writes, baud changes, and release require that token, so a
stale caller cannot release or use a newer owner's session.

The initial lease classes are:

| Lease class | Current equivalent | Purpose | Preemption behavior |
| --- | --- | --- | --- |
| Normal TCP stream | `TCP_NORMAL` | Zigbee2MQTT, ZHA, or another ordinary controller client | May be quarantined only by an authorized maintenance operation or ended by an explicit transport change |
| TCP maintenance stream | `TCP_MAINTENANCE` | External bootloader/reset tool | Admitted by the maintenance takeover state machine; blocks normal admission |
| USB Bridged stream | `USB_BRIDGE` | USB host connected through the ESP | Retained across external reset/BSL control actions; ended by an explicit transport change or internal update takeover |
| Local transaction | `LOCAL` | Identification, metadata refresh, or a local host-protocol/bootloader transaction | Never steals an active ordinary stream; once admitted, new remote sessions wait |
| No lease | `NONE` | Idle NCP path or active USB Direct route | No ESP data-plane access |

Passive observation is not a lease and cannot send bytes. It is a tap attached
by the arbiter only to streams that are safe for the selected application
protocol handler. For the current implementation those are Normal TCP and USB
Bridged application streams. Maintenance bootloader traffic, local
transactions, and USB Direct are not observed.

### 8.5 Priority and admission policy

Priority is not a single numerical ordering:

- A routine local diagnostic or metadata refresh waits or fails cleanly while
  an external stream owns the radio. It must never interrupt Zigbee2MQTT, ZHA,
  or another controller merely to refresh information.
- Once a local transaction has been admitted, ordinary remote connections
  cannot take the UART until it finishes.
- An authorized maintenance request may preempt an ordinary TCP stream. The
  existing client remains usable until the maintenance command has arrived and
  the takeover is committed; it is disconnected or quarantined at the last
  safe moment.
- An internal firmware installation is also an authorized maintenance
  operation. It must use the same coordinated takeover mechanism rather than
  bypassing the arbiter.
- A user-requested transport change may terminate the old transport, but the
  transport coordinator performs the complete stop, switch, and start
  transition.
- A control-plane reset or bootloader action may preserve the current external
  byte owner when that owner is the intended recipient of the post-action
  stream.

Only one unadmitted maintenance socket may occupy the pending slot. The first
pending socket remains the candidate and later candidates are rejected until
that slot is released. This keeps admission deterministic.

**DEFERRED:** A protocol-aware preemption policy may wait for a confidently
identified transaction boundary before committing takeover, subject to a hard
deadline. The interfaces may carry grace and deadline information, but the
first generalized implementation need not parse transaction boundaries.

### 8.6 Transport transitions

All transport transitions are serialized by the transport coordinator:

| Target | Required transition |
| --- | --- |
| TCP | Leave USB Direct, stop USB Bridged, restore the application link settings, and admit TCP clients only after the NCP path is ready |
| USB Bridged | Stop TCP admission and sessions, leave USB Direct, restore the application settings of both byte endpoints, and acquire the NCP stream for the bridge |
| USB Direct | Stop TCP and USB Bridged, release every ESP NCP data-plane lease, then apply the declared direct route and host-withdrawal strategy |

On USB Direct:

- the ESP does not probe, seek, sniff, parse, or flash through the NCP
  application link;
- no radio data-plane lease exists;
- the radio backend may still perform supported control-plane actions;
- persisted metadata remains visible;
- mutable metadata provenance becomes cached, or remains awaiting observation
  if bootloader entry has made the running image uncertain.

A transition either reaches the complete target state or reports failure and
returns to a defined safe state. Merely changing a selector value is not a
successful transition.

### 8.7 Operation model

Public operations should report more than success or failure. The eventual
operation result must distinguish at least:

- accepted and running;
- completed;
- busy;
- unavailable in the selected transport;
- incompatible radio or firmware;
- missing compile-time capability;
- timed out;
- failed with an implementation-specific diagnostic.

Long-running operations are cooperative state machines or delegated platform
jobs that report completion back to the coordinator. In particular, network
downloads and radio erase/write/verify work must not monopolize the ESPHome
event loop. Radio flashing may be slow and must feed watchdogs, publish
meaningful progress, honor ownership, and define recovery after interruption.

### 8.8 Firmware-install ownership sequence

The future real installation path must use the following order:

1. Validate the selected catalog entry and any reusable staged image without
   touching the active radio session.
2. Download and validate the complete staged image if required.
3. Request an internal maintenance takeover from the transport coordinator.
4. Preserve the existing external session until the latest safe commit point.
5. Acquire an exclusive maintenance lease and ask the radio backend to enter
   its bootloader.
6. Cooperatively erase, write real staged bytes, and verify them through the
   backend.
7. Reset the radio, release the maintenance lease, and restore the selected
   normal transport.
8. Mark only mutable running-image metadata as awaiting observation, then
   refresh it from a compatible observed protocol or an admitted local query.

Failure before step 3 leaves normal communication untouched. Failure after
takeover enters an explicit recovery state and must not silently restore a
normal transport to a radio still in its bootloader.

### 8.9 Current-to-target placement

The current implementation can migrate incrementally:

| Current implementation | Target placement |
| --- | --- |
| `ZigbeeGatewayComponent` transport selection and request dispatch | Gateway and transport coordinators |
| `ZigbeeSerialInterface` | Radio-link arbiter with lease/token enforcement |
| `ZigbeeTcpServer` | TCP transport; maintenance decisions expressed through coordinator requests |
| `ZigbeeUsbBridge` and shared stream pump | USB bridge transport plus reusable duplex pump |
| Reset, BSL, chip, CCFG, NV, and ROM-BSL code in `ZigbeeGatewayComponent` | TI radio backend |
| ZNP command and sniffing code in `ZigbeeGatewayComponent` | ZNP host-protocol handler |
| Metadata blobs and publication code | Metadata repository plus optional entity adapters |
| `ZigbeeFirmwareManager` | Portable firmware manager using platform, catalog, staging, and radio-update interfaces |
| Direct ESP-IDF facilities | ESP32/ESP-IDF platform-service implementations |

The migration should first put exclusive authority behind interfaces while
preserving behavior and host tests. File moves and public renames should
follow, not lead, that separation.

## 9. C++ service contracts

This section defines the first proposed interface-level contract. Names and
method spelling are illustrative. The responsibilities, dependency direction,
and absence of direct cross-service resource access are the decisions to
review first.

### 9.1 Contract principles

1. Interfaces describe domain behavior and do not expose ESPHome entities,
   GPIO classes, socket classes, ESP-IDF handles, partitions, or preference
   blobs.
2. Concrete platform and ESPHome adapters translate those native facilities
   into the domain interfaces.
3. Every mutable resource has the single authority defined in Section 8.
4. Long-running requests return immediately and advance cooperatively.
5. State is exposed as typed snapshots and events. Human-readable text is a UI
   or logging representation, not the stored state machine.
6. Unsupported, incompatible, unavailable, and failed are distinct outcomes.
7. Interfaces use bounded buffers, caller-owned storage, or explicitly sized
   views on memory-constrained hosts. Unbounded allocation is not assumed.
8. Exceptions and run-time type information are not required.
9. A service may implement ESPHome `Component` lifecycle hooks when needed,
   but domain interfaces do not inherit from `Component`.
10. Public YAML IDs and optional entities do not appear in these contracts.

### 9.2 Shared capability and status types

The Python schema derives and validates the compile-time capability set.
Code generation supplies the resulting fixed set to C++; users do not populate
it.

Illustrative types:

```cpp
enum class Compatibility : uint8_t {
  NOT_REQUIRED,
  UNKNOWN,
  COMPATIBLE,
  INCOMPATIBLE,
};

enum class Availability : uint8_t {
  AVAILABLE,
  STARTING,
  BUSY,
  BLOCKED_BY_TRANSPORT,
  BLOCKED_BY_OWNER,
  WAITING_FOR_NETWORK,
  WAITING_FOR_TIME,
  STORAGE_UNAVAILABLE,
  RADIO_UNAVAILABLE,
  PROTOCOL_UNKNOWN,
  RECOVERY_REQUIRED,
};

struct CapabilityStatus {
  bool compiled;
  Compatibility compatibility;
  Availability availability;
};
```

`compiled == false` means the configuration does not contain the required
implementation or resource. `INCOMPATIBLE` means sufficient evidence disproved
compatibility. Lack of radio access, lack of a cached identity, or an
unrecognized stream produces `UNKNOWN`, not `INCOMPATIBLE`.

Protocol-independent transparent transports use `NOT_REQUIRED`. Availability
is evaluated on request and may change without changing capability or
compatibility.

Capability identifiers should initially be a closed internal enum stored in a
fixed-size set. The dotted names in Section 7 are documentation labels. Whether
those identifiers later become a stable external diagnostic contract remains
part of Decision 5.

### 9.3 Shared operation types

A button press, HTTP command, scheduled action, and internal workflow use the
same request contract:

```cpp
struct OperationId {
  uint32_t value;  // zero is invalid
};

enum class RequestDisposition : uint8_t {
  ACCEPTED,
  ALREADY_SATISFIED,
  REJECTED,
};

enum class OperationPhase : uint8_t {
  QUEUED,
  PREPARING,
  WAITING_FOR_ACCESS,
  RUNNING,
  VERIFYING,
  RECOVERING,
  SUCCEEDED,
  FAILED,
  CANCELLED,
};

enum class OperationProblem : uint8_t {
  NONE,
  BUSY,
  NOT_COMPILED,
  INCOMPATIBLE,
  TRANSPORT_BLOCKED,
  OWNER_BLOCKED,
  NETWORK_UNAVAILABLE,
  STORAGE_UNAVAILABLE,
  INVALID_REQUEST,
  INVALID_IMAGE,
  TIMED_OUT,
  IO_ERROR,
  RECOVERY_REQUIRED,
};

struct RequestResult {
  RequestDisposition disposition;
  OperationId operation;
  OperationProblem problem;
};

struct OperationSnapshot {
  OperationId id;
  OperationPhase phase;
  OperationProblem problem;
  uint8_t progress_percent;
};
```

Services may add typed, operation-specific detail, but the common status never
depends on a free-form English string. Logs may include backend error codes and
technical detail. Entity adapters map stable enum values to concise labels.

An accepted request is not a completed request. Each operation has one terminal
state, and its identifier prevents a late callback from completing a newer
operation.

### 9.4 Dependency direction

The intended dependency direction is:

```text
ESPHome controls / HTTP / schedules
                 │
                 ▼
        Gateway operation façade
                 │
                 ▼
        Gateway coordinator/policy
          │        │          │
          │        │          └──────────────┐
          ▼        ▼                         ▼
 Transport     Radio façade           Firmware manager
 coordinator   │     │                 │      │
   │           ├─ NCP protocol         ├─ catalog/staging
   │           └─ radio backend        └─ maintenance broker
   ▼                    │
 transports             ▼
   │               metadata repository
   ▼
 NCP byte-link arbiter

Typed snapshots/events ───────────────► optional entity adapters/indicators
```

The firmware manager uses a narrow maintenance-broker port; it does not call
entity controls or reach into the gateway object. The gateway coordinator
implements or delegates that port and preserves the ownership policy.

Events flow upward or sideways through typed sinks. Lower services do not hold
a pointer to the concrete top-level component merely to call private methods.

### 9.5 Gateway operation façade

The façade is the single entry point for user-visible and automation-visible
gateway operations:

```cpp
class GatewayOperations {
 public:
  virtual RequestResult request_transport(TransportKind target) = 0;
  virtual RequestResult request_radio_control(RadioControlAction action) = 0;
  virtual RequestResult request_metadata_refresh() = 0;
  virtual RequestResult request_firmware_catalog_refresh(bool force) = 0;
  virtual RequestResult request_firmware_install() = 0;

  virtual GatewaySnapshot snapshot() const = 0;
  virtual CapabilityStatus capability_status(Capability capability) const = 0;
};
```

`GatewaySnapshot` is an aggregate of references or copies of the authoritative
transport, radio, metadata, and firmware snapshots. It must not duplicate their
mutable state.

Buttons, selects, HTTP endpoints, schedules, and future web interfaces call
this façade. They do not call a transport, backend, or firmware manager
directly. Internal operations may use narrower service ports rather than
pretending to be UI requests.

### 9.6 NCP byte-link arbiter and lease-bound stream

The byte-link arbiter owns the physical NCP UART. Its public contract replaces
owner mutation:

```cpp
enum class RadioLeasePurpose : uint8_t {
  TCP_NORMAL,
  TCP_MAINTENANCE,
  USB_BRIDGE,
  LOCAL_PROTOCOL,
  LOCAL_BOOTLOADER,
  FIRMWARE_UPDATE,
};

struct RadioLeaseRequest {
  RadioLeasePurpose purpose;
};

class NcpByteLease {
 public:
  NcpByteLease(NcpByteLease &&);
  NcpByteLease &operator=(NcpByteLease &&);
  NcpByteLease(const NcpByteLease &) = delete;

  bool valid() const;
  int available() const;
  bool read(uint8_t *data, size_t length);
  bool write(const uint8_t *data, size_t length);
  void drain();
};

class NcpByteLink {
 public:
  virtual LeaseResult try_acquire(const RadioLeaseRequest &request) = 0;
  virtual bool release(NcpByteLease &&lease) = 0;
  virtual RadioLinkSnapshot snapshot() const = 0;
};
```

The actual lease contains an opaque generation and owner token. All byte
operations validate it. `NcpByteLease` may use explicit release rather than
destructor-only release so shutdown and transition failures are visible, but
it remains move-only.

No `force_owner()`, public `set_owner()`, or unguarded UART accessor is
permitted. A takeover consists of:

1. the current transport quarantining or stopping its endpoint;
2. the current lease being released or explicitly handed off;
3. the next purpose acquiring the link.

The arbiter may provide an atomic handoff primitive, but only when the caller
presents the valid current lease. It cannot manufacture authorization to
preempt a session.

UART settings belong to the link. A backend may describe required application
or bootloader settings, but the link applies settings only as part of an
orchestrated transition.

Observation eligibility is derived internally from `RadioLeasePurpose`.
A requester cannot opt into observation by setting a flag. This prevents a
transport or updater from accidentally exposing bootloader or local-transaction
bytes to an application-protocol parser.

### 9.7 Observation taps

Observation uses a read-only interface:

```cpp
enum class ByteDirection : uint8_t { HOST_TO_RADIO, RADIO_TO_HOST };

class RadioByteObserver {
 public:
  virtual void observe(ByteDirection direction,
                       const uint8_t *data, size_t length) = 0;
};
```

The arbiter invokes the selected observer only when the active lease permits
application observation. An observer cannot access the lease and cannot write.
The byte callback performs bounded parsing and queues typed observations;
metadata persistence and entity publication occur later in the main loop.

This replaces reliance on a global UART-debug callback as the architectural
observation boundary. A concrete ESPHome UART adapter may still use available
framework hooks internally, but eligibility remains controlled by the active
lease purpose.

### 9.8 Radio backend façade and facets

One selected radio backend exposes only the facets compiled for that family:

```cpp
class RadioBackend {
 public:
  virtual const RadioBackendDescriptor &descriptor() const = 0;
  virtual RadioControl *control() = 0;
  virtual RadioIdentifier *identifier() = 0;  // nullable
  virtual RadioUpdater *updater() = 0;        // nullable
};
```

The descriptor contains radio-family identity, compatible silicon families,
physical control mechanisms, supported image formats, flash constraints, and
required NCP link settings. It does not contain product UI policy.

#### Control facet

```cpp
enum class RadioControlAction : uint8_t {
  RESET,
  ENTER_BOOTLOADER,
  ROUTER_FACTORY_RESET,
};

enum class ControlDataRoute : uint8_t {
  LOCAL_LEASE,
  CURRENT_EXTERNAL_LEASE,
  HARDWARE_BYPASS,
  NO_DATA_RECIPIENT,
};

struct RadioControlRequest {
  RadioControlAction action;
  ControlDataRoute data_route;
};
```

The coordinator validates the route before calling the backend. The backend
drives only its owned controls and reports asynchronous completion. It cannot
change transport mode or replace the current lease.

Backend-required link settings, including UART baud rates where applicable,
are returned as typed requirements.
The transport coordinator applies them to every host-owned endpoint involved
in the selected route. A USB Direct client controls its own direct endpoint.

#### Identification facet

The identifier receives a valid local bootloader or application-protocol lease
as required by its operation. It returns typed physical identity and mutable
image evidence. It never acquires or changes link ownership itself and never
publishes an entity.

#### Update facet

The updater accepts:

- a valid `FIRMWARE_UPDATE` lease;
- a validated random-access staged-image reader;
- typed image metadata;
- a progress/event sink.

It implements family-specific erase, write, verify, reset, and recovery steps.
The firmware manager owns the overall workflow; the backend owns the physical
algorithm.

Optional facets are represented explicitly by null/absent interfaces plus the
compile-time capability set. Calling a missing facet is a programming error
already prevented by schema validation, not a guessed no-op.

### 9.9 NCP host-protocol registry and handlers

The selected radio package privately registers every compatible
host-protocol handler compiled for that backend:

```cpp
class HostProtocolHandler {
 public:
  virtual const HostProtocolDescriptor &descriptor() const = 0;
  virtual void reset_detection() = 0;
  virtual ProtocolEvidence observe(ByteDirection direction,
                                   const uint8_t *data, size_t length,
                                   ProtocolObservationSink &sink) = 0;
  virtual RequestResult request(LocalProtocolOperation operation,
                                NcpByteLease &lease) = 0;
  virtual void loop() = 0;
};

class HostProtocolRegistry {
 public:
  virtual HostProtocolHandler *selected() const = 0;
  virtual Compatibility compatibility() const = 0;
  virtual void invalidate_running_image() = 0;
};
```

Protocol selection requires confident evidence, such as a valid framed
response with the expected command structure. A few coincidental bytes cannot
select a handler. Conflicting evidence resets selection to unknown.

Before selection, the registry may feed eligible bytes to every candidate
detector. Once selected, only that handler interprets the application stream.
The observation sink is bounded and carries semantic metadata evidence; it
does not publish entities or persist data inside the byte callback.

After bootloader entry or a completed external maintenance session, the
registry invalidates the selected running-image compatibility until it
observes or locally probes the new application.

Transparent transport never requires a selected handler. Local commands and
passive metadata interpretation do. A handler receives an already-admitted
local lease for active commands and cannot acquire or preempt one itself.

The common `LocalProtocolOperation` set should contain semantic operations such
as reading device information or setting a supported connection indicator.
Vendor command identifiers remain inside the handler. Backend- or
firmware-specific operations that have no honest common semantic contract stay
in optional facets rather than being forced into the enum.

### 9.10 Metadata repository

The repository owns three independent records:

1. physical identity;
2. running image;
3. network snapshot.

Its mutation contract accepts typed evidence:

```cpp
enum class EvidenceSource : uint8_t {
  RESTORED_CACHE,
  RADIO_BOOTLOADER,
  LOCAL_APPLICATION_QUERY,
  PASSIVE_PROTOCOL_OBSERVATION,
};

class RadioMetadataRepository {
 public:
  virtual void apply(const PhysicalIdentityEvidence &value) = 0;
  virtual void apply(const RunningImageEvidence &value) = 0;
  virtual void apply(const NetworkEvidence &value) = 0;
  virtual void mark_running_image_uncertain(UncertaintyCause cause) = 0;
  virtual void mark_network_cached(UncertaintyCause cause) = 0;
  virtual MetadataSnapshot snapshot() const = 0;
};
```

Evidence structures carry field-presence masks, source, and compatibility
context. Missing fields do not erase known fields unless an operation
explicitly establishes their absence.

The repository enforces these rules:

- factory silicon identity and factory IEEE may be cached indefinitely;
- an NV-configured active IEEE is mutable and refreshes when compatible
  evidence is observed;
- bootloader entry dirties only mutable running-image fields;
- role, application version, and active protocol compatibility are refreshed
  after an image change;
- a confidently parsed application frame is valid evidence, not an unverified
  guess;
- network values remain a last-known snapshot after access is lost or until
  later evidence changes them;
- passive protocol observation is preferred for fields present in normal
  traffic;
- NV inspection is a compatible local fallback for fields that cannot be
  learned passively, not a mandatory duplicate scan;
- persistence and UI publication occur outside synchronous byte callbacks.

Presentation adapters subscribe to snapshot changes. The repository does not
contain pointers to `sensor`, `text_sensor`, or `binary_sensor`.

### 9.11 Transport contracts

The transport coordinator owns selection and transition:

```cpp
class MaintenanceBroker;

class TransportCoordinator {
 public:
  virtual RequestResult request_mode(TransportKind target) = 0;
  virtual MaintenanceBroker &maintenance_broker() = 0;
  virtual TransportSnapshot snapshot() const = 0;
  virtual void loop() = 0;
};
```

Each compiled transport implements a lifecycle contract:

```cpp
class RadioTransport {
 public:
  virtual TransportKind kind() const = 0;
  virtual TransitionResult prepare_start() = 0;
  virtual TransitionResult start() = 0;
  virtual TransitionResult prepare_stop(StopReason reason) = 0;
  virtual TransitionResult stop() = 0;
  virtual TransportSnapshot snapshot() const = 0;
  virtual void loop() = 0;
};
```

Transitions may return in-progress and complete over later loop iterations.
The coordinator changes the published selected mode only after the target
transport reaches its active state.

TCP maintenance is an optional transport facet. It reports a typed maintenance
request containing the socket/session identity and `RESET` or
`ENTER_BOOTLOADER`; it does not invoke backend functions. The coordinator tells
the TCP transport when to park, quarantine, promote, resume, or close a client.

`MaintenanceBroker` is the narrow maintenance portion exposed by the
coordinator. It is shown in the next section and may be passed independently
to the firmware manager.

The USB bridge holds both its USB endpoint and one NCP byte lease. The shared
stream pump operates on lease-bound endpoints and remains protocol-neutral.

USB Direct implements transport lifecycle but never requests an NCP byte
lease. Its concrete controller alone applies the profile's route and
host-withdrawal strategy.

### 9.12 Maintenance broker

External TCP tools and internal firmware installation share one maintenance
contract:

```cpp
struct MaintenanceRequest {
  MaintenanceRequester requester;
  RadioControlAction initial_action;
  uint32_t admission_timeout_ms;
  uint32_t parked_timeout_ms;
};

class MaintenanceBroker {
 public:
  virtual MaintenanceResult request(const MaintenanceRequest &request) = 0;
  virtual MaintenanceSnapshot snapshot(MaintenanceHandle handle) const = 0;
  virtual RequestResult release(MaintenanceHandle handle,
                                MaintenanceOutcome outcome) = 0;
};
```

An admitted maintenance handle identifies the coordinated transport state and,
when host byte access is required, owns the corresponding maintenance lease.
It cannot be copied or forged. External maintenance keeps the lease attached to
its TCP endpoint; internal installation exposes it only to the selected radio
updater.

The broker is the only route by which an internal updater may displace a
normal stream. This preserves the same last-safe-moment and recovery policy for
both maintenance origins.

### 9.13 Firmware-manager and catalog ports

The portable firmware subsystem separates catalog transport from
source-specific parsing:

| Port or service | Responsibility |
| --- | --- |
| Generic catalog service | Wait for network availability; perform startup, scheduled, and manual refreshes; issue conditional HTTP requests; enforce response limits; invoke the configured parser; cache a normalized catalog and freshness validators; and publish typed catalog-change results |
| Catalog parser callback | Transform one immutable downloaded response body into the unified catalog representation |
| Catalog store | Persist normalized catalog data, parser/source identity, freshness validators, and target selection |
| Staging store | Random-access erase/write/read, validated image header, digest, role, version, and freshness metadata |
| Maintenance broker and radio updater | Acquire the radio safely and execute the family-specific installation |

**AGREED:** Catalog families and individual radio targets do not receive
separate downloader components. The public extension point is an ESPHome
`!lambda` written directly in YAML. It receives the response body and returns
the complete normalized catalog. The generic catalog service owns everything
around that transformation, including atomic replacement: a failed parser
result never partially replaces a valid cached catalog.

**AGREED:** One configured radio has exactly one active catalog and one
firmware manager. A profile selects one catalog package; the initial
architecture does not aggregate independently downloaded catalogs or resolve
duplicates and priorities among multiple sources.

**AGREED:** The configured radio backend is the single source of truth for the
catalog target. A source-specific YAML parser may read the fixed internal
`zigbee_radio` descriptor from its lambda. It must not require a duplicate
`firmware_catalog_chip` setting or depend on mutable detected metadata, so
catalog refresh remains possible while the radio is inaccessible.

Conceptually:

```yaml
firmware_catalog:
  id: zigbee_firmware_catalog
  url: ${firmware_catalog_url}

  refresh:
    time_id: gateway_time
    hour: ${firmware_catalog_refresh_hour}
    minute: ${firmware_catalog_refresh_minute}
    days: ${firmware_catalog_refresh_days}

  parser: !lambda |-
    FirmwareCatalog catalog;

    // Interpret `body` and append normalized entries.

    return catalog;
```

A reusable source package such as `catalogs/xzg-ti.yaml` contains that lambda
inline. Adding a catalog format therefore requires no new external component,
catalog-specific downloader, or `.h`/`.cpp` implementation. A large or reused
lambda may delegate to a C++ helper as a private optimization, but that is
never required by the public contract.

Persisted catalog state is associated with the source URL, parser identity and
version, and normalized-catalog schema version. A change to any of them
invalidates the corresponding cached result and HTTP validators.

#### Normalized firmware-image descriptor

**AGREED:** The catalog lambda returns a hybrid descriptor: lifecycle fields
used by the generic catalog and firmware managers are strongly typed, while
radio-specific installation values are carried as bounded, serializable
attributes for the configured updater.

| Group | Fields and meaning |
| --- | --- |
| Identity | Source identifier and a source-stable entry key |
| Presentation | Display name, opaque version text, and a source-provided ordering value |
| Firmware | Application (`zigbee` for the current scope), Coordinator or Router role, NCP protocol, and firmware lineage |
| Target | Radio-backend family, target silicon, and board/build variant |
| Download | Image URL plus optional declared size and digest |
| Installation | Image format, updater method, resulting application-link settings, and backend-specific attributes |

Conceptually:

```yaml
identity:
  source: xzg
  key: unique-source-entry

presentation:
  name: CC2652P7 Coordinator 20250321
  version: "20250321"
  order: 20250321

firmware:
  application: zigbee
  role: coordinator
  protocol: znp
  lineage: koenkk

target:
  backend: ti_cc13x2_cc26x2
  target: CC2652P7
  variant: default

download:
  url: https://example.invalid/radio.bin
  size: 123456
  sha256: optional-source-digest

installation:
  format: raw_binary
  method: ti_serial_bsl
  application_baud_rate: 115200
  attributes: {}
```

Version text is not interpreted universally. The parser supplies the ordering
value used to sort releases within the applicable target, role, variant, and
lineage. An installed-image comparison must not order unrelated lineages.

HTTP validators learned while checking or downloading an image belong to the
staged-image record, not to the catalog entry. Remotely supplied target and
installation values remain untrusted input: the generic manager validates
their bounds, and the configured updater validates the image format, layout,
and attributes it actually consumes before modifying the radio.

#### Staging resource ownership

**AGREED:** Firmware staging is a profile-provided host resource. The
firmware-update feature requires and consumes it but does not choose a
platform-specific storage mechanism or silently impose one universal size.

- The device profile selects a platform storage adapter and reserves capacity
  in its flash layout.
- The platform adapter owns partition, flash-region, or filesystem mechanics
  and exposes the actual usable image capacity through `StagingStore`.
- The generic firmware manager performs only bounded erase, write, read,
  validity-header, and commit operations through that interface.
- The catalog parser does not allocate or manage staging storage.
- The staging resource uses component-owned stable identifiers; callers do not
  rename its internal ID or partition label.

Conceptually, UZG-01 contributes:

```yaml
- !include
    file: ../storage/esp32-firmware-staging.yaml
    vars:
      firmware_staging:
        size: 768kB

- !include
    file: ../features/firmware-update/xzg-ti.yaml
```

Configuration validation ensures that the requested region fits the selected
host flash layout. Catalog entries with a declared size exceeding usable
capacity are omitted. An entry without a declared size may remain selectable,
but the downloader rejects a declared HTTP length or received byte count that
would exceed capacity. Header overhead is owned by the storage adapter and is
already excluded from the capacity reported to the manager.

Firmware-update support requires a staging resource; there is no implicit
stream-directly-to-radio fallback. Another host may implement the same
interface with a different flash region or storage technology.

Host networking and storage are provided through platform interfaces. The
catalog service and manager must not include ESP-IDF HTTP, partition, FreeRTOS,
or mbedTLS types in their portable headers.

The staged-image reader exposes real downloaded bytes to both simulation and
the future updater. Starting a replacement download invalidates its staged
identity before the first new byte is accepted; identity becomes valid only
after the full image and digest are committed.

Target role and version remain manager state independent of optional select
entities. Dynamic option changes emit a typed presentation event; the Home
Assistant adapter decides whether an API reconnect is required.

Startup gating is an optional host integration around catalog readiness. It is
not a concern of the catalog parser or radio backend.

### 9.14 Events, threading, and reentrancy

All orchestration state machines have one mutation context: the ESPHome main
loop.

- Socket and UART pumps do bounded work per loop iteration.
- Synchronous byte observers perform only bounded parsing and enqueue typed
  observations.
- Blocking HTTPS or storage work may run in a platform worker task.
- A worker receives an immutable request and returns an immutable result
  through a bounded mailbox.
- A worker never publishes entities, changes transport, releases a lease,
  updates metadata, or completes an operation directly.
- The main loop discards a late worker result whose operation identifier no
  longer matches the active request.
- Event callbacks must not synchronously start a second transition while a
  service is mutating state. Requests are queued for the next coordinator
  cycle.

Queues define an overflow policy. Metadata observations may coalesce by field
or retain the newest complete record. Ownership, maintenance, and operation
completion events must not be silently dropped.

### 9.15 State publication and optional UI

Every authoritative service exposes a typed snapshot and a revision counter.
Presentation adapters publish only when the relevant revision changes.

The default adapters provide entity metadata and translate snapshots into:

- transport selection and state;
- connection and maintenance diagnostics;
- radio identity and running-image information;
- network snapshot and provenance;
- firmware catalog, target, staged image, progress, and failures;
- operation availability where useful.

Zero adapters is valid. The gateway runs the same state machines, caches,
transport policy, and update workflow when no Home Assistant entities are
declared.

Indicator logic consumes the same typed events and snapshots. It must not infer
ownership by inspecting entity state.

### 9.16 Construction and lifetime

Code generation constructs concrete services from the selected profile or
advanced assembly:

1. platform adapters and physical resources;
2. NCP radio link, radio backend, and optional RF coexistence controller;
3. compatible NCP host-protocol handlers and registry;
4. metadata repository;
5. transports and transport coordinator;
6. catalog, staging, firmware manager, and maintenance port;
7. gateway coordinator and façade;
8. optional indicator and entity adapters.

Dependencies are supplied explicitly during construction or setup. A global
service locator is not required. Optional features omit their concrete service
and capability; a dummy service returning success must not hide an invalid
configuration.

Owned dependencies outlive their consumers. No service stores a reference to a
short-lived operation object or lease beyond its documented handle lifetime.

### 9.17 Test seams

The contracts create the following host-test boundaries:

- capability, compatibility, and availability derivation;
- request and operation lifecycle;
- lease generation, release, handoff, and stale-token rejection;
- transport transitions and failure rollback;
- maintenance admission, pending/parked timeouts, and recovery;
- protocol selection from framed evidence;
- metadata field merging and provenance;
- catalog selection, persistence, staging validity, and update workflow;
- radio updater behavior against a fake lease and staged-image reader;
- entity-free operation of every service.

Platform adapter tests cover ESPHome UART, sockets, preferences, HTTP, time,
and partitions. Supported-profile hardware tests remain necessary for pin
polarity, bypass routing, baud transitions, reset/BSL behavior, real flashing,
and recovery. End-to-end qualification may use Zigbee2MQTT, ZHA, and other
clients to exercise different traffic, connection, and reconnect patterns.
This validates client neutrality; it does not create client-specific gateway
implementations.

### 9.18 Initial migration rules

1. Introduce shared status, operation, and event types without changing current
   behavior.
2. Replace direct serial owner mutation with generation-checked leases.
3. Replace the TCP server's gateway friendship with maintenance and lifecycle
   interfaces.
4. Move entity pointers behind optional presentation adapters.
5. Extract metadata mutation and persistence from the gateway component.
6. Extract TI physical/bootloader behavior into the TI backend.
7. Extract ZNP parsing and commands into a compatible protocol handler.
8. Put transport switching behind the transport coordinator.
9. Split platform-specific catalog and staging facilities from the portable
   firmware manager.
10. Connect actual flashing only after the maintenance broker, TI updater, and
    recovery contract are host-tested.

Each step must preserve transparent TCP/USB behavior and the current testable
state machines. Architectural extraction must not be combined with unrelated
behavioral changes.

## 10. UZG-01 reference behavior

The UZG-01 implementation is the behavioral reference for the first
generalization. This section maps its workflows into the agreed services.
Refactoring is correct only when each applicable scenario still produces the
same externally observable result, unless a separate product decision changes
that behavior.

### 10.1 Reference hardware assembly

The current profile supplies:

| Resource | UZG-01 value | Target owner |
| --- | --- | --- |
| Radio UART | GPIO4 TX, GPIO36 RX, 115200 baud, 2048-byte RX buffer | Radio link |
| USB bridge UART | GPIO1 TX, GPIO3 RX, 115200 baud, 2048-byte RX buffer | USB bridge transport |
| Direct/bridged hardware switch | GPIO33 | USB Direct controller |
| Radio reset | Inverted GPIO16 | TI radio-control facet |
| Radio BSL/router control | Inverted GPIO32 | TI radio-control facet |
| Red mode LED | GPIO12 | Indicator output adapter |
| Blue power LED | GPIO14 | Profile-level indicator output |
| Mode button | Inverted GPIO35 input | ESPHome control adapter |
| Ethernet | LAN8720 with profile-supplied pins and PHY configuration | ESPHome/platform network adapter |
| Firmware staging | 768 KiB `zigbee_fw` custom partition | ESP32 staging-store adapter |

UART0 logging is disabled because the USB-facing serial endpoint owns it.
Enabling a serial logger would inject bytes into the Zigbee stream and remains
an invalid UZG-01 configuration.

### 10.2 Startup sequence

The target services preserve this startup order:

1. Initialize the radio control pins into their released logical states.
2. Restore the requested serial transport and apply the direct-path switch
   before admitting external traffic.
3. Load the independent physical-identity, running-image, and network records.
4. Publish cached values and truthful provenance through any attached
   presentation adapters.
5. If physical identity is already valid, do not block startup by probing the
   radio.
6. If physical identity is missing and TCP is selected, admit a one-time local
   identification operation before opening the ordinary TCP data path.
7. If physical identity is missing in either USB mode, defer identification;
   the ESP cannot safely assume the USB host is idle.
8. Start USB Bridged immediately when selected. Start the TCP listener only
   after network readiness. USB Direct creates no radio lease.
9. Independently load the firmware catalog cache, target selection, and staged
   image header.
10. Request one upstream catalog refresh after network connection. Hold the
    initial Home Assistant API connection until the refresh completes or the
    configured startup timeout expires.

A startup catalog failure does not block the gateway. A valid cached catalog
remains usable; without one, catalog state remains unavailable.

### 10.3 Transport-mode behavior

| Selected mode | Radio byte path | ESP radio lease | TCP listener | Local radio operations |
| --- | --- | --- | --- | --- |
| TCP | TCP client through ESP to radio | Normal, maintenance, or local as admitted | Enabled when network is ready | Allowed only while no TCP client owns or waits on the path |
| USB Bridged | USB UART through ESP to radio | USB Bridge | Stopped | Not admitted because USB host lifecycle cannot be detected |
| USB Direct | USB hardware directly to radio | None | Stopped | No byte access; supported reset/BSL control remains possible |

Changing modes is explicit and may end the previous external session. The old
transport stops and releases its lease before GPIO33 or the new transport is
activated.

If the radio is expected to be in BSL:

- entering USB Bridged configures both host-owned UARTs to the TI flashing baud;
- resetting restores the normal radio and USB baud rates;
- switching to TCP resets the radio into a known application state before
  reopening ordinary service;
- USB Direct leaves serial speed under control of the external USB tool.

Entering USB Direct preserves cached metadata and changes mutable provenance
to cached unless the running image already awaits observation.

### 10.4 Ordinary TCP session

The reference sequence is:

1. Accept the first connection as provisional without giving it the UART.
2. Buffer its initial bytes.
3. Treat the first non-maintenance payload as evidence of an ordinary client.
4. Before forwarding those bytes, perform the best-effort TI/ZNP connection
   indicator command while the client remains quarantined.
5. Acquire a Normal TCP lease, clear stale UART bytes, and forward the buffered
   payload.
6. Pump bytes bidirectionally with bounded per-loop work.
7. Pass eligible application bytes to passive ZNP observation without
   consuming or modifying them.
8. On disconnect, release the lease and perform the best-effort indicator-off
   operation.

The connection-indicator command is skipped when the known running image is a
router because that firmware does not expose the required ZNP command. Failure
or timeout of this cosmetic command never rejects or delays the ordinary
session beyond its bounded command timeout.

### 10.5 Additional TCP connections

With one ordinary client active:

- the first additional connection occupies the single pending slot;
- it is accepted at the TCP level but receives no UART access;
- its early bytes are buffered only up to the bounded prebuffer limit;
- a later additional connection is rejected;
- a pending connection that does not become an authorized maintenance client
  before the configured timeout is closed;
- if the ordinary owner disconnects, the pending connection becomes the new
  provisional candidate and is classified from its buffered or later bytes.

The first pending client is retained. A later arrival never replaces it.

### 10.6 TCP maintenance compatibility

Both command-first and connection-first flashing tools must remain compatible.

| Situation when `/cmdZigBSL` arrives | Required behavior |
| --- | --- |
| No TCP client | Enter BSL, arm the rendezvous timeout, and give the next accepted client the maintenance stream |
| Silent provisional client | Convert that client to maintenance and enter BSL |
| Ordinary client plus pending client | Park the ordinary client, promote the pending client to maintenance, and enter BSL |
| Ordinary client without pending client | Arm rendezvous while leaving the ordinary client fully operational; commit takeover only when the flashing connection arrives |
| Existing maintenance client | Reapply the requested BSL control action to that client |

If an armed ordinary owner disconnects before the flashing socket arrives, the
gateway preserves the armed request. The replacement connection becomes the
maintenance client and BSL is entered at that last possible moment.

`/cmdZigRST` follows the same ownership selection but resets the radio into its
application instead of forcing BSL. With no TCP client it performs a control
reset without creating a maintenance data session.

At maintenance takeover:

1. stop forwarding and clear stream-pump buffers;
2. park the previous normal socket when one exists;
3. discard incoming data from the parked socket while keeping it connected;
4. attach the maintenance lease exclusively to the flashing socket;
5. perform the requested control action without consuming the resulting radio
   bytes locally.

At maintenance completion or failure:

- reset the radio if it may still be in BSL;
- restore normal UART settings;
- close the parked ordinary socket abortively so the controller client
  reconnects against the post-maintenance radio;
- mark mutable running-image metadata as awaiting observation;
- preserve physical identity;
- reopen ordinary admission immediately.

An armed command that never receives its client expires. If BSL had already
been entered, the gateway performs a recovery reset. A parked connection also
has a longer safety timeout so it cannot consume a slot indefinitely.

### 10.7 Local radio controls

The same user request has a route determined by current transport:

| Request | TCP with no client | TCP with client | USB Bridged | USB Direct |
| --- | --- | --- | --- | --- |
| Reset radio | Acquire local lease, toggle reset, consume reset indication, and run compatible diagnostics | Preserve client as external recipient and do not consume its reset bytes | Preserve bridge owner; control pins only | Toggle control pin; USB hardware remains byte recipient |
| Enter BSL | Coordinate a TCP maintenance rendezvous when the listener exists | Arm or perform TCP maintenance takeover | Preserve bridge owner, enter BSL, and change both bridge UARTs to flashing baud | Enter BSL while bytes bypass the ESP |
| Refresh information | Run only after acquiring an idle local lease | Reject without disturbing client | Unavailable | Unavailable |
| Router factory reset/pairing | Pulse the compatible router control when role is Router or unknown | Same control-plane action; no UART bytes consumed | Same | Same |

Local reset waits cooperatively for `SYS_RESET_IND` with a timeout and performs
post-reset application diagnostics only when it owns the bytes. An external
recipient must receive the reset indication unmodified.

The router factory-reset/pairing control is unavailable when a confidently
known coordinator role makes it inapplicable. Unknown role remains permitted
because an externally flashed router may not yet have been observed.

### 10.8 Identification and metadata refresh

The current TI refresh performs:

1. mark mutable running-image metadata as awaiting observation;
2. enter TI ROM BSL under a local bootloader lease;
3. synchronize with the bootloader;
4. detect the chip family and its flash/NV layout;
5. read physical identity, CCFG, and compatible NV information;
6. reset the radio;
7. run compatible application-level ZNP diagnostics;
8. commit each successfully collected metadata scope independently.

Failure does not erase previously valid records. The target implementation
makes the pin delays, reads, and reset wait cooperative but preserves the same
transaction boundary.

Metadata scopes are:

| Scope | Fields in the current implementation | Lifetime |
| --- | --- | --- |
| Physical identity | Silicon family and identifiers, revision, supported protocols, flash geometry, hardware label, factory IEEE | Cached indefinitely once verified |
| Running image | CCFG mode/BSL configuration, firmware version, stack version, active IEEE, role | Cached until compatible evidence refreshes it; marked awaiting observation after BSL |
| Network snapshot | PAN ID, channel, joined state, parent IEEE, extended PAN ID | Last known; updated from compatible observation or explicit local acquisition |
| Live observation | Transmit power | Published when an understood ZNP command changes it; not persisted as network metadata |

Passive ZNP observation recognizes valid framed responses and indications for
version, device information, extended network information, state change, and
supported transmit-power commands. Only an FCS-valid, structurally valid frame
may update state.

Network data learned from a valid frame is observed evidence. It becomes
cached—not disproved—when later access is unavailable. NV inspection remains a
fallback for compatible fields not learned through normal traffic.

### 10.9 Firmware catalog lifecycle

The reference firmware-manager behavior is:

1. Restore persistent target role and version independently of any select
   entity.
2. Inspect the staging header and adopt its target only when no explicit
   persistent target exists.
3. Load the normalized catalog and ETag from persistent storage.
4. Populate complete role and version option sets.
5. Fetch the upstream catalog after network connection on every boot.
6. Use a conditional request unless a forced refresh was requested.
7. Normalize only supported Zigbee roles and the configured TI chip.
8. Sort versions for presentation and preserve a remembered selection per
   role.
9. Persist a valid changed selection immediately.
10. Request one Home Assistant API reconnect only when a complete dynamic
    option set has changed.

Manual and scheduled refresh use the same workflow. The daily schedule is a
profile policy provided by the Home Assistant time component; catalog parsing
does not depend on Home Assistant.

On refresh failure:

- preserve and use a valid cached catalog;
- report the catalog check failure;
- remain unavailable when no valid catalog exists;
- do not create a private retry schedule beyond normal boot, configured
  schedule, or explicit automation requests.

### 10.10 Staged image and update simulation

The current simulation is the behavioral foundation for actual installation:

1. Require a catalog-backed selected role and version.
2. Reuse a staged image only when role, version, and filename match.
3. If a matching image exists and the network is available, issue a
   conditional freshness request using its ETag.
4. Treat `304` as unchanged.
5. Treat a successful `200` freshness response as a changed image requiring
   replacement.
6. If the freshness request itself fails, retain and use the locally verified
   staged image.
7. When no matching staged image exists, require network and download it.
8. Invalidate the old staging header before erasing or accepting replacement
   bytes.
9. Stream real downloaded bytes into the staging partition while calculating
   SHA-256.
10. Read the entire staged image back cooperatively and verify the digest.
11. Commit role, version, filename, ETag, size, baud, and digest only after
    validation succeeds.
12. Simulate radio erase in layout-sized steps.
13. Simulate radio writes using the actual staged bytes and BSL-sized chunks.
14. Simulate verification by reading those staged bytes again and comparing
    all digests.

The staged image and its metadata survive reboot. Merely changing either target
select does not erase them. Reuse eligibility is checked only when that image
is needed.

Manual invalidation erases the validity header and leaves no reusable staged
identity. A replacement download that fails after invalidation remains invalid,
so the next attempt downloads again.

The simulation never requests radio ownership. Real installation adds the
maintenance and TI updater steps from Section 8 without changing catalog,
selection, freshness, staging, or digest semantics.

### 10.11 Optional presentation and integration behavior

All current Home Assistant entities have a target presentation adapter:

- transport selection;
- TCP connection, pending, parked, event, and counter diagnostics;
- physical, image, network, and provenance entities;
- reset, BSL, router pairing reset, and metadata-refresh controls;
- catalog target selection, status, refresh, staged-image invalidation,
  simulation progress, and failure diagnostics.

Omitting one or every entity does not remove the underlying function. The
complete demonstration profile exposes all applicable entities.

The compatibility HTTP endpoints `/cmdZigBSL` and `/cmdZigRST` are request
adapters over the same gateway operations. They exist only when a compatible
web-server adapter is compiled.

UZG-compatible mDNS advertisement remains profile integration. It describes
the configured TCP service and is not owned by the radio backend.

### 10.12 Indicator behavior

Current UZG-01 behavior is preserved separately from future generalized
indicator policy:

- the blue ESP-controlled power LED is continuously on;
- the red ESP-controlled mode LED is off for TCP and on for either USB mode;
- the TI-radio yellow/white LED is best-effort on during an admitted normal TCP
  session and off after it ends;
- the USB hardware exposes no reliable port-open signal, so the gateway does
  not currently claim precise USB client connection indication.

Future flashing patterns and global/night-time suppression remain deferred.
They must be layered in the indicator controller without changing transport or
radio ownership.

### 10.13 Current implementation gaps versus target form

Every current behavior has a target service. The remaining gaps are structural:

- some TI pin sequences and the startup metadata probe still use blocking
  delays;
- `ZigbeeTcpServer` directly mutates serial ownership and calls private gateway
  methods;
- metadata storage and entity publication remain combined;
- passive ZNP parsing relies on the UART debug callback;
- transmit-power observation publishes directly from the byte callback;
- the firmware manager directly owns ESP-IDF tasks, HTTP, hashing, partition,
  preference, entity, and API-reconnect details;
- firmware simulation is not yet connected to radio maintenance or a real TI
  updater;
- indicator outputs are not yet behind the generalized indicator controller.

These are migration tasks, not reasons to change the observable reference
behavior.

## 11. Architecture invariants

The following current behaviors must remain true across supported products:

1. Exactly one owner may communicate through an external radio link at a time.
2. An admitted local operation has priority over new ordinary remote access,
   but routine local diagnostics never steal an active external stream.
3. Maintenance takeover preserves an existing client until the last safe
   moment.
4. A transport that bypasses the host also prevents host-side observation and
   local radio access.
5. Cached metadata remains visible when access is unavailable, with truthful
   provenance.
6. Entering a radio bootloader dirties only metadata that may have changed.
7. Home Assistant entities are optional and explicitly requested in YAML.
8. Entity schemas provide sensible metadata that callers may override.
9. A device profile contains physical facts, not copies of gateway algorithms.
10. Radio and NCP host-protocol backends contain their specific mechanisms,
    not product policy.
11. Platform-independent algorithms remain testable without ESPHome hardware.
12. Unsupported combinations fail during configuration whenever they can be
    identified at compile time.
13. Unknown NCP firmware does not disable transparent serial transport.
14. Protocol-dependent local operations run only through a compatible NCP
    handler.
15. One ESPHome node owns no more than one Zigbee gateway and Zigbee radio.
16. Exactly one Zigbee2MQTT, ZHA, or other controller client may own a
    Coordinator NCP at a time.

## 12. Relationship to the current implementation

The current UZG-01 implementation is both the first supported product and the
behavioral reference for the generalized architecture.

Reusable foundations already exist:

- exclusive serial ownership;
- a shared stream pump for TCP and USB Bridged transports;
- TCP maintenance takeover policy;
- transport-mode state handling;
- passive ZNP observation;
- metadata caching and provenance;
- firmware catalog, staging, and simulated update workflow;
- host tests for platform-independent logic;
- optional entity construction in the ESPHome schema.

Current couplings that will require architectural placement include:

- ESP32-only component validation;
- direct ESP-IDF HTTP, partition, task, and SHA facilities;
- UZG-01-specific USB and GPIO resources;
- TI-specific reset, BSL, identification, NV, and firmware behavior;
- ZNP-specific passive observation;
- one firmware catalog format and chip selection.

Sections 8 through 10 map these couplings into agreed services and reference
workflows. Exact file boundaries and implementation names may evolve within
those contracts.

## 13. ESP Thread Border Router board research profile

This section evaluates Espressif's ESP Thread Border Router/Zigbee Gateway
board as the first materially different external-NCP hardware profile. It
records architecture evidence, not a current support claim or implementation
promise. The evidence was reviewed on 2026-07-28.

### 13.1 Verified hardware assembly

The official board combines two independently programmable Espressif devices:

| Resource | Verified board implementation | Architectural placement |
| --- | --- | --- |
| ESPHome host | ESP32-S3-WROOM-1 | Host platform and board profile |
| 802.15.4 radio | ESP32-H2-MINI-1 | Radio family and backend |
| Host/radio UART | S3 GPIO17 RX/GPIO18 TX to H2 UART0 TX/RX | NCP radio link |
| Radio reset | S3 GPIO7 to H2 enable | Espressif radio backend control |
| Radio boot mode | S3 GPIO8 to H2 GPIO9 | Espressif ROM-downloader control |
| Direct radio USB | Board USB1 to H2 native USB | Unmanaged recovery/development path |
| Host USB | Board USB2 to S3 native USB | Host console or USB Bridged endpoint |
| Wi-Fi backbone | ESP32-S3 Wi-Fi | Wi-Fi network package |
| Optional Ethernet | Sub-Ethernet board using W5500 and SPI | Alternate Ethernet network package |
| RF coexistence | S3/H2 external PTA signals | Optional profile/platform coordination service |

The H2 UART0 application pins are GPIO24 TX and GPIO23 RX. The official board
therefore expects an H2 application built with that UART mapping. The S3-side
SPI Ethernet defaults documented by Espressif are GPIO21 SCLK, GPIO45 MOSI,
GPIO38 MISO, GPIO41 CS, GPIO39 interrupt, and GPIO40 reset. A profile must still
match its values to the actual board revision rather than treating this table
as a universal ESP32-S3 pinout.

The external host has a dedicated physical path to the H2 through USB1, but the
S3 cannot connect or disconnect that route. It therefore does not qualify as a
managed USB Direct transport. The profile leaves USB1 available for recovery
and development without advertising it in the transport selector. A future
complete route controller requires H2-firmware-assisted arbitration that lets
the S3 grant and revoke the direct host before reclaiming its UART link.

The two network choices are profile variants. ESPHome does not permit Wi-Fi
and Ethernet in the same configuration, so the known-device profile should
select one reusable network package rather than attempting runtime failover
between both.

References:

- [Espressif board hardware overview](https://docs.espressif.com/projects/esp-thread-br/en/latest/hardware_platforms.html)
- [Main-board schematic](https://dl.espressif.com/dl/schematics/esp_thread_br_zigbee_gw_schematics_v1.3.pdf)
- [Sub-Ethernet schematic](https://dl.espressif.com/dl/schematics/esp_thread_br_zigbee_gw_sub_ethernet_schematiccs_v1.0.pdf)
- [ESP32-H2-MINI-1 datasheet](https://documentation.espressif.com/esp32-h2-mini-1_mini-1u_datasheet_en.html)
- [ESPHome Ethernet component](https://esphome.io/components/ethernet/)

### 13.2 Verified firmware choices and scope

The same H2 hardware can run firmware with incompatible host interfaces:

| H2 firmware path | Fits this project | Reason |
| --- | --- | --- |
| Espressif Thread `ot_rcp` | No | RCP requires the S3 to run OpenThread and border-router services |
| Current Espressif Zigbee gateway example | No | RCP requires the S3 to run the Espressif Zigbee stack |
| Third-party ZBOSS NCP | Candidate | H2 runs the Zigbee stack and exposes a serial NCP protocol |

Espressif's official Thread design runs the OpenThread stack and border-router
functions on the S3. The H2 runs an `ot_rcp` image and exchanges Spinel traffic
with the host over UART or SPI. The official implementation also supports
host-controlled H2 RCP updates and external RF coexistence.

These official RCP paths confirm why they are outside the current project:
using either would move a protocol stack onto the ESPHome host. They are not
alternative serial backends for the common gateway.

Espressif's current Zigbee gateway example similarly uses an H2 RCP and runs
the Zigbee stack on the host. It is not a transparent Zigbee2MQTT NCP.
An older Espressif NCP example used Espressif ZNSP framing, which is not the
ZBOSS wire protocol implemented by zigbee-herdsman.

The practical first Zigbee path is the independently maintained
`tostmann/esp-coordinator` ZBOSS NCP firmware. Its v1.5.0 release supports
ESP32-H2, and the firmware exposes the NCP stream over UART and native USB.
Standard Zigbee2MQTT provides the baseline `zboss` adapter. The firmware's
extended backup, restore, and reset workflows currently require the
maintainer's patched Zigbee2MQTT build, so they are not generic external-client
capabilities of this profile.

Stock ZHA does not currently list ZBOSS among its selectable radio types.
Third-party zigpy/ZHA work exists, but it is a separate compatibility path and
does not justify a stock-ZHA support claim. The first H2 profile is therefore a
Zigbee2MQTT experiment unless and until a ZHA adapter path is integrated and
qualified.

The reviewed published H2 image is coordinator firmware. No compatible router
image has yet been established, so an initial profile must expose only the
Coordinator target role. The firmware's default non-C5 UART TX pin is GPIO22,
while this board requires H2 GPIO24. A board-specific upstream build using
GPIO24 TX and GPIO23 RX is therefore a prerequisite for S3↔H2 operation.
USB1 may work with the generic build, but that does not prove the internal UART
path.

References:

- [Espressif Thread Border Router implementation](https://github.com/espressif/esp-thread-br)
- [Espressif RCP update design](https://docs.espressif.com/projects/esp-thread-br/en/latest/dev-guide/ota_update.html)
- [ESPHome OpenThread component](https://esphome.io/components/openthread/)
- [ESPHome OpenThread implementation](https://github.com/esphome/esphome/blob/dev/esphome/components/openthread/__init__.py)
- [Espressif Zigbee SDK](https://github.com/espressif/esp-zigbee-sdk)
- [Zigbee2MQTT ZBOSS adapter status](https://www.zigbee2mqtt.io/guide/adapters/zboss.html)
- [ESP32 ZBOSS NCP firmware](https://github.com/tostmann/esp-coordinator)

### 13.3 Candidate first Zigbee profile

The first implementation target for this board should be an experimental
Zigbee NCP Coordinator profile:

| User mode | Data path | Key difference |
| --- | --- | --- |
| TCP | Controller client → TCP → S3 → UART → H2 | Network client; S3 owns the NCP link |
| USB Bridged | USB2 → S3 USB CDC → UART → H2 | S3 remains in control and can coordinate maintenance |

ESPHome's `tinyusb` and `usb_cdc_acm` components provide a suitable S3
USB-facing duplex byte endpoint for USB Bridged mode. The shared stream pump
can therefore be reused without pretending that the endpoint is a hardware
UART. ESPHome permits its separate ESP32-S3 USB Serial/JTAG logger alongside
TinyUSB; the profile must reject only a logger configured onto the same USB
CDC endpoint.

USB1 remains a direct recovery and development connection to the H2, but it is
not advertised as a gateway transport. The S3 cannot disconnect that route or
prove that its external host has stopped transmitting. Exposing it as USB
Direct would therefore violate the single-owner invariant. A future
H2-firmware-assisted arbitration mechanism may allow the profile to add that
mode.

References:

- [ESPHome TinyUSB component](https://esphome.io/components/tinyusb/)
- [ESPHome USB CDC ACM component](https://esphome.io/components/usb_cdc_acm/)

The proposed profile inputs are:

| Input | Proposed provider |
| --- | --- |
| Host | ESP32-S3 ESP-IDF package |
| Network | Wi-Fi or W5500 profile variant |
| Radio backend | ESP32-H2 lifecycle, identity, ROM download, flash, and recovery |
| Host protocol | ZBOSS handler privately selected by the H2 NCP package |
| Radio application link | S3 UART using GPIO17 RX/GPIO18 TX |
| USB Bridged endpoint | S3 native USB2 CDC |
| Direct H2 USB | Recovery/development path only; not a managed transport |
| Controls | S3 GPIO7 H2 enable and GPIO8 H2 boot mode |
| Firmware update | Espressif ROM serial updater using validated merged or segmented images |
| RF coexistence | Profile-specific S3/H2 PTA implementation when Wi-Fi is used |
| Optional host features | Separate ESPHome packages, including Bluetooth proxy where resources permit |

The candidate Wi-Fi profile is:

```yaml
packages:
  - !include
    file: ../hosts/esp32-s3-idf.yaml
    vars:
      host:
        esp32:
          variant: esp32s3
          flash_size: 4MB
          framework:
            type: esp-idf

        psram:
          mode: quad
          speed: 80MHz

  - !include
    file: ../network/wifi.yaml
    vars:
      wifi:
        ssid: ${wifi_ssid}
        password: ${wifi_password}

  - !include
    file: ../zigbee/radios/espressif-h2-zboss-ncp.yaml
    vars:
      zigbee_radio:
        backend:
          expected_target: ESP32-H2

          reset_pin:
            number: GPIO7
            inverted: true

          boot_pin:
            number: GPIO8
            inverted: true

  - !include
    file: ../zigbee/gateway.yaml
    vars:
      zigbee_gateway:
        radio_uart:
          tx_pin: GPIO18
          rx_pin: GPIO17
          baud_rate: 115200
          rx_buffer_size: 2048

        tcp:
          port: ${tcp_serial_port}
          pending_socket_timeout: ${tcp_pending_socket_timeout_ms}ms
          parked_socket_timeout: ${tcp_parked_socket_timeout_ms}ms

        usb_bridge:
          usb_cdc_acm:
            rx_buffer_size: 2048
            tx_buffer_size: 2048

  - !include ../integrations/esphome-management.yaml
```

This is the target package contract, not currently valid input for the
unimplemented gateway and H2 packages. The underlying ESPHome primitives were
independently accepted by `esphome config` with ESPHome 2026.7.2: ESP32-S3
ESP-IDF, conservative 4 MB flash layout, quad PSRAM, Wi-Fi, UART on GPIO18/17,
TinyUSB, one USB CDC-ACM interface, and GPIO7/8 outputs.

The 4 MB host setting is deliberately conservative because Espressif records
early board samples with 4 MB rather than the current 8 MB. It will also run on
an 8 MB board while leaving capacity unused. An 8 MB profile variant may be
added when a feature, such as larger firmware staging, benefits from it and the
actual board revision is known.

The transport coordinator, TCP service, byte-link leases, stream pump,
maintenance broker, metadata repository, firmware manager, optional entity
adapters, and indicator services remain common with UZG-01. The new work is
concentrated in the S3 host integration, H2 backend, ZBOSS protocol handler,
native USB endpoint, catalog adapter, and possibly RF coexistence controller.

The initial profile does not include firmware catalog/update, ZBOSS metadata,
radio-controlled indicators, a transport button, or managed USB Direct.
Those omissions are capability declarations, not placeholders silently
pretending that the features exist.

#### Self-assessment

| Profile element | Assessment | Evidence or blocker |
| --- | --- | --- |
| S3 host, flash, and PSRAM | Structurally valid | Official board uses ESP32-S3 with 2 MB quad PSRAM; 4 MB is safe for both early 4 MB and current 8 MB samples |
| Wi-Fi TCP transport | Architecturally valid | Uses ordinary ESPHome Wi-Fi and the common protocol-neutral TCP transport |
| S3↔H2 UART wiring | Verified hardware | Official schematic and Espressif board defaults use S3 GPIO17 RX and GPIO18 TX |
| H2 reset and boot control | Verified hardware | Official board defaults use S3 GPIO7 reset and GPIO8 boot |
| H2 ZBOSS NCP firmware | Candidate, not board-ready | Upstream supports H2, but its default UART TX GPIO22 does not match this board's H2 GPIO24 TX path |
| Transparent ZBOSS over TCP | Plausible, unverified | Requires a board-specific H2 build and the common stream implementation; no ZBOSS parsing is needed |
| S3 USB CDC endpoint | ESPHome primitive verified | ESPHome supports TinyUSB CDC-ACM on ESP32-S3 and the primitive configuration validates |
| USB Bridged end to end | Plausible, unverified | Requires the new gateway CDC endpoint adapter and hardware throughput/reset testing |
| Managed USB Direct | Rejected for the initial profile | S3 cannot disconnect H2 USB1 or prevent a second valid NCP client from taking the H2 firmware's shared stream |
| ZHA compatibility | Experimental external evidence only | Requires a third-party ZHA/zigpy-ZBOSS path; it does not change gateway behavior |
| Firmware installation | Not provided | Requires Espressif updater implementation, image validation, staging policy, recovery, and hardware tests |
| Metadata and diagnostics | Not provided | Requires a ZBOSS handler and evidence definitions rather than reuse of TI/ZNP parsing |
| Indicators and mode button | Not declared | No verified populated, gateway-controlled product indicators or transport button are assumed |
| RF coexistence | Not provided | The board exposes PTA wiring, but the candidate ZBOSS image does not implement the board's external coexistence path; Wi-Fi/Zigbee reliability requires bench evidence |

**Assessment:** The agreed profile and ownership model survives this second
device without adding a product name or vendor condition to the common gateway.
The profile is nevertheless not implementable as a supported gateway from
published binaries alone. The first blocking artifact is an H2 coordinator
image built for GPIO24 TX, GPIO23 RX, and 115200 baud. After that, TCP is the
smallest credible hardware milestone, followed by USB Bridged.

The current third-party NCP services USB and UART in parallel, selecting the
interface that last supplied a valid NCP request. That behavior is useful for
recovery but cannot satisfy the gateway's exclusive-owner contract. A future
managed USB Direct mode requires H2-side arbitration that allows the S3 to
grant and revoke the direct host before reclaiming the UART. Until that exists
and is verified, USB1 remains outside the transport selector.

### 13.4 RF coexistence boundary

The official board wires external packet-traffic arbitration between the S3
Wi-Fi radio and H2 802.15.4 radio. Espressif's official Thread images can
enable that facility on both devices. No equivalent support was found in the
reviewed third-party H2 ZBOSS NCP firmware.

This creates two different initial qualification paths:

- an Ethernet profile may keep S3 Wi-Fi disabled and does not need Wi-Fi/H2
  RF arbitration;
- a Wi-Fi profile remains experimental until the H2 NCP and S3 integration
  implement compatible PTA behavior or hardware tests establish an acceptable
  documented limitation.

RF coexistence belongs to the host/radio/profile integration layer. It is not
a ZBOSS parser feature and must not be hidden inside a transport.

### 13.5 Firmware catalog and staging implications

The current firmware catalog model must normalize more than chip, role, and
version. At minimum, a candidate image carries:

- radio target and board-specific pin/build variant;
- Zigbee NCP application type;
- NCP protocol and source lineage;
- Coordinator or Router role when applicable;
- image format, flash address or segment map, and bootloader method;
- application link settings;
- size, version, digest, and freshness validators.

The reviewed H2 merged image is approximately 720 KiB. It fits the current
768 KiB UZG-01 staging size with little expansion room, but this is evidence
for one image, not a universal guarantee. Staging capacity and accepted image
formats must therefore be profile/backend constraints. Starting an actual
download still invalidates the staged identity before replacement, and a new
identity is committed only after complete digest verification.

An H2 updater can reuse the portable catalog, target-selection, conditional
fetch, staging, digest, progress, maintenance, and recovery orchestration.
The backend-specific portion implements Espressif ROM download, image parsing,
erase/write/verify, reset, and boot recovery. It must remain cooperative during
the slower real flash operation.

### 13.6 API validation result

The board validates the layered design while requiring these refinements:

| Existing design boundary | Result |
| --- | --- |
| Curated profiles plus advanced assembly | Valid; the board composes host, network, radio, transport, and feature packages |
| Backend-specific radio package | Valid; H2 wiring and recovery do not fit the TI schema |
| External-NCP product scope | Valid; the H2 is usable only with a compatible NCP image |
| One universal set of radio-link parameters | Rejected; the profile supplies the complete endpoint beneath the gateway and the selected backend declares and validates its requirements |
| One serial ownership contract | Valid across the TI and H2 NCP profiles |
| Private host-protocol selection | Valid; ZBOSS is hidden behind the H2 NCP package |
| Transparent transports | Valid for every compatible NCP byte stream |
| USB bridge UART | Generalized to a duplex byte endpoint so native USB CDC can be reused |
| Dedicated H2 USB | Does not derive USB Direct; a complete bidirectional ownership strategy is still required |
| Radio backend and portable firmware manager | Valid; image format, flash layout, and staging limits move into descriptors |
| Optional Home Assistant UI | Valid without change |
| Official Espressif RCP firmware | Outside scope; it requires a protocol stack on the S3 |

The common public operations should remain semantic: select an available
transport, reset or recover a radio, refresh metadata, inspect network state,
refresh a catalog, select a compatible image, and install it. UART, ZNP,
ZBOSS, ROM-loader, and SDK-specific operations stay behind the selected package
and C++ backend.

### 13.7 Qualification gates

Before this becomes a supported Zigbee profile:

1. Confirm the exact board revision, host flash size, and presence of the
   W5500 daughterboard on the test unit.
2. Obtain or contribute an upstream H2 NCP build using GPIO24 TX/GPIO23 RX.
3. Verify H2 USB1 recovery and S3 UART communication independently.
4. Verify TCP and S3 USB CDC bridging under sustained bidirectional traffic.
5. Verify that an attached USB1 host cannot corrupt normal TCP or USB Bridged
   operation under the profile's documented recovery-only restriction.
6. Verify reset, boot-mode entry, ROM synchronization, erase, write, digest or
   readback verification, boot, and interrupted-update recovery.
7. Verify ZBOSS metadata queries and passive observations without disturbing
   the active controller.
8. Form, persist, restart, and rejoin a Zigbee network with every controller
   adapter for which the profile claims support; initially Zigbee2MQTT ZBOSS.
9. Qualify Wi-Fi coexistence or document Ethernet-only support until PTA is
   implemented.
10. Confirm catalog filtering and staging limits against every published image
    offered by the profile.
11. Run sustained controller traffic and firmware-management scenarios with
    each claimed host feature, including Bluetooth proxy, before supporting
    the combined profile.

Thread remains a separate future project. It must not be presented as an extra
firmware option in the Zigbee NCP profile or pre-designed through unused
host-stack interfaces.

## 14. Support and compatibility model

**AGREED:** End-user support is claimed for a complete device profile, not for
every theoretical combination of individually working building blocks.

Possible levels:

- **Supported profile** — known product, maintained configuration, required
  software checks passing, and all applicable hardware scenarios verified.
- **Experimental profile** — known product with maintained configuration but
  incomplete hardware validation.
- **Available building block** — host adapter, radio backend, NCP
  host-protocol backend, transport, or feature implementation is available for
  advanced assembly and has its own documented compatibility and test
  evidence. Availability does not imply that every combination is supported.
- **Custom configuration** — user-supplied resources and combinations outside
  maintained profiles.

Compilation and host tests are software evidence. They do not replace physical
validation and do not independently promote a profile to supported status.

Reusing a verified building block in a new combination reduces the required
work, but the new complete profile remains experimental until its applicable
hardware interactions are verified.

## 15. Highest-level open decisions

### Decision 1: Product model

Choose the public structure of the project:

1. **Curated profiles only** — simplest support boundary, but poor extensibility.
2. **Generic assembly only** — maximum flexibility, but every user must
   understand host, radio, wiring, and capability details.
3. **Hybrid** — curated profiles for known products, plus an advanced assembly
   API using the same implementation.

**AGREED:** Use the hybrid model. Known products have simple, curated profiles.
Advanced users may compose supported hosts, radios, and features explicitly.
Both paths use exactly the same packages, component schemas, and C++
implementations; a device profile only fixes the choices and physical wiring.

### Decision 2: Support claims

**AGREED:** Reserve the end-user **supported** claim for complete,
hardware-verified device profiles. Distinguish those profiles from experimental
profiles, available building blocks, and custom configurations. Successful
compilation alone must not imply hardware support, and a supported backend must
not imply that every host/radio combination is supported.

### Decision 3: Capability authority

**AGREED:** The installed NCP firmware is authoritative for its runtime
protocol capabilities. The radio assembly selected by a profile privately
includes its compatible host-protocol handlers, runtime probing selects a
compatible handler, and unknown firmware retains transparent serial service
while protocol-dependent local functions become unavailable.

**AGREED:** Compile-time configurations declare resources and implementations,
not resulting capabilities. The component schema derives capabilities,
validates feature requirements, and rejects contradictions. User choices may
enable or disable optional features only within what the assembled product can
provide.

**AGREED:** Capability describes compile-time potential, compatibility
describes whether the detected radio, firmware, or image matches an
implementation, and availability describes whether current runtime state
permits the operation. Runtime actions require all applicable layers.

**AGREED:** Resource and implementation presence is expressed by the typed
component schema and selected backend facets, not by a second public descriptor
language. Code generation derives the typed internal capability set and
validates each feature's prerequisites according to Section 7.2.

### Decision 4: Public configuration surface

**AGREED:** A known product is selected by including its profile package, not
by setting a product-name field in one universal package. The profile composes
reusable resource, backend, and feature packages and provides the
backend-specific physical configuration to the selected package. The component
and packages own any required stable IDs; private component-owned resources do
not need public YAML IDs.

An advanced configuration includes the same reusable packages directly and
wires them to the same generic gateway component contract. Product names must
not appear in shared C++ behavior or capability rules.

**AGREED:** Reusable packages are parameterized templates. Their namespaced
`vars` objects contain chip, pins, modes, inversion, addresses, and other
physical facts. They pass complete native ESPHome mappings where possible and
add only project-specific logic or validation. Resources exclusively owned by
the Zigbee assembly, including its UARTs, are configured directly through the
Zigbee component and are not separately declared and attached by ID.

**AGREED:** A profile selects one backend-specific `zigbee_radio` package and
provides the parameters required by that backend. Backend variable schemas may
differ. The radio package privately composes the backend and compatible NCP
host-protocol handlers. Their common contract begins at the C++ interfaces,
not in a universal YAML hardware descriptor or separate user-facing protocol
selection.

**AGREED:** Device configuration owns identity, credentials, user policy
substitutions, profile selection, and selected UI exposure. A profile owns
fixed hardware assembly and its default functional features. Advanced
configurations replace the profile with the same reusable host, network,
storage, radio, gateway, control, indicator, integration, catalog, and feature
packages. Entity declarations and presentation overrides remain exclusively in
optional `ui/` packages.

### Decision 5: Compatibility contract

**AGREED:** The stable public configuration contract consists of:

- documented profile and reusable-package include paths;
- documented namespaced `vars` structures;
- fixed, non-overridable semantic IDs required for package composition,
  including `zigbee_radio`, `zigbee_gateway`, `zigbee_firmware_catalog`,
  `zigbee_firmware_manager`, `zigbee_firmware_staging`, and `gateway_time`;
- documented YAML actions and the types and values exposed to YAML lambdas;
- catalog-lambda inputs and the normalized catalog result contract;
- user-visible entity semantics and default metadata supplied by UI packages;
- persisted behavior across upgrades, guarded by explicit schema versions.

Fixed semantic IDs are stable integration points, not user-configurable
values.

Unexposed C++ class and file organization, private NCP handlers, worker/task
implementation, internal state-machine names, implementation-only IDs, and
the exact byte layouts of NVS records and staging headers remain internal.
Persisted binary layouts may change when their schema is deliberately migrated
or invalidated without changing the documented persistence behavior.

**AGREED:** External-client identity is not a gateway compatibility contract.
Compatibility inside the component concerns the NCP firmware and protocol.
Zigbee2MQTT/ZHA interoperability may be recorded as test evidence and user
guidance without creating different runtime capabilities or code paths.

### Decision 6: Runtime service model

**AGREED:** Adopt the services, exclusive authorities, data/control-plane
separation, lease classes, and admission rules in Section 8. Descriptive
service names remain non-binding until their C++ interfaces are designed.

### Decision 7: C++ service contracts

**AGREED:** Adopt the dependency direction, typed runtime state, lease-bound
radio stream, backend facets, protocol registry, metadata evidence model,
transport lifecycle, shared maintenance broker, and optional presentation
adapters in Section 9.

This is an internal engineering decision delegated by the product owner.
Implementation evidence may refine names and method signatures without
reopening the product architecture, provided the agreed authorities,
dependencies, and observable behavior remain unchanged.

### Decision 8: UZG-01 reference mapping

**AGREED:** Treat the scenarios and preservation rules in Section 10 as the
behavioral acceptance baseline for architectural extraction. Structural
refactoring does not authorize user-visible changes.

This detailed technical mapping is maintained as an engineering responsibility.
Questions are escalated to the product owner only when they affect observable
behavior, supported hardware, configuration, compatibility, or recovery.

### Decision 9: External-NCP scope and first additional profile

**AGREED:** Limit the current product architecture to external Zigbee NCPs.
Do not introduce stack-topology selection, RCP/native-radio services, or a
Zigbee/Thread host-stack backend. The ESPHome host remains available for
gateway functions and unrelated ESPHome capabilities such as BLE proxy.

**AGREED:** Treat Zigbee2MQTT, ZHA, and every compatible application
identically as an opaque external client over the protocol-neutral gateway
transport. Do not add a controller selector or controller-specific runtime
state.

**AGREED:** Use the Espressif ESP Thread Border Router board first as an
experimental ESP32-H2 ZBOSS NCP Coordinator profile. Official H2 RCP firmware
is outside the current project. The profile and qualification gates are
defined in Section 13.

## 16. Planned design sequence

Design should proceed in this order:

1. Agree on the product and support model.
2. Define configuration audiences and the public YAML surface.
3. Define the capability model and validation rules.
4. Define major runtime services and ownership boundaries. **AGREED**
5. Define C++ interfaces between policy, platform, transport, and radio code.
   **AGREED**
6. Map current UZG-01 behavior into those interfaces. **AGREED**
7. Define the first additional device profile and its required backends.
   **EXPERIMENTAL PROFILE DEFINED**
8. Define persistence, catalog, and firmware-update portability.
   **PARTIALLY DEFINED**
9. Define testing levels and hardware qualification. **PROFILE GATES DEFINED**
10. Produce an implementation and migration plan.

Lower-level class diagrams, method contracts, and file layouts should be added
only after their higher-level decisions are agreed.
