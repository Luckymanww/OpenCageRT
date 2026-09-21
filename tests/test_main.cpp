#include <cstdlib>
#include <iostream>

extern int run_math_tests();
extern int run_clipper_tests();
extern int run_cage_builder_tests();
extern int run_micro_scene_tests();
extern int run_parity_tests();

int main() {
  int failures = 0;
  failures += run_math_tests();
  failures += run_clipper_tests();
  failures += run_cage_builder_tests();
  failures += run_micro_scene_tests();
  failures += run_parity_tests();
  if (failures != 0) {
    std::cerr << failures << " test group(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All OpenCageRT CPU tests passed.\n";
  return EXIT_SUCCESS;
}
