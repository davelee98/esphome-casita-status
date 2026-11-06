# Casita Status Board Architecture

Status: Proposed  
Version: 0.18
Last updated: 2026-11-06

## 1. Purpose

This document defines the architecture for using the ESPHome Starter Kit Casita LED and buzzer module as a semantic status board.

The system presents statuses and typed numeric measurements through four logical LED groups. Callers work with group names, integer statuses, measurements, and a small set of meta-states. They do not work with physical LED numbers. A translation layer maps measurements to status integers, and a rendering layer maps those statuses to colors, fill counts, and animation across the Casita module's ten addressable RGB LEDs.

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
- Six ordered integer statuses derived from the AIR-1/EPA AQI scale.
- Three meta-states: Off, Unknown/Unavailable, and Initializing.
- Direct integer statuses and CO2-, AQI-, PM2.5-, VOC-, and NOx-to-status lookup tables.
- Group sources: sensors attached to the device, Home Assistant entities, and action-driven values.
- Fill, color, brightness, and animation rendering.
- ESPHome and Home Assistant integration boundaries.
- State precedence, transition, validation, and test requirements.

### 2.2 Out of scope for the first implementation

- Arbitrary user-facing per-pixel control.
- Decorative or entertainment lighting effects.
- User-defined physical group geometry.
- Overlapping logical groups.
- Audio/buzzer behavior. Status-board states never activate the buzzer; any buzzer feature is an independent subsystem.
- A fixed assignment of home-automation domains to the four groups. Domain assignments remain configuration choices.

## 3. Source hardware

The official ESK-1 configuration defines the Casita RGB module as a ten-pixel WS2812 strip on GPIO 14 using GRB channel order. The underlying light has zero transition time.

Upstream references, maintained by Apollo Automation under CC BY-NC-SA 4.0 and not copied into this repository:

- [Official ESK-1 ESPHome configuration](https://github.com/ApolloAutomation/ESPHome-Starter-Kit/blob/main/Integrations/ESPHome/ESK-1.yaml)
- [LED and buzzer module schematic](https://github.com/ApolloAutomation/ESPHome-Starter-Kit/blob/main/hardware/electronics/Modules/LED_And_Buzzer_Module.pdf)
- [LED and buzzer module STEP model](https://github.com/ApolloAutomation/ESPHome-Starter-Kit/blob/main/hardware/cad/pcb-placeholders/LED%20and%20Buzzer%20Module.step)
- [Upstream ESK-1 README](https://github.com/ApolloAutomation/ESPHome-Starter-Kit/blob/main/README.md)

Device configurations include the ESK-1 YAML as an ESPHome remote package pinned to an upstream commit.

## 4. Physical LED topology

PCB designators run from `LED1` through `LED10`; ESPHome addressable-light indices run from `0` through `9`. The physical data chain starts at the roof peak (`LED10`), travels clockwise down the right side, around the base, and up the left side, and ends at the upper-left roof LED. Designator numbers therefore match ESPHome indices except at the peak, where `LED10` is index `0`. Hardware testing confirmed this order on 2026-10-12; versions before 0.13 assumed the chain started at the upper-right roof, which put every pixel one position counter-clockwise of its intended place.

```text
                         LED10 / index 0

                  LED9 / 9       LED1 / 1

             LED8 / 8               LED2 / 2

             LED7 / 7               LED3 / 3

                  LED6 / 6  LED5 / 5  LED4 / 4
```

| Physical position | PCB designator | ESPHome index |
|---|---:|---:|
| Roof peak | LED10 | 0 |
| Upper-right roof | LED1 | 1 |
| Upper-right side | LED2 | 2 |
| Lower-right side | LED3 | 3 |
| Bottom-right | LED4 | 4 |
| Bottom center | LED5 | 5 |
| Bottom-left | LED6 | 6 |
| Lower-left side | LED7 | 7 |
| Upper-left side | LED8 | 8 |
| Upper-left roof | LED9 | 9 |

## 5. Logical groups

The public model contains exactly four groups. Pixel order is part of the private rendering configuration and determines how a partially filled three-LED group grows.

| Group ID | Description | PCB LEDs | ESPHome membership | Render/fill order |
|---|---|---|---|---|
| `left` | Bottom-left three | LED6, LED7, LED8 | 6, 7, 8 | 6, 7, 8; bottom to top |
| `right` | Bottom-right three | LED2, LED3, LED4 | 2, 3, 4 | 4, 3, 2; bottom to top |
| `center` | Bottom-center indicator | LED5 | 5 | 5 |
| `top` | Top three | LED9, LED10, LED1 | 9, 0, 1 | 9, 1, 0; upper-left roof, upper-right roof, then peak |

```text
                           TOP
                        LED10 / 0
                 LED9 / 9   LED1 / 1

             LEFT                  RIGHT
             LED8 / 8              LED2 / 2
             LED7 / 7              LED3 / 3

             LED6 / 6   LED5 / 5   LED4 / 4
                           CENTER
```

The `top` group wraps around the end of the WS2812 chain. That fact must remain hidden behind the renderer. Code outside the physical mapping layer must never assume that a logical group is a contiguous numeric range.

## 6. Architectural principles

### 6.1 Semantic interface

Callers set an integer status from 1 through 6, a typed numeric measurement, or a meta-state. Callers never set named normal states, pixels, or colors directly.

Examples:

```text
set_value(group="top", value=4)    # top is configured as status
set_value(group="right", value=72) # right is configured as aqi
set_unavailable(group="top")
```

### 6.2 One rendering owner

Exactly one writer owns the ten-pixel output buffer at a time. The Casita renderer owns it except while explicitly suspended. While suspended, Casita writes nothing and one other on-device component may write the addressable buffer and schedule its output, without changing the light state, effect, or brightness. Home Assistant automations, ESPHome sensors, scripts, partitions, and additional effects must not write directly to the raw addressable light.

This prevents:

- competing animations;
- partial frames;
- one group erasing another group;
- bypassing state precedence; and
- leaking physical pixel numbering into automations.

### 6.3 Validity is separate from severity

A valid measurement resolves to one of six ordered integer statuses. Off, Unknown/Unavailable, and Initializing describe the condition of the data or group, not its severity.

### 6.4 Redundant visual encoding

Color is not the only carrier of meaning. Three-LED groups also use fill count, while higher statuses and meta-states use animation. This improves readability from across a room and for users with color-vision deficiencies.

### 6.5 Fixed version 1 policy

The measurement lookup tables, palette, fill rules, effects, and animation timings are fixed component policy in version 1. Global brightness is the only exposed visual setting. The renderer operates on normalized integer statuses and does not contain domain-specific sensor logic.

## 7. System context

```text
 Local ESPHome     Home Assistant entities    Automations and     Device lifecycle,
 sensors           (homeassistant sensors)    native API actions  API connection
       |                    |                        |                   |
       +--------------------+------------------------+-------------------+
                                |
                   Source bindings and actions
                                |
                    typed value translation
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

- Source bindings and actions know how to receive data and how its freshness is judged (Section 8.5).
- Input translators know how `status`, `co2`, `aqi`, `pm25`, `voc`, and `nox` values become integers from 1 through 6.
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
1
2
3
4
5
6
```

The six normal statuses are public integers only. An implementation may use internal enum constants, but those names are not part of the configuration or automation interface. There are no additional public meta-states.

### 8.2 Source state

The resolver derives the display state from one configured input type and one source per group (Section 8.5). Every source submits values through the same internal update path.

```text
GroupRuntimeState
  display_state: signed integer
    -2 = Initializing
    -1 = UnknownUnavailable
     0 = Off
    1..6 = normal status
  source_value: optional number, diagnostics only
  last_received_at: optional monotonic timestamp
  last_valid_at: optional monotonic timestamp
  resolved_since: monotonic timestamp
```

`input_type` and the source kind are immutable group configuration rather than duplicated runtime state. The initialization deadline is derived from `resolved_since +` the group's initialization window (Section 8.5). TTL is derived from the source kind and configured type unless the group overrides it. The renderer consumes only `display_state`, so contradictory combinations of `enabled`, `valid`, `phase`, and `status` cannot exist.

The signed values are a private C++ representation, not public action values. Public diagnostics continue to use `off`, `unknown_unavailable`, `initializing`, and `1` through `6`. Age checks use rollover-safe unsigned monotonic subtraction. The component loop compares the current state and timestamps rather than scheduling expiry callbacks that could outlive the state they describe.

### 8.3 Resolution order

The resolver applies this order:

```text
display_state == 0       -> Off
display_state == -2      -> Initializing
display_state == -1      -> UnknownUnavailable
display_state in 1..6    -> integer status 1 through 6
```

Off is exclusively the result of disabled enablement. Initializing means the group is waiting for its first valid update before its per-group initialization deadline. Unknown/Unavailable means no trustworthy current value is available.

### 8.4 State transitions

```text
Boot                                   -> Initializing
Home Assistant connects (HA source)    -> Initializing
First valid input                      -> integer status 1..6
Input becomes invalid                  -> UnknownUnavailable
Source exceeds its TTL                 -> UnknownUnavailable
Home Assistant disconnects (HA source) -> UnknownUnavailable
Group explicitly disabled              -> Off
Group explicitly enabled               -> Initializing
```

Disabling a group invalidates its current status and drops value updates received while disabled. Re-enabling starts a fresh initialization window; it never restores the group's discarded status. A Home Assistant-sourced group may then submit the bound sensor's current value under the connection rules in Section 8.5. Repeated enable or disable calls that match the current enablement are no-ops and do not restart initialization.

The transitions above apply to enabled groups. Invalid input, explicit Unknown/Unavailable, TTL expiry, and Home Assistant connection changes leave disabled groups Off. At boot, enablement comes from configuration. During operation, only explicit enablement can leave Off. Renderer recovery redraws the existing group states and does not reinitialize them.

The first valid update ends Initializing. If the group's initialization deadline is reached first, the group becomes Unknown/Unavailable. Initializing is entered only by lifecycle events: boot, re-enable, or, for Home Assistant-sourced groups, a Home Assistant connection. Invalid input, explicit Unknown/Unavailable, disablement, TTL expiry, and Home Assistant disconnection transition immediately. The architecture does not expose a separate Stale state.

### 8.5 Sources and freshness

Every enabled group has exactly one source. The source kind determines how the group's value is resynchronized after a reboot or reconnect and how staleness is detected.

| Source kind | Configuration | Resync after reboot or reconnect | Staleness signal | Initialization window |
|---|---|---|---|---|
| Local sensor | `sensor:` referencing a sensor attached to this device | Next sensor publish | TTL | Greater of the global initialization timeout and the group's TTL |
| Home Assistant entity | `sensor:` referencing a `platform: homeassistant` sensor | Automatic: Home Assistant sends current entity state when it connects | Home Assistant disconnection; the entity's own `unavailable`/`unknown` state while connected | Global initialization timeout, restarted at each Home Assistant connection |
| Action-driven | No `sensor:` | None; the caller must resend | TTL | Global initialization timeout |
| Derived (`worst`) | `type: worst` with `of:` | Recomputed whenever a source group changes | None of its own; follows its sources | None of its own; follows its sources |

The source kind is inferred at configuration validation from the platform of the referenced sensor. There is no separate source key that could contradict the binding.

**Local sensors.** For sensors that publish a filtered value on every polling update, the default TTL is three times the update interval of the polling component that owns the bound sensor, resolved at configuration validation. If that interval cannot be determined, or filters or sensor behavior suppress or delay publication, the group must declare an explicit `ttl` or configuration is rejected. The explicit TTL must allow for the expected filtered publication interval so a healthy sensor does not falsely expire. A sensor that publishes `NaN` resolves immediately to Unknown/Unavailable. The initialization window is at least the TTL to allow for the expected first publication.

Python final validation resolves the sensor ID and its polling parent from the complete configuration, including nested outputs such as `scd4x.co2`. Default TTL derivation is conservative: only known polling platforms and filter configurations known to publish on every update qualify. Unknown publication behavior requires an explicit TTL. Bind Home Assistant entities directly through `platform: homeassistant`; a `copy` sensor is not inferred to be a Home Assistant source and requires an explicit TTL when its publication interval cannot be established.

**Home Assistant entities.** Values reach the device through ESPHome's `homeassistant` sensor platform, which subscribes to the entity. Home Assistant sends the entity's current state when the connection is established and on every later state change. Home Assistant publishes only on state change, so a steady valid value may legitimately go unrepeated for hours. These groups therefore have no TTL, and configuring `ttl` on them is rejected. Freshness is tied to the connection:

- "Connected" means an API client with a state subscription is connected. The ESPHome dashboard log viewer and other clients without a state subscription do not count. The component evaluates this condition in its loop and acts on edges rather than registering callbacks. Submit and re-enable paths may also evaluate it using the same edge-processing method, but correctness does not depend on detecting connection before the first sensor value arrives.
- At boot and on each detected disconnect edge, clear replay eligibility for every bound Home Assistant sensor. On disconnection, every enabled Home Assistant-sourced group becomes Unknown/Unavailable immediately; disabled groups remain Off. A Home Assistant restart therefore shows Unknown/Unavailable briefly; this is accurate and accepted.
- A sensor publication after boot or the last detected disconnect makes the sensor's latest value eligible for replay, including publications received while the connection marker is false or the group is disabled. The bound sensor continues updating independently of group enablement. While disconnected, retain the reading for replay without displaying it as current. Replay eligibility is source-binding bookkeeping, not another display state.
- On connection, every enabled Home Assistant-sourced group enters Initializing with a fresh initialization window, then immediately submits the bound sensor's latest eligible value through the normal validation path. This handles a value arriving before connection detection; initialization cannot discard it and leave the group waiting for an entity change.
- Reconnect obtains the entity's current value through the subscription, even when the entity has not changed. Re-enable during a live connection uses the same replay rule after entering Initializing. An unchanged value received before disablement remains eligible if no disconnect has been detected since its publication. A value retained from before the last detected disconnect is not accepted. If no eligible value exists, the group waits in Initializing. No forced reconnect or Home Assistant automation is required.
- While connected, the entity's `unavailable` or `unknown` state reaches the device as `NaN` and resolves to Unknown/Unavailable.

Any connected API client with a state subscription counts, including another Home Assistant instance or a script. Such a client can keep the connection marker active while the primary Home Assistant restarts; bound values still resynchronize on its reconnect.

At boot, a Home Assistant-sourced group starts Initializing with the global window. If Wi-Fi and the API are not connected before that window expires, the group becomes Unknown/Unavailable and re-enters Initializing when the connection is established.

**Action-driven values.** A group without a sensor binding receives values only through `set_group_value`, from local automations or the native API wrapper. Nothing resends these values after a reboot or reconnect; the caller is responsible for resending. Values that exist as Home Assistant entities, including template sensors and `input_number` helpers, should be bound as Home Assistant entities rather than pushed. Action-driven TTL defaults are selected by type:

| Type | Default TTL |
|---|---:|
| `status` | `never` |
| `co2` | 5 minutes |
| `pm25` | 5 minutes |
| `voc` | 5 minutes |
| `nox` | 5 minutes |
| `aqi` | 30 minutes |

**Derived groups.** A `type: worst` group takes no input. Its display state is recomputed in the core after every state change of any group: the highest status from 1 through 6 among the groups listed in `of`, ignoring Off, Unknown/Unavailable, and Initializing sources. With no such status it is Initializing while any source is Initializing, and Unknown/Unavailable otherwise. It has no TTL or initialization deadline and is skipped by the deadline check; it reaches Unknown/Unavailable only through its sources. Validation rejects `sensor` and `ttl` on it, an empty `of`, duplicates, itself, and other `worst` groups, so derived states depend only on non-derived groups and one recomputation pass is always complete.

For local-sensor and action-driven groups, a positive per-group `ttl` overrides the default. `never` is allowed only for action-driven `type: status` groups. Expiry occurs when `now - last_valid_at >= ttl` and resolves to Unknown/Unavailable.

## 9. Six-status scale

The fixed version 1 six-status rendering adopts the ordering and simplified RGB values used by Apollo AIR-1, with two exceptions tuned for gamma 2.8. AIR-1's orange, `255, 128, 0`, comes out at about 255:37 red to green and looks red-orange, close to status 4; version 0.16 uses `255, 160, 0`, about 255:69 after gamma, which reads as orange and sits between red and yellow. AIR-1's purple, `153, 0, 255`, comes out at about 61:255 red to blue and looks blue-violet; version 0.15 uses `200, 0, 255`, about 129:255 after gamma, which reads as purple and stays clearly bluer than red and maroon. The integers are ordered from lowest to highest severity, but they have no public names. Measurement-specific meaning belongs to the lookup table selected by `type`.

| Status | Color | RGB | Three-LED fill | Effect |
|---:|---|---|---:|---|
| 1 | Green | `0, 255, 0` | 1 | Steady |
| 2 | Yellow | `255, 255, 0` | 2 | Steady |
| 3 | Orange | `255, 160, 0` | 3 | Steady |
| 4 | Red | `255, 0, 0` | 3 | Steady |
| 5 | Purple | `200, 0, 255` | 3 | Slow pulse |
| 6 | Maroon | `128, 0, 0` | 3 | Fast pulse |

The one-LED `center` group cannot use fill count. Statuses 1 through 4 are steady and distinguished on `center` by color alone; statuses 5 and 6 keep their slow and fast pulses. Versions before 0.17 added brief off-notches to center statuses 2 through 4 (one to three every 6 seconds). They were removed because the periodic blinking was distracting, accepting that `center` relies on color for statuses 1 through 4.

### 9.1 Color limitations

The AIR-1 palette is retained for compatibility, but it is not sufficient as the sole encoding:

- red and maroon mainly differ in brightness;
- yellow and orange may be hard to tell apart;
- green is perceptually brighter than red or purple on typical RGB LEDs; and
- red, green, yellow, orange, and maroon may be difficult to separate for common color-vision deficiencies.

Fill, color, and animation are therefore normative parts of the fixed version 1 rendering policy, not optional decoration or configuration.

## 10. Meta-state rendering

| Meta-state | Three-LED rendering | Center rendering | Timing |
|---|---|---|---|
| Off | All pixels off | Off | Static |
| Unknown/Unavailable | All pixels white at 15% base output intensity, slow pulse | White at 15% base output intensity, slow pulse | 3-second cycle |
| Initializing | One blue (`0, 0, 255`) pixel moves through the group | Blue (`0, 0, 255`) breathing pulse | 250 ms per chase step; 2-second center pulse |

Meta-state colors, patterns, and timings are fixed version 1 component policy. They are not exposed as configuration.

The 15% white base is defined in output intensity, not as RGB `38, 38, 38` before gamma. Section 12.4 defines conversion to ESPHome pixel values.

Animations must use monotonic time and must not use blocking delays. A new state must take effect on the next render frame.

## 11. Typed values and lookup tables

Every group configures exactly one input `type`. A normal update therefore contains only one numeric `value`; the group's configured type determines whether that value is already a status or must be translated through a lookup table.

| Type | Input | Behavior |
|---|---|---|
| `status` | exact integer `1` through `6` | Use the integer directly; do not run a lookup. |
| `co2` | finite CO2 value in ppm | Translate through the CO2 lookup table. |
| `aqi` | finite U.S. EPA AQI value | Translate through the AQI lookup table. |
| `pm25` | finite PM2.5 concentration in µg/m³ | Translate through the PM2.5 lookup table. |
| `voc` | finite Sensirion VOC Index | Translate through the VOC lookup table. |
| `nox` | finite Sensirion NOx Index | Translate through the NOx lookup table. |
| `worst` | none | No value; derived from other groups' resolved statuses (Section 8.5). |

The public interface does not accept named normal states. Any string supplied as a normal status is invalid. Off, Unknown/Unavailable, and Initializing remain separate resolved meta-states. Off is controlled through enablement, Unknown/Unavailable has an explicit invalidation action, and Initializing is lifecycle-controlled only.

`aqi` accepts an already-computed AQI value, not a raw pollutant concentration. `pm25` accepts a raw PM2.5 concentration in µg/m³ and maps it directly to the AQI category bands, so no AQI number is computed. `voc` and `nox` mean the Sensirion VOC and NOx Indexes on their 1 through 500 scales, not TVOC in ppb or NOx concentrations. Supporting another VOC or NOx representation requires another explicit type or conversion before calling this component.

### 11.1 Fixed lookup model

Version 1 ships one fixed lookup table for each measurement type. Lookup tables are compiled component policy, not user configuration. All groups using the same type therefore share the same thresholds. Adding custom or per-group tables later must not change the public action's `group` and `value` fields.

Inputs are real numbers and bands are evaluated using the explicit continuous predicates below. Every accepted input produces an integer status from 1 through 6. `NaN`, infinity, parse failures, missing values, negative CO2/AQI/PM2.5 values, and VOC or NOx Index values outside 1 through 500 resolve immediately to Unknown/Unavailable.

### 11.2 Built-in lookup tables

The CO2, AQI, and VOC tables follow the current Apollo AIR-1 indicator behavior. The PM2.5 table uses the U.S. EPA 2024 PM2.5 breakpoints, which fall on the same category boundaries as the AQI table. The NOx table is project policy: AIR-1 has no NOx indicator, and the Sensirion NOx Index rests at 1 in clean air, so its bands sit much lower than the VOC bands.

| Status | CO2 in ppm | U.S. EPA AQI | Sensirion VOC Index |
|---:|---|---|---|
| 1 | `x <= 800` | `0 <= x <= 50` | `1 <= x < 80` |
| 2 | `800 < x <= 1000` | `50 < x <= 100` | `80 <= x < 150` |
| 3 | `1000 < x <= 1500` | `100 < x <= 150` | `150 <= x < 250` |
| 4 | `1500 < x <= 2000` | `150 < x <= 200` | `250 <= x < 400` |
| 5 | `2000 < x <= 2500` | `200 < x <= 300` | `400 <= x <= 500` |
| 6 | `x > 2500` | `x > 300` | not used by the fixed table |

| Status | PM2.5 in µg/m³ (after truncation) | Sensirion NOx Index |
|---:|---|---|
| 1 | `0 <= x <= 9.0` | `1 <= x < 20` |
| 2 | `9.0 < x <= 35.4` | `20 <= x < 50` |
| 3 | `35.4 < x <= 55.4` | `50 <= x < 100` |
| 4 | `55.4 < x <= 125.4` | `100 <= x < 250` |
| 5 | `125.4 < x <= 225.4` | `250 <= x <= 500` |
| 6 | `x > 225.4` | not used by the fixed table |

Finite non-negative CO2 values are accepted and values above the final threshold remain status 6. Finite non-negative AQI values are accepted; values above 500 are beyond the formal AQI scale but remain status 6 for display. Following EPA practice, PM2.5 values are truncated to 0.1 µg/m³ before banding, so 9.05 is status 1; finite non-negative values are accepted and values above 225.4 remain status 6. VOC and NOx Index values outside 1 through 500 are invalid. The CO2 table is display policy rather than a universal health scale.

### 11.3 Stateless translation

Every valid input translates and commits immediately. The first implementation does not implement hysteresis, minimum dwell, or pending candidate states. Noisy sensors should use ESPHome sensor filters on the bound sensor, or smoothing upstream of an action call. Invalid input, TTL expiry, explicit invalidation, disablement, and lifecycle reinitialization also transition immediately, with the result visible on the next render frame.

## 12. Rendering pipeline

### 12.1 Frame production

The renderer produces a complete ten-pixel frame on every animated update:

1. Clear an in-memory ten-pixel frame.
2. Resolve the current pattern for each group.
3. Apply the pattern using that group's render order.
4. Apply global brightness in output intensity.
5. Encode the frame for ESPHome's gamma correction and commit the complete frame to the addressable light.

All four groups are disjoint, so normal group rendering cannot conflict. Future overlays must be composed in this single pipeline rather than writing independently to the LEDs.

### 12.2 Fill behavior

For a three-LED group, the fixed active count is:

```text
Status 1 -> first pixel in render order
Status 2 -> first two pixels
Status 3 -> all three pixels
Status 4 -> all three pixels
Status 5 -> all three pixels
Status 6 -> all three pixels
```

The fill direction and order are fixed by the group table in Section 5 and are not user configuration.

### 12.3 Animation behavior

Fixed version 1 render cadence and timing:

- Static states: render immediately on change and reuse the frame.
- Animated states: render at 20 frames per second.
- Chase step: 250 ms.
- Slow pulse: 3 seconds per cycle.
- Fast pulse: 500 ms per cycle.
- Initializing center breathing pulse: 2 seconds per cycle.

These values are compiled constants, not schema options. Effects remain legible rather than visually aggressive, and version 1 provides no rapid-strobe effect.

Slow pulse, fast pulse, and center breathing use a triangular output-intensity wave from 25% to 100% of the state's base output. The phase begins at 100% when the state is entered, making a new state visible immediately. The Unknown/Unavailable pulse applies that wave to its 15% white base output intensity. Global brightness is applied after these per-state intensity calculations. These multipliers are encoded as described in Section 12.4 so gamma does not turn the intended pulse trough into an off interval.

### 12.4 Brightness

Brightness is a global multiplier on output intensity applied after group pattern composition. It is the only exposed visual setting. Version 1 accepts 25% through 100% and defaults to 50%. The 25% floor is provisional until the physical validation in Section 20; it is a compiled limit, not another configuration option.

ESPHome retains its default gamma of 2.8 and the underlying light remains at 100% brightness. The Section 9 RGB palette stays in ESPHome's pre-gamma color domain, preserving its full-brightness AIR-1 appearance. Global brightness and pulse depth are defined in the output domain. For a normalized palette channel `c`, brightness `b`, and pulse multiplier `p` (`1` for steady states), the ideal conversion is:

```text
base_output   = c^2.8
target_output = base_output * b * p
pixel_input   = target_output^(1 / 2.8)
              = c * (b * p)^(1 / 2.8)
```

For Unknown/Unavailable, use `base_output = 0.15` on each RGB channel. Off and unfilled pixels have `target_output = 0`. The component applies the multipliers once and encodes the result before assigning pixels; ESPHome then applies its normal gamma correction. Version 1 exposes no gamma override or per-channel calibration.

Use sufficient intermediate precision and quantize only when writing the encoded channel values. Validate the resulting driver bytes through the supported ESPHome gamma/output path, including its integer rounding, rather than testing only the ideal formula. At the provisional 25% brightness floor and 25% pulse trough, maroon and Unknown/Unavailable each target roughly two output counts per active channel. Their active pulse troughs must remain nonzero in the supported output paths; physical readability still requires hardware validation.

The renderer avoids full-brightness white across all ten LEDs. Version 1 does not expose per-channel calibration.

### 12.5 Transitions

Status changes are immediate. The raw ESPHome light retains a zero-second transition so that interpolation does not obscure state changes or interfere with animation timing.

## 13. Control interfaces

The architecture defines semantic operations. Their exact ESPHome API syntax is an implementation detail.

### 13.1 Required operations

```text
set_group_value(group_id, numeric_value)
set_group_unavailable(group_id)
set_group_enabled(group_id, boolean)
set_global_brightness(percent)
run_test()
```

Local ESPHome automations also expose `casita_status.suspend` and `casita_status.resume`, accepting either a component ID directly or an `id:` mapping. These synchronous actions hand over output ownership; they are not Home Assistant services, group operations, or display states. Put them first in the other component's start and finish automations, before any delay or deferred work.

`casita_status.run_test` starts a fixed self-test, accepting either a component ID directly or an `id:` mapping. It is intended for a template button in the device YAML and is not a Home Assistant service. The sequence is fixed policy: a white dot walks the chain from index 0 through 9 at 400 ms per pixel; every group then shows statuses 1 through 6 for 2 seconds each; finally every group shows Initializing and then Unknown/Unavailable for 3 seconds each, about 22 seconds in total. The test lights every group, including disabled ones, so the module and LED order can be checked by eye. It overlays only the rendered frame: group states, sensor updates, deadlines, and diagnostics continue underneath, and the next frame after the test shows the current states. Calling it again restarts the sequence. It is ignored with a warning while the renderer is suspended, and it is not persisted across reboot.

`set_group_value` is the sole normal-state operation for action-driven groups. The group configuration supplies its type. Calling `set_group_value` on a sensor-bound group is rejected with a warning log, because an action value would bypass that group's freshness rules and be silently overwritten by the next sensor publish. `set_group_unavailable` and `set_group_enabled` apply to every group regardless of source, except that a `worst` group, whose state always reflects its sources, rejects `set_group_unavailable` (and `set_group_value`) with a warning. For a group configured as `status`, the action accepts a finite numeric value whose mathematical value is integral and within 1 through 6; `4` and `4.0` are valid, while `4.0001`, strings, and booleans are invalid. Groups configured as `co2`, `aqi`, `pm25`, `voc`, or `nox` translate the value through the corresponding lookup table.

Action-driven groups accept the same semantic actions from local ESPHome automations and Home Assistant. If multiple producers target one action-driven group, the last call processed on the ESPHome main loop wins. Applications that need priority, timestamps, or arbitration must implement that policy upstream.

Setting a valid value clears Initializing and Unknown/Unavailable for that group. `set_group_unavailable` invalidates the current status and may retain the last source value and type only as explicitly historical diagnostics. No public action sets Initializing. `set_group_enabled(false)` is the sole way to produce Off.

On a sensor-bound enabled group, explicit unavailability lasts until the next sensor publish; it is not a persistent override. Unknown group IDs are rejected with a warning and leave all group states unchanged.

### 13.2 Home Assistant exposure

Home Assistant entity values reach the device by subscription, not by push. A group bound to a `platform: homeassistant` sensor needs no Home Assistant automation and resynchronizes automatically on every connection (Section 8.5). Subscribing to entity state does not require granting the device permission to perform Home Assistant actions.

```yaml
sensor:
  - platform: homeassistant
    id: ha_outdoor_aqi
    entity_id: sensor.outdoor_aqi
```

The native API actions remain for action-driven groups, typically statuses computed in Home Assistant automations. They do not resynchronize after a reboot or reconnect. An action-driven `status` group with the default `ttl: never` retains its last status when Home Assistant disconnects; use a positive TTL when that value must expire, or bind an existing Home Assistant entity by subscription.

The Home Assistant surface is semantic rather than pixel-oriented:

- one native API action for setting an action-driven group's numeric value;
- one native API action for setting a group to Unknown/Unavailable;
- one native API action for enabling or disabling a group;
- one global brightness number; and
- diagnostic entities described in Section 18.

The external component registers synchronous ESPHome automation actions. Those component actions do not become Home Assistant actions automatically. The integration package must declare explicit native API wrappers that validate their wire fields and forward to the component actions. For the upstream ESK-1 integration, append wrappers under `api.services`, the supported legacy spelling already used by its buzzer service. Do not add a simultaneous `api.actions` key: the supported API schema makes the two keys mutually exclusive. Preserve the upstream service while merging the wrappers. Local ESPHome automations call the same component actions directly.

The normal-value API wrapper contains exactly two fields: `group` and numeric `value`. It does not carry a type selector; changing a group's interpretation requires changing configuration and rebooting the device.

Resolved-state text uses the stable wire strings `off`, `unknown_unavailable`, `initializing`, and `1` through `6`. Before component initialization, diagnostics publish no state rather than inventing a tenth display state.

The raw ten-pixel light and any implementation-only partitions must be marked internal so that direct light calls cannot bypass or stop the renderer.

### 13.3 Local ESPHome sources

Sensors attached to the device are bound to a group by ID. The component subscribes to the sensor's state callback; no `on_value` automation is needed. Loss of the Home Assistant connection must not stop local rendering or invalidate local-sensor groups.

```yaml
sensor:
  - platform: scd4x
    update_interval: 60s
    co2:
      id: bedroom_co2

casita_status:
  groups:
    left:
      type: co2
      sensor: bedroom_co2   # default TTL = 3 x 60s
```

Noise handling belongs in the bound sensor's ESPHome filters. The group receives the filtered value. Filters that suppress or delay publication require an explicit group TTL as described in Section 8.5.

## 14. ESPHome implementation boundary

### 14.1 Hardware driver

The upstream ESK-1 configuration already declares the Casita strip as `rgb_module`. The status-board package must extend that declaration by ID; it must not declare another `esp32_rmt_led_strip` instance or another GPIO 14 driver.

```yaml
light:
  - id: !extend rgb_module
    internal: true
    restore_mode: ALWAYS_OFF
    default_transition_length: 0s
    effects:
      - addressable_lambda:
          name: Casita Status Renderer
          update_interval: 50ms
          lambda: |-
            id(casita_board).render(it, initial_run);
```

This fragment illustrates the required merge mechanism. Extending the existing light preserves its official GPIO 14, ten-pixel WS2812, GRB, and `rmt_symbols: 48` configuration. The package does not override `gamma_correct`; the light keeps ESPHome's default gamma of 2.8, matching the upstream ESK-1 and AIR-1 behavior that the fixed palette assumes. `restore_mode: ALWAYS_OFF` keeps the output dark during setup, avoiding a full-white frame before the renderer starts. The component turns the light on and starts the named renderer effect after both it and the light are ready and keeps the underlying light at 100% brightness. The component applies and encodes global brightness exactly once as defined in Section 12.4.

The package must verify at configuration/compile time where possible, and at runtime otherwise, that the referenced output is addressable and contains exactly ten pixels. A mismatch fails visibly and logs an error rather than writing an unknown buffer.

### 14.2 External component and renderer

The production implementation is a local ESPHome external component under `components/casita_status/`. Its Python module owns schema validation, ID references, code generation, and synchronous automation-action registration. Its C++ class owns state resolution, fixed lookup translation, TTL timing, animation, diagnostics, and complete-frame rendering.

The minimum supported ESPHome release is 2026.3.0. The component's Python validation enforces this minimum independently of the YAML `esphome.min_version` value, which can be replaced during package merging. Automation actions declare `synchronous=True`, and templatable primitive arguments use the current documented code-generation path. CI also compiles against current stable ESPHome so later API changes are caught promptly.

The addressable-lambda fragment above is only a thin, package-owned bridge that supplies ESPHome's existing addressable buffer to the C++ renderer. It does not contain policy or duplicate rendering logic. The renderer contains:

- the fixed group-to-pixel map;
- group state storage;
- lookup-independent pattern rendering;
- non-blocking animation calculations; and
- complete-frame commit behavior.

The raw light is internal, the bridge is the only configured effect, and the component treats an unexpectedly stopped or replaced bridge as a fault: it logs the event and restores the renderer effect on the next component loop. Rendering is a no-op until the light reports ready, preventing early-boot access to an uninitialized output.

Suspension leaves the bridge effect and renderer recovery check running. The renderer returns before consuming dirty state or writing pixels; sensor updates, deadlines, connection handling, diagnostics, and brightness changes continue normally. Resume invalidates the cached frame so the next render redraws all current group states, including unchanged steady states. Repeated suspend or resume calls are no-ops. The other component must suspend before its first buffer write and resume after its last, with no idle writes. It supplies its own pixel levels because Casita brightness affects only Casita frames; the underlying light remains at 100%.

### 14.3 Partitions

The production package does not create light partitions. Partitions would introduce additional writers and expose the physical layout. Mapping diagnostics operate through renderer-owned test methods that still compose and commit the complete ten-pixel frame. The `run_test` self-test (Section 13.1) is one such method.

## 15. Update ordering and state mutation

### 15.1 Single update path

Each enabled group has exactly one configured input type and one source. Bound-sensor state callbacks, local component actions, and native API wrappers all converge on one internal submit path that validates, translates, and commits the value. Source kind affects only which producers a group accepts and how freshness is judged, not how a value is translated or rendered.

All updates are serialized on the ESPHome main loop, and the renderer observes a coherent snapshot. A sensor-bound group accepts values only from its bound sensor. Home Assistant sensor publications update replay eligibility even before connection detection; they change the display only while connected and enabled. On a detected connect, initialization is followed by replay through this same submit path (Section 8.5). For action-driven groups, the last processed call wins. The component does not arbitrate between producers or track priority.

### 15.2 Display-state mutation

There is no precedence stack or collection of state flags. Each group has one `display_state` field, and operations replace it atomically on the ESPHome main loop:

```text
disable                                  ->  0  (Off)
boot, enable, HA connect                 -> -2  (Initializing)
invalid, expired, absent, HA disconnect  -> -1  (UnknownUnavailable)
valid translated value                   -> 1..6
```

Updates received while `display_state == 0` are dropped from group state. The bound Home Assistant sensor continues to update independently for re-enable recovery (Section 8.5). Invalidation, expiry, and connection lifecycle operations also preserve `0`; during operation, only explicit enablement can leave Off. This single-field representation makes it impossible for a group to be simultaneously disabled, initializing, invalid, and valid.

### 15.3 Status 6 scope

Status 6 remains local to its assigned group. It never produces a full-board overlay or hides another group's state. No status, including status 6, activates the buzzer.

## 16. Startup and recovery

Suspension is not persisted: every boot starts unsuspended. There is no automatic timeout; the caller must resume after handing back the buffer. Resume forces a full redraw without resetting group states.

Recommended boot sequence:

1. Initialize the raw light with all pixels off.
2. Apply configured group enablement: enabled groups start Initializing and disabled groups start Off.
3. Force `prevent_sleep` on and prevent automatic sleep through `deep_sleep_1` during component setup.
4. Start local sensors and Home Assistant/API connectivity.
5. Have the component start the renderer bridge after it and `rgb_module` are ready.
6. Resolve each group to an integer status when its first valid input arrives.
7. Resolve a group to Unknown/Unavailable when its initialization window (Section 8.5) expires without valid data.
8. When Home Assistant connects, enter Initializing for every enabled Home Assistant-sourced group, then replay any eligible sensor value; otherwise await the subscription's current entity state.

Measurements and resolved statuses are never restored across reboot. Enabled groups begin in Initializing so that an old good status cannot be mistaken for a current measurement.

Each group's configured `enabled` value defaults to true. Runtime enablement changes are not persisted: every device boot reapplies configured enablement. TTL overrides are configuration, not runtime restored state. Global brightness is persisted and restored on reboot; clamp a restored value to 25% through 100%. If no valid saved brightness exists, use the configured brightness (50% by default). Action-driven values are not restored; their callers must resend them.

The first implementation is an always-awake status board. Its package extends the upstream `prevent_sleep` switch to make it internal and forced on. This keeps the ESK-1 `on_client_connected` automation out of its manual `deep_sleep.enter` branch; manual sleep bypasses the prevent flag. The component also calls `prevent_deep_sleep()` on the existing `deep_sleep_1` during setup to block its automatic run-duration timer. Both protections are required. Sleep-enabled operation is out of scope because LEDs, animation, API actions, and TTL evaluation do not run during deep sleep.

```yaml
# Integration intent; final syntax must validate against the supported ESPHome release.
switch:
  - id: !extend prevent_sleep
    internal: true
    restore_mode: ALWAYS_ON
```

The component accepts an optional `deep_sleep_id`. The ESK-1 package supplies `deep_sleep_1`, and component code generation binds it for the setup call. Configurations without deep sleep omit the ID and skip that call; the component does not require or create a deep-sleep instance. No additional `on_boot` mapping is needed for this protection, so the upstream boot automation remains intact. Any other added boot automation must use list form compatible with the upstream list. The example's final main configuration declares `esphome.min_version: 2026.3.0` for clarity; component validation remains authoritative. Validate the fully merged configuration, with ESK-1 included before the status-board package, rather than validating either fragment in isolation. The upstream ESK-1 YAML is included as a pinned remote package and is never modified.

## 17. Failure handling

| Condition | Required result |
|---|---|
| Missing or invalid source value | Unknown/Unavailable |
| `NaN` or infinite numeric value | Unknown/Unavailable |
| Unknown configured input type | Reject configuration at validation time |
| Unknown group ID in an action | Reject action plus warning log; all group states unchanged |
| Non-integral or out-of-range `status` value | Unknown/Unavailable plus warning log |
| Attempt to set Initializing through a public action | Reject action at validation or dispatch time |
| Update received while disabled | Drop update plus diagnostic log; remain Off |
| `set_group_value` on a sensor-bound group | Reject action plus warning log; state unchanged |
| Bound sensor ID missing or not a numeric sensor | Reject configuration at validation time |
| `ttl` configured on a Home Assistant-sourced group | Reject configuration at validation time |
| Local-sensor TTL cannot be derived and no `ttl` given | Reject configuration at validation time |
| Initialization deadline exceeded | Unknown/Unavailable |
| Home Assistant disconnect | Enabled Home Assistant-sourced groups become Unknown/Unavailable immediately; disabled groups remain Off; local-sensor groups are unaffected; action-driven groups follow their TTL |
| Home Assistant reconnect or group re-enable | Enabled Home Assistant-sourced groups re-enter Initializing and recover the current entity value under Section 8.5, even when unchanged; disabled groups remain Off on reconnect |
| Home Assistant entity `unavailable` or `unknown` | Unknown/Unavailable |
| Renderer restart | Rebuild all ten pixels from current group states |
| Renderer effect stopped or replaced | Log fault and restore it on the next component loop |
| Addressable output is not ten pixels | Refuse rendering and log an error |
| Group disabled | Off |

One group's failure must not erase or reset another group's display.

## 18. Observability

The implementation exposes a minimal diagnostic set for each group, marked `entity_category: diagnostic` and disabled by default except for resolved state:

- resolved meta-state or integer status for each group;
- configured input type;
- source kind and bound sensor ID;
- Home Assistant state-subscription connection status;
- most recent numeric source value;
- time since the last valid update and TTL remaining;
- invalid/rejected update counter;
- global brightness;
- renderer frame/update counter; and
- warnings for invalid types, values, or group IDs.

The required Home Assistant surface is the resolved-state text sensor. Other diagnostics may be entities or log-only values in the first implementation, but all must appear in `dump_config()` or diagnostic logs. Retained type/value metadata is labeled historical whenever the resolved state is Off, Initializing, or Unknown/Unavailable.

State-change logs identify the group, previous state, new state, input type, source kind, and reason. Animation frames are not logged.

## 19. Configuration schema

The following illustrates the desired configuration model. It is architectural pseudocode, not a promise of exact ESPHome YAML syntax.

```yaml
casita_status:
  id: casita_board
  light_id: rgb_module
  deep_sleep_id: deep_sleep_1   # optional component binding; supplied by ESK-1 package
  brightness: 50%
  initialization_timeout: 30s

  groups:
    left:
      type: co2
      sensor: bedroom_co2      # local sensor: TTL = 3 x update interval
    right:
      type: aqi
      sensor: ha_outdoor_aqi   # platform: homeassistant; connection-based freshness
    center:
      type: voc
      sensor: bedroom_voc
      ttl: 10min               # explicit override
    top:
      type: status             # action-driven; no sensor
      enabled: true            # optional; defaults to true
```

The physical map, colors, fills, effects, animation timings, and 50 ms animated render interval ship as built-in Casita policy and are not user configuration. `brightness` is the only visual option.

Every configured group declares exactly one type and at most one `sensor`. A `type: worst` group instead lists the groups it follows in `of` and takes no `sensor` or `ttl`. The source kind is inferred from the bound sensor's platform (Section 8.5). Per-group `enabled` defaults to true and determines boot enablement; runtime changes are not persisted. The global `initialization_timeout` defaults to 30 seconds and determines each group's initialization window together with its source policy. `ttl` is optional for local-sensor and action-driven groups and is rejected on Home Assistant-sourced groups; when omitted, it uses the defaults in Section 8.5 where they can be safely derived. Brightness accepts 25% through 100% with a 50% default, subject to physical validation of the provisional floor.

## 20. Testing strategy

The C++ core is unit-tested on the host with an injected clock. Python validation and merged ESPHome integration checks live in `tests/integration/`. Only these tests are required:

1. **Mapping:** all ten pixels belong to exactly one group, fill order matches Section 5, and `top` wraps from index 9 to 0 (upper-left roof to peak).
2. **Lookups:** every threshold in Section 11.2 lands in the correct band, and `NaN`, infinity, and out-of-range values resolve to Unknown/Unavailable.
3. **State:** boot, disable/re-enable, initialization timeout, TTL expiry (including counter rollover), and `status` validation (`4.0` accepted, `4.0001` rejected) produce the states in Section 8. Disabled groups remain Off through invalidation, expiry, and Home Assistant connection changes. Repeated enablement calls are no-ops, and a device reboot reapplies configured enablement. Verify brightness restoration, clamping to 25–100%, and fallback to configured brightness when no valid saved value exists.
4. **Sources:** Home Assistant disconnect invalidates only enabled Home Assistant-sourced groups; reconnect and re-enable recover an unchanged entity's current value; and `set_group_value` on a sensor-bound group is rejected. Test a sensor publication before connection detection: it does not display as current while disconnected, but connect enters Initializing and immediately replays it. Replay accepts values published since boot or the last detected disconnect, including while disabled, and rejects older retained values. A healthy filtered local sensor with a suitable explicit TTL does not falsely expire.
5. **Renderer:** host tests compare golden ten-pixel frames for each state on one three-LED group and on `center`, and verify that a change to one group leaves the others' pixels unchanged. Renderer recovery redraws the existing states. The self-test walks one pixel at a time in chain order, shows each status and meta-state on every group including disabled ones, leaves group states updating underneath, and hands back to the live frame when it ends. Host tests verify the component's encoded frame; the actual ESPHome gamma/output check runs on-device in item 8.
6. **Validation:** Python or expected configuration-failure tests enforce the minimum ESPHome version independently of package `min_version`, reject invalid bindings, and require an explicit TTL when local sensor publication cannot be safely inferred. Test a known nested polling output, a publication-suppressing filter, and a configuration without a deep-sleep binding.
7. **Integration/build:** the fully merged ESK-1 example compiles for ESP32-C6 on the minimum and current ESPHome releases, with one GPIO 14 driver, `rgb_module` internal, one renderer bridge that the component starts, and both sleep protections applied. Verify that the upstream boot automation and buzzer service remain present, and that native API wrappers use one compatible services/actions key. The existing device-check configuration exercises suspend and resume in short and long forms after its output observer finishes; existing validation and builds cover their schema and code generation.
8. **Device output and physical:** on-device checks with the minimum and current ESPHome test firmware inspect the driver buffer after normal pixel assignment and gamma correction. At minimum brightness, active pulse troughs for maroon and Unknown/Unavailable remain nonzero, while Off pixels remain zero and steady statuses on `center` never go dark. View each adjacent status pair and the meta-states on the module at minimum and default brightness. Confirm readable pulse troughs and that Unknown/Unavailable is distinct from Off. Validate the provisional 25% brightness floor; revise compiled visual policy through an architecture update if readability is unacceptable. On a combined device, render unchanged steady status-1 inputs, run Fortune, and confirm an undisturbed animation followed promptly by the same steady statuses. Check for a white flash or unexpected renderer-recovery warnings.

## 21. Acceptance criteria

The first implementation is complete when:

1. The four group mappings match the physical Casita layout.
2. Automations can set a direct integer status, a typed CO2/AQI/PM2.5/VOC/NOx value, Unknown/Unavailable, or enabled/Off without knowing pixel indices; Initializing remains lifecycle-only.
3. Every valid typed value resolves deterministically to an integer status from 1 through 6.
4. Off, Unknown/Unavailable, and Initializing are visually distinct.
5. On three-LED groups, statuses remain distinguishable through fill or animation in addition to color. On `center`, statuses 5 and 6 are distinguished by animation; statuses 1 through 4 rely on color.
6. A change to one group does not alter another group.
7. The top group's chain wrap is invisible to callers.
8. Exactly one component writes the raw ten-pixel frame at a time. Another on-device component may write only while Casita is explicitly suspended, without changing light state, effect, or brightness.
9. Boot never displays a restored measurement as current before validation.
10. Invalid inputs fail visibly as Unknown/Unavailable and are logged.
11. The system continues rendering local sources without Home Assistant.
12. Home Assistant-sourced groups show current entity values after a device reboot, a Home Assistant restart, or a reconnect, without any Home Assistant automation.
13. Physical readability testing on the module has been completed.
14. The compiled configuration contains exactly one GPIO 14 driver, keeps `rgb_module` internal, and remains continuously awake.
15. Global brightness is the only exposed visual setting; the version 1 palette, fills, effects, and timings cannot be overridden through configuration.

## 22. Proposed repository structure

```text
esphome-casita-status/
  ARCHITECTURE.md
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
  LICENSE
```

The Python/C++ external component is the production implementation from day one. YAML remains a thin integration layer for extending `rgb_module`, declaring local and `homeassistant` sensors for groups to bind, bridging native API actions, preventing deep sleep, and selecting configuration values.

## 23. Decisions remaining before implementation

- Assign initial real-world domains to `left`, `right`, `center`, and `top`.
- Validate the fixed RGB values, animation timings, default brightness, and provisional 25% minimum brightness on physical hardware.

These are configuration and product-behavior decisions. They do not change the physical grouping or semantic architecture defined here.

## 24. External references

- [ESPHome Starter Kit repository](https://github.com/ApolloAutomation/ESPHome-Starter-Kit)
- [Apollo AIR-1 firmware](https://github.com/ApolloAutomation/AIR-1/blob/main/Integrations/ESPHome/Core.yaml)
- [Apollo AIR-1 LED indicator documentation](https://wiki.apolloautomation.com/products/air1/additional-info/led-indicator/)
- [U.S. EPA/AirNow AQI basics](https://www.airnow.gov/aqi/aqi-basics/)
- [ESPHome external components](https://esphome.io/components/external_components/)
- [ESPHome component automation actions](https://developers.esphome.io/architecture/components/automations/)
- [ESPHome deep sleep](https://esphome.io/components/deep_sleep/)
- [ESPHome Home Assistant sensor](https://esphome.io/components/sensor/homeassistant/)
- [ESPHome native API](https://esphome.io/components/api/)
- [ESPHome Light Partition](https://esphome.io/components/light/partition/)
- [ESPHome addressable light effects](https://esphome.io/components/light/)
