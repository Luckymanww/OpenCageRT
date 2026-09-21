#include "opencagert/animation.h"

#include <cmath>

namespace opencagert {

std::vector<Tetrahedron> animate_cage_wind(const std::vector<Tetrahedron>& rest, float time_sec) {
  std::vector<Tetrahedron> out = rest;
  for (Tetrahedron& tet : out) {
    Vec3* verts[] = {&tet.v0, &tet.v1, &tet.v2, &tet.v3};
    for (Vec3* v : verts) {
      const float phase = v->x * 4.f + v->z * 3.5f;
      const float sway = 0.04f * std::sin(time_sec * 1.7f + phase);
      const float lift = 0.02f * std::cos(time_sec * 2.1f + phase * 0.5f);
      v->x += sway;
      v->y += lift;
      v->z += sway * 0.5f;
    }
  }
  return out;
}

void animate_mesh_wind(Mesh& mesh, const Mesh& rest, float time_sec) {
  mesh.positions = rest.positions;
  for (size_t i = 0; i < mesh.positions.size(); ++i) {
    Vec3& v = mesh.positions[i];
    const Vec3& r = rest.positions[i];
    const float phase = r.x * 4.f + r.z * 3.5f;
    const float sway = 0.04f * std::sin(time_sec * 1.7f + phase);
    const float lift = 0.02f * std::cos(time_sec * 2.1f + phase * 0.5f);
    v.x = r.x + sway;
    v.y = r.y + lift;
    v.z = r.z + sway * 0.5f;
  }
}

} // namespace opencagert
