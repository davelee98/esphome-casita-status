# Project Agent Guide

## Project summary

This repository develops an ESPHome status-board implementation for the ESPHome Starter Kit Casita LED and buzzer module. The Casita board has ten individually addressable WS2812 RGB LEDs arranged around a house-shaped perimeter. The project accepts direct integer statuses or typed CO2, AQI, and VOC measurements and converts them into stable, readable LED patterns without exposing physical pixel indices to automations.

The primary specification is [ARCHITECTURE.md](ARCHITECTURE.md). Read it before changing the component interface, LED grouping, state model, palette, or renderer behavior.

## Fixed physical mapping

PCB LED designators are one-based; ESPHome indices are zero-based. The data chain starts at the upper-right roof LED and travels clockwise around the perimeter, ending at the roof peak.

```text
                         LED10 / index 9
                  LED9 / 8       LED1 / 0
             LED8 / 7               LED2 / 1
             LED7 / 6               LED3 / 2
                  LED6 / 5  LED5 / 4  LED4 / 3
```

The four logical groups are fixed:

| Group | Membership | Render order |
|---|---|---|
| `left` | indices 5, 6, 7 | 5, 6, 7; bottom to top |
| `right` | indices 1, 2, 3 | 3, 2, 1; bottom to top |
| `center` | index 4 | 4 |
| `top` | indices 8, 9, 0 | 8, 9, 0; left to right |

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
| 3 | Orange | `255, 128, 0` | Three pixels, steady |
| 4 | Red | `255, 0, 0` | Three pixels, steady |
| 5 | Purple | `153, 0, 255` | Three pixels, slow pulse |
| 6 | Maroon | `128, 0, 0` | Three pixels, fast pulse |

Color is not sufficient on its own. Preserve fill and animation as redundant encodings.

## Implementation direction

Build this as a local ESPHome external component with a thin YAML integration layer.

- Put schema validation and ESPHome code generation in the component's Python module.
- Put state resolution, typed lookup translation, animation, and full-frame rendering in non-blocking C++.
- Target ESPHome 2026.3.0 or newer and register component automation actions as synchronous.
- Reference the existing ESK-1 addressable-light instance instead of declaring another GPIO 14 driver.
- Extend the existing `rgb_module` declaration by ID, mark it internal, preserve `rmt_symbols: 48`, and use only the package-owned renderer bridge effect.
- Keep the raw ten-pixel light internal.
- Maintain one rendering owner for the complete pixel frame.
- Configure exactly one input type per group. Expose a single-value numeric action: `status` accepts integers 1 through 6, while `co2`, `aqi`, and `voc` use their fixed built-in lookup table. Keep explicit unavailable and enablement controls separate.
- Give each group exactly one source: a local sensor, a Home Assistant entity bound through a `platform: homeassistant` sensor, or action-driven values. Infer the source kind from the bound sensor's platform. See ARCHITECTURE.md Section 8.5.
- Receive Home Assistant entity values by subscription, not push, so they resynchronize on every connection without Home Assistant automations.
- Use the fixed built-in CO2, AQI, and VOC lookup tables in version 1; do not expose lookup overrides.
- Keep the version 1 palette, fill rules, effects, animation timings, and render cadence fixed in the component. Expose global brightness as the only visual setting.
- Route all sources through one internal submit path. Reject `set_group_value` on sensor-bound groups; for action-driven groups, the last call processed on the main loop wins.
- Judge freshness by source kind. Local sensors use a TTL of 3 × the sensor's update interval, and their initialization window is the greater of 30 s and that TTL. Home Assistant sources have no TTL: they go `UnknownUnavailable` when Home Assistant disconnects and re-enter `Initializing` when it connects. Action-driven TTLs default to `never` for status, 5 minutes for CO2 and VOC, and 30 minutes for AQI. Permit a positive per-group TTL override except on Home Assistant sources.
- Count only API clients with a state subscription as a Home Assistant connection; the dashboard log viewer does not count.
- Keep the status-board package continuously awake.

A large collection of YAML globals, scripts, and duplicated lambdas is acceptable only for a short-lived prototype, not the production implementation.

## Guardrails

- Do not expose raw pixel numbers as the normal user interface.
- Do not let multiple effects, partitions, or automations write to the raw light concurrently.
- Do not assume logical groups are contiguous numeric ranges.
- Do not restore an old measurement as current after boot; begin enabled groups in `Initializing`.
- Map missing, invalid, `NaN`, infinite, expired, or unrecognized source data to `UnknownUnavailable`.
- Keep animations non-blocking and driven by monotonic time.
- Render and commit complete frames so one group cannot erase another.
- Preserve the official hardware settings: GPIO 14, ten WS2812 pixels, GRB order, and zero default transition time.
- Preserve the official `rmt_symbols: 48` setting and never create a second GPIO 14 light.
- Do not override `gamma_correct`; keep ESPHome's default gamma (2.8), which the AIR-1 palette assumes.
- Treat Off as enablement state only; disabling invalidates current data, and re-enabling starts in `Initializing`.
- Treat Initializing as lifecycle-only; no public action may set it.
- Keep status 6 group-local. No status may create a full-board overlay or activate the buzzer.
- Do not add configuration for colors, fills, effects, animation timings, render cadence, or per-channel calibration in version 1.
- Treat everything under `references/ESPHome-Starter-Kit/` as vendored read-only source material.
- Do not edit or regenerate the copied PDF or STEP references.

## Expected repository structure

```text
components/
  casita_status/
    __init__.py
    casita_status.h
    casita_status.cpp
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
references/
  ESPHome-Starter-Kit/
```

Create these directories as implementation work requires them; do not add empty placeholder directories.

## Validation expectations

For implementation changes, keep the minimal test set in ARCHITECTURE.md Section 20 passing: host unit tests for mapping, lookups, state, sources, and renderer, plus an ESP32-C6 compile. Do the physical check before declaring the palette final.

## Reference material

- [Architecture specification](ARCHITECTURE.md)
- [Official ESK-1 YAML](references/ESPHome-Starter-Kit/Integrations/ESPHome/ESK-1.yaml)
- [Casita LED and buzzer schematic](references/ESPHome-Starter-Kit/hardware/electronics/Modules/LED_And_Buzzer_Module.pdf)
- [Casita STEP model](references/ESPHome-Starter-Kit/hardware/cad/pcb-placeholders/LED%20and%20Buzzer%20Module.step)
- [Apollo AIR-1 firmware](https://github.com/ApolloAutomation/AIR-1/blob/main/Integrations/ESPHome/Core.yaml)
- [ESPHome external components documentation](https://esphome.io/components/external_components/)
