#include "test_support.h"
int main() {
  test_mapping();
  test_lookups();
  test_state();
  test_sources();
  test_worst();
  test_renderer();
  std::cout << "Mapping, lookups, state, sources, worst and renderer: passed\n";
}
