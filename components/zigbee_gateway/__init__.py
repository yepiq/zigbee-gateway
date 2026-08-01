from esphome import pins
import esphome.codegen as cg
from esphome.components import (
    binary_sensor,
    button,
    esp32,
    select,
    sensor,
    text_sensor,
    uart,
)
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_RESET_PIN,
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_DATA_SIZE,
    DEVICE_CLASS_PROBLEM,
    DEVICE_CLASS_RESTART,
    DEVICE_CLASS_SIGNAL_STRENGTH,
    DEVICE_CLASS_UPDATE,
    ENTITY_CATEGORY_CONFIG,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_BYTES,
    UNIT_DECIBEL_MILLIWATT,
    UNIT_PERCENT,
)
from esphome.core import CORE

CODEOWNERS = ["@yepiq"]
DEPENDENCIES = ["network", "uart"]
AUTO_LOAD = [
    "binary_sensor",
    "button",
    "json",
    "select",
    "sensor",
    "socket",
    "text_sensor",
]

CONF_SOCKET_CONNECTED = "socket_connected"
CONF_CONNECTION_COUNT = "connection_count"
CONF_TRANSPORT_STATE = "transport_state"
CONF_PENDING_SOCKET = "pending_socket"
CONF_PARKED_SOCKET = "parked_socket"
CONF_LAST_TRANSPORT_EVENT = "last_transport_event"
CONF_REJECTED_CONNECTIONS = "rejected_connections"
CONF_PENDING_TIMEOUTS = "pending_timeouts"
CONF_MAINTENANCE_SESSIONS = "maintenance_sessions"
CONF_RECOVERY_RESETS = "recovery_resets"
CONF_BSL_PIN = "bsl_pin"
CONF_MODE_PIN = "mode_pin"
CONF_MODE_LED_PIN = "mode_led_pin"
CONF_USB_UART_ID = "usb_uart_id"
CONF_SERIAL_TRANSPORT = "serial_transport"
CONF_IP_ADDRESS = "ip_address"
CONF_TCP_PORT = "tcp_port"
CONF_PENDING_SOCKET_TIMEOUT = "pending_socket_timeout"
CONF_PARKED_SOCKET_TIMEOUT = "parked_socket_timeout"

CONF_FLASH_SIZE = "flash_size"
CONF_TX_POWER = "tx_power"
CONF_PAN_ID = "pan_id"
CONF_CHANNEL = "channel"
CONF_ON_NETWORK = "on_network"
CONF_FIRMWARE = "firmware"
CONF_STACK = "stack"
CONF_FACTORY_IEEE = "factory_ieee"
CONF_SELF_IEEE = "self_ieee"
CONF_PARENT_IEEE = "parent_ieee"
CONF_ROLE = "role"
CONF_EXTENDED_PAN_ID = "extended_pan_id"
CONF_HARDWARE = "hardware"
CONF_METADATA_STATUS = "metadata_status"
CONF_NETWORK_INFORMATION_STATUS = "network_information_status"

CONF_RESTART = "restart"
CONF_ENTER_BSL = "enter_bsl"
CONF_ROUTER_FACTORY_RESET = "router_factory_reset"
CONF_REFRESH_METADATA = "refresh_metadata"

CONF_FIRMWARE_UPDATE = "firmware_update"
CONF_MANIFEST_URL = "manifest_url"
CONF_CHIP = "chip"
CONF_PREFERRED_ROLE = "preferred_role"
CONF_STARTUP_TIMEOUT = "startup_timeout"
CONF_HTTP_TIMEOUT = "http_timeout"
CONF_MAX_MANIFEST_SIZE = "max_manifest_size"
CONF_BOOTLOADER_BACKDOOR_DIO = "bootloader_backdoor_dio"
CONF_BOOTLOADER_BACKDOOR_ACTIVE_HIGH = "bootloader_backdoor_active_high"
CONF_TARGET_FIRMWARE_ROLE = "target_firmware_role"
CONF_TARGET_FIRMWARE_VERSION = "target_firmware_version"
CONF_REFRESH_FIRMWARE_CATALOG = "refresh_firmware_catalog"
CONF_INSTALL_FIRMWARE = "install_firmware"
CONF_INVALIDATE_STAGED_FIRMWARE = "invalidate_staged_firmware"
CONF_FIRMWARE_CATALOG_STATUS = "firmware_catalog_status"
CONF_FIRMWARE_UPDATE_STATUS = "firmware_update_status"
CONF_FIRMWARE_UPDATE_PROGRESS = "firmware_update_progress"
CONF_FIRMWARE_CATALOG_CHECK_FAILED = "firmware_catalog_check_failed"

zigbee_gateway_ns = cg.esphome_ns.namespace("zigbee_gateway")
ZigbeeGatewayComponent = zigbee_gateway_ns.class_(
    "ZigbeeGatewayComponent", cg.Component, uart.UARTDevice
)
RadioRestartButton = zigbee_gateway_ns.class_(
    "RadioRestartButton", button.Button, cg.Parented.template(ZigbeeGatewayComponent)
)
RadioBslButton = zigbee_gateway_ns.class_(
    "RadioBslButton", button.Button, cg.Parented.template(ZigbeeGatewayComponent)
)
RouterFactoryResetButton = zigbee_gateway_ns.class_(
    "RouterFactoryResetButton",
    button.Button,
    cg.Parented.template(ZigbeeGatewayComponent),
)
RadioMetadataRefreshButton = zigbee_gateway_ns.class_(
    "RadioMetadataRefreshButton",
    button.Button,
    cg.Parented.template(ZigbeeGatewayComponent),
)
ZigbeeTransportSelect = zigbee_gateway_ns.class_(
    "ZigbeeTransportSelect",
    select.Select,
    cg.Component,
    cg.Parented.template(ZigbeeGatewayComponent),
)
ZigbeeFirmwareManager = zigbee_gateway_ns.class_(
    "ZigbeeFirmwareManager", cg.Component
)
FirmwareRoleSelect = zigbee_gateway_ns.class_(
    "FirmwareRoleSelect",
    select.Select,
    cg.Component,
    cg.Parented.template(ZigbeeFirmwareManager),
)
FirmwareVersionSelect = zigbee_gateway_ns.class_(
    "FirmwareVersionSelect",
    select.Select,
    cg.Component,
    cg.Parented.template(ZigbeeFirmwareManager),
)
FirmwareCatalogRefreshButton = zigbee_gateway_ns.class_(
    "FirmwareCatalogRefreshButton",
    button.Button,
    cg.Parented.template(ZigbeeFirmwareManager),
)
FirmwareInstallButton = zigbee_gateway_ns.class_(
    "FirmwareInstallButton",
    button.Button,
    cg.Parented.template(ZigbeeFirmwareManager),
)
StagedFirmwareInvalidateButton = zigbee_gateway_ns.class_(
    "StagedFirmwareInvalidateButton",
    button.Button,
    cg.Parented.template(ZigbeeFirmwareManager),
)

FIRMWARE_UPDATE_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ZigbeeFirmwareManager),
        cv.Required(CONF_MANIFEST_URL): cv.url,
        cv.Required(CONF_CHIP): cv.string_strict,
        cv.Optional(CONF_PREFERRED_ROLE, default="coordinator"): cv.string_strict,
        cv.Required(CONF_STARTUP_TIMEOUT): cv.positive_time_period_milliseconds,
        cv.Required(CONF_HTTP_TIMEOUT): cv.positive_time_period_milliseconds,
        cv.Required(CONF_MAX_MANIFEST_SIZE): cv.validate_bytes,
        cv.Required(CONF_BOOTLOADER_BACKDOOR_DIO): cv.int_range(min=0, max=31),
        cv.Required(CONF_BOOTLOADER_BACKDOOR_ACTIVE_HIGH): cv.boolean,
        cv.Optional(CONF_TARGET_FIRMWARE_ROLE): select.select_schema(
            FirmwareRoleSelect,
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon="mdi:family-tree",
        ),
        cv.Optional(CONF_TARGET_FIRMWARE_VERSION): select.select_schema(
            FirmwareVersionSelect,
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon="mdi:chip",
        ),
        cv.Optional(CONF_REFRESH_FIRMWARE_CATALOG): button.button_schema(
            FirmwareCatalogRefreshButton,
            device_class=DEVICE_CLASS_UPDATE,
            entity_category=ENTITY_CATEGORY_CONFIG,
        ),
        cv.Optional(CONF_INSTALL_FIRMWARE): button.button_schema(
            FirmwareInstallButton,
            device_class=DEVICE_CLASS_UPDATE,
            entity_category=ENTITY_CATEGORY_CONFIG,
        ),
        cv.Optional(CONF_INVALIDATE_STAGED_FIRMWARE): button.button_schema(
            StagedFirmwareInvalidateButton,
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon="mdi:database-remove",
        ),
        cv.Optional(CONF_FIRMWARE_CATALOG_STATUS): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:list-status",
        ),
        cv.Optional(CONF_FIRMWARE_UPDATE_STATUS): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:update",
        ),
        cv.Optional(CONF_FIRMWARE_UPDATE_PROGRESS): sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:progress-check",
        ),
        cv.Optional(
            CONF_FIRMWARE_CATALOG_CHECK_FAILED
        ): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_PROBLEM,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
).extend(cv.COMPONENT_SCHEMA)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ZigbeeGatewayComponent),
            cv.Required(CONF_RESET_PIN): pins.gpio_output_pin_schema,
            cv.Required(CONF_BSL_PIN): pins.gpio_output_pin_schema,
            cv.Required(CONF_MODE_PIN): pins.gpio_output_pin_schema,
            cv.Required(CONF_MODE_LED_PIN): pins.gpio_output_pin_schema,
            cv.Required(CONF_USB_UART_ID): cv.use_id(uart.UARTComponent),
            cv.Optional(CONF_SERIAL_TRANSPORT): select.select_schema(
                ZigbeeTransportSelect,
                entity_category=ENTITY_CATEGORY_CONFIG,
                icon="mdi:swap-horizontal",
            ),
            cv.Optional(CONF_SOCKET_CONNECTED): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_CONNECTIVITY,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_CONNECTION_COUNT): sensor.sensor_schema(
                accuracy_decimals=0,
                state_class=STATE_CLASS_MEASUREMENT,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                icon="mdi:counter",
            ),
            cv.Optional(CONF_TRANSPORT_STATE): text_sensor.text_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                icon="mdi:serial-port",
            ),
            cv.Optional(CONF_PENDING_SOCKET): binary_sensor.binary_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                icon="mdi:timer-sand",
            ),
            cv.Optional(CONF_PARKED_SOCKET): binary_sensor.binary_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                icon="mdi:pause-circle-outline",
            ),
            cv.Optional(CONF_LAST_TRANSPORT_EVENT): text_sensor.text_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                icon="mdi:timeline-text-outline",
            ),
            cv.Optional(CONF_REJECTED_CONNECTIONS): sensor.sensor_schema(
                accuracy_decimals=0,
                state_class=STATE_CLASS_TOTAL_INCREASING,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                icon="mdi:lan-disconnect",
            ),
            cv.Optional(CONF_PENDING_TIMEOUTS): sensor.sensor_schema(
                accuracy_decimals=0,
                state_class=STATE_CLASS_TOTAL_INCREASING,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                icon="mdi:timer-alert-outline",
            ),
            cv.Optional(CONF_MAINTENANCE_SESSIONS): sensor.sensor_schema(
                accuracy_decimals=0,
                state_class=STATE_CLASS_TOTAL_INCREASING,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                icon="mdi:tools",
            ),
            cv.Optional(CONF_RECOVERY_RESETS): sensor.sensor_schema(
                accuracy_decimals=0,
                state_class=STATE_CLASS_TOTAL_INCREASING,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                icon="mdi:backup-restore",
            ),
            cv.Optional(CONF_IP_ADDRESS): cv.use_id(text_sensor.TextSensor),
            cv.Optional(CONF_TCP_PORT, default=6638): cv.port,
            cv.Optional(
                CONF_PENDING_SOCKET_TIMEOUT, default="30s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_PARKED_SOCKET_TIMEOUT, default="10min"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_FLASH_SIZE): sensor.sensor_schema(
                device_class=DEVICE_CLASS_DATA_SIZE,
                unit_of_measurement=UNIT_BYTES,
                accuracy_decimals=0,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_TX_POWER): sensor.sensor_schema(
                device_class=DEVICE_CLASS_SIGNAL_STRENGTH,
                unit_of_measurement=UNIT_DECIBEL_MILLIWATT,
                accuracy_decimals=0,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_PAN_ID): sensor.sensor_schema(
                accuracy_decimals=0,
                icon="mdi:identifier",
            ),
            cv.Optional(CONF_CHANNEL): sensor.sensor_schema(
                accuracy_decimals=0,
                icon="mdi:access-point-network",
            ),
            cv.Optional(CONF_ON_NETWORK): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_CONNECTIVITY,
            ),
            cv.Optional(CONF_FIRMWARE): text_sensor.text_sensor_schema(
                icon="mdi:source-repository",
            ),
            cv.Optional(CONF_STACK): text_sensor.text_sensor_schema(icon="mdi:zigbee"),
            cv.Optional(CONF_FACTORY_IEEE): text_sensor.text_sensor_schema(
                icon="mdi:identifier",
            ),
            cv.Optional(CONF_SELF_IEEE): text_sensor.text_sensor_schema(
                icon="mdi:identifier",
            ),
            cv.Optional(CONF_PARENT_IEEE): text_sensor.text_sensor_schema(
                icon="mdi:access-point-network",
            ),
            cv.Optional(CONF_ROLE): text_sensor.text_sensor_schema(
                icon="mdi:access-point",
            ),
            cv.Optional(CONF_EXTENDED_PAN_ID): text_sensor.text_sensor_schema(
                icon="mdi:identifier",
            ),
            cv.Optional(CONF_HARDWARE): text_sensor.text_sensor_schema(
                icon="mdi:chip",
            ),
            cv.Optional(CONF_METADATA_STATUS): text_sensor.text_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                icon="mdi:database-check",
            ),
            cv.Optional(
                CONF_NETWORK_INFORMATION_STATUS
            ): text_sensor.text_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                icon="mdi:database-eye",
            ),
            cv.Optional(CONF_RESTART): button.button_schema(
                RadioRestartButton,
                device_class=DEVICE_CLASS_RESTART,
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
            cv.Optional(CONF_ENTER_BSL): button.button_schema(
                RadioBslButton,
                entity_category=ENTITY_CATEGORY_CONFIG,
                icon="mdi:cellphone-arrow-down",
            ),
            cv.Optional(CONF_ROUTER_FACTORY_RESET): button.button_schema(
                RouterFactoryResetButton,
                entity_category=ENTITY_CATEGORY_CONFIG,
                icon="mdi:delete-restore",
            ),
            cv.Optional(CONF_REFRESH_METADATA): button.button_schema(
                RadioMetadataRefreshButton,
                device_class=DEVICE_CLASS_UPDATE,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_FIRMWARE_UPDATE): FIRMWARE_UPDATE_SCHEMA,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA),
    cv.only_on_esp32,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    cg.add(var.set_reset_pin(await cg.gpio_pin_expression(config[CONF_RESET_PIN])))
    cg.add(var.set_bsl_pin(await cg.gpio_pin_expression(config[CONF_BSL_PIN])))
    cg.add(var.set_mode_pin(await cg.gpio_pin_expression(config[CONF_MODE_PIN])))
    cg.add(
        var.set_mode_led_pin(
            await cg.gpio_pin_expression(config[CONF_MODE_LED_PIN])
        )
    )
    cg.add(var.set_usb_uart(await cg.get_variable(config[CONF_USB_UART_ID])))

    if CONF_SERIAL_TRANSPORT in config:
        transport = await select.new_select(
            config[CONF_SERIAL_TRANSPORT],
            options=["TCP", "USB Bridged", "USB Direct"],
        )
        await cg.register_component(transport, config[CONF_SERIAL_TRANSPORT])
        await cg.register_parented(transport, config[CONF_ID])

    binary_sensor_setters = {
        CONF_SOCKET_CONNECTED: "set_socket_connected_binary_sensor",
        CONF_PENDING_SOCKET: "set_pending_socket_binary_sensor",
        CONF_PARKED_SOCKET: "set_parked_socket_binary_sensor",
        CONF_ON_NETWORK: "set_on_network_binary_sensor",
    }
    for key, setter in binary_sensor_setters.items():
        if key not in config:
            continue
        sens = await binary_sensor.new_binary_sensor(config[key])
        cg.add(getattr(var, setter)(sens))

    if CONF_IP_ADDRESS in config:
        cg.add(
            var.set_ip_address_text_sensor(
                await cg.get_variable(config[CONF_IP_ADDRESS])
            )
        )

    sensor_setters = {
        CONF_CONNECTION_COUNT: "set_connection_count_sensor",
        CONF_FLASH_SIZE: "set_flash_size_sensor",
        CONF_TX_POWER: "set_tx_power_sensor",
        CONF_PAN_ID: "set_pan_id_sensor",
        CONF_CHANNEL: "set_channel_sensor",
        CONF_REJECTED_CONNECTIONS: "set_rejected_connections_sensor",
        CONF_PENDING_TIMEOUTS: "set_pending_timeouts_sensor",
        CONF_MAINTENANCE_SESSIONS: "set_maintenance_sessions_sensor",
        CONF_RECOVERY_RESETS: "set_recovery_resets_sensor",
    }
    for key, setter in sensor_setters.items():
        if key not in config:
            continue
        sens = await sensor.new_sensor(config[key])
        cg.add(getattr(var, setter)(sens))

    text_sensor_setters = {
        CONF_FIRMWARE: "set_firmware_text_sensor",
        CONF_STACK: "set_stack_text_sensor",
        CONF_FACTORY_IEEE: "set_factory_ieee_text_sensor",
        CONF_SELF_IEEE: "set_self_ieee_text_sensor",
        CONF_PARENT_IEEE: "set_parent_ieee_text_sensor",
        CONF_ROLE: "set_role_text_sensor",
        CONF_EXTENDED_PAN_ID: "set_ext_pan_id_text_sensor",
        CONF_HARDWARE: "set_hardware_text_sensor",
        CONF_METADATA_STATUS: "set_metadata_status_text_sensor",
        CONF_NETWORK_INFORMATION_STATUS: "set_network_information_status_text_sensor",
        CONF_TRANSPORT_STATE: "set_transport_state_text_sensor",
        CONF_LAST_TRANSPORT_EVENT: "set_last_transport_event_text_sensor",
    }
    for key, setter in text_sensor_setters.items():
        if key not in config:
            continue
        sens = await text_sensor.new_text_sensor(config[key])
        cg.add(getattr(var, setter)(sens))

    for key in (
        CONF_RESTART,
        CONF_ENTER_BSL,
        CONF_ROUTER_FACTORY_RESET,
        CONF_REFRESH_METADATA,
    ):
        if key not in config:
            continue
        btn = await button.new_button(config[key])
        await cg.register_parented(btn, config[CONF_ID])

    cg.add(var.set_tcp_port(config[CONF_TCP_PORT]))
    cg.add(
        var.set_pending_socket_timeout(
            config[CONF_PENDING_SOCKET_TIMEOUT].total_milliseconds
        )
    )
    cg.add(
        var.set_parked_socket_timeout(
            config[CONF_PARKED_SOCKET_TIMEOUT].total_milliseconds
        )
    )

    if CONF_FIRMWARE_UPDATE in config:
        firmware_config = config[CONF_FIRMWARE_UPDATE]
        if CORE.using_arduino:
            raise cv.Invalid("firmware_update requires the ESP-IDF framework")

        firmware = cg.new_Pvariable(firmware_config[CONF_ID])
        await cg.register_component(firmware, firmware_config)
        cg.add(var.set_firmware_manager(firmware))
        cg.add(firmware.set_gateway(var))
        cg.add(firmware.set_manifest_url(firmware_config[CONF_MANIFEST_URL]))
        cg.add(firmware.set_chip(firmware_config[CONF_CHIP]))
        cg.add(
            firmware.set_preferred_role(
                firmware_config[CONF_PREFERRED_ROLE]
            )
        )
        cg.add(
            firmware.set_startup_timeout(
                firmware_config[CONF_STARTUP_TIMEOUT].total_milliseconds
            )
        )
        cg.add(
            firmware.set_http_timeout(
                firmware_config[CONF_HTTP_TIMEOUT].total_milliseconds
            )
        )
        cg.add(
            firmware.set_max_manifest_size(
                firmware_config[CONF_MAX_MANIFEST_SIZE]
            )
        )
        cg.add(
            firmware.set_bootloader_backdoor_dio(
                firmware_config[CONF_BOOTLOADER_BACKDOOR_DIO]
            )
        )
        cg.add(
            firmware.set_bootloader_backdoor_active_high(
                firmware_config[CONF_BOOTLOADER_BACKDOOR_ACTIVE_HIGH]
            )
        )

        firmware_selects = {
            CONF_TARGET_FIRMWARE_ROLE: (
                FirmwareRoleSelect,
                "set_role_select",
            ),
            CONF_TARGET_FIRMWARE_VERSION: (
                FirmwareVersionSelect,
                "set_firmware_select",
            ),
        }
        for key, (_, setter) in firmware_selects.items():
            if key not in firmware_config:
                continue
            entity = await select.new_select(
                firmware_config[key],
                options=["Catalog unavailable"],
            )
            await cg.register_component(entity, firmware_config[key])
            await cg.register_parented(entity, firmware_config[CONF_ID])
            cg.add(getattr(firmware, setter)(entity))

        firmware_buttons = (
            CONF_REFRESH_FIRMWARE_CATALOG,
            CONF_INSTALL_FIRMWARE,
            CONF_INVALIDATE_STAGED_FIRMWARE,
        )
        for key in firmware_buttons:
            if key not in firmware_config:
                continue
            entity = await button.new_button(firmware_config[key])
            await cg.register_parented(entity, firmware_config[CONF_ID])

        firmware_text_sensors = {
            CONF_FIRMWARE_CATALOG_STATUS: "set_status_text_sensor",
            CONF_FIRMWARE_UPDATE_STATUS: "set_flash_status_text_sensor",
        }
        for key, setter in firmware_text_sensors.items():
            if key not in firmware_config:
                continue
            entity = await text_sensor.new_text_sensor(firmware_config[key])
            cg.add(getattr(firmware, setter)(entity))

        if CONF_FIRMWARE_UPDATE_PROGRESS in firmware_config:
            entity = await sensor.new_sensor(
                firmware_config[CONF_FIRMWARE_UPDATE_PROGRESS]
            )
            cg.add(firmware.set_flash_progress_sensor(entity))

        if CONF_FIRMWARE_CATALOG_CHECK_FAILED in firmware_config:
            entity = await binary_sensor.new_binary_sensor(
                firmware_config[CONF_FIRMWARE_CATALOG_CHECK_FAILED]
            )
            cg.add(firmware.set_catalog_check_failed_binary_sensor(entity))

        esp32.include_builtin_idf_component("esp_http_client")
        esp32.include_builtin_idf_component("esp_partition")
        esp32.add_partition("zigbee_fw", 0x40, 0x00, 0x0C0000)
        esp32.add_idf_sdkconfig_option(
            "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE", True
        )

    # UART debug callbacks are a passive tap. Reads and writes are exclusively
    # gated by the component's ZigbeeSerialInterface.
    cg.add_define("USE_UART_DEBUGGER")
