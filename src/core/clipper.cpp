#include "opencagert/clipper.h"

#include "opencagert/math.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace opencagert {
namespace {

struct Plane {
  Vec3 n;
  float d;
};

Plane plane_from_points(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& inside_ref) {
  const Vec3 ab = b - a;
  const Vec3 ac = c - a;
  Vec3 n = cross(ab, ac);
  const float len = length(n);
  if (len > 0.f) {
    n = n * (1.f / len);
  }
  float dist = dot(n, a);
  if (dot(n, inside_ref) - dist < 0.f) {
    n = n * -1.f;
    dist = -dist;
  }
  return {n, dist};
}

float signed_distance(const Plane& p, const Vec3& v) { return dot(p.n, v) - p.d; }

Vec3 lerp_vertex(const Vec3& a, const Vec3& b, float ta, float tb) {
  const float denom = ta - tb;
  if (std::fabs(denom) < 1e-12f) {
    return a;
  }
  const float t = ta / (ta - tb);
  return a + (b - a) * t;
}

std::vector<Vec3> clip_polygon(const std::vector<Vec3>& input, const Plane& plane) {
  if (input.empty()) {
    return {};
  }
  std::vector<Vec3> output;
  output.reserve(input.size() + 1);
  Vec3 prev = input.back();
  float prev_d = signed_distance(plane, prev);
  for (const Vec3& cur : input) {
    const float cur_d = signed_distance(plane, cur);
    const bool prev_in = prev_d >= -1e-6f;
    const bool cur_in = cur_d >= -1e-6f;
    if (prev_in && cur_in) {
      output.push_back(cur);
    } else if (prev_in && !cur_in) {
      output.push_back(lerp_vertex(prev, cur, prev_d, cur_d));
    } else if (!prev_in && cur_in) {
      output.push_back(lerp_vertex(prev, cur, prev_d, cur_d));
      output.push_back(cur);
    }
    prev = cur;
    prev_d = cur_d;
  }
  return output;
}

std::vector<Triangle> fan_triangulate(const std::vector<Vec3>& poly) {
  std::vector<Triangle> tris;
  if (poly.size() < 3) {
    return tris;
  }
  for (size_t i = 1; i + 1 < poly.size(); ++i) {
    tris.push_back({poly[0], poly[i], poly[i + 1]});
  }
  return tris;
}

std::vector<Triangle> clip_triangle_to_planes(const Triangle& tri,
                                              const std::array<Plane, 4>& planes) {
  std::vector<Vec3> poly = {tri.v0, tri.v1, tri.v2};
  for (const Plane& p : planes) {
    poly = clip_polygon(poly, p);
    if (poly.size() < 3) {
      return {};
    }
  }
  return fan_triangulate(poly);
}

} // namespace

bool point_inside_tetrahedron(const Vec3& p, const Tetrahedron& tet, float eps) {
  const std::array<Plane, 4> planes = {
      plane_from_points(tet.v0, tet.v1, tet.v2, tet.v3),
      plane_from_points(tet.v0, tet.v1, tet.v3, tet.v2),
      plane_from_points(tet.v0, tet.v2, tet.v3, tet.v1),
      plane_from_points(tet.v1, tet.v2, tet.v3, tet.v0),
  };
  for (const Plane& pl : planes) {
    if (signed_distance(pl, p) < -eps) {
      return false;
    }
  }
  return true;
}

std::vector<Triangle> clip_triangle_to_tetrahedron(const Triangle& tri, const Tetrahedron& tet) {
  const std::array<Plane, 4> planes = {
      plane_from_points(tet.v0, tet.v1, tet.v2, tet.v3),
      plane_from_points(tet.v0, tet.v1, tet.v3, tet.v2),
      plane_from_points(tet.v0, tet.v2, tet.v3, tet.v1),
      plane_from_points(tet.v1, tet.v2, tet.v3, tet.v0),
  };
  return clip_triangle_to_planes(tri, planes);
}

} // namespace opencagert
