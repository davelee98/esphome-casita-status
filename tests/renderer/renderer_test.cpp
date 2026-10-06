#include "test_support.h"
void test_renderer() {
  auto board = action_board();
  board.set_brightness(1);
  for (uint8_t i = 0; i < 4; ++i) board.set_enabled(i, false, 0);
  const std::array<RGB, 6> palette{{{0, 255, 0}, {255, 255, 0}, {255, 128, 0}, {255, 0, 0}, {153, 0, 255}, {128, 0, 0}}};
  board.set_enabled(3, true, 0);
  for (int state = 1; state <= 6; ++state) {
    board.submit(3, state, false, state * 100);
    Frame expected{};
    for (int i = 0; i < std::min(state, 3); ++i) expected[GROUP_MAP[3].pixels[i]] = palette[state - 1];
    CHECK(board.frame(state * 100) == expected);
  }
  board.set_enabled(3, false, 1000);
  CHECK(board.frame(1000) == Frame{});
  board.set_enabled(3, true, 1001);
  for (uint32_t step = 0; step < 3; ++step) {
    Frame expected{};
    expected[GROUP_MAP[3].pixels[step]] = {0, 0, 255};
    CHECK(board.frame(1001 + step * 250) == expected);
  }
  board.set_unavailable(3, 2000);
  Frame unknown{};
  for (uint8_t i : GROUP_MAP[3].pixels) unknown[i] = {130, 130, 130};
  CHECK(board.frame(2000) == unknown);
  for (uint8_t i : GROUP_MAP[3].pixels) unknown[i] = {79, 79, 79};
  CHECK(board.frame(3500) == unknown);
  board.set_enabled(3, false, 4000);
  board.set_enabled(2, true, 4000);
  Frame center_initializing{};
  center_initializing[4] = {0, 0, 255};
  CHECK(board.frame(4000) == center_initializing);
  center_initializing[4] = {0, 0, 155};
  CHECK(board.frame(5000) == center_initializing);
  for (int state = 1; state <= 6; ++state) {
    const uint32_t entered = 5000 + state * 1000;
    board.submit(2, state, false, entered);
    auto frame = board.frame(entered);
    Frame expected{};
    expected[4] = (state >= 2 && state <= 4) ? RGB{} : palette[state - 1];
    CHECK(frame == expected);
    if (state >= 2 && state <= 4) {
      CHECK(board.frame(entered + 119)[4] == RGB{});
      CHECK(board.frame(entered + 120)[4] == palette[state - 1]);
      CHECK(board.frame(entered + (state - 1) * 300)[4] == palette[state - 1]);
      CHECK(board.frame(entered + 6000)[4] == RGB{});
    }
  }
  board.set_unavailable(2, 11700);
  Frame center_unknown{};
  center_unknown[4] = {130, 130, 130};
  CHECK(board.frame(11700) == center_unknown);
  board.set_enabled(0, true, 12000);
  board.submit(0, 1, false, 12000);
  board.submit(2, 4, false, 12000);
  const Frame before = board.frame(12120);
  board.submit(3, 6, false, 12120); // Disabled group remains dark.
  CHECK(board.frame(12120) == before);
  board.submit(0, 3, false, 12120);
  const Frame after = board.frame(12120);
  for (uint8_t i : {0, 1, 2, 3, 4, 8, 9}) CHECK(before[i] == after[i]);
  board.set_brightness(0.25f);
  board.submit(0, 6, false, 13000);
  CHECK(board.frame(13250)[5].r > 0); // Encoded trough; actual gamma is checked on-device.
}
