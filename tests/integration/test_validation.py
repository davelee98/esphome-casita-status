"""Validate real merged YAML, plus the source policy and action schemas."""
import importlib.util
from pathlib import Path
import subprocess
import sys

import pytest
import esphome.config_validation as cv
from esphome.const import CONF_TYPE

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("casita_component", ROOT / "components/casita_status/__init__.py")
component = importlib.util.module_from_spec(spec)
spec.loader.exec_module(component)


def test_action_numeric_contract():
    assert component.numeric(4.0) == 4
    for invalid in ("4", True, False):
        with pytest.raises(cv.Invalid):
            component.numeric(invalid)


def test_minimum_version(monkeypatch):
    monkeypatch.setattr(cv, "parse_esphome_version", lambda: (2026, 2, 0))
    # Newer releases compare their version string directly instead of parsing a tuple.
    monkeypatch.setattr(cv, "ESPHOME_VERSION", "2026.2.0")
    with pytest.raises(cv.Invalid, match="requires at least ESPHome"):
        component.CONFIG_SCHEMA({})


def test_action_defaults_and_overrides():
    for kind, expected in {"status": 0, "co2": 300000, "voc": 300000, "pm25": 300000, "nox": 300000,
                           "aqi": 1800000}.items():
        assert component.resolve_source({CONF_TYPE: kind}, None, None, 30000) == ("ACTION", expected, 30000)
    assert component.resolve_source({CONF_TYPE: "status", "ttl": "never"}, None, None, 30000)[1] == 0
    with pytest.raises(cv.Invalid, match="only on action-driven status"):
        component.resolve_source({CONF_TYPE: "co2", "ttl": "never"}, None, None, 30000)


def test_homeassistant_has_no_ttl():
    owner = {"platform": "homeassistant"}
    assert component.resolve_source({CONF_TYPE: "aqi"}, owner, owner, 30000) == ("HOME_ASSISTANT", 0, 30000)
    with pytest.raises(cv.Invalid, match="not allowed"):
        component.resolve_source({CONF_TYPE: "aqi", "ttl": component.ttl("5min")}, owner, owner, 30000)


def test_polling_parent_and_filter_policy():
    owner = {"platform": "scd4x", "update_interval": component.positive_duration("60s")}
    group = {CONF_TYPE: "co2"}
    assert component.resolve_source(group, owner, {}, 30000) == ("LOCAL", 180000, 180000)
    sen6x = {"platform": "sen6x", "update_interval": component.positive_duration("60s")}
    assert component.resolve_source({CONF_TYPE: "pm25"}, sen6x, {}, 30000) == ("LOCAL", 180000, 180000)
    suppressed = {"filters": [{"delta": 10}]}
    with pytest.raises(cv.Invalid, match="specify ttl"):
        component.resolve_source(group, owner, suppressed, 30000)
    group["ttl"] = component.ttl("15min")
    assert component.resolve_source(group, owner, suppressed, 30000) == ("LOCAL", 900000, 900000)
    assert component.filters_publish_every_update([{"multiply": 2}, {"round": 1}])
    assert not component.filters_publish_every_update([{"lambda": "return x;"}])
    with pytest.raises(cv.Invalid, match="specify ttl"):
        component.resolve_source({CONF_TYPE: "aqi"}, {"platform": "copy"}, {}, 30000)


def test_template_sensor_requires_explicit_ttl():
    owner = {"platform": "template", "update_interval": component.positive_duration("60s"), "lambda": "return {};"}
    group = {CONF_TYPE: "co2"}
    with pytest.raises(cv.Invalid, match="specify ttl"):
        component.resolve_source(group, owner, owner, 30000)
    group["ttl"] = component.ttl("5min")
    assert component.resolve_source(group, owner, owner, 30000) == ("LOCAL", 300000, 300000)


@pytest.mark.parametrize("value", ["0s", "never", "2147483648ms"])
def test_duration_bounds(value):
    with pytest.raises(cv.Invalid):
        component.positive_duration(value)


def fixture_config(tmp_path, edits="", use_package=True):
    # Absolute paths ensure fixtures work in pytest's temporary directories.
    text = f"""substitutions:
  casita_components_path: {ROOT.as_posix()}/components
packages:
  esk1: github://ApolloAutomation/ESPHome-Starter-Kit/Integrations/ESPHome/ESK-1.yaml@41dd7d47efa14a7ba9bf56fd4c58f7d14eb18c2f
"""
    if use_package:
        text += f"  board: !include {ROOT.as_posix()}/packages/casita_status.yaml\n"
    text += f"""external_components:
  - source:
      type: local
      path: {ROOT.as_posix()}/components
    components: [casita_status]
esphome:
  name: casita-validation
casita_status:
  groups:
    left:
      type: status
    right:
      type: status
    center:
      type: status
    top:
      type: status
"""
    text += edits
    file = tmp_path / "config.yaml"
    file.write_text(text)
    return file


def run_config(path):
    return subprocess.run([sys.executable, "-m", "esphome", "config", str(path)], capture_output=True, text=True)


def test_merged_example():
    result = run_config(ROOT / "examples/esk1_basic_status_board.yaml")
    assert result.returncode == 0, result.stdout + result.stderr
    merged = result.stdout
    assert "play_buzzer" in merged and "casita_set_group_value" in merged
    assert "on_boot:" in merged and "deep_sleep_1" in merged
    assert "rmt_symbols: 48" in merged
    assert "restore_mode: ALWAYS_OFF" in merged
    assert merged.count("number: 14") == 1
    assert "Casita Left State" in merged and "Casita Brightness" in merged


def test_device_check_config():
    result = run_config(ROOT / "tests/integration/esk1_device_checks.yaml")
    assert result.returncode == 0, result.stdout + result.stderr


def test_suppressed_polling_requires_ttl(tmp_path):
    path = fixture_config(tmp_path, """sensor:
  - platform: wifi_signal
    id: local_measurement
    update_interval: 60s
    filters:
      - throttle: 5min
""")
    path.write_text(path.read_text().replace("left:\n      type: status", "left:\n      type: co2\n      sensor: local_measurement"))
    result = run_config(path)
    assert result.returncode != 0
    assert "specify ttl" in result.stdout
    path.write_text(path.read_text().replace("sensor: local_measurement", "sensor: local_measurement\n      ttl: 15min"))
    result = run_config(path)
    assert result.returncode == 0, result.stdout + result.stderr


def test_nested_polling_output(tmp_path):
    path = fixture_config(tmp_path, """sensor:
  - platform: scd4x
    update_interval: 60s
    co2:
      id: nested_co2
""")
    path.write_text(path.read_text().replace("left:\n      type: status", "left:\n      type: co2\n      sensor: nested_co2"))
    result = run_config(path)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "_ttl_ms: 180000" in result.stdout


def test_sen6x_pm25_and_nox(tmp_path):
    path = fixture_config(tmp_path, """sensor:
  - platform: sen6x
    update_interval: 60s
    pm_2_5:
      id: room_pm25
    nox_index:
      id: room_nox
""")
    text = path.read_text().replace("left:\n      type: status", "left:\n      type: pm25\n      sensor: room_pm25")
    path.write_text(text.replace("right:\n      type: status", "right:\n      type: nox\n      sensor: room_nox"))
    result = run_config(path)
    assert result.returncode == 0, result.stdout + result.stderr
    assert result.stdout.count("_ttl_ms: 180000") == 2


def test_run_test_button(tmp_path):
    path = fixture_config(tmp_path, """button:
  - platform: template
    name: Casita Self Test
    on_press:
      - casita_status.run_test: casita_board
""")
    result = run_config(path)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "casita_status.run_test" in result.stdout


def test_worst_resolves_as_derived():
    assert component.resolve_source({CONF_TYPE: "worst"}, None, None, 30000) == ("DERIVED", 0, 30000)


def test_worst_group(tmp_path):
    path = fixture_config(tmp_path)
    path.write_text(path.read_text().replace("center:\n      type: status",
                                             "center:\n      type: worst\n      of: [left, right, top]"))
    result = run_config(path)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "_source: DERIVED" in result.stdout


@pytest.mark.parametrize("center, other, message", [
    ("type: worst", "", "requires 'of'"),
    ("type: worst\n      of: [center]", "", "cannot follow itself"),
    ("type: worst\n      of: [left, left]", "", "more than once"),
    ("type: worst\n      of: [top]", "type: worst\n      of: [left]", "another worst group"),
    ("type: worst\n      of: [left]\n      ttl: 5min", "", "takes no ttl"),
    ("type: worst\n      of: [left]\n      sensor: wifi_rssi", "", "takes no sensor"),
    ("type: status\n      of: [left]", "", "only valid on type: worst"),
])
def test_worst_group_rules(tmp_path, center, other, message):
    path = fixture_config(tmp_path, """sensor:
  - platform: wifi_signal
    id: wifi_rssi
""")
    text = path.read_text().replace("center:\n      type: status", "center:\n      " + center)
    if other:
        text = text.replace("top:\n      type: status", "top:\n      " + other)
    path.write_text(text)
    result = run_config(path)
    assert result.returncode != 0
    assert message in result.stdout + result.stderr


def test_no_deep_sleep_binding(tmp_path):
    path = tmp_path / "config.yaml"
    path.write_text(f"""esphome:
  name: casita-validation
esp32:
  variant: esp32c6
  framework:
    type: esp-idf
external_components:
  - source:
      type: local
      path: {ROOT.as_posix()}/components
    components: [casita_status]
casita_status:
  id: casita_board
  light_id: rgb_module
  groups:
    left:
      type: status
    right:
      type: status
    center:
      type: status
    top:
      type: status
light:
  - platform: esp32_rmt_led_strip
    id: rgb_module
    pin: GPIO14
    chipset: WS2812
    num_leds: 10
    rmt_symbols: 48
    rgb_order: GRB
    default_transition_length: 0s
    internal: true
    effects:
      - addressable_lambda:
          name: Casita Status Renderer
          update_interval: 50ms
          lambda: id(casita_board).render(it, initial_run);
""")
    result = run_config(path)
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.parametrize("edit, message", [
    ("num_leds: 9", "num_leds"), ("gamma_correct: 1", "gamma_correct"),
    ("rmt_symbols: 96", "rmt_symbols"), ("internal: false", "internal"),
    ("effects:\n      - addressable_rainbow:", "only the package-owned"),
])
def test_raw_light_guardrails(tmp_path, edit, message):
    path = fixture_config(tmp_path, "light:\n  - id: !extend rgb_module\n    " + edit + "\n")
    result = run_config(path)
    assert result.returncode != 0
    assert message in result.stdout + result.stderr
