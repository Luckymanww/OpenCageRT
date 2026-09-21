#include "opencagert/math.h"

#include <cmath>
#include <cstring>

namespace opencagert {

float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

float length(const Vec3& v) { return std::sqrt(dot(v, v)); }

Vec3 normalize(const Vec3& v) {
  const float len = length(v);
  if (len <= 0.f) {
    return {};
  }
  return v * (1.f / len);
}

Mat3 mat3_from_columns(const Vec3& c0, const Vec3& c1, const Vec3& c2) {
  return {c0, c1, c2};
}

std::optional<Mat3> invert_mat3(const Mat3& m) {
  const Vec3 r0 = {m.c0.x, m.c1.x, m.c2.x};
  const Vec3 r1 = {m.c0.y, m.c1.y, m.c2.y};
  const Vec3 r2 = {m.c0.z, m.c1.z, m.c2.z};

  const Vec3 c0 = cross(r1, r2);
  const Vec3 c1 = cross(r2, r0);
  const Vec3 c2 = cross(r0, r1);

  const float det = dot(r0, c0);
  if (std::fabs(det) < 1e-12f) {
    return std::nullopt;
  }

  const float inv_det = 1.f / det;
  Mat3 inv;
  inv.c0 = c0 * inv_det;
  inv.c1 = c1 * inv_det;
  inv.c2 = c2 * inv_det;
  return inv;
}

Mat3 rest_tet_basis(const Tetrahedron& rest) {
  return mat3_from_columns(rest.v1 - rest.v0, rest.v2 - rest.v0, rest.v3 - rest.v0);
}

Mat34 animated_tet_matrix(const Tetrahedron& animated) {
  Mat34 a{};
  a.m[0][0] = animated.v1.x - animated.v0.x;
  a.m[0][1] = animated.v2.x - animated.v0.x;
  a.m[0][2] = animated.v3.x - animated.v0.x;
  a.m[0][3] = animated.v0.x;

  a.m[1][0] = animated.v1.y - animated.v0.y;
  a.m[1][1] = animated.v2.y - animated.v0.y;
  a.m[1][2] = animated.v3.y - animated.v0.y;
  a.m[1][3] = animated.v0.y;

  a.m[2][0] = animated.v1.z - animated.v0.z;
  a.m[2][1] = animated.v2.z - animated.v0.z;
  a.m[2][2] = animated.v3.z - animated.v0.z;
  a.m[2][3] = animated.v0.z;
  return a;
}

Mat34 mat3_to_mat34(const Mat3& m) {
  Mat34 out{};
  out.m[0][0] = m.c0.x;
  out.m[0][1] = m.c1.x;
  out.m[0][2] = m.c2.x;
  out.m[1][0] = m.c0.y;
  out.m[1][1] = m.c1.y;
  out.m[1][2] = m.c2.y;
  out.m[2][0] = m.c0.z;
  out.m[2][1] = m.c1.z;
  out.m[2][2] = m.c2.z;
  return out;
}

Mat34 mat34_translate(const Vec3& t) {
  Mat34 out{};
  out.m[0][0] = 1.f;
  out.m[1][1] = 1.f;
  out.m[2][2] = 1.f;
  out.m[0][3] = t.x;
  out.m[1][3] = t.y;
  out.m[2][3] = t.z;
  return out;
}

Mat34 multiply_mat34(const Mat34& a, const Mat34& b) {
  Mat34 out{};
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      out.m[r][c] = a.m[r][0] * b.m[0][c] + a.m[r][1] * b.m[1][c] + a.m[r][2] * b.m[2][c];
    }
    out.m[r][3] =
        a.m[r][0] * b.m[0][3] + a.m[r][1] * b.m[1][3] + a.m[r][2] * b.m[2][3] + a.m[r][3];
  }
  return out;
}

Mat34 invert_mat34(const Mat34& m) {
  Mat3 rot{{m.m[0][0], m.m[1][0], m.m[2][0]},
           {m.m[0][1], m.m[1][1], m.m[2][1]},
           {m.m[0][2], m.m[1][2], m.m[2][2]}};
  const auto inv_rot = invert_mat3(rot);
  Mat34 out{};
  if (!inv_rot) {
    return out;
  }
  const Vec3 t = {m.m[0][3], m.m[1][3], m.m[2][3]};
  const Vec3 inv_t = {
      -(inv_rot->c0.x * t.x + inv_rot->c1.x * t.y + inv_rot->c2.x * t.z),
      -(inv_rot->c0.y * t.x + inv_rot->c1.y * t.y + inv_rot->c2.y * t.z),
      -(inv_rot->c0.z * t.x + inv_rot->c1.z * t.y + inv_rot->c2.z * t.z),
  };

  out.m[0][0] = inv_rot->c0.x;
  out.m[0][1] = inv_rot->c1.x;
  out.m[0][2] = inv_rot->c2.x;
  out.m[0][3] = inv_t.x;

  out.m[1][0] = inv_rot->c0.y;
  out.m[1][1] = inv_rot->c1.y;
  out.m[1][2] = inv_rot->c2.y;
  out.m[1][3] = inv_t.y;

  out.m[2][0] = inv_rot->c0.z;
  out.m[2][1] = inv_rot->c1.z;
  out.m[2][2] = inv_rot->c2.z;
  out.m[2][3] = inv_t.z;
  return out;
}

Mat34 instance_transform_rest_to_animated(const Tetrahedron& rest,
                                          const Tetrahedron& animated) {
  const Mat3 m = rest_tet_basis(rest);
  const auto m_inv = invert_mat3(m);
  if (!m_inv) {
    return {};
  }
  // u = M^-1 (p - rest.v0);  p' = A_lin u + animated.v0
  Mat34 m_inv34 = mat3_to_mat34(*m_inv);
  const Vec3 r0 = rest.v0;
  m_inv34.m[0][3] = -(m_inv->c0.x * r0.x + m_inv->c1.x * r0.y + m_inv->c2.x * r0.z);
  m_inv34.m[1][3] = -(m_inv->c0.y * r0.x + m_inv->c1.y * r0.y + m_inv->c2.y * r0.z);
  m_inv34.m[2][3] = -(m_inv->c0.z * r0.x + m_inv->c1.z * r0.y + m_inv->c2.z * r0.z);
  return multiply_mat34(animated_tet_matrix(animated), m_inv34);
}

Vec3 transform_point(const Mat34& t, const Vec3& p) {
  return {t.m[0][0] * p.x + t.m[0][1] * p.y + t.m[0][2] * p.z + t.m[0][3],
          t.m[1][0] * p.x + t.m[1][1] * p.y + t.m[1][2] * p.z + t.m[1][3],
          t.m[2][0] * p.x + t.m[2][1] * p.y + t.m[2][2] * p.z + t.m[2][3]};
}

Aabb merge_aabb(const Aabb& a, const Aabb& b) {
  return {{std::min(a.min.x, b.min.x), std::min(a.min.y, b.min.y), std::min(a.min.z, b.min.z)},
          {std::max(a.max.x, b.max.x), std::max(a.max.y, b.max.y), std::max(a.max.z, b.max.z)}};
}

Aabb aabb_from_mesh(const MicroMesh& mesh) {
  Aabb box{{1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}};
  for (const Vec3& p : mesh.positions) {
    box.min.x = std::min(box.min.x, p.x);
    box.min.y = std::min(box.min.y, p.y);
    box.min.z = std::min(box.min.z, p.z);
    box.max.x = std::max(box.max.x, p.x);
    box.max.y = std::max(box.max.y, p.y);
    box.max.z = std::max(box.max.z, p.z);
  }
  return box;
}

} // namespace opencagert
