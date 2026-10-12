#include "casita_core.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace esphome::casita_status {

int group_index(const char *name) {
  for (int i = 0; i < 4; ++i)
    if (std::strcmp(name, GROUP_NAMES[i]) == 0) return i;
  return -1;
}
const char *state_name(int8_t state) {
  static constexpr const char *NAMES[]{"initializing", "unknown_unavailable", "off", "1", "2", "3", "4", "5", "6"};
  return state >= INITIALIZING && state <= 6 ? NAMES[state + 2] : "unknown_unavailable";
}
const char *type_name(InputType type) {
  static constexpr const char *NAMES[]{"status", "co2", "aqi", "voc", "pm25", "nox"};
  return NAMES[static_cast<uint8_t>(type)];
}
const char *source_name(SourceKind source) {
  static constexpr const char *NAMES[]{"action", "local", "homeassistant"};
  return NAMES[static_cast<uint8_t>(source)];
}
uint32_t default_action_ttl(InputType type) {
  return type == InputType::STATUS ? 0 : type == InputType::AQI ? 1800000 : 300000;
}
int8_t translate(InputType type, double value) {
  if (!std::isfinite(value)) return UNKNOWN;
  if (type == InputType::STATUS)
    return value >= 1 && value <= 6 && std::floor(value) == value ? static_cast<int8_t>(value) : UNKNOWN;
  if (type == InputType::VOC || type == InputType::NOX) {
    if (value < 1 || value > 500) return UNKNOWN;
    static constexpr std::array<double, 4> VOC{{80, 150, 250, 400}}, NOX{{20, 50, 100, 250}};
    const auto &limits = type == InputType::VOC ? VOC : NOX;
    for (uint8_t i = 0; i < limits.size(); ++i)
      if (value < limits[i]) return i + 1;
    return 5;
  }
  if (value < 0) return UNKNOWN;
  // EPA truncates PM2.5 to 0.1 ug/m3 before applying its 2024 breakpoints.
  if (type == InputType::PM25) value = std::floor(value * 10 + 1e-6) / 10;
  static constexpr std::array<double, 5> CO2{{800, 1000, 1500, 2000, 2500}}, AQI{{50, 100, 150, 200, 300}},
      PM25{{9.0, 35.4, 55.4, 125.4, 225.4}};
  const auto &limits = type == InputType::CO2 ? CO2 : type == InputType::AQI ? AQI : PM25;
  for (uint8_t i = 0; i < limits.size(); ++i)
    if (value <= limits[i]) return i + 1;
  return 6;
}
float restored_brightness(float configured, std::optional<float> saved) {
  float value = saved && std::isfinite(*saved) ? *saved : configured;
  return std::clamp(value, 0.25f, 1.0f);
}
void Board::boot(uint32_t now) {
  states_ = {};
  replay_values_ = {};
  test_started_.reset();
  connected_ = false;
  dirty_ = true;
  for (uint8_t i = 0; i < 4; ++i) {
    states_[i].display_state = configs_[i].enabled ? INITIALIZING : OFF;
    states_[i].resolved_since = now;
  }
}
void Board::transition_(uint8_t group, int8_t state, uint32_t now) {
  auto &runtime = states_[group];
  if (runtime.display_state == state) return;
  runtime.display_state = state;
  runtime.resolved_since = now;
  dirty_ = true;
}
void Board::initialize_(uint8_t group, uint32_t now) {
  auto &runtime = states_[group];
  runtime.source_value.reset();
  runtime.last_received_at.reset();
  runtime.last_valid_at.reset();
  transition_(group, INITIALIZING, now);
  // A reconnect during initialization still restarts its deadline.
  runtime.resolved_since = now;
}
SubmitResult Board::submit(uint8_t group, double value, bool from_sensor, uint32_t now) {
  auto &runtime = states_[group];
  const auto &config = configs_[group];
  if (from_sensor != (config.source != SourceKind::ACTION)) {
    ++runtime.rejected_updates;
    return SubmitResult::WRONG_SOURCE;
  }
  if (config.source == SourceKind::HOME_ASSISTANT) {
    replay_values_[group] = {true, value};
  }
  if (runtime.display_state == OFF) return SubmitResult::DISABLED;
  if (config.source == SourceKind::HOME_ASSISTANT && !connected_) return SubmitResult::PENDING_CONNECTION;
  runtime.source_value = value;
  runtime.last_received_at = now;
  const int8_t state = translate(config.type, value);
  if (state != UNKNOWN) runtime.last_valid_at = now;
  else ++runtime.rejected_updates;
  transition_(group, state, now);
  return state == UNKNOWN ? SubmitResult::INVALID : SubmitResult::ACCEPTED;
}
void Board::replay_(uint8_t group, uint32_t now) {
  if (connected_ && replay_values_[group].eligible) submit(group, replay_values_[group].value, true, now);
}
void Board::set_connected(bool connected, uint32_t now) {
  if (connected_ == connected) return;
  connected_ = connected;
  for (uint8_t i = 0; i < 4; ++i) {
    if (configs_[i].source != SourceKind::HOME_ASSISTANT) continue;
    if (!connected) replay_values_[i].eligible = false;
    if (states_[i].display_state == OFF) continue;
    if (connected) {
      initialize_(i, now);
      replay_(i, now);
    } else transition_(i, UNKNOWN, now);
  }
}
void Board::set_enabled(uint8_t group, bool enabled, uint32_t now) {
  auto &runtime = states_[group];
  if (enabled == (runtime.display_state != OFF)) return;
  if (enabled) {
    initialize_(group, now);
    if (configs_[group].source == SourceKind::HOME_ASSISTANT) replay_(group, now);
  } else {
    runtime.source_value.reset();
    runtime.last_received_at.reset();
    runtime.last_valid_at.reset();
    transition_(group, OFF, now);
  }
}
void Board::set_unavailable(uint8_t group, uint32_t now) {
  if (states_[group].display_state != OFF) transition_(group, UNKNOWN, now);
}
void Board::tick(uint32_t now) {
  if (test_started_ && uint32_t(now - *test_started_) >= TEST_DURATION_MS) {
    test_started_.reset();
    dirty_ = true;
  }
  for (uint8_t i = 0; i < 4; ++i) {
    const auto &config = configs_[i];
    const auto &runtime = states_[i];
    if (runtime.display_state == INITIALIZING && uint32_t(now - runtime.resolved_since) >= config.initialization_ms)
      transition_(i, UNKNOWN, now);
    else if (runtime.display_state > OFF && config.ttl_ms && runtime.last_valid_at &&
             uint32_t(now - *runtime.last_valid_at) >= config.ttl_ms)
      transition_(i, UNKNOWN, now);
  }
}
void Board::set_brightness(float value) {
  if (!std::isfinite(value) || value < 0.25f || value > 1.0f || brightness_ == value) return;
  brightness_ = value;
  dirty_ = true;
}
bool Board::animated() const {
  if (test_started_) return true;
  for (uint8_t i = 0; i < 4; ++i) {
    const auto state = states_[i].display_state;
    if (state < OFF || state >= 5 || (i == 2 && state >= 2)) return true;
  }
  return false;
}
static double pulse(uint32_t elapsed, uint32_t period) {
  const double phase = double(elapsed % period) / period;
  return 0.25 + 0.75 * std::abs(2 * phase - 1);
}
static uint8_t encode(double value) {
  return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0, 1.0) * 255));
}
Frame Board::frame(uint32_t now) const {
  Frame frame{};
  if (test_started_ && uint32_t(now - *test_started_) < TEST_DURATION_MS) {
    uint32_t t = now - *test_started_;
    if (t < TEST_WALK_MS) {
      const auto channel = encode(std::pow(brightness_, 1 / 2.8));
      frame[t / TEST_PIXEL_MS] = {channel, channel, channel};
      return frame;
    }
    t -= TEST_WALK_MS;
    int8_t state;
    uint32_t elapsed;
    if (t < TEST_STATUSES_MS) {
      state = static_cast<int8_t>(1 + t / TEST_STATUS_MS);
      elapsed = t % TEST_STATUS_MS;
    } else {
      t -= TEST_STATUSES_MS;
      state = t < TEST_META_MS ? INITIALIZING : UNKNOWN;
      elapsed = t % TEST_META_MS;
    }
    for (uint8_t i = 0; i < 4; ++i) draw_(frame, i, state, elapsed);
    return frame;
  }
  for (uint8_t i = 0; i < 4; ++i) draw_(frame, i, states_[i].display_state, now - states_[i].resolved_since);
  return frame;
}
void Board::draw_(Frame &frame, uint8_t group, int8_t state, uint32_t elapsed) const {
  static constexpr std::array<RGB, 6> PALETTE{{{0, 255, 0}, {255, 255, 0}, {255, 160, 0},
                                                            {255, 0, 0}, {200, 0, 255}, {128, 0, 0}}};
  const auto &map = GROUP_MAP[group];
  if (state == OFF) return;
  RGB color;
  uint8_t count = map.size;
  double multiplier = 1;
  if (state == UNKNOWN) {
    const auto channel = encode(std::pow(0.15 * brightness_ * pulse(elapsed, 3000), 1 / 2.8));
    color = {channel, channel, channel};
  } else {
    if (state == INITIALIZING) {
      color = {0, 0, 255};
      if (map.size == 1) multiplier = pulse(elapsed, 2000);
    } else {
      color = PALETTE[state - 1];
      count = std::min<uint8_t>(map.size, state);
      if (state == 5 || state == 6) multiplier = pulse(elapsed, state == 5 ? 3000 : 500);
      if (map.size == 1 && state >= 2 && state <= 4) {
        const uint32_t phase = elapsed % 6000;
        if (phase / 300 < uint32_t(state - 1) && phase % 300 < 120) return;
      }
    }
    const double scale = std::pow(brightness_ * multiplier, 1 / 2.8);
    color = {encode(color.r / 255.0 * scale), encode(color.g / 255.0 * scale), encode(color.b / 255.0 * scale)};
  }
  if (state == INITIALIZING && map.size > 1) frame[map.pixels[(elapsed / 250) % map.size]] = color;
  else for (uint8_t pixel = 0; pixel < count; ++pixel) frame[map.pixels[pixel]] = color;
}
}  // namespace esphome::casita_status
