from esphome import automation
import esphome.codegen as cg
from esphome.components import binary_sensor, button, sensor, text_sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_MODEL,
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_VOLTAGE,
    ENTITY_CATEGORY_CONFIG,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_VOLT,
)

AUTO_LOAD = ["binary_sensor", "button", "network", "sensor", "text_sensor"]

wisafe2_ns = cg.esphome_ns.namespace("wisafe2")
WiSafe2Component = wisafe2_ns.class_("WiSafe2Component", cg.Component)
WiSafe2InitButton = wisafe2_ns.class_("WiSafe2InitButton", button.Button)
WiSafe2SpiButton = wisafe2_ns.class_("WiSafe2SpiButton", button.Button)
WiSafe2RmStatusButton = wisafe2_ns.class_("WiSafe2RmStatusButton", button.Button)
WiSafe2SidMapButton = wisafe2_ns.class_("WiSafe2SidMapButton", button.Button)
WiSafe2DiscoverButton = wisafe2_ns.class_("WiSafe2DiscoverButton", button.Button)
WiSafe2RediscoverButton = wisafe2_ns.class_("WiSafe2RediscoverButton", button.Button)
WiSafe2RegistryLogButton = wisafe2_ns.class_("WiSafe2RegistryLogButton", button.Button)
QueryRemoteStatusAction = wisafe2_ns.class_("QueryRemoteStatusAction", automation.Action)
QueryIdentityAction = wisafe2_ns.class_("QueryIdentityAction", automation.Action)
QueryDeviceAction = wisafe2_ns.class_("QueryDeviceAction", automation.Action)

CONF_DEVICE_ID = "device_id"
CONF_SID = "sid"
CONF_STATUS = "status"
CONF_LAST_REPLY = "last_reply"
CONF_RM_SID = "rm_sid"
CONF_RM_BATTERY = "rm_battery"
CONF_RM_RSSI = "rm_rssi"
CONF_RM_RADIO_FAULTS = "rm_radio_faults"
CONF_RM_FIRMWARE = "rm_firmware"
CONF_RM_DEVICE_ID = "rm_device_id"
CONF_RM_PAIRED = "rm_paired"
CONF_INIT_BUTTON = "init_button"
CONF_SPI_BUTTON = "spi_button"
CONF_RM_STATUS_BUTTON = "rm_status_button"
CONF_SID_MAP_BUTTON = "sid_map_button"
CONF_DISCOVER_BUTTON = "discover_button"
CONF_REDISCOVER_BUTTON = "rediscover_button"
CONF_REGISTRY_LOG_BUTTON = "registry_log_button"
CONF_AUTO_DISCOVERY = "auto_discovery"
CONF_REMOTE_STATUS_INTERVAL = "remote_status_interval"
CONF_BATTERY_INTERVAL = "battery_interval"

# WST-630, wie Arduino-Firmware und NikNakk; im Paket 11 03.
DEFAULT_MODEL = 0x0311


def device_id_value(value):
    """Geräte-ID in Anzeigereihenfolge, wie das Gateway sie zeigt, etwa 14B8A5."""
    value = cv.string_strict(value).strip().upper()
    if len(value) != 6 or any(c not in "0123456789ABCDEF" for c in value):
        raise cv.Invalid("Geräte-ID braucht 6 Hex-Ziffern, etwa 14B8A5")
    return value


def bridge_device_id(value):
    value = device_id_value(value)
    if value == "000000":
        raise cv.Invalid("000000 steht für ein RM ohne gelernte Geräte-ID")
    if value == "030201":
        raise cv.Invalid("030201 benutzen Gateways für ihr eigenes RM")
    return value


# Die periodische Planung vergleicht Zeitpunkte mit Vorzeichen; 20 Tage bleiben sicher unter 2^31 ms.
interval = cv.All(
    cv.positive_time_period_milliseconds,
    cv.Range(min=cv.TimePeriod(minutes=10), max=cv.TimePeriod(days=20)),
)


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(WiSafe2Component),
        cv.Optional(CONF_DEVICE_ID): bridge_device_id,
        cv.Optional(CONF_MODEL, default=DEFAULT_MODEL): cv.int_range(min=1, max=0xFFFF),
        cv.Optional(CONF_AUTO_DISCOVERY, default=False): cv.boolean,
        cv.Optional(CONF_REMOTE_STATUS_INTERVAL, default="19h"): interval,
        cv.Optional(CONF_BATTERY_INTERVAL, default="24h"): interval,
        cv.Optional(CONF_STATUS): text_sensor.text_sensor_schema(),
        cv.Optional(CONF_LAST_REPLY): text_sensor.text_sensor_schema(
            icon="mdi:message-reply-text-outline",
        ),
        cv.Optional(CONF_RM_SID): sensor.sensor_schema(
            accuracy_decimals=0,
            icon="mdi:identifier",
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_RM_BATTERY): sensor.sensor_schema(
            unit_of_measurement=UNIT_VOLT,
            accuracy_decimals=2,
            device_class=DEVICE_CLASS_VOLTAGE,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_RM_RSSI): sensor.sensor_schema(
            accuracy_decimals=0,
            icon="mdi:signal",
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_RM_RADIO_FAULTS): sensor.sensor_schema(
            accuracy_decimals=0,
            icon="mdi:alert-circle-outline",
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_RM_FIRMWARE): text_sensor.text_sensor_schema(
            icon="mdi:chip",
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_RM_DEVICE_ID): text_sensor.text_sensor_schema(
            icon="mdi:identifier",
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_RM_PAIRED): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_CONNECTIVITY,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_INIT_BUTTON): button.button_schema(
            WiSafe2InitButton,
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon="mdi:radio-tower",
        ),
        cv.Optional(CONF_SPI_BUTTON): button.button_schema(
            WiSafe2SpiButton,
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon="mdi:restart",
        ),
        cv.Optional(CONF_RM_STATUS_BUTTON): button.button_schema(
            WiSafe2RmStatusButton,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:information-outline",
        ),
        cv.Optional(CONF_SID_MAP_BUTTON): button.button_schema(
            WiSafe2SidMapButton,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:lan",
        ),
        cv.Optional(CONF_DISCOVER_BUTTON): button.button_schema(
            WiSafe2DiscoverButton,
            icon="mdi:lan-connect",
        ),
        cv.Optional(CONF_REDISCOVER_BUTTON): button.button_schema(
            WiSafe2RediscoverButton,
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon="mdi:lan-pending",
        ),
        cv.Optional(CONF_REGISTRY_LOG_BUTTON): button.button_schema(
            WiSafe2RegistryLogButton,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:format-list-bulleted",
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    if CONF_DEVICE_ID in config:
        display = bytes.fromhex(config[CONF_DEVICE_ID])
        cg.add(var.set_identity(display[2], display[1], display[0], config[CONF_MODEL]))
    cg.add(var.set_auto_discovery(config[CONF_AUTO_DISCOVERY]))
    cg.add(var.set_remote_status_interval(config[CONF_REMOTE_STATUS_INTERVAL]))
    cg.add(var.set_battery_interval(config[CONF_BATTERY_INTERVAL]))
    # nodes.bin liegt im LittleFS von arduino-pico.
    cg.add_library("LittleFS", None)

    text_sensors = {
        CONF_STATUS: var.set_status_sensor,
        CONF_LAST_REPLY: var.set_last_reply_sensor,
        CONF_RM_FIRMWARE: var.set_rm_firmware_sensor,
        CONF_RM_DEVICE_ID: var.set_rm_device_id_sensor,
    }
    for key, setter in text_sensors.items():
        if key in config:
            sens = await text_sensor.new_text_sensor(config[key])
            cg.add(setter(sens))

    sensors = {
        CONF_RM_SID: var.set_rm_sid_sensor,
        CONF_RM_BATTERY: var.set_rm_battery_sensor,
        CONF_RM_RSSI: var.set_rm_rssi_sensor,
        CONF_RM_RADIO_FAULTS: var.set_rm_radio_faults_sensor,
    }
    for key, setter in sensors.items():
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(setter(sens))

    if CONF_RM_PAIRED in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_RM_PAIRED])
        cg.add(var.set_rm_paired_sensor(sens))

    for key in (
        CONF_INIT_BUTTON,
        CONF_RM_STATUS_BUTTON,
        CONF_SID_MAP_BUTTON,
        CONF_DISCOVER_BUTTON,
        CONF_REDISCOVER_BUTTON,
        CONF_REGISTRY_LOG_BUTTON,
    ):
        if key in config:
            btn = await button.new_button(config[key])
            cg.add(btn.set_parent(var))
    if CONF_SPI_BUTTON in config:
        await button.new_button(config[CONF_SPI_BUTTON])


SID_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(WiSafe2Component),
        cv.Required(CONF_SID): cv.templatable(cv.int_range(min=0, max=63)),
    }
)

DEVICE_QUERY_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(WiSafe2Component),
            cv.Optional(CONF_SID): cv.templatable(cv.int_range(min=0, max=63)),
            cv.Optional(CONF_DEVICE_ID): cv.templatable(device_id_value),
            cv.Optional(CONF_MODEL): cv.templatable(cv.int_range(min=0, max=0xFFFF)),
        }
    ),
    cv.has_at_least_one_key(CONF_SID, CONF_DEVICE_ID),
)


async def sid_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    sid = await cg.templatable(config[CONF_SID], args, cg.uint8)
    cg.add(var.set_sid(sid))
    return var


automation.register_action(
    "wisafe2.query_remote_status", QueryRemoteStatusAction, SID_SCHEMA, synchronous=True
)(sid_action_to_code)
automation.register_action(
    "wisafe2.query_identity", QueryIdentityAction, SID_SCHEMA, synchronous=True
)(sid_action_to_code)


def register_device_query(name, request_code):
    async def device_query_to_code(config, action_id, template_arg, args):
        var = cg.new_Pvariable(action_id, template_arg, request_code)
        await cg.register_parented(var, config[CONF_ID])
        if CONF_SID in config:
            cg.add(var.set_sid(await cg.templatable(config[CONF_SID], args, cg.uint8)))
        if CONF_DEVICE_ID in config:
            cg.add(var.set_device_id(await cg.templatable(config[CONF_DEVICE_ID], args, cg.std_string)))
        if CONF_MODEL in config:
            cg.add(var.set_model(await cg.templatable(config[CONF_MODEL], args, cg.uint16)))
        return var

    automation.register_action(name, QueryDeviceAction, DEVICE_QUERY_SCHEMA, synchronous=True)(
        device_query_to_code
    )


register_device_query("wisafe2.query_battery", 0xC7)
register_device_query("wisafe2.query_fault_details", 0x72)
register_device_query("wisafe2.query_fault_events", 0xC5)
