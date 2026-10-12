#include "test_support.h"
void test_mapping() {
  std::array<unsigned, 10> membership{};
  for (const auto &group : GROUP_MAP)
    for (uint8_t i = 0; i < group.size; ++i) ++membership[group.pixels[i]];
  for (unsigned count : membership) CHECK(count == 1);
  CHECK((GROUP_MAP[0].pixels == std::array<uint8_t, 3>{6, 7, 8}));
  CHECK((GROUP_MAP[1].pixels == std::array<uint8_t, 3>{4, 3, 2}));
  CHECK(GROUP_MAP[2].size == 1 && GROUP_MAP[2].pixels[0] == 5);
  CHECK((GROUP_MAP[3].pixels == std::array<uint8_t, 3>{9, 1, 0}));
  for (int i = 0; i < 4; ++i) CHECK(group_index(GROUP_NAMES[i]) == i);
  CHECK(group_index("roof") == -1);
}
