#include "test_support.h"
#include <cmath>
void test_lookups() {
  for (InputType type : {InputType::STATUS, InputType::CO2, InputType::AQI, InputType::VOC}) {
    CHECK(translate(type, NAN) == UNKNOWN);
    CHECK(translate(type, INFINITY) == UNKNOWN);
    CHECK(translate(type, -INFINITY) == UNKNOWN);
    CHECK(translate(type, -1) == UNKNOWN);
  }
  CHECK(translate(InputType::STATUS, 4.0) == 4);
  CHECK(translate(InputType::STATUS, 4.0001) == UNKNOWN);
  CHECK(translate(InputType::STATUS, 0) == UNKNOWN);
  CHECK(translate(InputType::STATUS, 7) == UNKNOWN);
  for (const auto &entry : {std::pair{InputType::CO2, std::array<double, 5>{800, 1000, 1500, 2000, 2500}},
                            std::pair{InputType::AQI, std::array<double, 5>{50, 100, 150, 200, 300}}}) {
    for (uint8_t i = 0; i < 5; ++i) {
      const auto threshold = entry.second[i];
      CHECK(translate(entry.first, std::nextafter(threshold, 0)) == i + 1);
      CHECK(translate(entry.first, threshold) == i + 1);
      CHECK(translate(entry.first, std::nextafter(threshold, INFINITY)) == i + 2);
    }
    CHECK(translate(entry.first, 0) == 1);
    CHECK(translate(entry.first, 1000000) == 6);
  }
  const std::array<double, 4> voc{80, 150, 250, 400};
  for (uint8_t i = 0; i < voc.size(); ++i) {
    CHECK(translate(InputType::VOC, std::nextafter(voc[i], 0)) == i + 1);
    CHECK(translate(InputType::VOC, voc[i]) == i + 2);
    CHECK(translate(InputType::VOC, std::nextafter(voc[i], INFINITY)) == i + 2);
  }
  CHECK(translate(InputType::VOC, 1) == 1);
  CHECK(translate(InputType::VOC, 500) == 5);
  CHECK(translate(InputType::VOC, 0.999) == UNKNOWN);
  CHECK(translate(InputType::VOC, 500.001) == UNKNOWN);
  CHECK(default_action_ttl(InputType::STATUS) == 0);
  CHECK(default_action_ttl(InputType::CO2) == 300000);
  CHECK(default_action_ttl(InputType::AQI) == 1800000);
  CHECK(default_action_ttl(InputType::VOC) == 300000);
}
