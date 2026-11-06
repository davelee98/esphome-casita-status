# Project Agent Guide

## Project summary

This repository develops an ESPHome status-board implementation for the ESPHome Starter Kit Casita LED and buzzer module. The Casita board has ten individually addressable WS2812 RGB LEDs arranged around a house-shaped perimeter. The project accepts direct integer statuses or typed CO2, AQI, PM2.5, VOC, and NOx measurements and converts them into stable, readable LED patterns without exposing physical pixel indices to automations.

The primary specification is [ARCHITECTURE.md](ARCHITECTURE.md). Read it before changing the component interface, LED grouping, state model, palette, or renderer behavior.

## Fixed physical mapping

The data chain starts at the roof peak (LED10, index 0) and travels clockwise around the perimeter, ending at the upper-left roof. Other designators match their index (LED1 is index 1). This order was confirmed on hardware; do not revert to the earlier upper-right-roof start.

```text
                         LED10 / index 0
                  LED9 / 9       LED1 / 1
             LED8 / 8               LED2 / 2
             LED7 / 7               LED3 / 3
                  LED6 / 6  LED5 / 5  LED4 / 4
```

The four logical groups are fixed:

| Group | Membership | Render order |
|---|---|---|
| `left` | indices 6, 7, 8 | 6, 7, 8; bottom to top |
| `right` | indices 2, 3, 4 | 4, 3, 2; bottom to top |
| `center` | index 5 | 5 |
| `top` | indices 9, 0, 1 | 9, 1, 0; upper-left roof, upper-right roof, then peak |

The `top` group wraps across the end of the WS2812 chain. Keep that implementation detail behind the renderer.

## State model

Every group resolves to exactly one of nine display states:

- `Off`
- `UnknownUnavailable`
- `Initializing`
- integer status `1` through `6`

Do not add another public meta-state without an explicit architecture decision. Internally, a source TTL may expire, but the display result is `UnknownUnavailable`, not a separate stale state.

The fixed version 1 six-status palette follows Apollo AIR-1 semantics:

| Status | Color | RGB | Three-pixel behavior |
|---:|---|---|---|
| 1 | Green | `0, 255, 0` | One pixel, steady |
| 2 | Yellow | `255, 255, 0` | Two pixels, steady |
| 3 | Orange | `255, 160, 0` | Three pixels, steady |
| 4 | Red | `255, 0, 0` | Three pixels, steady |
| 5 | Purple | `200, 0, 255` | Three pixels, slow pulse |
| 6 | Maroon | `128, 0, 0` | Three pixels, fast pulse |

Orange and purple deliberately depart from AIR-1's `255, 128, 0` and `153, 0, 255`, which look red-orange and blue-violet after gamma 2.8. Color is not sufficient on its own. Preserve fill and animation as redundant encodings.

## Implementation direction

Build this as a local ESPHome external component with a thin YAML integration layer.

- Put schema validation and ESPHome code generation in the component's Python module.
- Put state resolution, typed lookup translation, animation, and full-frame rendering in non-blocking C++.
- Target ESPHome 2026.3.0 or newer, enforce the minimum in component Python validation, and register component automation actions as synchronous.
- Reference the existing ESK-1 addressable-light instance instead of declaring another GPIO 14 driver.
- Extend the existing `rgb_module` declaration by ID, mark it internal, preserve `rmt_symbols: 48`, and use only the package-owned renderer bridge effect.
- Have the component start the renderer effect when the light is ready and restore it if stopped; renderer recovery redraws existing group states.
- Keep the raw light off during setup with `restore_mode: ALWAYS_OFF`; the component turns it on with the renderer to avoid a full-white startup frame.
- Keep the raw ten-pixel light internal.
- Maintain one writer for the complete pixel frame at a time. Casita owns the buffer except while explicitly suspended; one other on-device component may then write the buffer and schedule output without changing light state, effect, or brightness.
- Configure exactly one input type per group. Expose a single-value numeric action: `status` accepts integers 1 through 6, while `co2`, `aqi`, `pm25`, `voc`, and `nox` use their fixed built-in lookup table. Keep explicit unavailable and enablement controls separate.
- Give each group exactly one source: a local sensor, a Home Assistant entity bound through a `platform: homeassistant` sensor, or action-driven values. A `type: worst` group has no source; it derives its state in the core from the non-`worst` groups listed in `of`. Infer the source kind from the bound sensor's platform. See ARCHITECTURE.md Section 8.5.
- Receive Home Assistant entity values by subscription, not push, so they resynchronize on every connection without Home Assistant automations.
- Clear Home Assistant replay eligibility at boot and each detected disconnect. Record subsequent sensor publications even before connection detection or while disabled; on connect or re-enable, enter Initializing and replay an eligible value through the normal submit path. Keep disabled groups Off and do not display a replay candidate as current while disconnected.
- Append native API wrappers under `api.services` to merge with ESK-1's existing buzzer service. Do not configure both `api.services` and `api.actions`.
- Use the fixed built-in CO2, AQI, PM2.5, VOC, and NOx lookup tables in version 1; do not expose lookup overrides.
- Keep the version 1 palette, fill rules, effects, animation timings, and render cadence fixed in the component. Expose global brightness as the only visual setting.
- Apply brightness and pulse multipliers in output intensity, then encode for ESPHome gamma as defined in ARCHITECTURE.md Section 12.4. Use a provisional brightness range of 25–100%, default 50%; persist global brightness and clamp restored values to that range, falling back to configured brightness when no valid saved value exists.
- Route all sources through one internal submit path. Reject `set_group_value` on sensor-bound groups; for action-driven groups, the last call processed on the main loop wins.
- Judge freshness by source kind. Derive local-sensor TTL as 3 × the polling interval only for known platforms and filters that publish every update; otherwise require an explicit TTL. Their initialization window is the greater of the global initialization timeout (default 30 s) and that TTL. Enabled Home Assistant sources have no TTL: they go `UnknownUnavailable` when Home Assistant disconnects and re-enter `Initializing` when it connects. Action-driven TTLs default to `never` for status, 5 minutes for CO2, PM2.5, VOC, and NOx, and 30 minutes for AQI. Permit a positive per-group TTL override except on Home Assistant sources.
- Count only API clients with a state subscription as a Home Assistant connection; the dashboard log viewer does not count.
- Keep the status-board package continuously awake: force the internal `prevent_sleep` switch on to avoid ESK-1's manual sleep branch, and have the component call `prevent_deep_sleep()` during setup to block automatic sleep. The component's `deep_sleep_id` is optional; the ESK-1 package supplies `deep_sleep_1`.

A large collection of YAML globals, scripts, and duplicated lambdas is acceptable only for a short-lived prototype, not the production implementation.

## Guardrails

- Do not expose raw pixel numbers as the normal user interface.
- Do not let multiple components write to the raw light concurrently. Automations, partitions, and additional effects must not write pixels; shared ownership uses local suspend/resume actions while the renderer bridge stays running.
- Do not assume logical groups are contiguous numeric ranges.
- Do not restore an old measurement as current after boot; begin enabled groups in `Initializing`.
- Map missing, invalid, `NaN`, infinite, expired, or unrecognized source data to `UnknownUnavailable`.
- Keep animations non-blocking and driven by monotonic time.
- Render and commit complete frames so one group cannot erase another.
- Preserve the official hardware settings: GPIO 14, ten WS2812 pixels, GRB order, and zero default transition time.
- Preserve the official `rmt_symbols: 48` setting and never create a second GPIO 14 light.
- Do not override `gamma_correct`; keep ESPHome's default gamma (2.8), which the AIR-1 palette assumes.
- Treat Off as enablement state only; disabling invalidates current data, and re-enabling starts in `Initializing`.
- Default configured group enablement to true and reapply it on each device boot. Do not persist runtime enablement changes; repeated enable/disable calls matching the current state are no-ops.
- Treat Initializing as lifecycle-only; no public action may set it.
- Keep `run_test` a frame overlay with a fixed sequence. It must not change group states, and it is ignored while suspended.
- Keep status 6 group-local. No status may create a full-board overlay or activate the buzzer.
- Do not add configuration for colors, fills, effects, animation timings, render cadence, or per-channel calibration in version 1.
- Do not vendor Apollo ESK-1 files into this repository; they are licensed CC BY-NC-SA 4.0. Include the official ESK-1 YAML as an ESPHome remote package pinned to an upstream commit.

## Expected repository structure

```text
components/
  casita_status/
    __init__.py
    casita_status.h
    casita_status.cpp
    casita_core.h
    casita_core.cpp
packages/
  casita_status.yaml
examples/
  esk1_basic_status_board.yaml
tests/
  mapping/
  lookups/
  state/
  renderer/
  integration/
```

Create these directories as implementation work requires them; do not add empty placeholder directories.

## Validation expectations

For implementation changes, keep the minimal test set in ARCHITECTURE.md Section 20 passing: host unit tests for mapping, lookups, state, sources, and renderer; Python validation tests in `tests/integration/`; and a merged ESK-1 ESP32-C6 compile on minimum and current ESPHome releases. Host renderer tests cover encoded frames; check the actual gamma-corrected driver output on-device. Complete physical validation before declaring the visual policy, including the provisional 25% brightness floor, final.

## Reference material

- [Architecture specification](ARCHITECTURE.md)
- [Official ESK-1 YAML](https://github.com/ApolloAutomation/ESPHome-Starter-Kit/blob/main/Integrations/ESPHome/ESK-1.yaml)
- [Casita LED and buzzer schematic](https://github.com/ApolloAutomation/ESPHome-Starter-Kit/blob/main/hardware/electronics/Modules/LED_And_Buzzer_Module.pdf)
- [Casita STEP model](https://github.com/ApolloAutomation/ESPHome-Starter-Kit/blob/main/hardware/cad/pcb-placeholders/LED%20and%20Buzzer%20Module.step)
- [Apollo AIR-1 firmware](https://github.com/ApolloAutomation/AIR-1/blob/main/Integrations/ESPHome/Core.yaml)
- [ESPHome external components documentation](https://esphome.io/components/external_components/)
