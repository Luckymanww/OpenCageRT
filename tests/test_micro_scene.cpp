#include "opencagert/micro_scene.h"

#include <cmath>
#include <iostream>

using namespace opencagert;

namespace {

bool nearly_equal(float a, float b, float eps = 1.5e-4f) { return std::fabs(a - b) <= eps; }

bool vec_near(const Vec3& a, const Vec3& b, float eps = 1.5e-4f) {
  return nearly_equal(a.x, b.x, eps) && nearly_equal(a.y, b.y, eps) && nearly_equal(a.z, b.z, eps);
}

bool check(bool cond, const char* msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << "\n";
  }
  return cond;
}

} // namespace

int run_micro_scene_tests() {
  int failed = 0;

  const MicroScene frozen = make_micro_scene(1.25f, true);
  if (!check(!frozen.pieces.empty(), "voxel plant has cage pieces")) {
    ++failed;
  }
  if (!check(frozen.classic_ib.size() >= 3, "classic has triangles")) {
    ++failed;
  }
  if (!check(frozen.rest_tets.size() <= kMicroMaxTets, "tet count within overlay budget")) {
    ++failed;
  }
  if (!frozen.classic_vb.empty()) {
    const bool same = vec_near(frozen.classic_vb[0], frozen.classic_rest_vb[0]);
    if (!check(same, "freeze keeps first vert")) {
      const uint32_t ti = frozen.classic_vert_tet[0];
      const Vec3 a = frozen.classic_rest_vb[0];
      const Vec3 b = frozen.classic_vb[0];
      std::cerr << " tet=" << ti << " rest=(" << a.x << "," << a.y << "," << a.z << ") classic=("
                << b.x << "," << b.y << "," << b.z << ") tets=" << frozen.rest_tets.size() << "\n";
      ++failed;
    }
  }

  const MicroScene moving = make_micro_scene(0.85f, false);
  if (!check(moving.classic_vb.size() == moving.classic_rest_vb.size(), "classic size")) {
    ++failed;
  }
  bool deformed = false;
  for (size_t i = 0; i < moving.classic_vb.size(); ++i) {
    const uint32_t ti = moving.classic_vert_tet[i];
    const Mat34 xf =
        instance_transform_rest_to_animated(moving.rest_tets[ti], moving.animated_tets[ti]);
    const Vec3 again = transform_point(xf, moving.classic_rest_vb[i]);
    if (!check(vec_near(again, moving.classic_vb[i]), "classic vert == T * rest")) {
      ++failed;
      break;
    }
    if (!vec_near(moving.classic_vb[i], moving.classic_rest_vb[i], 1e-3f)) {
      deformed = true;
    }
  }
  if (!check(deformed, "animation moves plant")) {
    ++failed;
  }

  return failed > 0 ? 1 : 0;
}
