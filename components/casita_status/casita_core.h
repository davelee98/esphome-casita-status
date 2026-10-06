#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace esphome::casita_status {

enum class InputType : uint8_t { STATUS, CO2, AQI, VOC };
enum class SourceKind : uint8_t { ACTION, LOCAL, HOME_ASSISTANT };
enum class SubmitResult : uint8_t { ACCEPTED, INVALID, DISABLED, WRONG_SOURCE, PENDING_CONNECTION };
constexpr int8_t OFF = 0, UNKNOWN = -1, INITIALIZING = -2;
constexpr std::array<const char *, 4> GROUP_NAMES{{"left", "right", "center", "top"}};
struct GroupMap {
  std::array<uint8_t, 3> pixels;
  uint8_t size;
};
constexpr std::array<GroupMap, 4> GROUP_MAP{{{{5, 6, 7}, 3}, {{3, 2, 1}, 3}, {{4, 4, 4}, 1}, {{8, 9, 0}, 3}}};
struct RGB {
  uint8_t r{0}, g{0}, b{0};
  bool operator==(const RGB &other) const { return r == other.r && g == other.g && b == other.b; }
};
using Frame = std::array<RGB, 10>;
struct GroupConfig {
  InputType type{InputType::STATUS};
  SourceKind source{SourceKind::ACTION};
  bool enabled{true};
  uint32_t ttl_ms{0};  // Zero means never; Python permits it only for action status or HA.
  uint32_t initialization_ms{30000};
};
struct GroupState {
  int8_t display_state{INITIALIZING};
  std::optional<double> source_value;
  std::optional<uint32_t> last_received_at, last_valid_at;
  uint32_t resolved_since{0}, rejected_updates{0};
};
struct ReplayValue {
  bool eligible{false};
  double value{0};
};

int group_index(const char *name);
int8_t translate(InputType type, double value);
const char *state_name(int8_t state);
const char *type_name(InputType type);
const char *source_name(SourceKind source);
uint32_t default_action_ttl(InputType type);
float restored_brightness(float configured, std::optional<float> saved);

// Clock values are injected into every operation; the core has no ESPHome dependency.
class Board {
 public:
  void configure(uint8_t group, GroupConfig config) { configs_[group] = config; }
  void boot(uint32_t now);
  SubmitResult submit(uint8_t group, double value, bool from_sensor, uint32_t now);
  void set_connected(bool connected, uint32_t now);
  void set_enabled(uint8_t group, bool enabled, uint32_t now);
  void set_unavailable(uint8_t group, uint32_t now);
  void tick(uint32_t now);
  void set_brightness(float value);
  float brightness() const { return brightness_; }
  bool connected() const { return connected_; }
  const GroupState &state(uint8_t group) const { return states_[group]; }
  const GroupConfig &config(uint8_t group) const { return configs_[group]; }
  Frame frame(uint32_t now) const;
  bool animated() const;
  bool take_dirty() { bool value = dirty_; dirty_ = false; return value; }

 protected:
  void transition_(uint8_t group, int8_t state, uint32_t now);
  void initialize_(uint8_t group, uint32_t now);
  void replay_(uint8_t group, uint32_t now);
  std::array<GroupConfig, 4> configs_{};
  std::array<GroupState, 4> states_{};
  std::array<ReplayValue, 4> replay_values_{};
  bool connected_{false}, dirty_{true};
  float brightness_{0.5f};
};
}  // namespace esphome::casita_status
