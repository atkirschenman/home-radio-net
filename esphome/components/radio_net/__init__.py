# acronym  meaning
# rnet     home radio network (this protocol)
#
# radio_net: the home radio network gateway (architecture N-10, ADR-0003).
# The radio is a weather board running the radio_bridge image, on a uart: bus.
from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, sensor, uart
from esphome.const import (
    CONF_ID,
    CONF_NAME,
    CONF_TYPE,
    DEVICE_CLASS_CONNECTIVITY,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_TOTAL_INCREASING,
)

from .rnet_tables import NODE_TYPES

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor", "binary_sensor"]

REPOSITORY_ROOT = Path(__file__).resolve().parents[3]
BRIDGE_BAUD_RATE = 115200
MASTER_KEY_LENGTH_BYTES = 16

CONF_MASTER_KEY = "master_key"
CONF_NODES = "nodes"
CONF_NODE_ID = "node_id"
CONF_FRAMES_RECEIVED = "frames_received"
CONF_FRAMES_REJECTED = "frames_rejected"
CONF_BRIDGE_ONLINE = "bridge_online"

radio_net_namespace = cg.esphome_ns.namespace("radio_net")
radio_net_component = radio_net_namespace.class_(
    "radio_net_component", cg.Component, uart.UARTDevice
)


def master_key_bytes(value):
    text = cv.string_strict(value).replace(" ", "").replace(":", "")
    if len(text) != 2 * MASTER_KEY_LENGTH_BYTES:
        raise cv.Invalid(f"master_key must be {MASTER_KEY_LENGTH_BYTES} bytes as 32 hex digits")
    try:
        return list(bytes.fromhex(text))
    except ValueError as error:
        raise cv.Invalid("master_key must be hex digits") from error


NODE_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_NODE_ID): cv.hex_uint16_t,
        cv.Required(CONF_TYPE): cv.one_of(*NODE_TYPES, lower=True),
        cv.Required(CONF_NAME): cv.string_strict,
    }
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(radio_net_component),
            cv.Required(CONF_MASTER_KEY): master_key_bytes,
            cv.Optional(CONF_NODES, default=[]): cv.ensure_list(NODE_SCHEMA),
            cv.Optional(CONF_FRAMES_RECEIVED): sensor.sensor_schema(
                accuracy_decimals=0,
                state_class=STATE_CLASS_TOTAL_INCREASING,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                icon="mdi:radio-tower",
            ),
            cv.Optional(CONF_FRAMES_REJECTED): sensor.sensor_schema(
                accuracy_decimals=0,
                state_class=STATE_CLASS_TOTAL_INCREASING,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                icon="mdi:shield-alert",
            ),
            cv.Optional(CONF_BRIDGE_ONLINE): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_CONNECTIVITY,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "radio_net", baud_rate=BRIDGE_BAUD_RATE, require_rx=True, require_tx=True
)


async def to_code(config):
    component = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(component, config)
    await uart.register_uart_device(component, config)

    cg.add_library("home-radio-net", None, f"symlink://{REPOSITORY_ROOT}")
    cg.add(component.set_master_key(cg.ArrayInitializer(*config[CONF_MASTER_KEY])))
    for node in config[CONF_NODES]:
        cg.add(component.add_node(node[CONF_NODE_ID], NODE_TYPES[node[CONF_TYPE]], node[CONF_NAME]))

    if CONF_FRAMES_RECEIVED in config:
        counter = await sensor.new_sensor(config[CONF_FRAMES_RECEIVED])
        cg.add(component.set_frames_received_sensor(counter))
    if CONF_FRAMES_REJECTED in config:
        counter = await sensor.new_sensor(config[CONF_FRAMES_REJECTED])
        cg.add(component.set_frames_rejected_sensor(counter))
    if CONF_BRIDGE_ONLINE in config:
        online = await binary_sensor.new_binary_sensor(config[CONF_BRIDGE_ONLINE])
        cg.add(component.set_bridge_online_sensor(online))
