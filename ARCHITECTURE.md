# Casita Status Board Architecture

Status: Proposed  
Version: 0.1  
Last updated: 2026-09-22

## 1. Purpose

This document defines the architecture for using the ESPHome Starter Kit Casita LED and buzzer module as a semantic status board.

The system presents statuses and numeric ranges through four logical LED groups. Callers work with group names, labels, values, and a small set of display states. They do not work with physical LED numbers. A rendering layer translates the semantic state into colors, fill levels, and animation across the Casita module's ten addressable RGB LEDs.

The architecture is intended to be:

- understandable from across a room;
- deterministic and locally rendered;
- usable from ESPHome and Home Assistant;
- independent of the physical WS2812 chain order;
- extensible without changing the public status interface; and
- readable without relying on color alone.

## 2. Scope

### 2.1 In scope

- The Casita module's ten WS2812 RGB LEDs.
- Four fixed logical LED groups.
- Six ordered status levels derived from the AIR-1/EPA AQI scale.
- Three meta-states: Off, Unknown/Unavailable, and Initializing.
- Label-to-level and numeric-range-to-level mappings.
- Fill, color, brightness, and animation rendering.
- ESPHome and Home Assistant integration boundaries.
- State precedence, transition, validation, and test requirements.

### 2.2 Out of scope for the first implementation

- Arbitrary user-facing per-pixel control.
- Decorative or entertainment lighting effects.
- User-defined physical group geometry.
- Overlapping logical groups.
- Audio/buzzer behavior. The buzzer may be added later as another output of the same semantic state model.
- A fixed assignment of home-automation domains to the four groups. Domain assignments remain configuration choices.

## 3. Source hardware

The official ESK-1 configuration defines the Casita RGB module as a ten-pixel WS2812 strip on GPIO 14 using GRB channel order. The underlying light has zero transition time.

Relevant local references:

- [Official ESK-1 ESPHome configuration](references/ESPHome-Starter-Kit/Integrations/ESPHome/ESK-1.yaml)
- [LED and buzzer module schematic](references/ESPHome-Starter-Kit/hardware/electronics/Modules/LED_And_Buzzer_Module.pdf)
- [LED and buzzer module STEP model](references/ESPHome-Starter-Kit/hardware/cad/pcb-placeholders/LED%20and%20Buzzer%20Module.step)
- [Upstream ESK-1 README](references/ESPHome-Starter-Kit/README.md)

## 4. Physical LED topology

PCB designators are one-based (`LED1` through `LED10`). ESPHome addressable-light indices are zero-based (`0` through `9`). The physical data chain starts at the upper-right roof LED, travels clockwise down the right side, around the base, and up the left side, and ends at the roof peak.

```text
                         LED10 / index 9

                  LED9 / 8       LED1 / 0

             LED8 / 7               LED2 / 1

             LED7 / 6               LED3 / 2

                  LED6 / 5  LED5 / 4  LED4 / 3
```

| Physical position | PCB designator | ESPHome index |
|---|---:|---:|
| Upper-right roof | LED1 | 0 |
| Upper-right side | LED2 | 1 |
| Lower-right side | LED3 | 2 |
| Bottom-right | LED4 | 3 |
| Bottom center | LED5 | 4 |
| Bottom-left | LED6 | 5 |
| Lower-left side | LED7 | 6 |
| Upper-left side | LED8 | 7 |
| Upper-left roof | LED9 | 8 |
| Roof peak | LED10 | 9 |

## 5. Logical groups

The public model contains exactly four groups. Pixel order is part of the private rendering configuration and determines how a partially filled three-LED group grows.

| Group ID | Description | PCB LEDs | ESPHome membership | Render/fill order |
|---|---|---|---|---|
| `left` | Bottom-left three | LED6, LED7, LED8 | 5, 6, 7 | 5, 6, 7; bottom to top |
| `right` | Bottom-right three | LED2, LED3, LED4 | 1, 2, 3 | 3, 2, 1; bottom to top |
| `center` | Bottom-center indicator | LED5 | 4 | 4 |
| `top` | Top three | LED9, LED10, LED1 | 8, 9, 0 | 8, 9, 0; left to right |

```text
                           TOP
                        LED10 / 9
                 LED9 / 8   LED1 / 0

             LEFT                  RIGHT
             LED8 / 7              LED2 / 1
             LED7 / 6              LED3 / 2

             LED6 / 5   LED5 / 4   LED4 / 3
                           CENTER
```

The `top` group wraps around the end of the WS2812 chain. That fact must remain hidden behind the renderer. Code outside the physical mapping layer must never assume that a logical group is a contiguous numeric range.

## 6. Architectural principles

### 6.1 Semantic interface

Callers set a semantic label, a numeric value with a mapping profile, or a meta-state. Callers never set pixels or colors directly.

Examples:

```text
set_label(group="left", label="warning", profile="home_status")
set_value(group="right", value=72, profile="percentage")
set_meta(group="top", state="initializing")
```

### 6.2 One rendering owner

Exactly one renderer owns the ten-pixel output buffer. Home Assistant automations, ESPHome sensors, effects, and scripts must not write directly to the raw addressable light.

This prevents:

- competing animations;
- partial frames;
- one group erasing another group;
- bypassing state precedence; and
- leaking physical pixel numbering into automations.

### 6.3 Validity is separate from severity

A valid measurement resolves to one of six ordered levels. Off, Unknown/Unavailable, and Initializing describe the condition of the data or group, not its severity.

### 6.4 Redundant visual encoding

Color is not the only carrier of meaning. Three-LED groups also use fill count, while higher levels and meta-states use animation. This improves readability through a diffuser and for users with color-vision deficiencies.

### 6.5 Configuration over branching logic

Thresholds, label aliases, brightness, timing, and escalation behavior belong in profiles. The renderer operates on normalized group states and does not contain domain-specific sensor logic.

## 7. System context

```text
 Home Assistant       Local ESPHome sensors       Device lifecycle
       |                        |                         |
       +------------------------+-------------------------+
                                |
                         Input adapters
                                |
                  label/range profile normalization
                                |
                        Per-group state model
                                |
                         State resolver
                                |
                       Pattern renderer
                                |
                    Global brightness limiter
                                |
                Internal 10-pixel WS2812 light
```

The system separates policy from mechanism:

- Input adapters know how to receive data.
- Mapping profiles know what labels and ranges mean.
- The state resolver decides the current display state.
- The renderer knows how states appear on a particular group geometry.
- The hardware driver knows how to transmit pixels.

## 8. State model

### 8.1 Resolved display states

Each group is always in exactly one of these nine resolved states:

```text
Off
UnknownUnavailable
Initializing
Level1
Level2
Level3
Level4
Level5
Level6
```

There are no additional public meta-states.

### 8.2 Source state

The resolver should derive the display state from a compact source model instead of allowing contradictory state flags:

```text
GroupSourceState
  enabled: boolean
  initializing: boolean
  valid: boolean
  level: 1..6 when valid
  source_value: optional number
  source_label: optional string
  updated_at: monotonic timestamp
```

The source value and label are diagnostic metadata. The renderer consumes only the resolved display state.

### 8.3 Resolution order

The resolver applies this order:

```text
if enabled is false:
    Off
else if initializing is true:
    Initializing
else if valid is false:
    UnknownUnavailable
else:
    Level1 through Level6
```

Off is an explicit configuration choice. Initializing means a source is expected to become ready. Unknown/Unavailable means no trustworthy value is currently available.

### 8.4 State transitions

```text
Boot or source start       -> Initializing
First valid input          -> Level1..Level6
Input becomes invalid      -> UnknownUnavailable
Source exceeds its TTL     -> UnknownUnavailable
Source begins reinitializing -> Initializing
Group explicitly disabled -> Off
Group explicitly enabled  -> Initializing or UnknownUnavailable
```

The architecture does not expose a separate Stale state. A configured time-to-live may still be used internally; expiry resolves to Unknown/Unavailable.

## 9. Six-level scale

The default six-level profile adopts the semantic ordering and simplified RGB values used by Apollo AIR-1. Those meanings originate in the U.S. EPA AQI scale. Other profiles may use different labels and thresholds while retaining the same ordered level numbers.

| Level | Default meaning | Color | RGB | Three-LED fill | Default effect |
|---:|---|---|---|---:|---|
| 1 | Good | Green | `0, 255, 0` | 1 | Steady |
| 2 | Moderate | Yellow | `255, 255, 0` | 2 | Steady |
| 3 | Unhealthy for sensitive groups | Orange | `255, 128, 0` | 3 | Steady |
| 4 | Unhealthy | Red | `255, 0, 0` | 3 | Steady |
| 5 | Very unhealthy | Purple | `153, 0, 255` | 3 | Slow pulse |
| 6 | Hazardous | Maroon | `128, 0, 0` | 3 | Fast pulse |

The level names are defaults, not hard-coded domain vocabulary. A profile may call them `normal`, `notice`, `attention`, `warning`, `severe`, and `critical`, for example.

For the one-LED `center` group, levels 1 through 6 always illuminate the single pixel. The level is conveyed by color and, for levels 5 and 6, animation.

### 9.1 Color limitations

The AIR-1 palette is retained for compatibility, but it is not sufficient as the sole encoding:

- red and maroon mainly differ in brightness;
- yellow and orange may converge behind a diffuser;
- green is perceptually brighter than red or purple on typical RGB LEDs; and
- red, green, yellow, orange, and maroon may be difficult to separate for common color-vision deficiencies.

Fill and animation are therefore normative parts of the default rendering profile, not optional decoration.

## 10. Meta-state rendering

| Meta-state | Three-LED rendering | Center rendering | Default timing |
|---|---|---|---|
| Off | All pixels off | Off | Static |
| Unknown/Unavailable | All pixels dim white, slow pulse | Dim white, slow pulse | 3-second cycle |
| Initializing | One blue pixel moves through the group | Blue breathing pulse | 250 ms per chase step; 2-second center pulse |

Meta-state colors and timings are profile settings. Their semantics are fixed.

Animations must use monotonic time and must not use blocking delays. A new state must take effect on the next render frame.

## 11. Mapping profiles

A mapping profile converts domain input into a normalized level. Profiles do not contain physical pixels.

### 11.1 Label mappings

Labels are normalized by trimming whitespace and comparing case-insensitively. Profiles may define aliases.

Conceptual example:

```yaml
# Architecture schema, not literal ESPHome syntax.
profiles:
  home_status:
    labels:
      okay: 1
      normal: 1
      notice: 2
      attention: 3
      warning: 4
      severe: 5
      critical: 6
```

An unrecognized label resolves to Unknown/Unavailable and produces a diagnostic log entry.

### 11.2 Numeric range mappings

Numeric profiles contain ordered, non-overlapping ranges with complete handling for expected input values.

```yaml
# Architecture schema, not literal ESPHome syntax.
profiles:
  air_quality_index:
    ranges:
      - { max: 50,  level: 1 }
      - { max: 100, level: 2 }
      - { max: 150, level: 3 }
      - { max: 200, level: 4 }
      - { max: 300, level: 5 }
      - { above: 300, level: 6 }
```

Range rules:

- Upper bounds are inclusive unless explicitly configured otherwise.
- `NaN`, infinity, parse failures, and missing values resolve to Unknown/Unavailable.
- Ranges must be validated for gaps, overlap, and monotonic level order.
- A profile may clamp out-of-domain finite values or mark them unavailable; that choice must be explicit.
- Optional hysteresis should be supported to prevent flicker near thresholds.
- Optional minimum dwell time should be supported for noisy sources.

### 11.3 Percentage profile

A generic percentage profile may map a continuous value into six equal or configured bands. The numeric value determines the level; the resolved level then determines both fill and color. The renderer does not calculate thresholds.

## 12. Rendering pipeline

### 12.1 Frame production

The renderer produces a complete ten-pixel frame on every animated update:

1. Clear an in-memory ten-pixel frame.
2. Resolve the current pattern for each group.
3. Apply the pattern using that group's render order.
4. Apply global brightness and optional color calibration.
5. Commit the complete frame to the addressable light.

All four groups are disjoint, so normal group rendering cannot conflict. Future overlays must be composed in this single pipeline rather than writing independently to the LEDs.

### 12.2 Fill behavior

For a three-LED group, the default active count is:

```text
Level 1 -> first pixel in render order
Level 2 -> first two pixels
Level 3 -> all three pixels
Level 4 -> all three pixels
Level 5 -> all three pixels
Level 6 -> all three pixels
```

The fill direction is defined by the group table in Section 5. Fill order may be changed in physical configuration without changing profiles or automations.

### 12.3 Animation behavior

Recommended default render cadence:

- Static states: render immediately on change and reuse the frame.
- Animated states: 20 frames per second maximum.
- Chase step: 250 ms.
- Slow pulse: 2 to 3 seconds per cycle.
- Fast pulse: 500 ms per cycle.

Animation timing must remain configurable. Effects should remain legible rather than visually aggressive. Rapid strobing is not a default behavior.

### 12.4 Brightness

Brightness is a global multiplier applied after group pattern composition. The first implementation should expose a 5% to 100% setting and start at 50% until physical daytime/nighttime testing establishes a better default.

The renderer should avoid full-brightness white across all ten LEDs by default. Any later current limiting or per-channel calibration belongs at this final rendering stage.

### 12.5 Transitions

Status changes are immediate by default. The raw ESPHome light should retain a zero-second default transition so that interpolation does not obscure state changes or interfere with animation timing.

## 13. Control interfaces

The architecture defines semantic operations. Their exact ESPHome API syntax is an implementation detail.

### 13.1 Required operations

```text
set_group_level(group_id, level)
set_group_label(group_id, label, profile_id)
set_group_value(group_id, numeric_value, profile_id)
set_group_meta(group_id, off | unknown_unavailable | initializing)
set_group_enabled(group_id, boolean)
set_global_brightness(percent)
```

Setting a valid level, label, or value clears Initializing and Unknown/Unavailable for that group. Setting a meta-state clears the currently displayed level but may retain the last source value for diagnostics.

### 13.2 Home Assistant exposure

The recommended Home Assistant surface is semantic rather than pixel-oriented:

- one action/service for setting a group label;
- one action/service for setting a group numeric value and profile;
- one action/service for setting a meta-state;
- one global brightness number; and
- one diagnostic text sensor per group showing the resolved state.

The raw ten-pixel light and any implementation-only partitions should be marked internal so that direct light calls cannot bypass the renderer.

### 13.3 Local ESPHome sources

Local sensors may call the same semantic operations without Home Assistant. Loss of the Home Assistant connection must not stop local rendering.

## 14. ESPHome implementation boundary

### 14.1 Hardware driver

Use one internal `esp32_rmt_led_strip` light with the official hardware parameters:

```yaml
light:
  - platform: esp32_rmt_led_strip
    id: casita_pixels
    internal: true
    pin: GPIO14
    chipset: WS2812
    num_leds: 10
    rgb_order: grb
    default_transition_length: 0s
```

This is an illustrative fragment; the final package must merge safely with the upstream ESK-1 configuration and must not declare a competing driver for GPIO 14.

### 14.2 Renderer

The recommended implementation is a small renderer with direct access to the addressable frame, invoked by an ESPHome `addressable_lambda` effect or a focused custom component. It should contain:

- the fixed group-to-pixel map;
- group state storage;
- profile-independent pattern rendering;
- non-blocking animation calculations; and
- complete-frame commit behavior.

Pure YAML scripts may be used for a prototype, but duplicated per-group branching should not become the long-term architecture.

### 14.3 Partitions

ESPHome's `partition` light platform can split and combine addressable segments, including reversed segments. It is useful for diagnostics and early prototypes. It is not the preferred public control interface because independent partition entities can create multiple writers and expose implementation details.

If partitions are used internally:

- `left` uses indices 5 through 7;
- `right` uses indices 1 through 3 with reversed presentation order;
- `center` uses index 4; and
- `top` combines indices 8 through 9 with index 0.

## 15. Ownership and precedence

### 15.1 Group ownership

Each input binding owns one group's source state until it is replaced or cleared. Multiple automations must not race by writing raw pixels.

If multiple semantic sources need the same group, an adapter must arbitrate them before calling the status-board interface. Source arbitration is domain policy and is intentionally outside the renderer.

### 15.2 Display precedence

Within a group, the resolver precedence is:

```text
Off > Initializing > Unknown/Unavailable > Level1..Level6
```

This is resolution precedence, not severity. A valid Level6 does not override an explicitly disabled group.

### 15.3 Optional escalation

A profile may request that Level6 use a broader visual treatment, such as a full-group fast pulse. Full-board takeover is disabled by default because it would hide the other three groups. If added later, it must be implemented as a renderer overlay with explicit priority and timeout, not as a new meta-state.

## 16. Startup and recovery

Recommended boot sequence:

1. Initialize the raw light with all pixels off.
2. Initialize enabled groups to Initializing.
3. Start the renderer.
4. Start local sensors and Home Assistant/API connectivity.
5. Resolve each group to a level when its first valid input arrives.
6. Resolve a group to Unknown/Unavailable if initialization completes without valid data.

Displayed state should not be restored blindly across reboot. Boot must begin in Initializing so that an old good status cannot be mistaken for a current measurement.

Configuration such as group enablement, profile selection, brightness, hysteresis, and TTL may be restored when appropriate.

## 17. Failure handling

| Condition | Required result |
|---|---|
| Missing or invalid source value | Unknown/Unavailable |
| `NaN` or infinite numeric value | Unknown/Unavailable |
| Unknown label | Unknown/Unavailable plus warning log |
| Missing profile | Unknown/Unavailable plus error log |
| Invalid range profile | Reject configuration at validation time |
| Home Assistant disconnect | Continue local sources; remote-only sources eventually become Unknown/Unavailable according to TTL |
| Renderer restart | Rebuild all ten pixels from current group states |
| Group disabled | Off |

One group's failure must not erase or reset another group's display.

## 18. Observability

The implementation should expose enough diagnostics to understand a display without decoding the LEDs:

- resolved state for each group;
- active profile for each group;
- most recent source label or numeric value;
- time since the last valid update;
- global brightness;
- renderer frame/update counter; and
- warnings for invalid labels, ranges, profiles, or group IDs.

State-change logs should identify the group, previous state, new state, and source. Animation frames should not be logged.

## 19. Configuration schema

The following illustrates the desired configuration model. It is architectural pseudocode, not a promise of exact ESPHome YAML syntax.

```yaml
casita_status_board:
  brightness: 50%
  render_interval: 50ms

  groups:
    left:
      pixels: [5, 6, 7]
      fill_order: [5, 6, 7]
    right:
      pixels: [1, 2, 3]
      fill_order: [3, 2, 1]
    center:
      pixels: [4]
      fill_order: [4]
    top:
      pixels: [8, 9, 0]
      fill_order: [8, 9, 0]

  meta_states:
    off:
      effect: off
    unknown_unavailable:
      color: [255, 255, 255]
      brightness: 15%
      effect: slow_pulse
    initializing:
      color: [0, 0, 255]
      effect: chase

  levels:
    1: { color: [0, 255, 0],   fill: 1, effect: steady }
    2: { color: [255, 255, 0], fill: 2, effect: steady }
    3: { color: [255, 128, 0], fill: 3, effect: steady }
    4: { color: [255, 0, 0],   fill: 3, effect: steady }
    5: { color: [153, 0, 255], fill: 3, effect: slow_pulse }
    6: { color: [128, 0, 0],   fill: 3, effect: fast_pulse }
```

The physical map should ship as a built-in Casita layout. It may be represented in configuration for testing, but normal users should not need to specify it.

## 20. Testing strategy

### 20.1 Mapping tests

- Verify every physical LED lights individually in chain order 0 through 9.
- Verify the physical-position table against the real module.
- Verify every pixel belongs to exactly one group.
- Verify all ten pixels are covered.
- Verify the `top` group correctly wraps from index 9 to index 0.
- Verify left and right fill upward from the base.

### 20.2 State-resolution tests

For every group, verify:

- disabled resolves to Off;
- initializing resolves to Initializing;
- invalid input resolves to Unknown/Unavailable;
- each valid level resolves to Level1 through Level6;
- a new valid value exits Initializing and Unknown/Unavailable; and
- TTL expiry resolves to Unknown/Unavailable.

### 20.3 Profile tests

- Test every exact threshold boundary.
- Test values immediately below and above boundaries.
- Test negative, extremely large, `NaN`, and infinite values.
- Test aliases and unknown labels.
- Test hysteresis in both directions.
- Reject gaps, overlaps, invalid levels, and unsorted range definitions.

### 20.4 Renderer tests

- Compare complete ten-pixel frames for all nine states on all four groups.
- Verify group isolation when another group changes.
- Verify animations are non-blocking and deterministic for a supplied timestamp.
- Verify brightness scales colors without changing resolved states.
- Verify an immediate state change interrupts the previous animation.

### 20.5 Physical visual validation

Test the actual Casita module:

- in daylight and darkness;
- at 5%, 25%, 50%, and 100% brightness;
- through the intended stand/diffuser;
- from typical viewing distance and angle;
- with each adjacent level pair; and
- with users who have common color-vision deficiencies when possible.

Particular attention should be paid to yellow versus orange and red versus maroon. If they are not reliably distinguishable, adjust brightness, animation, or the calibrated RGB values without changing level semantics.

## 21. Acceptance criteria

The first implementation is complete when:

1. The four group mappings match the physical Casita layout.
2. Automations can set labels, numeric values, and the three meta-states without knowing pixel indices.
3. Valid values resolve deterministically to one of six levels.
4. Off, Unknown/Unavailable, and Initializing are visually distinct.
5. Levels remain distinguishable through redundant fill and animation cues.
6. A change to one group does not alter another group.
7. The top group's chain wrap is invisible to callers.
8. No component other than the renderer writes the raw ten-pixel frame.
9. Boot never displays a restored measurement as current before validation.
10. Invalid inputs fail visibly as Unknown/Unavailable and are logged.
11. The system continues rendering local sources without Home Assistant.
12. Physical daytime/nighttime and diffuser testing has been completed.

## 22. Proposed repository structure

```text
esk_casita_status/
  ARCHITECTURE.md
  packages/
    casita_status_board.yaml
  include/
    casita_status_board.h
  examples/
    basic_status_board.yaml
  tests/
    mapping/
    profiles/
    renderer/
  references/
    ESPHome-Starter-Kit/
```

The architecture does not require a C++ helper on day one, but the public semantic interface and tests should remain stable if a YAML prototype is later replaced by a custom component.

## 23. Decisions remaining before implementation

- Assign initial real-world domains to `left`, `right`, `center`, and `top`.
- Select the first label and numeric mapping profiles.
- Decide source-specific TTL and hysteresis values.
- Validate default RGB values and brightness on physical hardware.
- Decide whether Level6 remains group-local or may optionally request a full-board overlay.
- Decide whether a later version should coordinate the onboard buzzer with Level6.

These are configuration and product-behavior decisions. They do not change the physical grouping or semantic architecture defined here.

## 24. External references

- [ESPHome Starter Kit repository](https://github.com/ApolloAutomation/ESPHome-Starter-Kit)
- [Apollo AIR-1 firmware](https://github.com/ApolloAutomation/AIR-1/blob/main/Integrations/ESPHome/Core.yaml)
- [Apollo AIR-1 LED indicator documentation](https://wiki.apolloautomation.com/products/air1/additional-info/led-indicator/)
- [U.S. EPA/AirNow AQI basics](https://www.airnow.gov/aqi/aqi-basics/)
- [ESPHome Light Partition](https://esphome.io/components/light/partition/)
- [ESPHome addressable light effects](https://esphome.io/components/light/)

