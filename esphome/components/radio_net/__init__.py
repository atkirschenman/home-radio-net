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
    CONF_DEVICE_ID,
    CONF_ID,
    CONF_NAME,
    CONF_TYPE,
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_SIGNAL_STRENGTH,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_DECIBEL_MILLIWATT,
)

from .rnet_tables import NODE_TYPE_CONSTANTS, NODE_TYPES, UPLINK_MESSAGES

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
CONF_FIELD_ENTITIES = "field_entities"
CONF_SIGNAL_ENTITY = "signal_entity"
CONF_ONLINE_ENTITY = "online_entity"
NANOMETRES_PER_MILLIMETRE = 1_000_000

DISPLAY_NAMES = {
    "wind_mean": "Wind speed",
    "wind_direction_at_gust": "Wind direction at gust",
    "rain_tips": "Rain",
    "relative_humidity": "Humidity",
    "interval_seconds": "Report interval",
    "downlink_rssi": "Downlink signal",
}

radio_net_namespace = cg.esphome_ns.namespace("radio_net")
radio_net_component = radio_net_namespace.class_(
    "radio_net_component", cg.Component, uart.UARTDevice
)
raw_type = radio_net_namespace.enum("raw_type", is_class=True)
RAW_TYPES = {
    "u8": raw_type.u8,
    "u16": raw_type.u16,
    "u32": raw_type.u32,
    "i8": raw_type.i8,
    "i16": raw_type.i16,
    "i32": raw_type.i32,
    "bits": raw_type.u8,
}


def master_key_bytes(value):
    text = cv.string_strict(value).replace(" ", "").replace(":", "")
    if len(text) != 2 * MASTER_KEY_LENGTH_BYTES:
        raise cv.Invalid(f"master_key must be {MASTER_KEY_LENGTH_BYTES} bytes as 32 hex digits")
    try:
        return list(bytes.fromhex(text))
    except ValueError as error:
        raise cv.Invalid("master_key must be hex digits") from error


def messages_for_node_type(node_type):
    return [message for message in UPLINK_MESSAGES if node_type in message["node_types"]]


def fields_for_node_type(node_type):
    fields = {}
    for message in messages_for_node_type(node_type):
        for field in message["fields"]:
            fields.setdefault(field["name"], field)
    return fields


def display_name(field_name):
    return DISPLAY_NAMES.get(field_name, field_name.replace("_", " ").capitalize())


def entity_config(node, name):
    config = {CONF_NAME: name if CONF_DEVICE_ID in node else f"{node[CONF_NAME]} {name}"}
    if CONF_DEVICE_ID in node:
        config[CONF_DEVICE_ID] = node[CONF_DEVICE_ID]
    return config


def field_sensor_schema(field):
    home_assistant = field["home_assistant"]
    options = {
        "unit_of_measurement": field["unit"],
        "accuracy_decimals": home_assistant.get("accuracy_decimals", 0),
    }
    for key in ("device_class", "state_class", "entity_category"):
        if key in home_assistant:
            options[key] = home_assistant[key]
    return sensor.sensor_schema(**options)


def add_node_entities(node):
    node[CONF_FIELD_ENTITIES] = {
        field_name: field_sensor_schema(field)(entity_config(node, display_name(field_name)))
        for field_name, field in fields_for_node_type(node[CONF_TYPE]).items()
    }
    node[CONF_SIGNAL_ENTITY] = sensor.sensor_schema(
        unit_of_measurement=UNIT_DECIBEL_MILLIWATT,
        accuracy_decimals=0,
        device_class=DEVICE_CLASS_SIGNAL_STRENGTH,
        state_class=STATE_CLASS_MEASUREMENT,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    )(entity_config(node, "Signal strength"))
    node[CONF_ONLINE_ENTITY] = binary_sensor.binary_sensor_schema(
        device_class=DEVICE_CLASS_CONNECTIVITY,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    )(entity_config(node, "Online"))
    return node


def field_scale(field, node_type):
    if "convert" in field:
        return NODE_TYPE_CONSTANTS[node_type][field["convert"]] / NANOMETRES_PER_MILLIMETRE
    return field.get("scale", 1)


NODE_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.Required(CONF_NODE_ID): cv.hex_uint16_t,
            cv.Required(CONF_TYPE): cv.one_of(*NODE_TYPES, lower=True),
            cv.Required(CONF_NAME): cv.string_strict,
            cv.Optional(CONF_DEVICE_ID): cv.string_strict,
        }
    ),
    add_node_entities,
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
    for node_index, node in enumerate(config[CONF_NODES]):
        await add_node_to_code(component, node_index, node)

    if CONF_FRAMES_RECEIVED in config:
        counter = await sensor.new_sensor(config[CONF_FRAMES_RECEIVED])
        cg.add(component.set_frames_received_sensor(counter))
    if CONF_FRAMES_REJECTED in config:
        counter = await sensor.new_sensor(config[CONF_FRAMES_REJECTED])
        cg.add(component.set_frames_rejected_sensor(counter))
    if CONF_BRIDGE_ONLINE in config:
        online = await binary_sensor.new_binary_sensor(config[CONF_BRIDGE_ONLINE])
        cg.add(component.set_bridge_online_sensor(online))


async def add_node_to_code(component, node_index, node):
    node_type = node[CONF_TYPE]
    cg.add(component.add_node(node[CONF_NODE_ID], NODE_TYPES[node_type], node[CONF_NAME]))
    field_sensors = {}
    for field_name, entity in node[CONF_FIELD_ENTITIES].items():
        field_sensors[field_name] = await sensor.new_sensor(entity)
    for message in messages_for_node_type(node_type):
        for field in message["fields"]:
            invalid = field.get("invalid")
            cg.add(
                component.add_field_sensor(
                    node_index,
                    message["id"],
                    field["offset"],
                    RAW_TYPES[field["type"]],
                    field_scale(field, node_type),
                    invalid is not None,
                    invalid if invalid is not None else 0,
                    field_sensors[field["name"]],
                )
            )
    signal = await sensor.new_sensor(node[CONF_SIGNAL_ENTITY])
    cg.add(component.set_node_signal_sensor(node_index, signal))
    online = await binary_sensor.new_binary_sensor(node[CONF_ONLINE_ENTITY])
    cg.add(component.set_node_online_sensor(node_index, online))
