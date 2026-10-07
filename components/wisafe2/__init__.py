import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch, text_sensor
from esphome.const import CONF_ID

AUTO_LOAD = ["text_sensor", "switch"]

wisafe2_ns = cg.esphome_ns.namespace("wisafe2")
WiSafe2Component = wisafe2_ns.class_("WiSafe2Component", cg.Component)
WiSafe2BlinkSwitch = wisafe2_ns.class_("WiSafe2BlinkSwitch", switch.Switch, cg.Component)

CONF_STATUS = "status"
CONF_BLINK = "blink"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(WiSafe2Component),
        cv.Optional(CONF_STATUS): text_sensor.text_sensor_schema(),
        cv.Optional(
            CONF_BLINK,
            default={"name": "LED Blinken"},
        ): switch.switch_schema(
            WiSafe2BlinkSwitch,
            default_restore_mode="ALWAYS_ON",
            block_inverted=True,
            icon="mdi:led-on",
        ).extend(cv.COMPONENT_SCHEMA),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    if CONF_STATUS in config:
        sens = cg.new_Pvariable(config[CONF_STATUS][CONF_ID])
        await text_sensor.register_text_sensor(sens, config[CONF_STATUS])
        cg.add(var.set_status_sensor(sens))
    if CONF_BLINK in config:
        blink = await switch.new_switch(config[CONF_BLINK])
        await cg.register_component(blink, config[CONF_BLINK])
        cg.add(var.set_blink_switch(blink))
