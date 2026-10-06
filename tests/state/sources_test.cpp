#include "test_support.h"
void test_sources() {
  auto board = action_board();
  board.configure(0, {InputType::CO2, SourceKind::HOME_ASSISTANT, true, 0, 30000});
  board.configure(1, {InputType::CO2, SourceKind::LOCAL, true, 180000, 180000});
  board.boot(0);
  CHECK(board.submit(0, 1100, true, 10) == SubmitResult::PENDING_CONNECTION);
  CHECK(board.state(0).display_state == INITIALIZING);
  board.set_connected(true, 11);
  CHECK(board.state(0).display_state == 3);
  board.submit(1, 700, true, 12);
  board.submit(2, 4, false, 12);
  CHECK(board.submit(0, 5, false, 13) == SubmitResult::WRONG_SOURCE);
  CHECK(board.state(0).display_state == 3);
  board.set_connected(false, 14);
  CHECK(board.state(0).display_state == UNKNOWN);
  CHECK(board.state(1).display_state == 1);
  CHECK(board.state(2).display_state == 4);
  board.set_connected(true, 15);
  CHECK(board.state(0).display_state == INITIALIZING); // Previous connection's cached value is ineligible.
  board.submit(0, 1100, true, 16);
  board.set_enabled(0, false, 17);
  board.submit(0, 1501, true, 18);
  CHECK(board.state(0).display_state == OFF);
  board.set_enabled(0, true, 19);
  CHECK(board.state(0).display_state == 4);
  board.set_enabled(0, false, 20);
  board.set_connected(false, 21);
  board.set_connected(true, 22);
  CHECK(board.state(0).display_state == OFF);
  board.set_enabled(0, true, 23);
  CHECK(board.state(0).display_state == INITIALIZING);
  board.set_connected(false, 24);
  board.submit(0, 800, true, 25);
  CHECK(board.state(0).display_state == UNKNOWN);
  board.set_connected(true, 26);
  CHECK(board.state(0).display_state == 1);
  board.tick(180011);
  CHECK(board.state(1).display_state == 1);
  board.tick(180012);
  CHECK(board.state(1).display_state == UNKNOWN);
  // Explicit TTL accommodates a healthy filtered sensor with a five-minute publish interval.
  board.configure(1, {InputType::CO2, SourceKind::LOCAL, true, 900000, 900000});
  board.boot(0);
  board.submit(1, 700, true, 1);
  board.tick(300000);
  CHECK(board.state(1).display_state == 1);
  board.submit(1, 700, true, 300001);
  board.tick(600000);
  CHECK(board.state(1).display_state == 1);
}
