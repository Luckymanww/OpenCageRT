#pragma once

#include "opencagert/types.h"

namespace opencagert {

struct CageBuildOptions {
  int grid_x = 4;
  int grid_y = 4;
  int grid_z = 4;
  float padding = 0.05f; // fraction of bbox extent
  float tet_inflate_epsilon = 2.5e-6f; // paper default for non-watertight path
};

// Regular voxel grid → 6 tetrahedra per occupied voxel (Freudenthal split).
// Clips source mesh into per-tet micro-meshes.
CageAsset build_voxel_cage(const Mesh& mesh, const CageBuildOptions& opts = {});

} // namespace opencagert
