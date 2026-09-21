#include "opencagert/clipper.h"
#include "opencagert/types.h"

#include <iostream>

using namespace opencagert;

int run_clipper_tests() {
  Tetrahedron tet{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

  Triangle inside{{0.1f, 0.1f, 0.1f}, {0.2f, 0.15f, 0.1f}, {0.12f, 0.2f, 0.11f}};
  const auto clipped_in = clip_triangle_to_tetrahedron(inside, tet);
  if (clipped_in.empty()) {
    std::cerr << "FAIL: expected inside triangle to survive clipping\n";
    return 1;
  }

  Triangle outside{{2, 2, 2}, {3, 2, 2}, {2, 3, 2}};
  const auto clipped_out = clip_triangle_to_tetrahedron(outside, tet);
  if (!clipped_out.empty()) {
    std::cerr << "FAIL: outside triangle should be fully clipped\n";
    return 1;
  }

  if (!point_inside_tetrahedron({0.2f, 0.2f, 0.2f}, tet)) {
    std::cerr << "FAIL: point_inside_tetrahedron\n";
    return 1;
  }

  return 0;
}
