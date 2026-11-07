#pragma once

#include "casita_core.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/components/light/addressable_light.h"
#include "esphome/components/light/addressable_light_effect.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/number/number.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#ifdef USE_DEEP_SLEEP
#include "esphome/components/deep_sleep/deep_sleep_component.h"
#endif

namespace esphome::casita_status {
class CasitaStatus;
class BrightnessNumber final : public number::Number {
 public:
  void set_parent(CasitaStatus *parent) { parent_ = parent; }
 protected:
  void control(float value) override;
  CasitaStatus *parent_{nullptr};
};
class CasitaStatus final : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }
  void set_light(light::LightState *light) { light_ = light; }
  void set_brightness(float brightness) { configured_brightness_ = brightness; }
  void set_preference_key(uint32_t key) { preference_key_ = key; }
  void set_brightness_number(BrightnessNumber *number) { brightness_number_ = number; }
#ifdef USE_DEEP_SLEEP
  void set_deep_sleep(deep_sleep::DeepSleepComponent *sleep) { deep_sleep_ = sleep; }
#endif
  void configure_group(uint8_t group, InputType type, SourceKind source, bool enabled, uint32_t ttl,
                       uint32_t initialization, sensor::Sensor *sensor, const char *sensor_id,
                       text_sensor::TextSensor *diagnostic, uint8_t worst_of);
  void render(light::AddressableLight &output, bool initial_run);
  void set_group_value(const std::string &group, double value);
  void set_group_unavailable(const std::string &group);
  void set_group_enabled(const std::string &group, bool enabled);
  void set_global_brightness(float percent);
  void set_suspended(bool suspended);
  void run_test();
 protected:
  void sensor_value_(uint8_t group, float value);
  void connection_(uint32_t now);
  void report_(const char *reason);
  bool validate_output_();
  void ensure_renderer_();
  int resolve_group_(const std::string &name);
  Board board_;
  light::LightState *light_{nullptr};
  light::AddressableLight *output_{nullptr};
  BrightnessNumber *brightness_number_{nullptr};
#ifdef USE_DEEP_SLEEP
  deep_sleep::DeepSleepComponent *deep_sleep_{nullptr};
#endif
  std::array<sensor::Sensor *, 4> sensors_{};
  std::array<const char *, 4> sensor_ids_{{"none", "none", "none", "none"}};
  std::array<text_sensor::TextSensor *, 4> diagnostics_{};
  std::array<int8_t, 4> reported_states_{{10, 10, 10, 10}};
  ESPPreferenceObject brightness_preference_;
  float configured_brightness_{0.5f};
  uint32_t preference_key_{0}, frames_{0};
  bool initialized_{false}, renderer_started_{false}, frame_valid_{false};
  bool suspended_{false};
};

// The light's only effect (`- casita_status:` under the light's effects). ESPHome runs it while the light is
// on; every 50 ms it hands the pixel buffer to CasitaStatus::render().
class CasitaRendererEffect final : public light::AddressableLightEffect {
 public:
  explicit CasitaRendererEffect(const char *name) : AddressableLightEffect(name) {}
  void set_parent(CasitaStatus *parent) { parent_ = parent; }
  void start() override { initial_run_ = true; }
  void apply(light::AddressableLight &it, const Color &current_color) override;
 protected:
  CasitaStatus *parent_{nullptr};
  uint32_t last_run_{0};
  bool initial_run_{true};
};

template<typename... Ts> class SetSuspendedAction final : public Action<Ts...> {
 public:
  SetSuspendedAction(CasitaStatus *parent, bool suspended) : parent_(parent), suspended_(suspended) {}
  void play(const Ts &...x) override { parent_->set_suspended(suspended_); }
 protected:
  CasitaStatus *parent_;
  bool suspended_;
};
template<typename... Ts> class RunTestAction final : public Action<Ts...> {
 public:
  explicit RunTestAction(CasitaStatus *parent) : parent_(parent) {}
  void play(const Ts &...x) override { parent_->run_test(); }
 protected:
  CasitaStatus *parent_;
};
template<typename... Ts> class SetGroupValueAction final : public Action<Ts...> {
 public:
  explicit SetGroupValueAction(CasitaStatus *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(std::string, group)
  TEMPLATABLE_VALUE(double, value)
  void play(const Ts &...x) override { parent_->set_group_value(group_.value(x...), value_.value(x...)); }
 protected:
  CasitaStatus *parent_;
};
template<typename... Ts> class SetGroupUnavailableAction final : public Action<Ts...> {
 public:
  explicit SetGroupUnavailableAction(CasitaStatus *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(std::string, group)
  void play(const Ts &...x) override { parent_->set_group_unavailable(group_.value(x...)); }
 protected:
  CasitaStatus *parent_;
};
template<typename... Ts> class SetGroupEnabledAction final : public Action<Ts...> {
 public:
  explicit SetGroupEnabledAction(CasitaStatus *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(std::string, group)
  TEMPLATABLE_VALUE(bool, enabled)
  void play(const Ts &...x) override { parent_->set_group_enabled(group_.value(x...), enabled_.value(x...)); }
 protected:
  CasitaStatus *parent_;
};
template<typename... Ts> class SetBrightnessAction final : public Action<Ts...> {
 public:
  explicit SetBrightnessAction(CasitaStatus *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(float, value)
  void play(const Ts &...x) override { parent_->set_global_brightness(value_.value(x...)); }
 protected:
  CasitaStatus *parent_;
};
}  // namespace esphome::casita_status
