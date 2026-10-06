# Casita status board

An ESPHome external component for the Starter Kit's ten-pixel Casita module. Four logical groups accept integer statuses or CO2, AQI, and VOC measurements. The implementation follows [ARCHITECTURE.md](ARCHITECTURE.md).

## Setup

Use ESPHome 2026.3.0 or newer. Start with [the ESK-1 example](examples/esk1_basic_status_board.yaml), replace the three Home Assistant entity IDs, and add your network settings:

```yaml
wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

api:
  encryption:
    key: !secret api_encryption_key
```

Keep the secrets file beside your device YAML. The example otherwise uses the upstream fallback access point. Register the device with Home Assistant to receive subscribed entity values.

The package loads the local component using [ESPHome external components](https://esphome.io/components/external_components/). Paths resolve relative to your device YAML. If that file lives outside `examples/`, set `substitutions.casita_components_path` to this repository's `components` directory and adjust the two package include paths, all relative to your device YAML. For example, with this repository cloned as `esphome-casita-status/` inside your ESPHome config directory:

```yaml
substitutions:
  casita_components_path: esphome-casita-status/components
packages:
  esk1: !include esphome-casita-status/references/ESPHome-Starter-Kit/Integrations/ESPHome/ESK-1.yaml
  status_board: !include esphome-casita-status/packages/casita_status.yaml
```

The package extends the existing `rgb_module`, keeps it internal, starts and recovers the renderer effect, and prevents automatic and upstream API-triggered sleep. It preserves the buzzer service. Do not add another GPIO 14 light or another effect.

## Values and controls

Every group (`left`, `right`, `center`, `top`) requires a `type`: `status`, `co2`, `aqi`, or `voc`. A `sensor` binding selects its source; omit it for action-driven values. Home Assistant bindings have no TTL and replay eligible subscription values on reconnect or re-enable. Local polling sources derive a TTL only when their publication behavior is known; otherwise specify a positive `ttl`. Action-driven status defaults to `ttl: never`, so it survives an API disconnect until another call or reboot. Other action defaults are five minutes for CO2/VOC and thirty minutes for AQI.

Local automations can call:

```yaml
- casita_status.set_group_value:
    id: casita_board
    group: top
    value: 4
- casita_status.set_group_unavailable:
    id: casita_board
    group: top
- casita_status.set_group_enabled:
    id: casita_board
    group: top
    enabled: false
- casita_status.set_global_brightness:
    id: casita_board
    value: 25  # Percent, 25 through 100
```

The first three also have native API wrappers named `casita_set_group_value`, `casita_set_group_unavailable`, and `casita_set_group_enabled`. Home Assistant exposes them under the device's ESPHome actions; use their group/value or group/enabled fields. Value actions on sensor-bound groups are rejected. Explicit unavailability on a sensor-bound group lasts until its next publish.

`Casita Brightness` is a persisted global number, default 50%, with a provisional 25% floor. Brightness and pulses multiply output intensity before encoding for ESPHome's unchanged gamma. Runtime enablement is not persisted; reboot reapplies each group's configured `enabled` value, default true. Measurements are never restored as current. Four diagnostic text sensors report resolved states; logs include source, age, TTL, rejected updates, and renderer counts. Any API client with a state subscription counts as connected, including a second Home Assistant instance.

## Build and tests

From the repository root, the installed Windows Device Builder's bundled Python can validate and compile directly:

```powershell
$builderPython = "$env:LOCALAPPDATA\ESPHome Device Builder\python\python.exe"
& $builderPython -m esphome config examples/esk1_basic_status_board.yaml
& $builderPython -m esphome compile examples/esk1_basic_status_board.yaml
```

For development tests, use Linux/WSL with Python 3.12+ and g++ or clang++:

```sh
python3 -m venv .venv-linux
.venv-linux/bin/python -m pip install esphome==2026.3.0 pytest
.venv-linux/bin/python tests/run_host_tests.py
.venv-linux/bin/python -m pytest tests/integration -q
.venv-linux/bin/python -m esphome compile examples/esk1_basic_status_board.yaml
```

Repeat validation and compilation using the current stable release. CI pins minimum and current versions so upgrades are reviewable. Host tests exercise the standalone C++ core; Python tests validate real merged configurations and expected failures. Host frame tests do not run ESPHome's gamma or driver.

For hardware checks, compile [the device-check configuration](tests/integration/esk1_device_checks.yaml). It logs a PASS/FAIL result for gamma-corrected pulse troughs, disabled pixels, and center off-notches. See [the physical check instructions](tests/integration/DEVICE_CHECKS.md). Compilation is verified; hardware execution and physical readability remain pending.
