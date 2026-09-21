#pragma once

#include "opencagert/math.h"
#include "opencagert/types.h"

#include <cstdint>
#include <vector>

namespace opencagert {

constexpr uint32_t kMicroMaxTets = 128;

struct CagePieceGpu {
  uint32_t tet_index = 0;
  uint32_t vertex_offset = 0;
  uint32_t vertex_count = 0;
  uint32_t index_offset = 0;
  uint32_t index_count = 0;
};

// 3x3 plant patch in a coarse voxel cage. Classic verts = T_tet * rest.
struct MicroScene {
  std::vector<Tetrahedron> rest_tets;
  std::vector<Tetrahedron> animated_tets;
  std::vector<Vec3> cage_rest_vb;
  std::vector<uint32_t> cage_ib;
  std::vector<CagePieceGpu> pieces;
  std::vector<Vec3> classic_rest_vb;
  std::vector<uint32_t> classic_ib;
  std::vector<uint32_t> classic_vert_tet;
  std::vector<Vec3> classic_vb;
  std::vector<Mat34> piece_transforms;
};

MicroScene make_micro_scene(float time_sec, bool freeze);
std::vector<Tetrahedron> animate_micro_tets(const std::vector<Tetrahedron>& rest, float time_sec,
                                           bool freeze);
void animate_micro_tets_into(const std::vector<Tetrahedron>& rest, float time_sec, bool freeze,
                             std::vector<Tetrahedron>& out);
Vec3 instance_grid_offset(uint32_t index, uint32_t count, float spacing = 0.55f);

} // namespace opencagert
