"""Semantic status board for the ten-pixel Casita module (ESPHome >= 2026.3)."""

from esphome import automation
import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components import deep_sleep, light, number, sensor, text_sensor
from esphome.components.light.effects import register_addressable_effect
from esphome.components.light.types import AddressableLightEffect
from esphome.const import CONF_ID, CONF_NAME, CONF_SENSOR, CONF_TYPE, CONF_VALUE

AUTO_LOAD = ["sensor", "text_sensor", "number"]
DEPENDENCIES = ["light"]
MULTI_CONF = False
GROUPS = ("left", "right", "center", "top")
EFFECT_KEY = "casita_status"  # The light effect's YAML key: `effects: [casita_status: {}]`.
EFFECT_NAME = "Casita Status Renderer"
CONF_CASITA_ID = "casita_id"
ns = cg.esphome_ns.namespace("casita_status")
CasitaStatus = ns.class_("CasitaStatus", cg.Component)
CasitaRendererEffect = ns.class_("CasitaRendererEffect", AddressableLightEffect)
BrightnessNumber = ns.class_("BrightnessNumber", number.Number)
InputType = ns.enum("InputType", is_class=True)
SourceKind = ns.enum("SourceKind", is_class=True)
TYPES = {name: getattr(InputType, name.upper()) for name in ("status", "co2", "aqi", "voc", "pm25", "nox", "worst")}
SetGroupValueAction = ns.class_("SetGroupValueAction", automation.Action)
SetGroupUnavailableAction = ns.class_("SetGroupUnavailableAction", automation.Action)
SetGroupEnabledAction = ns.class_("SetGroupEnabledAction", automation.Action)
SetBrightnessAction = ns.class_("SetBrightnessAction", automation.Action)
RunTestAction = ns.class_("RunTestAction", automation.Action)
SetSuspendedAction = ns.class_("SetSuspendedAction", automation.Action)


def positive_duration(value):
    result = cv.positive_time_period_milliseconds(value)
    if not 0 < result.total_milliseconds <= 0x7FFFFFFF:
        raise cv.Invalid("duration must be positive and at most 2147483647ms")
    return result


def ttl(value):
    return "never" if value == "never" else positive_duration(value)


def numeric(value):
    if isinstance(value, (bool, str)):
        raise cv.Invalid("value must be numeric, not a string or boolean")
    return cv.float_(value)


GROUP_SCHEMA = cv.Schema({
    cv.Required(CONF_TYPE): cv.one_of(*TYPES, lower=True),
    cv.Optional(CONF_SENSOR): cv.use_id(sensor.Sensor),
    cv.Optional("enabled", default=True): cv.boolean,
    cv.Optional("ttl"): ttl,
    cv.Optional("of"): cv.ensure_list(cv.one_of(*GROUPS, lower=True)),
    cv.Optional("state", default={}): text_sensor.text_sensor_schema(
        text_sensor.TextSensor, entity_category="diagnostic"
    ),
})


def validate_worst(config):
    """A worst group takes no input and follows only non-worst groups other than itself."""
    groups = config["groups"]
    for name, group in groups.items():
        with cv.prepend_path(["groups", name]):
            if group[CONF_TYPE] != "worst":
                if "of" in group:
                    raise cv.Invalid("'of' is only valid on type: worst groups", path=["of"])
                continue
            for key in (CONF_SENSOR, "ttl"):
                if key in group:
                    raise cv.Invalid(f"type: worst follows other groups and takes no {key}", path=[key])
            sources = group.get("of", [])
            if not sources:
                raise cv.Invalid("type: worst requires 'of', the groups it follows", path=["of"])
            if len(set(sources)) != len(sources):
                raise cv.Invalid("'of' lists a group more than once", path=["of"])
            for source in sources:
                if source == name:
                    raise cv.Invalid("a worst group cannot follow itself", path=["of"])
                if groups[source][CONF_TYPE] == "worst":
                    raise cv.Invalid(f"a worst group cannot follow another worst group ({source})", path=["of"])
    return config


def diagnostic_defaults(config):
    for name, group in config.get("groups", {}).items():
        if name in GROUPS and "state" not in group:
            group["state"] = {"name": f"Casita {name.title()} State"}
    return config


CONFIG_SCHEMA = cv.All(
    cv.require_esphome_version(2026, 3, 0),
    diagnostic_defaults,
    cv.Schema({
        cv.GenerateID(): cv.declare_id(CasitaStatus),
        cv.Required("light_id"): cv.use_id(light.AddressableLightState),
        cv.Optional("deep_sleep_id"): cv.use_id(deep_sleep.DeepSleepComponent),
        cv.Optional("brightness", default="50%"): cv.All(cv.percentage, cv.Range(min=0.25, max=1.0)),
        cv.Optional("initialization_timeout", default="30s"): positive_duration,
        cv.Optional("brightness_number", default={"name": "Casita Brightness"}): number.number_schema(
            BrightnessNumber, entity_category="config"
        ),
        cv.Required("groups"): cv.Schema({cv.Required(group): GROUP_SCHEMA for group in GROUPS}),
    }).extend(cv.COMPONENT_SCHEMA),
    validate_worst,
)

# Conservative defaults: unknown platforms or filters need an explicit TTL.
POLLING_PLATFORMS = {"scd4x", "scd30", "sht4x", "sht3xd", "aht10", "dallas_temp", "wifi_signal",
                     "uptime", "internal_temperature", "sgp4x", "sen5x", "sen6x", "bme280_i2c"}
PER_PUBLISH_FILTERS = {"offset", "multiply", "calibrate_linear", "calibrate_polynomial", "round", "round_to_multiple"}


def filters_publish_every_update(filters):
    for config in filters:
        for key, value in config.items():
            if key in PER_PUBLISH_FILTERS:
                continue
            if key in {"sliding_window_moving_average", "exponential_moving_average"} and value.get("send_every") == 1:
                continue
            return False
    return True


def resolve_source(group, owner, bound_sensor, initialization_ms):
    """Resolve source/freshness from validated sensor and parent configs."""
    if group[CONF_TYPE] == "worst":
        return "DERIVED", 0, initialization_ms
    if owner is None:
        source = "ACTION"
        default_ttl = {"status": 0, "co2": 300000, "voc": 300000, "pm25": 300000, "nox": 300000,
                       "aqi": 1800000}[group[CONF_TYPE]]
    elif owner.get("platform") == "homeassistant":
        if "ttl" in group:
            raise cv.Invalid("ttl is not allowed on Home Assistant sources")
        return "HOME_ASSISTANT", 0, initialization_ms
    else:
        source = "LOCAL"
        interval = owner.get("update_interval")
        known = owner.get("platform") in POLLING_PLATFORMS and interval is not None
        default_ttl = 0
        if known and filters_publish_every_update(bound_sensor.get("filters", [])):
            default_ttl = interval.total_milliseconds * 3
            if not 0 < default_ttl <= 0x7FFFFFFF:
                default_ttl = 0
        if "ttl" not in group and not default_ttl:
            raise cv.Invalid("local sensor publication interval cannot be safely derived; specify ttl")
    override = group.get("ttl")
    if override == "never":
        if source != "ACTION" or group[CONF_TYPE] != "status":
            raise cv.Invalid("ttl: never is allowed only on action-driven status groups")
        ttl_ms = 0
    else:
        ttl_ms = override.total_milliseconds if override is not None else default_ttl
    return source, ttl_ms, max(initialization_ms, ttl_ms) if source == "LOCAL" else initialization_ms


def final_validate(config):
    full = fv.full_config.get()
    light_path = full.get_path_for_id(config["light_id"])[:-1]
    raw = full.get_config_for_path(light_path)
    if raw.get("platform") != "esp32_rmt_led_strip":
        raise cv.Invalid("light_id must reference the existing Casita esp32_rmt_led_strip")
    expected = {"num_leds": 10, "chipset": "WS2812", "rmt_symbols": 48, "internal": True, "gamma_correct": 2.8}
    for key, value in expected.items():
        if raw.get(key) != value:
            raise cv.Invalid(f"Casita light requires {key}: {value}")
    # ESPHome 2026.8+ migrates rgb_order to channel_colors during validation.
    channel_order = raw.get("channel_colors", raw.get("rgb_order"))
    if raw["pin"]["number"] != 14 or str(channel_order) != "GRB":
        raise cv.Invalid("Casita light requires GPIO14 and GRB order")
    if raw["default_transition_length"].total_milliseconds != 0:
        raise cv.Invalid("Casita light requires zero transition time")
    effects = raw.get("effects", [])
    if len(effects) != 1 or EFFECT_KEY not in effects[0]:
        hint = (" It replaces the addressable_lambda renderer bridge used before."
                if any("addressable_lambda" in effect for effect in effects) else "")
        raise cv.Invalid(f"Casita light requires exactly one effect, '- {EFFECT_KEY}:'.{hint}")
    # The component selects the effect by name, so the name is fixed.
    if effects[0][EFFECT_KEY][CONF_NAME] != EFFECT_NAME:
        raise cv.Invalid(f"The {EFFECT_KEY} effect's name is fixed; remove 'name'")
    for group_name, group in config["groups"].items():
        owner = bound = None
        if CONF_SENSOR in group:
            path = full.get_path_for_id(group[CONF_SENSOR])[:-1]
            bound = full.get_config_for_path(path)
            # Sensor output may be nested beneath a multi-output polling platform.
            owner = next((full.get_config_for_path(path[:depth]) for depth in range(len(path), 0, -1)
                          if isinstance(full.get_config_for_path(path[:depth]), dict)
                          and "platform" in full.get_config_for_path(path[:depth])), None)
        with cv.prepend_path(["groups", group_name]):
            source, ttl_ms, initialization = resolve_source(group, owner, bound, config["initialization_timeout"].total_milliseconds)
        group["_source"] = source
        group["_ttl_ms"] = ttl_ms
        group["_initialization_ms"] = initialization
    return config


FINAL_VALIDATE_SCHEMA = final_validate


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_light(await cg.get_variable(config["light_id"])))
    cg.add(var.set_brightness(config["brightness"]))
    # Stable, component-ID-specific preference key; do not use Python's randomized hash.
    import zlib
    cg.add(var.set_preference_key(zlib.crc32(f"casita.brightness.{config[CONF_ID]}".encode())))
    if "deep_sleep_id" in config:
        cg.add(var.set_deep_sleep(await cg.get_variable(config["deep_sleep_id"])))
    brightness = await number.new_number(config["brightness_number"], min_value=25, max_value=100, step=1)
    cg.add(brightness.set_parent(var))
    cg.add(var.set_brightness_number(brightness))
    for index, name in enumerate(GROUPS):
        group = config["groups"][name]
        entity = await text_sensor.new_text_sensor(group["state"])
        bound_sensor = await cg.get_variable(group[CONF_SENSOR]) if CONF_SENSOR in group else cg.nullptr
        sources = group.get("of", [])
        worst_of = sum(1 << GROUPS.index(source) for source in sources)
        # A worst group has no sensor; its diagnostics name the groups it follows instead.
        sensor_id = "+".join(sources) if sources else str(group.get(CONF_SENSOR, "none"))
        cg.add(var.configure_group(index, TYPES[group[CONF_TYPE]], getattr(SourceKind, group["_source"]),
                                   group["enabled"], group["_ttl_ms"], group["_initialization_ms"], bound_sensor,
                                   sensor_id, entity, worst_of))


@register_addressable_effect(EFFECT_KEY, CasitaRendererEffect, EFFECT_NAME, {
    cv.GenerateID(CONF_CASITA_ID): cv.use_id(CasitaStatus),
})
async def renderer_effect_to_code(config, effect_id):
    parent = await cg.get_variable(config[CONF_CASITA_ID])
    effect = cg.new_Pvariable(effect_id, config[CONF_NAME])
    cg.add(effect.set_parent(parent))
    return effect


def group_name(value):
    return cv.one_of(*GROUPS)(value)


ACTION_BASE = {cv.Required(CONF_ID): cv.use_id(CasitaStatus), cv.Required("group"): cv.templatable(group_name)}
SUSPEND_SCHEMA = automation.maybe_simple_id({cv.Required(CONF_ID): cv.use_id(CasitaStatus)})


@automation.register_action("casita_status.suspend", SetSuspendedAction, SUSPEND_SCHEMA, synchronous=True)
async def suspend_to_code(config, action_id, template_arg, args):
    return cg.new_Pvariable(action_id, template_arg, await cg.get_variable(config[CONF_ID]), True)


@automation.register_action("casita_status.resume", SetSuspendedAction, SUSPEND_SCHEMA, synchronous=True)
async def resume_to_code(config, action_id, template_arg, args):
    return cg.new_Pvariable(action_id, template_arg, await cg.get_variable(config[CONF_ID]), False)


@automation.register_action("casita_status.run_test", RunTestAction, SUSPEND_SCHEMA, synchronous=True)
async def run_test_to_code(config, action_id, template_arg, args):
    return cg.new_Pvariable(action_id, template_arg, await cg.get_variable(config[CONF_ID]))


async def action_code(config, action_id, template_arg, args, fields):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    for name, kind in fields.items():
        cg.add(getattr(var, f"set_{name}")(await cg.templatable(config[name], args, kind)))
    return var


@automation.register_action("casita_status.set_group_value", SetGroupValueAction,
                            cv.Schema({**ACTION_BASE, cv.Required(CONF_VALUE): cv.templatable(numeric)}), synchronous=True)
async def set_value_to_code(config, action_id, template_arg, args):
    return await action_code(config, action_id, template_arg, args, {"group": cg.std_string, "value": cg.double})


@automation.register_action("casita_status.set_group_unavailable", SetGroupUnavailableAction,
                            cv.Schema(ACTION_BASE), synchronous=True)
async def unavailable_to_code(config, action_id, template_arg, args):
    return await action_code(config, action_id, template_arg, args, {"group": cg.std_string})


@automation.register_action("casita_status.set_group_enabled", SetGroupEnabledAction,
                            cv.Schema({**ACTION_BASE, cv.Required("enabled"): cv.templatable(cv.boolean)}), synchronous=True)
async def enabled_to_code(config, action_id, template_arg, args):
    return await action_code(config, action_id, template_arg, args, {"group": cg.std_string, "enabled": cg.bool_})


@automation.register_action("casita_status.set_global_brightness", SetBrightnessAction,
                            cv.Schema({cv.Required(CONF_ID): cv.use_id(CasitaStatus),
                                       cv.Required(CONF_VALUE): cv.templatable(cv.All(numeric, cv.Range(min=25, max=100)))}),
                            synchronous=True)
async def brightness_to_code(config, action_id, template_arg, args):
    return await action_code(config, action_id, template_arg, args, {"value": cg.float_})
