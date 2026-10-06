#include "test_support.h"
#include <cmath>
void test_state() {
  auto board = action_board(InputType::STATUS, 1000, UINT32_MAX - 500);
  CHECK(board.state(0).display_state == INITIALIZING);
  board.tick(100);
  CHECK(board.state(0).display_state == INITIALIZING);
  CHECK(board.submit(0, 4, false, UINT32_MAX - 500) == SubmitResult::ACCEPTED);
  board.tick(498);
  CHECK(board.state(0).display_state == 4);
  board.tick(499);
  CHECK(board.state(0).display_state == UNKNOWN);
  board.set_enabled(0, false, 500);
  board.set_unavailable(0, 501);
  board.tick(10000);
  CHECK(board.submit(0, 6, false, 10000) == SubmitResult::DISABLED);
  CHECK(board.state(0).display_state == OFF);
  board.set_enabled(0, true, 10001);
  CHECK(board.state(0).display_state == INITIALIZING);
  CHECK(!board.state(0).source_value);
  board.set_enabled(0, true, 20000);
  CHECK(board.state(0).resolved_since == 10001);
  board.tick(40001);
  CHECK(board.state(0).display_state == UNKNOWN);
  board.submit(0, 4.0, false, 40002);
  CHECK(board.state(0).display_state == 4);
  board.submit(0, 4.0001, false, 40003);
  CHECK(board.state(0).display_state == UNKNOWN);
  board.submit(0, 5, false, 40004);
  board.submit(0, 5, false, 40005);
  CHECK(board.state(0).resolved_since == 40004); // Unchanged readings do not restart animation.
  CHECK(*board.state(0).last_valid_at == 40005);
  board.configure(1, {InputType::STATUS, SourceKind::ACTION, false, 0, 30000});
  board.boot(50000);
  CHECK(board.state(0).display_state == INITIALIZING);
  CHECK(board.state(1).display_state == OFF);
  CHECK(!board.state(0).last_valid_at);
  CHECK(restored_brightness(0.5f, std::nullopt) == 0.5f);
  CHECK(restored_brightness(0.5f, NAN) == 0.5f);
  CHECK(restored_brightness(0.5f, 0.1f) == 0.25f);
  CHECK(restored_brightness(0.5f, 2.0f) == 1.0f);
  CHECK(restored_brightness(0.5f, 0.7f) == 0.7f);
  board.set_brightness(0.75f);
  board.set_brightness(NAN);
  board.set_brightness(0.1f);
  CHECK(board.brightness() == 0.75f);
}
