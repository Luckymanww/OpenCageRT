#pragma once

#include "opencagert/types.h"

#include <vector>

namespace opencagert {

// Piecewise wind wobble on cage vertices (connectivity-preserving).
std::vector<Tetrahedron> animate_cage_wind(const std::vector<Tetrahedron>& rest, float time_sec);

// Same wind field applied to mesh vertices (classic BLAS path).
void animate_mesh_wind(Mesh& mesh, const Mesh& rest, float time_sec);

} // namespace opencagert
