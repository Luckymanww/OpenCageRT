#include "opencagert/math.h"
#include "opencagert/types.h"

#include <cmath>
#include <iostream>

using namespace opencagert;

namespace {

bool nearly_equal(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

bool check(bool cond, const char* msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << "\n";
  }
  return cond;
}

} // namespace

int run_math_tests() {
  int failed = 0;

  Tetrahedron rest{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  Tetrahedron animated = rest;
  animated.v1 = {1.2f, 0.1f, 0.f};
  animated.v2 = {0.f, 1.1f, 0.f};
  animated.v3 = {0.f, 0.f, 1.05f};

  const Mat34 xf = instance_transform_rest_to_animated(rest, animated);
  const Vec3 p_rest{0.25f, 0.25f, 0.25f};

  const Mat34 id = instance_transform_rest_to_animated(rest, rest);
  const Vec3 unchanged = transform_point(id, p_rest);
  if (!check(nearly_equal(unchanged.x, p_rest.x) && nearly_equal(unchanged.y, p_rest.y) &&
                 nearly_equal(unchanged.z, p_rest.z),
             "rest==animated is identity")) {
    ++failed;
  }

  const Vec3 mapped_v0 = transform_point(xf, rest.v0);
  const Vec3 mapped_v1 = transform_point(xf, rest.v1);
  if (!check(nearly_equal(mapped_v0.x, animated.v0.x) && nearly_equal(mapped_v0.y, animated.v0.y) &&
                 nearly_equal(mapped_v0.z, animated.v0.z),
             "T maps rest v0 to animated v0")) {
    ++failed;
  }
  if (!check(nearly_equal(mapped_v1.x, animated.v1.x) && nearly_equal(mapped_v1.y, animated.v1.y) &&
                 nearly_equal(mapped_v1.z, animated.v1.z),
             "T maps rest v1 to animated v1")) {
    ++failed;
  }

  const Mat34 inv = invert_mat34(xf);
  const Vec3 roundtrip = transform_point(xf, transform_point(inv, p_rest));
  if (!check(nearly_equal(roundtrip.x, p_rest.x), "mat34 invert x")) {
    ++failed;
  }
  if (!check(nearly_equal(roundtrip.y, p_rest.y), "mat34 invert y")) {
    ++failed;
  }
  if (!check(nearly_equal(roundtrip.z, p_rest.z), "mat34 invert z")) {
    ++failed;
  }

  return failed > 0 ? 1 : 0;
}
