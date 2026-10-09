# Casita status board

An ESPHome external component that turns the ESPHome Starter Kit (ESK-1) Casita LED module into a four-zone status board. Each zone shows a status from 1 to 6, either set directly or converted from a CO2, AQI, or VOC measurement, using color, fill, and animation. Automations address zones by name and never touch individual pixels. The design is specified in [ARCHITECTURE.md](ARCHITECTURE.md).

## Requirements

- ESPHome 2026.3.0 or newer. The component refuses to validate on older releases.
- An ESK-1 with the Casita LED and buzzer module, configured from the official ESK-1 YAML. This repository vendors it under `references/`.
- A local clone of this repository. The package loads the component from disk.

## Adding the status board to a device YAML

The device YAML includes two packages, in this order: the official ESK-1 configuration, then [packages/casita_status.yaml](packages/casita_status.yaml). The status-board package does the hardware integration for you:

- loads the `casita_status` external component;
- declares `casita_status:` with `id: casita_board`, the ESK-1 `rgb_module` light, and the ESK-1 deep-sleep component;
- extends `rgb_module` to make it internal, keep it off until the renderer starts, and add the single renderer effect;
- forces the ESK-1 `prevent_sleep` switch on so the board stays awake; and
- adds native API actions for Home Assistant alongside ESK-1's buzzer action.

Your device YAML adds only the `groups:` block, plus any optional settings. ESPHome merges it into the package's `casita_status:` declaration.

### Minimal device YAML

This example assumes the repository is cloned as `esphome-casita-status/` next to the device YAML, for example inside your ESPHome config directory:

```yaml
substitutions:
  casita_components_path: esphome-casita-status/components

packages:
  esk1: !include esphome-casita-status/references/ESPHome-Starter-Kit/Integrations/ESPHome/ESK-1.yaml
  status_board: !include esphome-casita-status/packages/casita_status.yaml

esphome:
  name: casita-status-board
  friendly_name: Casita Status Board
  min_version: 2026.3.0

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

api:
  encryption:
    key: !secret api_encryption_key

sensor:
  - platform: homeassistant
    id: ha_outdoor_aqi
    entity_id: sensor.outdoor_aqi

casita_status:
  groups:
    left:
      type: status          # Set by actions
    right:
      type: aqi
      sensor: ha_outdoor_aqi
    center:
      type: status
      enabled: false        # Dark until enabled by an action
    top:
      type: status
```

All three paths are relative to the device YAML. `casita_components_path` must point at this repository's `components` directory. The package's default (`../components`) only works for files in `examples/`. Keep `secrets.yaml` beside the device YAML. [examples/esk1_basic_status_board.yaml](examples/esk1_basic_status_board.yaml) is a complete example that binds three Home Assistant sensors.

Do not declare another light on GPIO 14, add effects to `rgb_module`, or control `rgb_module` directly. Validation rejects a modified light. Casita owns its pixels except during an explicit handover to another on-device component (see [Sharing the light](#sharing-the-light)).

## Groups

All four groups must be configured. Each one has a fixed position on the module:

| Group | LEDs | Fill direction |
|---|---|---|
| `left` | 3 on the left wall | bottom to top |
| `right` | 3 on the right wall | bottom to top |
| `center` | 1 at the bottom center | — |
| `top` | 3 on the roof | left to right |

### Group options

| Option | Required | Description |
|---|---|---|
| `type` | yes | How the value is interpreted: `status`, `co2`, `aqi`, or `voc`. See [Values](#values). |
| `sensor` | no | ID of a numeric sensor that feeds this group. Omit it to drive the group with actions. See [Sources](#sources). |
| `enabled` | no | Whether the group is on at boot. Default `true`. A disabled group stays dark. |
| `ttl` | no | How long a value stays current before the group shows Unknown. A positive duration such as `10min`, or `never` for action-driven `status` groups. Not allowed on Home Assistant sensors. |
| `state` | no | Text sensor options for the group's diagnostic state entity, such as `name` or `disabled_by_default`. The default name is `Casita <Group> State`. |

### Board options

| Option | Default | Description |
|---|---|---|
| `brightness` | `50%` | Brightness used when no saved value exists. Range `25%` to `100%`. |
| `initialization_timeout` | `30s` | How long a group waits for its first value before showing Unknown. |
| `brightness_number` | `name: Casita Brightness` | Number entity options for the brightness control. |

The package already sets `id`, `light_id`, and `deep_sleep_id`; do not change them. Colors, fills, animation timings, and lookup thresholds are fixed and cannot be configured.

## Values

| `type` | Accepts | Conversion to status |
|---|---|---|
| `status` | Whole numbers 1 to 6 (`4` and `4.0` are valid) | Used as is |
| `co2` | CO2 in ppm, 0 or more | ≤800 → 1, ≤1000 → 2, ≤1500 → 3, ≤2000 → 4, ≤2500 → 5, above → 6 |
| `aqi` | Already-computed U.S. EPA AQI, 0 or more | ≤50 → 1, ≤100 → 2, ≤150 → 3, ≤200 → 4, ≤300 → 5, above → 6 |
| `voc` | Sensirion VOC Index, 1 to 500 | <80 → 1, <150 → 2, <250 → 3, <400 → 4, ≤500 → 5 |

`aqi` expects an AQI value, not a raw PM2.5 concentration, and `voc` expects the Sensirion VOC Index, not TVOC in ppb. Values outside the accepted range, `NaN`, and Home Assistant `unknown` or `unavailable` all show Unknown.

## Sources

Each group gets values from exactly one source. The source kind follows from the `sensor` binding.

**Home Assistant entity.** Declare a `platform: homeassistant` sensor and bind it. No Home Assistant automation is needed: the device subscribes to the entity and receives its current value on every connection. These groups have no TTL. They show Unknown while Home Assistant is disconnected and recover when it reconnects.

```yaml
sensor:
  - platform: homeassistant
    id: ha_bedroom_co2
    entity_id: sensor.bedroom_co2

casita_status:
  groups:
    left:
      type: co2
      sensor: ha_bedroom_co2
```

**Local sensor.** Bind a sensor attached to the device. For known polling sensors whose filters publish on every update, the TTL defaults to three times the sensor's `update_interval`. For any other sensor, including `template` and `copy` sensors and filters such as `throttle`, `delta`, or `lambda`, validation requires an explicit `ttl`. Local groups keep working without Home Assistant.

```yaml
sensor:
  - platform: scd4x
    update_interval: 60s
    co2:
      id: room_co2

casita_status:
  groups:
    left:
      type: co2
      sensor: room_co2      # TTL defaults to 3 x 60s
```

**Actions.** Omit `sensor` and set values with `casita_status.set_group_value` or the Home Assistant action. Values are not restored after a reboot, so the caller must resend them. The default TTL is `never` for `status`, 5 minutes for `co2` and `voc`, and 30 minutes for `aqi`. If a value already exists as a Home Assistant entity, bind it as a sensor instead.

## Actions

### In ESPHome automations

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
    value: 25  # Percent, 25 to 100
```

`group` and `value` accept lambdas. `set_group_value` is rejected, with a warning in the log, on groups that have a `sensor`. `set_group_unavailable` works on any group; on a sensor-bound group it lasts until the sensor's next value. Disabling a group discards its value; re-enabling it starts Initializing.

### From Home Assistant

The package adds three device actions: `casita_set_group_value` (`group`, `value`), `casita_set_group_unavailable` (`group`), and `casita_set_group_enabled` (`group`, `enabled`). Home Assistant names them `esphome.<device name>_<action>`. ESK-1 sets `name_add_mac_suffix: true`, so the device name includes the last six hex digits of its MAC address. Find the exact name under **Developer tools → Actions**.

```yaml
action: esphome.casita_status_board_a1b2c3_casita_set_group_value
data:
  group: top
  value: 3
```

Brightness is the `Casita Brightness` number entity, not an action. Its value is saved and restored after a reboot.

## Sharing the light

Use the synchronous local actions `casita_status.suspend` and `casita_status.resume` to let another on-device component temporarily draw on the same light. Both accept a component ID directly or an `id:` mapping. They are not exposed as Home Assistant services.

For Fortune's shared-light mode, connect its triggers in the device YAML:

```yaml
fortune_button:
  share_light: true
  on_start:
    - casita_status.suspend: casita_board
  on_finish:
    - casita_status.resume:
        id: casita_board
```

The other component must follow these rules:

1. Write only the addressable buffer and call `schedule_show()`. Do not turn the light on or off, change its effect, or change its brightness. Stopping the renderer effect can produce a full-white frame.
2. Suspend before the first pixel write and resume after the last. Put these direct actions first in the respective trigger automations, before delays or deferred work. Do not write pixels while idle.
3. Supply its own pixel levels. Casita brightness affects only Casita frames; the underlying light stays at 100%.

The renderer effect and recovery check keep running while suspended. Group values, deadlines, diagnostics, and brightness continue updating. Resume redraws the complete current frame on the next render, even when steady statuses have not changed. Repeated calls are no-ops. Suspension has no automatic timeout and is not persisted; every reboot starts unsuspended.

## What the LEDs show

| Status | Color | 3-LED groups | `center` |
|---:|---|---|---|
| 1 | Green | 1 LED, steady | Steady |
| 2 | Yellow | 2 LEDs, steady | One brief blink off every 6 s |
| 3 | Orange | 3 LEDs, steady | Two blinks off every 6 s |
| 4 | Red | 3 LEDs, steady | Three blinks off every 6 s |
| 5 | Purple | 3 LEDs, slow pulse | Slow pulse |
| 6 | Maroon | 3 LEDs, fast pulse | Fast pulse |

| State | Shown as | Meaning |
|---|---|---|
| Initializing | Blue LED chasing (center: blue pulse) | Waiting for the first value after boot, enable, or a Home Assistant connection |
| Unknown | Dim white slow pulse | No trustworthy current value: invalid, expired, timed out, or Home Assistant disconnected |
| Off | Dark | Group disabled |

The board never restores a measurement after a reboot; enabled groups start in Initializing. No state activates the buzzer.

## Diagnostics

Each group has a `Casita <Group> State` text sensor reporting `off`, `unknown_unavailable`, `initializing`, or `1` to `6`. The boot log (and the log whenever a log viewer connects) shows each group's type, source, bound sensor, last value, value age, TTL, and rejected-update count. State changes are logged with the reason.

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

Repeat validation and compilation with the current stable release. CI tests both the minimum and current versions. Host tests exercise the standalone C++ core; Python tests validate real merged configurations and expected failures. Host frame tests do not run ESPHome's gamma or LED driver.

For hardware checks, compile [the device-check configuration](tests/integration/esk1_device_checks.yaml). It logs a PASS/FAIL result for gamma-corrected pulse troughs, disabled pixels, and center off-notches. See [the physical check instructions](tests/integration/DEVICE_CHECKS.md). Compilation is verified; hardware execution and physical readability checks are still pending.
