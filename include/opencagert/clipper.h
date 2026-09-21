#pragma once

#include "opencagert/types.h"

#include <vector>

namespace opencagert {

// Clip a triangle to the interior of a tetrahedron (4 half-spaces).
// Returns zero or more triangles. Vertices on shared faces are deduplicated by caller.
std::vector<Triangle> clip_triangle_to_tetrahedron(const Triangle& tri,
                                                   const Tetrahedron& tet);

bool point_inside_tetrahedron(const Vec3& p, const Tetrahedron& tet, float eps = 1e-5f);

} // namespace opencagert
