#include "opencagert/cage_builder.h"

#include <iostream>

int run_cage_builder_tests() {
  opencagert::Mesh mesh;
  mesh.positions = {{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}};
  mesh.indices = {0, 1, 2, 0, 2, 3, 0, 3, 1, 1, 3, 2};

  opencagert::CageBuildOptions opts;
  opts.grid_x = opts.grid_y = opts.grid_z = 2;
  const opencagert::CageAsset asset = opencagert::build_voxel_cage(mesh, opts);

  if (asset.rest_tets.empty()) {
    std::cerr << "FAIL: cage should contain tetrahedra\n";
    return 1;
  }
  if (asset.micro_meshes.empty()) {
    std::cerr << "FAIL: cage should produce micro meshes\n";
    return 1;
  }

  size_t total_tris = 0;
  for (const auto& micro : asset.micro_meshes) {
    total_tris += micro.indices.size() / 3;
  }
  if (total_tris == 0) {
    std::cerr << "FAIL: expected clipped triangles\n";
    return 1;
  }

  return 0;
}
