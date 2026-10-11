import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button, text_sensor
from esphome.const import CONF_ID, ENTITY_CATEGORY_CONFIG, ENTITY_CATEGORY_DIAGNOSTIC

AUTO_LOAD = ["button", "network", "text_sensor"]

wisafe2_ns = cg.esphome_ns.namespace("wisafe2")
WiSafe2Component = wisafe2_ns.class_("WiSafe2Component", cg.Component)
WiSafe2InitButton = wisafe2_ns.class_("WiSafe2InitButton", button.Button)
WiSafe2SpiButton = wisafe2_ns.class_("WiSafe2SpiButton", button.Button)
WiSafe2RmStatusButton = wisafe2_ns.class_("WiSafe2RmStatusButton", button.Button)
WiSafe2SidMapButton = wisafe2_ns.class_("WiSafe2SidMapButton", button.Button)

CONF_STATUS = "status"
CONF_INIT_BUTTON = "init_button"
CONF_SPI_BUTTON = "spi_button"
CONF_RM_STATUS_BUTTON = "rm_status_button"
CONF_SID_MAP_BUTTON = "sid_map_button"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(WiSafe2Component),
        cv.Optional(CONF_STATUS): text_sensor.text_sensor_schema(),
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
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    if CONF_STATUS in config:
        sens = cg.new_Pvariable(config[CONF_STATUS][CONF_ID])
        await text_sensor.register_text_sensor(sens, config[CONF_STATUS])
        cg.add(var.set_status_sensor(sens))
    for key in (CONF_INIT_BUTTON, CONF_RM_STATUS_BUTTON, CONF_SID_MAP_BUTTON):
        if key in config:
            btn = await button.new_button(config[key])
            cg.add(btn.set_parent(var))
    if CONF_SPI_BUTTON in config:
        await button.new_button(config[CONF_SPI_BUTTON])
