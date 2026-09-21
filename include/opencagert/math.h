#pragma once

#include "opencagert/types.h"

#include <array>
#include <optional>

namespace opencagert {

struct Mat3 {
  // column-major: col0 = c0, col1 = c1, col2 = c2
  Vec3 c0;
  Vec3 c1;
  Vec3 c2;
};

struct Mat34 {
  // 3x4 row layout for DXR instance matrix (rows)
  float m[3][4]{};
};

float dot(const Vec3& a, const Vec3& b);
Vec3 cross(const Vec3& a, const Vec3& b);
float length(const Vec3& v);
Vec3 normalize(const Vec3& v);

Mat3 mat3_from_columns(const Vec3& c0, const Vec3& c1, const Vec3& c2);
std::optional<Mat3> invert_mat3(const Mat3& m);

// Rest tet basis M: columns (v1-v0, v2-v0, v3-v0)
Mat3 rest_tet_basis(const Tetrahedron& rest);

// Animated matrix A: columns (a1-a0, a2-a0, a3-a0), translation a0 in column 3 when extended
Mat34 animated_tet_matrix(const Tetrahedron& animated);

Mat34 multiply_mat34(const Mat34& a, const Mat34& b);
Mat34 mat3_to_mat34(const Mat3& m);
Mat34 invert_mat34(const Mat34& m);
Mat34 mat34_translate(const Vec3& t);

// DXR instance transform: O * A * (M^-1)_3x4 — here O is identity (object at origin)
Mat34 instance_transform_rest_to_animated(const Tetrahedron& rest,
                                          const Tetrahedron& animated);

Vec3 transform_point(const Mat34& t, const Vec3& p);

struct Aabb {
  Vec3 min;
  Vec3 max;
};

Aabb merge_aabb(const Aabb& a, const Aabb& b);
Aabb aabb_from_mesh(const MicroMesh& mesh);

} // namespace opencagert
