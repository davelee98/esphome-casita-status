#include "test_support.h"
int main() {
  test_mapping();
  test_lookups();
  test_state();
  test_sources();
  test_renderer();
  std::cout << "Mapping, lookups, state, sources and renderer: passed\n";
}
