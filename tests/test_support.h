#pragma once
#include "casita_core.h"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace esphome::casita_status;
#define CHECK(condition) do { if (!(condition)) { std::cerr << __FILE__ << ':' << __LINE__ << ": " #condition "\n"; std::exit(1); } } while (false)
inline Board action_board(InputType type = InputType::STATUS, uint32_t ttl = 0, uint32_t now = 0) {
  Board board;
  for (uint8_t i = 0; i < 4; ++i) board.configure(i, {type, SourceKind::ACTION, true, ttl, 30000});
  board.boot(now);
  return board;
}
inline bool dark(const RGB &pixel) { return pixel == RGB{}; }
void test_mapping();
void test_lookups();
void test_state();
void test_sources();
void test_renderer();
