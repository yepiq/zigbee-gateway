from esphome import pins
import esphome.codegen as cg
from esphome.components import binary_sensor, button, sensor, text_sensor, uart
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_RESET_PIN,
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_DATA_SIZE,
    DEVICE_CLASS_RESTART,
    DEVICE_CLASS_SIGNAL_STRENGTH,
    ENTITY_CATEGORY_CONFIG,
    ENTITY_CATEGORY_DIAGNOSTIC,
    ICON_RESTART,
    STATE_CLASS_MEASUREMENT,
    UNIT_BYTES,
    UNIT_DECIBEL_MILLIWATT,
)

CODEOWNERS = ["@yepiq"]
DEPENDENCIES = ["network", "uart"]
AUTO_LOAD = ["binary_sensor", "button", "sensor", "socket", "text_sensor"]

CONF_SOCKET_CONNECTED = "socket_connected"
CONF_CONNECTION_COUNT = "connection_count"
CONF_BSL_PIN = "bsl_pin"
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
CONF_SELF_IEEE = "self_ieee"
CONF_PARENT_IEEE = "parent_ieee"
CONF_ROLE = "role"
CONF_EXTENDED_PAN_ID = "extended_pan_id"
CONF_HARDWARE = "hardware"

CONF_RESTART = "restart"
CONF_ENTER_BSL = "enter_bsl"
CONF_ROUTER_REJOIN = "router_rejoin"

CONF_RESET_TIMEOUT = "reset_timeout"
CONF_ZNP_START_TIMEOUT = "znp_start_timeout"
CONF_ZNP_BYTE_TIMEOUT = "znp_byte_timeout"
CONF_ZNP_OVERALL_TIMEOUT = "znp_overall_timeout"
CONF_ZNP_POST_SEND_DELAY = "znp_post_send_delay"
CONF_ZNP_RETRIES = "znp_retries"
CONF_BSL_ACK_TIMEOUT = "bsl_ack_timeout"
CONF_BSL_HEADER_TIMEOUT = "bsl_header_timeout"
CONF_BSL_PAYLOAD_TIMEOUT = "bsl_payload_timeout"
CONF_BSL_SYNC_GAP = "bsl_sync_gap"

CONF_NV_BASE_CC26X2 = "nv_base_cc26x2"
CONF_NV_SIZE_CC26X2 = "nv_size_cc26x2"
CONF_NV_BASE_CC26X2X7 = "nv_base_cc26x2x7"
CONF_NV_SIZE_CC26X2X7 = "nv_size_cc26x2x7"

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
RouterRejoinButton = zigbee_gateway_ns.class_(
    "RouterRejoinButton", button.Button, cg.Parented.template(ZigbeeGatewayComponent)
)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ZigbeeGatewayComponent),
            cv.Required(CONF_RESET_PIN): pins.gpio_output_pin_schema,
            cv.Required(CONF_BSL_PIN): pins.gpio_output_pin_schema,
            cv.Required(CONF_SOCKET_CONNECTED): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_CONNECTIVITY,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Required(CONF_CONNECTION_COUNT): sensor.sensor_schema(
                accuracy_decimals=0,
                state_class=STATE_CLASS_MEASUREMENT,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                icon="mdi:counter",
            ),
            cv.Optional(CONF_IP_ADDRESS): cv.use_id(text_sensor.TextSensor),
            cv.Optional(CONF_TCP_PORT, default=6638): cv.port,
            cv.Optional(
                CONF_PENDING_SOCKET_TIMEOUT, default="30s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_PARKED_SOCKET_TIMEOUT, default="10min"
            ): cv.positive_time_period_milliseconds,
            cv.Required(CONF_FLASH_SIZE): sensor.sensor_schema(
                device_class=DEVICE_CLASS_DATA_SIZE,
                unit_of_measurement=UNIT_BYTES,
                accuracy_decimals=0,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:memory",
            ),
            cv.Required(CONF_TX_POWER): sensor.sensor_schema(
                device_class=DEVICE_CLASS_SIGNAL_STRENGTH,
                unit_of_measurement=UNIT_DECIBEL_MILLIWATT,
                accuracy_decimals=0,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:access-point",
            ),
            cv.Required(CONF_PAN_ID): sensor.sensor_schema(
                accuracy_decimals=0,
                icon="mdi:identifier",
            ),
            cv.Required(CONF_CHANNEL): sensor.sensor_schema(
                accuracy_decimals=0,
                icon="mdi:access-point-network",
            ),
            cv.Required(CONF_ON_NETWORK): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_CONNECTIVITY,
            ),
            cv.Required(CONF_FIRMWARE): text_sensor.text_sensor_schema(
                icon="mdi:source-repository",
            ),
            cv.Required(CONF_STACK): text_sensor.text_sensor_schema(icon="mdi:zigbee"),
            cv.Required(CONF_SELF_IEEE): text_sensor.text_sensor_schema(
                icon="mdi:identifier",
            ),
            cv.Required(CONF_PARENT_IEEE): text_sensor.text_sensor_schema(
                icon="mdi:access-point-network",
            ),
            cv.Required(CONF_ROLE): text_sensor.text_sensor_schema(
                icon="mdi:access-point",
            ),
            cv.Required(CONF_EXTENDED_PAN_ID): text_sensor.text_sensor_schema(
                icon="mdi:identifier",
            ),
            cv.Required(CONF_HARDWARE): text_sensor.text_sensor_schema(
                icon="mdi:chip",
            ),
            cv.Optional(CONF_RESTART): button.button_schema(
                RadioRestartButton,
                device_class=DEVICE_CLASS_RESTART,
                entity_category=ENTITY_CATEGORY_CONFIG,
                icon=ICON_RESTART,
            ),
            cv.Optional(CONF_ENTER_BSL): button.button_schema(
                RadioBslButton,
                entity_category=ENTITY_CATEGORY_CONFIG,
                icon="mdi:cellphone-arrow-down",
            ),
            cv.Optional(CONF_ROUTER_REJOIN): button.button_schema(
                RouterRejoinButton,
                entity_category=ENTITY_CATEGORY_CONFIG,
                icon="mdi:access-point-plus",
            ),
            cv.Optional(CONF_RESET_TIMEOUT, default="5s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_ZNP_START_TIMEOUT, default="100ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_ZNP_BYTE_TIMEOUT, default="10ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_ZNP_OVERALL_TIMEOUT, default="500ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_ZNP_POST_SEND_DELAY, default="10ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_ZNP_RETRIES, default=2): cv.int_range(min=1, max=10),
            cv.Optional(CONF_BSL_ACK_TIMEOUT, default="50ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_BSL_HEADER_TIMEOUT, default="50ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_BSL_PAYLOAD_TIMEOUT, default="50ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_BSL_SYNC_GAP, default="5ms"): cv.positive_time_period_milliseconds,
            # Koenkk Z-Stack NVOCMP layout hints. These remain configurable
            # because alternative radio firmware builds can place NV elsewhere.
            cv.Optional(CONF_NV_BASE_CC26X2, default=0x00050000): cv.uint32_t,
            cv.Optional(CONF_NV_SIZE_CC26X2, default=0x00006000): cv.uint32_t,
            cv.Optional(CONF_NV_BASE_CC26X2X7, default=0x000A6000): cv.uint32_t,
            cv.Optional(CONF_NV_SIZE_CC26X2X7, default=0x00008000): cv.uint32_t,
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
    socket_connected = await binary_sensor.new_binary_sensor(
        config[CONF_SOCKET_CONNECTED]
    )
    cg.add(var.set_socket_connected_binary_sensor(socket_connected))
    connection_count = await sensor.new_sensor(config[CONF_CONNECTION_COUNT])
    cg.add(var.set_connection_count_sensor(connection_count))
    if CONF_IP_ADDRESS in config:
        cg.add(
            var.set_ip_address_text_sensor(
                await cg.get_variable(config[CONF_IP_ADDRESS])
            )
        )

    sensor_setters = {
        CONF_FLASH_SIZE: "set_flash_size_sensor",
        CONF_TX_POWER: "set_tx_power_sensor",
        CONF_PAN_ID: "set_pan_id_sensor",
        CONF_CHANNEL: "set_channel_sensor",
    }
    for key, setter in sensor_setters.items():
        sens = await sensor.new_sensor(config[key])
        cg.add(getattr(var, setter)(sens))

    on_network = await binary_sensor.new_binary_sensor(config[CONF_ON_NETWORK])
    cg.add(var.set_on_network_binary_sensor(on_network))

    text_sensor_setters = {
        CONF_FIRMWARE: "set_firmware_text_sensor",
        CONF_STACK: "set_stack_text_sensor",
        CONF_SELF_IEEE: "set_self_ieee_text_sensor",
        CONF_PARENT_IEEE: "set_parent_ieee_text_sensor",
        CONF_ROLE: "set_role_text_sensor",
        CONF_EXTENDED_PAN_ID: "set_ext_pan_id_text_sensor",
        CONF_HARDWARE: "set_hardware_text_sensor",
    }
    for key, setter in text_sensor_setters.items():
        sens = await text_sensor.new_text_sensor(config[key])
        cg.add(getattr(var, setter)(sens))

    for key in (CONF_RESTART, CONF_ENTER_BSL, CONF_ROUTER_REJOIN):
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
    cg.add(var.set_reset_timeout(config[CONF_RESET_TIMEOUT].total_milliseconds))
    cg.add(
        var.set_znp_start_timeout(
            config[CONF_ZNP_START_TIMEOUT].total_milliseconds
        )
    )
    cg.add(
        var.set_znp_byte_timeout(config[CONF_ZNP_BYTE_TIMEOUT].total_milliseconds)
    )
    cg.add(
        var.set_znp_overall_timeout(
            config[CONF_ZNP_OVERALL_TIMEOUT].total_milliseconds
        )
    )
    cg.add(
        var.set_znp_post_send_delay(
            config[CONF_ZNP_POST_SEND_DELAY].total_milliseconds
        )
    )
    cg.add(var.set_znp_retries(config[CONF_ZNP_RETRIES]))
    cg.add(
        var.set_bsl_ack_timeout(config[CONF_BSL_ACK_TIMEOUT].total_milliseconds)
    )
    cg.add(
        var.set_bsl_header_timeout(
            config[CONF_BSL_HEADER_TIMEOUT].total_milliseconds
        )
    )
    cg.add(
        var.set_bsl_payload_timeout(
            config[CONF_BSL_PAYLOAD_TIMEOUT].total_milliseconds
        )
    )
    cg.add(var.set_bsl_sync_gap(config[CONF_BSL_SYNC_GAP].total_milliseconds))
    cg.add(
        var.set_nv_cc26x2(
            config[CONF_NV_BASE_CC26X2], config[CONF_NV_SIZE_CC26X2]
        )
    )
    cg.add(
        var.set_nv_cc26x2x7(
            config[CONF_NV_BASE_CC26X2X7], config[CONF_NV_SIZE_CC26X2X7]
        )
    )

    # UART debug callbacks are a passive tap. Reads and writes are exclusively
    # gated by the component's ZigbeeSerialInterface.
    cg.add_define("USE_UART_DEBUGGER")
