#include "test_support.h"
#include <cmath>
void test_lookups() {
  for (InputType type : {InputType::STATUS, InputType::CO2, InputType::AQI, InputType::VOC,
                         InputType::PM25, InputType::NOX}) {
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
  CHECK(default_action_ttl(InputType::PM25) == 300000);
  CHECK(default_action_ttl(InputType::NOX) == 300000);
  // PM2.5 uses the 2024 EPA breakpoints after truncating to 0.1 ug/m3.
  const std::array<double, 5> pm25{9.0, 35.4, 55.4, 125.4, 225.4};
  for (uint8_t i = 0; i < pm25.size(); ++i) {
    CHECK(translate(InputType::PM25, pm25[i]) == i + 1);
    CHECK(translate(InputType::PM25, pm25[i] + 0.05) == i + 1);
    CHECK(translate(InputType::PM25, pm25[i] + 0.1) == i + 2);
  }
  CHECK(translate(InputType::PM25, 0) == 1);
  CHECK(translate(InputType::PM25, 1000) == 6);
  const std::array<double, 4> nox{20, 50, 100, 250};
  for (uint8_t i = 0; i < nox.size(); ++i) {
    CHECK(translate(InputType::NOX, std::nextafter(nox[i], 0)) == i + 1);
    CHECK(translate(InputType::NOX, nox[i]) == i + 2);
  }
  CHECK(translate(InputType::NOX, 1) == 1);
  CHECK(translate(InputType::NOX, 500) == 5);
  CHECK(translate(InputType::NOX, 0.999) == UNKNOWN);
  CHECK(translate(InputType::NOX, 500.001) == UNKNOWN);
}
