from esphome import automation
import esphome.codegen as cg
from esphome.components import binary_sensor, button, event, sensor, switch, text_sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_DEVICE,
    CONF_DEVICES,
    CONF_ESPHOME,
    CONF_ID,
    CONF_MODEL,
    CONF_NAME,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_PROBLEM,
    DEVICE_CLASS_RUNNING,
    DEVICE_CLASS_SMOKE,
    DEVICE_CLASS_TIMESTAMP,
    DEVICE_CLASS_VOLTAGE,
    ENTITY_CATEGORY_CONFIG,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_MILLIVOLT,
    UNIT_VOLT,
)
from esphome.core import CORE
import esphome.final_validate as fv

AUTO_LOAD = ["binary_sensor", "button", "event", "network", "sensor", "switch", "text_sensor"]

wisafe2_ns = cg.esphome_ns.namespace("wisafe2")
WiSafe2Component = wisafe2_ns.class_("WiSafe2Component", cg.Component)
WiSafe2InitButton = wisafe2_ns.class_("WiSafe2InitButton", button.Button)
WiSafe2SpiButton = wisafe2_ns.class_("WiSafe2SpiButton", button.Button)
WiSafe2RmStatusButton = wisafe2_ns.class_("WiSafe2RmStatusButton", button.Button)
WiSafe2SidMapButton = wisafe2_ns.class_("WiSafe2SidMapButton", button.Button)
WiSafe2DiscoverButton = wisafe2_ns.class_("WiSafe2DiscoverButton", button.Button)
WiSafe2RediscoverButton = wisafe2_ns.class_("WiSafe2RediscoverButton", button.Button)
WiSafe2DetailsButton = wisafe2_ns.class_("WiSafe2DetailsButton", button.Button)
WiSafe2RegistryLogButton = wisafe2_ns.class_("WiSafe2RegistryLogButton", button.Button)
WiSafe2TemplateButton = wisafe2_ns.class_("WiSafe2TemplateButton", button.Button)
WiSafe2NetworkTestButton = wisafe2_ns.class_("WiSafe2NetworkTestButton", button.Button)
WiSafe2RefreshButton = wisafe2_ns.class_("WiSafe2RefreshButton", button.Button)
WiSafe2RawLogSwitch = wisafe2_ns.class_("WiSafe2RawLogSwitch", switch.Switch, cg.Component)
Detector = wisafe2_ns.class_("Detector")
QueryRemoteStatusAction = wisafe2_ns.class_("QueryRemoteStatusAction", automation.Action)
QueryIdentityAction = wisafe2_ns.class_("QueryIdentityAction", automation.Action)
QueryDeviceAction = wisafe2_ns.class_("QueryDeviceAction", automation.Action)
SendRawAction = wisafe2_ns.class_("SendRawAction", automation.Action)

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
CONF_DETAILS_BUTTON = "details_button"
CONF_REGISTRY_LOG_BUTTON = "registry_log_button"
CONF_AUTO_DISCOVERY = "auto_discovery"
CONF_REMOTE_STATUS_INTERVAL = "remote_status_interval"
CONF_BATTERY_INTERVAL = "battery_interval"
CONF_REMOTE_TIMEOUT = "remote_timeout"
CONF_DETECTORS = "detectors"
CONF_NETWORK_ALARM = "network_alarm"
CONF_NETWORK_SILENCED = "network_silenced"
CONF_DISCOVERY_RUNNING = "discovery_running"
CONF_NODE_COUNT = "node_count"
CONF_LAST_EVENT = "last_event"
CONF_UNASSIGNED = "unassigned"
CONF_NETWORK_EVENT = "network_event"
CONF_TEMPLATE_BUTTON = "template_button"
CONF_NETWORK_TEST_BUTTON = "network_test_button"
CONF_REMOTE_REQUESTS = "remote_requests"
CONF_RAW_LOG_SWITCH = "raw_log_switch"

# Wie event_type() in node_view.cpp.
EVENT_TYPES = ["alarm", "alarm_ende", "stummschaltung", "test", "fehlerbericht"]
# Das netzweite Ereignis meldet zusätzlich den Netztest der Brücke.
NETWORK_EVENT_TYPES = EVENT_TYPES + ["netztest"]

# WST-630, wie Arduino-Firmware und NikNakk; im Paket 11 03.
DEFAULT_MODEL = 0x0311


def device_id_value(value):
    """Geräte-ID in Anzeigereihenfolge, wie das Gateway sie zeigt, etwa 14B8A5."""
    value = cv.string_strict(value).strip().upper()
    if len(value) != 6 or any(c not in "0123456789ABCDEF" for c in value):
        raise cv.Invalid("Geräte-ID braucht 6 Hex-Ziffern, etwa 14B8A5")
    return value


MAX_RAW_PAYLOAD = 15


def raw_payload_value(value):
    """Befehl an das RM als Hex-Bytes ohne Rahmenende, etwa "D3 06 0F 02"."""
    value = cv.string_strict(value).strip().upper()
    digits = value.replace(" ", "")
    if (
        not digits
        or len(digits) % 2
        or len(digits) > 2 * MAX_RAW_PAYLOAD
        or any(c not in "0123456789ABCDEF" for c in digits)
        or any(len(part) % 2 for part in value.split())
    ):
        raise cv.Invalid(f"Rohbefehl braucht 1–{MAX_RAW_PAYLOAD} Hex-Bytes, etwa \"D3 06 0F 02\"")
    return value


def bridge_device_id(value):
    value = device_id_value(value)
    if value == "000000":
        raise cv.Invalid("000000 steht für ein RM ohne gelernte Geräte-ID")
    if value == "030201":
        raise cv.Invalid("030201 benutzen Gateways für ihr eigenes RM")
    return value


def detector_device_id(value):
    value = device_id_value(value)
    if value in ("000000", "030201"):
        raise cv.Invalid("000000 und 030201 kennzeichnen kein Gerät, hier die SID angeben")
    return value


# Die periodische Planung vergleicht Zeitpunkte mit Vorzeichen; 20 Tage bleiben sicher unter 2^31 ms.
interval = cv.All(
    cv.positive_time_period_milliseconds,
    cv.Range(min=cv.TimePeriod(minutes=10), max=cv.TimePeriod(days=20)),
)


# Entitäten je Melder: Schlüssel, Plattform, Schema, Name und Setter in Detector. Alle entstehen ohne Angabe;
# „false“ wählt eine ab.
DETECTOR_ENTITIES = {
    "alarm": (
        "binary_sensor",
        binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_SMOKE),
        "Alarm",
        "set_alarm_sensor",
    ),
    "fault": (
        "binary_sensor",
        binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_PROBLEM),
        "Fehler",
        "set_fault_sensor",
    ),
    "socket_problem": (
        "binary_sensor",
        binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_PROBLEM, icon="mdi:smoke-detector-off-outline"
        ),
        "Sockel-Problem",
        "set_socket_problem_sensor",
    ),
    "battery_low": (
        "binary_sensor",
        binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_BATTERY),
        "Batterie",
        "set_battery_low_sensor",
    ),
    "rm_battery_low": (
        "binary_sensor",
        binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_BATTERY, entity_category=ENTITY_CATEGORY_DIAGNOSTIC
        ),
        "RM-Batterie",
        "set_rm_battery_low_sensor",
    ),
    "online": (
        "binary_sensor",
        binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_CONNECTIVITY, entity_category=ENTITY_CATEGORY_DIAGNOSTIC
        ),
        "Im Netz",
        "set_online_sensor",
    ),
    "battery_voltage": (
        "sensor",
        sensor.sensor_schema(
            unit_of_measurement=UNIT_MILLIVOLT,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_VOLTAGE,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        "Batteriespannung",
        "set_battery_voltage_sensor",
    ),
    "rm_battery_voltage": (
        "sensor",
        sensor.sensor_schema(
            unit_of_measurement=UNIT_VOLT,
            accuracy_decimals=2,
            device_class=DEVICE_CLASS_VOLTAGE,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        "RM-Batteriespannung",
        "set_rm_battery_voltage_sensor",
    ),
    "rssi": (
        "sensor",
        sensor.sensor_schema(
            accuracy_decimals=0,
            icon="mdi:signal",
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        "Empfangspegel",
        "set_rssi_sensor",
    ),
    "last_seen": (
        "text_sensor",
        text_sensor.text_sensor_schema(
            device_class=DEVICE_CLASS_TIMESTAMP, entity_category=ENTITY_CATEGORY_DIAGNOSTIC
        ),
        "Zuletzt gesehen",
        "set_last_seen_sensor",
    ),
    "model": (
        "text_sensor",
        text_sensor.text_sensor_schema(
            icon="mdi:smoke-detector", entity_category=ENTITY_CATEGORY_DIAGNOSTIC
        ),
        "Modell",
        "set_model_sensor",
    ),
    "fault_details": (
        "text_sensor",
        text_sensor.text_sensor_schema(icon="mdi:alert-circle-outline"),
        "Fehlerdetails",
        "set_fault_details_sensor",
    ),
    "last_test": (
        "text_sensor",
        text_sensor.text_sensor_schema(icon="mdi:check-circle-outline"),
        "Letzter Test",
        "set_last_test_sensor",
    ),
    "refresh_button": (
        "button",
        button.button_schema(
            WiSafe2RefreshButton, icon="mdi:refresh", entity_category=ENTITY_CATEGORY_DIAGNOSTIC
        ),
        "Status aktualisieren",
        None,
    ),
    "events": (
        "event",
        event.event_schema(icon="mdi:bell-ring-outline"),
        "Ereignis",
        "set_event",
    ),
}


def entity_option(value):
    if value is None or value is True:
        return {}
    if value is False:
        return False
    if isinstance(value, dict):
        return value
    raise cv.Invalid("Erwartet false oder die Einstellungen der Entität")


DETECTOR_BASE_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(Detector),
        # Mit device darf name fehlen; dann gilt der Name des Geräts aus esphome: devices.
        cv.Optional(CONF_NAME): cv.string_strict,
        cv.Optional(CONF_SID): cv.int_range(min=0, max=63),
        cv.Optional(CONF_DEVICE_ID): detector_device_id,
        # Sub-Device aus esphome: devices; der Schlüssel device_id an den Entitäten bezeichnet es ebenfalls.
        cv.Optional(CONF_DEVICE): cv.validate_id_name,
        **{cv.Optional(key): entity_option for key in DETECTOR_ENTITIES},
    }
)


def detector_schema(value):
    """Ergänzt Name und Sub-Device jeder Entität vor deren Prüfung, damit die Duplikatprüfung von ESPHome greift."""
    config = cv.All(DETECTOR_BASE_SCHEMA, cv.has_at_least_one_key(CONF_SID, CONF_DEVICE_ID))(value)
    if CONF_NAME not in config and CONF_DEVICE not in config:
        raise cv.Invalid("name fehlt; ohne device ist name nötig", path=[CONF_NAME])
    for key, (_, schema, default_name, _) in DETECTOR_ENTITIES.items():
        raw = config.get(key, {})
        if raw is False:
            config[key] = False
            continue
        raw = dict(raw)
        if CONF_DEVICE in config:
            raw.setdefault(CONF_NAME, default_name)
            raw.setdefault(CONF_DEVICE_ID, config[CONF_DEVICE])
        else:
            raw.setdefault(CONF_NAME, f"{config[CONF_NAME]} {default_name}")
        with cv.prepend_path(key):
            config[key] = schema(raw)
    return config


def unique_detectors(detectors):
    for field in (CONF_SID, CONF_DEVICE_ID):
        seen = {}
        for index, detector in enumerate(detectors):
            if field not in detector:
                continue
            value = detector[field]
            if value in seen:
                raise cv.Invalid(
                    f"{field} {value} steht schon bei \"{seen[value]}\"",
                    path=[index, field],
                )
            seen[value] = detector.get(CONF_NAME, detector.get(CONF_DEVICE))
    return detectors


def device_names(full_config):
    return {
        device[CONF_ID].id: device[CONF_NAME]
        for device in full_config.get(CONF_ESPHOME, {}).get(CONF_DEVICES, [])
    }


def final_validate(config):
    names = device_names(fv.full_config.get())
    for index, detector in enumerate(config[CONF_DETECTORS]):
        device = detector.get(CONF_DEVICE)
        if device is not None and device not in names:
            raise cv.Invalid(
                f"Gerät {device} fehlt unter esphome: devices",
                path=[CONF_DETECTORS, index, CONF_DEVICE],
            )
    return config


FINAL_VALIDATE_SCHEMA = final_validate


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(WiSafe2Component),
        cv.Optional(CONF_DEVICE_ID): bridge_device_id,
        cv.Optional(CONF_MODEL, default=DEFAULT_MODEL): cv.int_range(min=1, max=0xFFFF),
        cv.Optional(CONF_AUTO_DISCOVERY, default=False): cv.boolean,
        cv.Optional(CONF_REMOTE_STATUS_INTERVAL, default="19h"): interval,
        cv.Optional(CONF_BATTERY_INTERVAL, default="24h"): interval,
        cv.Optional(CONF_REMOTE_TIMEOUT, default="240s"): cv.All(
            cv.positive_time_period_milliseconds,
            cv.Range(min=cv.TimePeriod(seconds=30), max=cv.TimePeriod(minutes=10)),
        ),
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
        cv.Optional(CONF_DETAILS_BUTTON): button.button_schema(
            WiSafe2DetailsButton,
            icon="mdi:lan-check",
        ),
        cv.Optional(CONF_REGISTRY_LOG_BUTTON): button.button_schema(
            WiSafe2RegistryLogButton,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:format-list-bulleted",
        ),
        cv.Optional(CONF_TEMPLATE_BUTTON): button.button_schema(
            WiSafe2TemplateButton,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:file-document-outline",
        ),
        # E1: Mit diesem Eintrag ist der Netztest zugeschaltet; ohne ihn sendet die Brücke nie 70.
        cv.Optional(CONF_NETWORK_TEST_BUTTON): button.button_schema(
            WiSafe2NetworkTestButton,
            icon="mdi:bullhorn-outline",
        ),
        cv.Optional(CONF_NETWORK_ALARM): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_SMOKE,
        ),
        cv.Optional(CONF_NETWORK_SILENCED): binary_sensor.binary_sensor_schema(
            icon="mdi:volume-off",
        ),
        cv.Optional(CONF_DISCOVERY_RUNNING): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_RUNNING,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_NODE_COUNT): sensor.sensor_schema(
            accuracy_decimals=0,
            icon="mdi:lan",
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_LAST_EVENT): text_sensor.text_sensor_schema(
            icon="mdi:history",
        ),
        cv.Optional(CONF_UNASSIGNED): text_sensor.text_sensor_schema(
            icon="mdi:help-network-outline",
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_NETWORK_EVENT): event.event_schema(
            icon="mdi:bell-ring-outline",
        ),
        # R1: Fernanfragen seit dem Start, die das RM mit 46 quittiert hat.
        cv.Optional(CONF_REMOTE_REQUESTS): sensor.sensor_schema(
            accuracy_decimals=0,
            icon="mdi:counter",
            state_class=STATE_CLASS_TOTAL_INCREASING,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        # Nach einem Neustart aus, solange restore_mode nichts anderes sagt.
        cv.Optional(CONF_RAW_LOG_SWITCH): switch.switch_schema(
            WiSafe2RawLogSwitch,
            icon="mdi:text-box-search-outline",
            entity_category=ENTITY_CATEGORY_CONFIG,
            default_restore_mode="ALWAYS_OFF",
        ).extend(cv.COMPONENT_SCHEMA),
        cv.Optional(CONF_DETECTORS, default=[]): cv.All(
            cv.ensure_list(detector_schema), unique_detectors
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


def network_test_needs_identity(config):
    if CONF_NETWORK_TEST_BUTTON in config and CONF_DEVICE_ID not in config:
        raise cv.Invalid(
            "Der Netztest sendet die eigene Identität; dafür device_id angeben",
            path=[CONF_NETWORK_TEST_BUTTON],
        )
    return config


CONFIG_SCHEMA = cv.All(CONFIG_SCHEMA, network_test_needs_identity)


async def detector_to_code(parent, config):
    name = config.get(CONF_NAME) or device_names(CORE.config)[config[CONF_DEVICE]]
    det = cg.new_Pvariable(config[CONF_ID], name, config.get(CONF_SID, 0xFF))
    if CONF_DEVICE_ID in config:
        display = bytes.fromhex(config[CONF_DEVICE_ID])
        cg.add(det.set_device_id(display[2], display[1], display[0]))
    for key, (platform, _, _, setter) in DETECTOR_ENTITIES.items():
        conf = config[key]
        if conf is False:
            continue
        if platform == "binary_sensor":
            obj = await binary_sensor.new_binary_sensor(conf)
        elif platform == "sensor":
            obj = await sensor.new_sensor(conf)
        elif platform == "text_sensor":
            obj = await text_sensor.new_text_sensor(conf)
        elif platform == "event":
            obj = await event.new_event(conf, event_types=EVENT_TYPES)
        else:
            obj = await button.new_button(conf)
            cg.add(obj.set_parent(parent))
            cg.add(obj.set_detector(det))
        if setter is not None:
            cg.add(getattr(det, setter)(obj))
    cg.add(parent.add_detector(det))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    if CONF_DEVICE_ID in config:
        display = bytes.fromhex(config[CONF_DEVICE_ID])
        cg.add(var.set_identity(display[2], display[1], display[0], config[CONF_MODEL]))
    cg.add(var.set_auto_discovery(config[CONF_AUTO_DISCOVERY]))
    cg.add(var.set_remote_status_interval(config[CONF_REMOTE_STATUS_INTERVAL]))
    cg.add(var.set_battery_interval(config[CONF_BATTERY_INTERVAL]))
    cg.add(var.set_remote_timeout(config[CONF_REMOTE_TIMEOUT]))
    # nodes.bin liegt im LittleFS von arduino-pico.
    cg.add_library("LittleFS", None)

    text_sensors = {
        CONF_STATUS: var.set_status_sensor,
        CONF_LAST_REPLY: var.set_last_reply_sensor,
        CONF_RM_FIRMWARE: var.set_rm_firmware_sensor,
        CONF_RM_DEVICE_ID: var.set_rm_device_id_sensor,
        CONF_LAST_EVENT: var.set_last_event_sensor,
        CONF_UNASSIGNED: var.set_unassigned_sensor,
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
        CONF_NODE_COUNT: var.set_node_count_sensor,
        CONF_REMOTE_REQUESTS: var.set_remote_requests_sensor,
    }
    for key, setter in sensors.items():
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(setter(sens))

    binary_sensors = {
        CONF_RM_PAIRED: var.set_rm_paired_sensor,
        CONF_NETWORK_ALARM: var.set_network_alarm_sensor,
        CONF_NETWORK_SILENCED: var.set_network_silenced_sensor,
        CONF_DISCOVERY_RUNNING: var.set_discovery_running_sensor,
    }
    for key, setter in binary_sensors.items():
        if key in config:
            sens = await binary_sensor.new_binary_sensor(config[key])
            cg.add(setter(sens))

    if CONF_NETWORK_EVENT in config:
        evt = await event.new_event(config[CONF_NETWORK_EVENT], event_types=NETWORK_EVENT_TYPES)
        cg.add(var.set_network_event(evt))

    for detector in config[CONF_DETECTORS]:
        await detector_to_code(var, detector)

    for key in (
        CONF_INIT_BUTTON,
        CONF_RM_STATUS_BUTTON,
        CONF_SID_MAP_BUTTON,
        CONF_DISCOVER_BUTTON,
        CONF_REDISCOVER_BUTTON,
        CONF_DETAILS_BUTTON,
        CONF_REGISTRY_LOG_BUTTON,
        CONF_TEMPLATE_BUTTON,
        CONF_NETWORK_TEST_BUTTON,
    ):
        if key in config:
            btn = await button.new_button(config[key])
            cg.add(btn.set_parent(var))
    if CONF_SPI_BUTTON in config:
        await button.new_button(config[CONF_SPI_BUTTON])
    if CONF_RAW_LOG_SWITCH in config:
        sw = await switch.new_switch(config[CONF_RAW_LOG_SWITCH])
        await cg.register_component(sw, config[CONF_RAW_LOG_SWITCH])
        cg.add(sw.set_parent(var))


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

CONF_PAYLOAD = "payload"

SEND_RAW_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(WiSafe2Component),
        cv.Required(CONF_PAYLOAD): cv.templatable(raw_payload_value),
    }
)


@automation.register_action("wisafe2.send_raw", SendRawAction, SEND_RAW_SCHEMA, synchronous=True)
async def send_raw_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    cg.add(var.set_payload(await cg.templatable(config[CONF_PAYLOAD], args, cg.std_string)))
    return var
