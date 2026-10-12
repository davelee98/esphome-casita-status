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
  center_initializing[5] = {0, 0, 255};
  CHECK(board.frame(4000) == center_initializing);
  center_initializing[5] = {0, 0, 155};
  CHECK(board.frame(5000) == center_initializing);
  for (int state = 1; state <= 6; ++state) {
    const uint32_t entered = 5000 + state * 1000;
    board.submit(2, state, false, entered);
    auto frame = board.frame(entered);
    Frame expected{};
    expected[5] = (state >= 2 && state <= 4) ? RGB{} : palette[state - 1];
    CHECK(frame == expected);
    if (state >= 2 && state <= 4) {
      CHECK(board.frame(entered + 119)[5] == RGB{});
      CHECK(board.frame(entered + 120)[5] == palette[state - 1]);
      CHECK(board.frame(entered + (state - 1) * 300)[5] == palette[state - 1]);
      CHECK(board.frame(entered + 6000)[5] == RGB{});
    }
  }
  board.set_unavailable(2, 11700);
  Frame center_unknown{};
  center_unknown[5] = {130, 130, 130};
  CHECK(board.frame(11700) == center_unknown);
  board.set_enabled(0, true, 12000);
  board.submit(0, 1, false, 12000);
  board.submit(2, 4, false, 12000);
  const Frame before = board.frame(12120);
  board.submit(3, 6, false, 12120); // Disabled group remains dark.
  CHECK(board.frame(12120) == before);
  board.submit(0, 3, false, 12120);
  const Frame after = board.frame(12120);
  for (uint8_t i : {0, 1, 2, 3, 4, 5, 9}) CHECK(before[i] == after[i]);
  board.set_brightness(0.25f);
  board.submit(0, 6, false, 13000);
  CHECK(board.frame(13250)[6].r > 0); // Encoded trough; actual gamma is checked on-device.

  // Self-test overlays every group, including disabled ones, then hands back to the live states.
  auto test = action_board();
  test.set_brightness(1);
  test.submit(0, 2, false, 0);
  test.set_enabled(3, false, 0);
  const Frame live = test.frame(100);
  test.start_test(100);
  CHECK(test.testing() && test.animated());
  for (uint8_t pixel = 0; pixel < 10; ++pixel) {
    const Frame walk = test.frame(100 + pixel * TEST_PIXEL_MS);
    for (uint8_t i = 0; i < 10; ++i) CHECK((walk[i] == RGB{255, 255, 255}) == (i == pixel));
  }
  for (int status = 1; status <= 6; ++status) {
    const Frame frame = test.frame(100 + TEST_WALK_MS + (status - 1) * TEST_STATUS_MS);
    for (uint8_t group : {0, 1, 3})
      for (uint8_t i = 0; i < 3; ++i)
        CHECK((frame[GROUP_MAP[group].pixels[i]] == palette[status - 1]) == (i < status));
  }
  const uint32_t meta = 100 + TEST_WALK_MS + TEST_STATUSES_MS;
  CHECK(test.frame(meta)[GROUP_MAP[3].pixels[0]] == (RGB{0, 0, 255}));
  CHECK(test.frame(meta + TEST_META_MS)[GROUP_MAP[3].pixels[0]] == (RGB{130, 130, 130}));
  test.submit(0, 4, false, 5000);  // States keep updating during the test.
  CHECK(test.state(0).display_state == 4);
  test.tick(100 + TEST_DURATION_MS - 1);
  CHECK(test.testing());
  test.tick(100 + TEST_DURATION_MS);
  CHECK(!test.testing() && test.take_dirty());
  const Frame after_test = test.frame(100 + TEST_DURATION_MS);
  CHECK(after_test[GROUP_MAP[0].pixels[2]] == palette[3]);
  for (uint8_t i : GROUP_MAP[3].pixels) CHECK(after_test[i] == live[i]);
}
