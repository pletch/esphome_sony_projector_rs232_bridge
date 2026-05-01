import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select, sensor, switch, text_sensor, uart
from esphome.const import (
    CONF_ID,
    CONF_ICON,
    CONF_NAME,
    CONF_UNIT_OF_MEASUREMENT,
    CONF_UPDATE_INTERVAL,
    DEVICE_CLASS_TEMPERATURE,
    STATE_CLASS_MEASUREMENT,
)

CODEOWNERS = ["@local"]
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["select", "sensor", "switch", "text_sensor"]

CONF_POWER = "power"
CONF_STATUS = "status"
CONF_LIGHT_SOURCE_HOURS = "light_source_hours"
CONF_TEMPERATURE = "temperature"
CONF_FILTER_STATUS = "filter_status"
CONF_MODEL_NAME = "model_name"
CONF_INPUT = "input"
CONF_OPTIONS = "options"

sony_projector_ns = cg.esphome_ns.namespace("sony_projector")
SonyProjector = sony_projector_ns.class_(
    "SonyProjector", cg.PollingComponent, uart.UARTDevice
)
SonyProjectorPowerSwitch = sony_projector_ns.class_(
    "SonyProjectorPowerSwitch", switch.Switch
)
SonyProjectorInputSelect = sony_projector_ns.class_(
    "SonyProjectorInputSelect", select.Select
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(SonyProjector),
            cv.Optional(CONF_POWER): switch.switch_schema(SonyProjectorPowerSwitch),
            cv.Optional(CONF_STATUS): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_LIGHT_SOURCE_HOURS): sensor.sensor_schema(
                accuracy_decimals=0,
            ).extend(
                {
                    cv.Optional(CONF_UNIT_OF_MEASUREMENT, default="h"): cv.string,
                    cv.Optional(CONF_ICON, default="mdi:lightbulb-on-outline"): cv.icon,
                }
            ),
            cv.Optional(CONF_TEMPERATURE): sensor.sensor_schema(
                accuracy_decimals=1,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
            ).extend(
                {
                    cv.Optional(CONF_UNIT_OF_MEASUREMENT, default="°C"): cv.string,
                }
            ),
            cv.Optional(CONF_FILTER_STATUS): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_MODEL_NAME): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_INPUT): select.select_schema(SonyProjectorInputSelect).extend(
                {
                    cv.Required(CONF_OPTIONS): cv.ensure_list(cv.string_strict),
                }
            ),
        }
    )
    .extend(cv.polling_component_schema("30s"))
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    if CONF_POWER in config:
        sw = await switch.new_switch(config[CONF_POWER])
        cg.add(sw.set_parent(var))
        cg.add(var.set_power_switch(sw))

    if CONF_STATUS in config:
        ts = await text_sensor.new_text_sensor(config[CONF_STATUS])
        cg.add(var.set_status_text_sensor(ts))

    if CONF_LIGHT_SOURCE_HOURS in config:
        s = await sensor.new_sensor(config[CONF_LIGHT_SOURCE_HOURS])
        cg.add(var.set_light_source_hours_sensor(s))

    if CONF_TEMPERATURE in config:
        s = await sensor.new_sensor(config[CONF_TEMPERATURE])
        cg.add(var.set_temperature_sensor(s))

    if CONF_FILTER_STATUS in config:
        ts = await text_sensor.new_text_sensor(config[CONF_FILTER_STATUS])
        cg.add(var.set_filter_status_text_sensor(ts))

    if CONF_MODEL_NAME in config:
        ts = await text_sensor.new_text_sensor(config[CONF_MODEL_NAME])
        cg.add(var.set_model_name_text_sensor(ts))

    if CONF_INPUT in config:
        cfg = config[CONF_INPUT]
        sel = await select.new_select(cfg, options=cfg[CONF_OPTIONS])
        cg.add(sel.set_parent(var))
        cg.add(var.set_input_select(sel))
