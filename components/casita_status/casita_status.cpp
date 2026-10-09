#include "casita_status.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#ifdef USE_API
#include "esphome/components/api/api_server.h"
#endif
#include <cmath>

namespace esphome::casita_status {
static const char *const TAG = "casita_status";
static const char *const EFFECT = "Casita Status Renderer";

void BrightnessNumber::control(float value) { parent_->set_global_brightness(value); }
void CasitaStatus::configure_group(uint8_t group, InputType type, SourceKind source, bool enabled, uint32_t ttl,
                                  uint32_t initialization, sensor::Sensor *sensor, const char *sensor_id,
                                  text_sensor::TextSensor *diagnostic) {
  board_.configure(group, {type, source, enabled, ttl, initialization});
  sensors_[group] = sensor;
  sensor_ids_[group] = sensor_id;
  diagnostics_[group] = diagnostic;
}
void CasitaStatus::setup() {
#ifdef USE_DEEP_SLEEP
  if (deep_sleep_ != nullptr) deep_sleep_->prevent_deep_sleep();
#endif
  board_.boot(millis());
  brightness_preference_ = global_preferences->make_preference<float>(preference_key_);
  float saved;
  const bool loaded = brightness_preference_.load(&saved);
  const float brightness = restored_brightness(configured_brightness_, loaded ? std::optional<float>(saved) : std::nullopt);
  board_.set_brightness(brightness);
  brightness_number_->publish_state(brightness * 100);
  initialized_ = true;
  for (uint8_t i = 0; i < 4; ++i)
    if (sensors_[i] != nullptr) sensors_[i]->add_on_state_callback([this, i](float value) { this->sensor_value_(i, value); });
  report_("boot");
}
void CasitaStatus::connection_(uint32_t now) {
#ifdef USE_API
  const bool connected = api::global_api_server != nullptr && api::global_api_server->is_connected_with_state_subscription();
  if (connected != board_.connected()) {
    board_.set_connected(connected, now);
    ESP_LOGI(TAG, "API state subscription: %s", connected ? "connected" : "disconnected");
    report_(connected ? "HA connect/replay" : "HA disconnect");
  }
#endif
}
bool CasitaStatus::validate_output_() {
  if (output_ != nullptr) return true;
  if (!light_->is_ready()) return false;
  if (light_->get_effect_count() != 1 || light_->get_effect_index(EFFECT) == 0) {
    ESP_LOGE(TAG, "Expected the single package-owned renderer effect");
    mark_failed();
    return false;
  }
  // Python final validation guarantees the addressable driver type.
  auto *candidate = static_cast<light::AddressableLight *>(light_->get_output());
  if (candidate->size() != 10) {
    ESP_LOGE(TAG, "Expected ten addressable pixels; got %ld", static_cast<long>(candidate->size()));
    mark_failed();
    return false;
  }
  output_ = candidate;
  return true;
}
void CasitaStatus::ensure_renderer_() {
  if (!validate_output_()) return;
  if (light_->get_effect_name() == EFFECT && light_->current_values.is_on() &&
      light_->current_values.get_brightness() == 1.0f) return;
  if (renderer_started_) ESP_LOGW(TAG, "Renderer stopped or light changed; restoring renderer");
  auto call = light_->make_call();
  // The validated default transition is zero; an explicit transition conflicts with effect selection.
  call.set_state(true).set_brightness(1.0f).set_rgb(1.0f, 1.0f, 1.0f)
      .set_effect(EFFECT).set_save(false).perform();
  renderer_started_ = true;
  frame_valid_ = false;
}
void CasitaStatus::loop() {
  const uint32_t now = millis();
  connection_(now);
  board_.tick(now);
  report_("source deadline");
  ensure_renderer_();
}
void CasitaStatus::render(light::AddressableLight &output, bool initial_run) {
  if (!initialized_ || is_failed() || !light_->is_ready()) return;
  if (output.size() != 10) {
    ESP_LOGE(TAG, "Refusing to render a buffer with %ld pixels", static_cast<long>(output.size()));
    mark_failed();
    return;
  }
  const bool dirty = board_.take_dirty();
  if (!initial_run && frame_valid_ && !dirty && !board_.animated()) return;
  const Frame frame = board_.frame(millis());
  for (uint8_t i = 0; i < frame.size(); ++i) output[i] = Color(frame[i].r, frame[i].g, frame[i].b);
  output.schedule_show();
  ++frames_;
  frame_valid_ = true;
}
int CasitaStatus::resolve_group_(const std::string &name) {
  const int index = group_index(name.c_str());
  if (index < 0) ESP_LOGW(TAG, "Unknown group '%s'; action rejected", name.c_str());
  return index;
}
void CasitaStatus::sensor_value_(uint8_t group, float value) {
  const uint32_t now = millis();
  connection_(now);
  const auto result = board_.submit(group, value, true, now);
  if (result == SubmitResult::INVALID) ESP_LOGW(TAG, "%s: invalid sensor value %g", GROUP_NAMES[group], value);
  if (result == SubmitResult::DISABLED) ESP_LOGD(TAG, "%s: dropped update while disabled", GROUP_NAMES[group]);
  report_("sensor publish");
}
void CasitaStatus::set_group_value(const std::string &group, double value) {
  const int index = resolve_group_(group);
  if (index < 0 || !initialized_) return;
  const auto result = board_.submit(index, value, false, millis());
  if (result == SubmitResult::WRONG_SOURCE) ESP_LOGW(TAG, "%s: set_group_value rejected on sensor-bound group", group.c_str());
  if (result == SubmitResult::INVALID) ESP_LOGW(TAG, "%s: invalid action value %g", group.c_str(), value);
  if (result == SubmitResult::DISABLED) ESP_LOGD(TAG, "%s: dropped update while disabled", group.c_str());
  report_("action value");
}
void CasitaStatus::set_group_unavailable(const std::string &group) {
  const int index = resolve_group_(group);
  if (index < 0 || !initialized_) return;
  board_.set_unavailable(index, millis());
  report_("explicit unavailable");
}
void CasitaStatus::set_group_enabled(const std::string &group, bool enabled) {
  const int index = resolve_group_(group);
  if (index < 0 || !initialized_) return;
  const uint32_t now = millis();
  connection_(now);
  board_.set_enabled(index, enabled, now);
  report_(enabled ? "enable/replay" : "disable");
}
void CasitaStatus::set_global_brightness(float percent) {
  if (!initialized_) return;
  if (!std::isfinite(percent) || percent < 25 || percent > 100) {
    ESP_LOGW(TAG, "Brightness must be between 25 and 100 percent");
    return;
  }
  const float value = percent / 100;
  if (value == board_.brightness()) return;
  board_.set_brightness(value);
  brightness_preference_.save(&value);
  brightness_number_->publish_state(percent);
}
void CasitaStatus::report_(const char *reason) {
  for (uint8_t i = 0; i < 4; ++i) {
    const int8_t state = board_.state(i).display_state;
    if (reported_states_[i] == state) continue;
    ESP_LOGI(TAG, "%s: %s -> %s; type=%s source=%s reason=%s", GROUP_NAMES[i],
             reported_states_[i] == 10 ? "boot" : state_name(reported_states_[i]), state_name(state),
             type_name(board_.config(i).type), source_name(board_.config(i).source), reason);
    reported_states_[i] = state;
    diagnostics_[i]->publish_state(state_name(state));
  }
}
void CasitaStatus::dump_config() {
  ESP_LOGCONFIG(TAG, "Casita Status Board: brightness=%.0f%% subscribed=%s frames=%lu", board_.brightness() * 100,
                board_.connected() ? "yes" : "no", static_cast<unsigned long>(frames_));
  const uint32_t now = millis();
  for (uint8_t i = 0; i < 4; ++i) {
    const auto &runtime = board_.state(i);
    const auto &config = board_.config(i);
    const uint32_t age = runtime.last_valid_at ? uint32_t(now - *runtime.last_valid_at) : 0;
    const uint32_t remaining = config.ttl_ms && runtime.last_valid_at && age < config.ttl_ms ? config.ttl_ms - age : 0;
    ESP_LOGCONFIG(TAG, "  %s: type=%s source=%s sensor=%s state=%s value=%g (%s) valid_age=%lu ms ttl=%lu ms remaining=%lu ms rejected=%lu",
                  GROUP_NAMES[i], type_name(config.type), source_name(config.source), sensor_ids_[i],
                  state_name(runtime.display_state), runtime.source_value.value_or(NAN),
                  runtime.display_state > OFF ? "current" : "historical", static_cast<unsigned long>(age),
                  static_cast<unsigned long>(config.ttl_ms), static_cast<unsigned long>(remaining),
                  static_cast<unsigned long>(runtime.rejected_updates));
  }
}
}  // namespace esphome::casita_status
