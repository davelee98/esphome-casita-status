#include "test_support.h"
#include <string>
void test_worst() {
  CHECK(std::string(type_name(InputType::WORST)) == "worst");
  CHECK(std::string(source_name(SourceKind::DERIVED)) == "derived");
  CHECK(translate(InputType::WORST, 3) == UNKNOWN);

  // center (2) follows left (0), right (1) and top (3).
  auto board = action_board();
  board.configure(2, {InputType::WORST, SourceKind::DERIVED, true, 0, 30000, 0b1011});
  board.boot(0);
  CHECK(board.state(2).display_state == INITIALIZING);
  board.submit(0, 2, false, 10);
  CHECK(board.state(2).display_state == 2);  // One valid source is enough.
  board.submit(1, 5, false, 11);
  CHECK(board.state(2).display_state == 5);
  board.submit(1, 1, false, 12);
  CHECK(board.state(2).display_state == 2);  // Follows improvements too.
  board.tick(100000);
  CHECK(board.state(3).display_state == UNKNOWN);
  CHECK(board.state(2).display_state == 2);  // Unknown sources are ignored.
  board.set_unavailable(0, 100001);
  board.set_unavailable(1, 100001);
  CHECK(board.state(2).display_state == UNKNOWN);  // No valid source and none initializing.
  board.set_enabled(0, false, 100002);
  board.set_enabled(0, true, 100003);
  CHECK(board.state(2).display_state == INITIALIZING);  // A restarting source shows Initializing.

  // A derived group takes no input and has no deadline of its own.
  CHECK(board.submit(2, 3, false, 100004) == SubmitResult::WRONG_SOURCE);
  board.set_unavailable(2, 100005);
  CHECK(board.state(2).display_state == INITIALIZING);
  board.tick(1000000);
  CHECK(board.state(0).display_state == UNKNOWN);
  CHECK(board.state(2).display_state == UNKNOWN);  // Follows its source's initialization deadline.

  // Disabled, it stays Off and stops following; re-enabled, it recomputes at once.
  board.submit(3, 4, false, 1000001);
  CHECK(board.state(2).display_state == 4);
  board.set_enabled(2, false, 1000002);
  CHECK(board.state(2).display_state == OFF);
  board.submit(3, 6, false, 1000003);
  CHECK(board.state(2).display_state == OFF);
  board.set_enabled(2, true, 1000004);
  CHECK(board.state(2).display_state == 6);
  CHECK(board.state(2).resolved_since == 1000004);
  board.set_enabled(3, false, 1000005);
  CHECK(board.state(2).display_state == UNKNOWN);  // Off sources are ignored like Unknown ones.

  // A source's TTL expiry reaches the derived group on the same tick.
  auto timed = action_board(InputType::STATUS, 1000);
  timed.configure(2, {InputType::WORST, SourceKind::DERIVED, true, 0, 30000, 0b0001});
  timed.boot(0);
  timed.submit(0, 3, false, 0);
  timed.tick(999);
  CHECK(timed.state(2).display_state == 3);
  timed.tick(1000);
  CHECK(timed.state(0).display_state == UNKNOWN);
  CHECK(timed.state(2).display_state == UNKNOWN);

  // Disabled in configuration, it boots Off.
  timed.configure(2, {InputType::WORST, SourceKind::DERIVED, false, 0, 30000, 0b0001});
  timed.boot(2000);
  CHECK(timed.state(2).display_state == OFF);
}
