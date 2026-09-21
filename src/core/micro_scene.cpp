#include "opencagert/micro_scene.h"

#include "opencagert/cage_builder.h"

#include <algorithm>
#include <cmath>

namespace opencagert {
namespace {

Mesh make_plant_mesh() {
  Mesh mesh;
  auto add_tri = [&](const Vec3& a, const Vec3& b, const Vec3& c) {
    const uint32_t i = static_cast<uint32_t>(mesh.positions.size());
    mesh.positions.push_back(a);
    mesh.positions.push_back(b);
    mesh.positions.push_back(c);
    mesh.indices.push_back(i);
    mesh.indices.push_back(i + 1);
    mesh.indices.push_back(i + 2);
  };

  for (int i = 0; i < 6; ++i) {
    const float a = static_cast<float>(i) * 1.04719755f;
    const float a2 = a + 0.28f;
    const Vec3 base{0.015f * std::cos(a), 0.f, 0.015f * std::sin(a)};
    const Vec3 side{0.11f * std::cos(a), 0.04f, 0.11f * std::sin(a)};
    const Vec3 tip{0.07f * std::cos(a2), 0.62f + 0.06f * std::sin(a * 2.f), 0.07f * std::sin(a2)};
    const Vec3 side2{0.10f * std::cos(a2), 0.05f, 0.10f * std::sin(a2)};
    add_tri(base, side, tip);
    add_tri(base, tip, side2);
  }

  auto midpoint = [](const Vec3& a, const Vec3& b) {
    return Vec3{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, (a.z + b.z) * 0.5f};
  };
  auto subdivide = [&](const Mesh& in) {
    Mesh out;
    out.positions.reserve(in.indices.size() * 4);
    out.indices.reserve(in.indices.size() * 4);
    for (size_t i = 0; i + 2 < in.indices.size(); i += 3) {
      const Vec3 a = in.positions[in.indices[i]];
      const Vec3 b = in.positions[in.indices[i + 1]];
      const Vec3 c = in.positions[in.indices[i + 2]];
      const Vec3 ab = midpoint(a, b);
      const Vec3 bc = midpoint(b, c);
      const Vec3 ca = midpoint(c, a);
      auto tri = [&](const Vec3& p, const Vec3& q, const Vec3& r) {
        const uint32_t idx = static_cast<uint32_t>(out.positions.size());
        out.positions.push_back(p);
        out.positions.push_back(q);
        out.positions.push_back(r);
        out.indices.push_back(idx);
        out.indices.push_back(idx + 1);
        out.indices.push_back(idx + 2);
      };
      tri(a, ab, ca);
      tri(ab, b, bc);
      tri(ca, bc, c);
      tri(ab, bc, ca);
    }
    return out;
  };
  mesh = subdivide(mesh);
  mesh = subdivide(mesh);
  return mesh;
}

Tetrahedron animate_tet(const Tetrahedron& rest, float time_sec) {
  Tetrahedron tet = rest;
  Vec3* verts[] = {&tet.v0, &tet.v1, &tet.v2, &tet.v3};
  for (Vec3* v : verts) {
    const float phase = v->x * 1.8f + v->z * 1.4f;
    const float sway = 0.16f * std::sin(time_sec * 1.35f + phase);
    const float nod = 0.10f * std::cos(time_sec * 1.1f + phase * 0.6f);
    v->x += sway;
    v->y += nod;
    v->z += sway * 0.35f;
  }
  return tet;
}

void pack_rest(MicroScene& scene) {
  CageBuildOptions opts;
  opts.grid_x = opts.grid_y = opts.grid_z = 1;
  opts.padding = 0.18f;
  const CageAsset asset = build_voxel_cage(make_plant_mesh(), opts);
  scene.rest_tets = asset.rest_tets;

  for (const MicroMesh& micro : asset.micro_meshes) {
    if (micro.indices.empty() || micro.positions.empty()) {
      continue;
    }
    if (micro.parent_tet_index >= asset.rest_tets.size() ||
        !invert_mat3(rest_tet_basis(asset.rest_tets[micro.parent_tet_index]))) {
      continue;
    }
    CagePieceGpu piece;
    piece.tet_index = micro.parent_tet_index;
    piece.vertex_offset = static_cast<uint32_t>(scene.cage_rest_vb.size());
    piece.vertex_count = static_cast<uint32_t>(micro.positions.size());
    piece.index_offset = static_cast<uint32_t>(scene.cage_ib.size());
    piece.index_count = static_cast<uint32_t>(micro.indices.size());
    scene.pieces.push_back(piece);

    scene.cage_rest_vb.insert(scene.cage_rest_vb.end(), micro.positions.begin(), micro.positions.end());
    scene.cage_ib.insert(scene.cage_ib.end(), micro.indices.begin(), micro.indices.end());

    const uint32_t classic_base = static_cast<uint32_t>(scene.classic_rest_vb.size());
    scene.classic_rest_vb.insert(scene.classic_rest_vb.end(), micro.positions.begin(),
                                 micro.positions.end());
    for (uint32_t i = 0; i < piece.vertex_count; ++i) {
      scene.classic_vert_tet.push_back(piece.tet_index);
    }
    for (uint32_t idx : micro.indices) {
      scene.classic_ib.push_back(classic_base + idx);
    }
  }
}

} // namespace

void animate_micro_tets_into(const std::vector<Tetrahedron>& rest, float time_sec, bool freeze,
                             std::vector<Tetrahedron>& out) {
  out.resize(rest.size());
  if (freeze) {
    out = rest;
    return;
  }
  for (size_t i = 0; i < rest.size(); ++i) {
    out[i] = animate_tet(rest[i], time_sec);
  }
}

std::vector<Tetrahedron> animate_micro_tets(const std::vector<Tetrahedron>& rest, float time_sec,
                                           bool freeze) {
  std::vector<Tetrahedron> out;
  animate_micro_tets_into(rest, time_sec, freeze, out);
  return out;
}

MicroScene make_micro_scene(float time_sec, bool freeze) {
  MicroScene scene;
  pack_rest(scene);
  scene.animated_tets.resize(scene.rest_tets.size());
  for (size_t i = 0; i < scene.rest_tets.size(); ++i) {
    scene.animated_tets[i] = freeze ? scene.rest_tets[i] : animate_tet(scene.rest_tets[i], time_sec);
  }

  scene.piece_transforms.resize(scene.pieces.size());
  for (size_t p = 0; p < scene.pieces.size(); ++p) {
    const uint32_t ti = scene.pieces[p].tet_index;
    scene.piece_transforms[p] =
        instance_transform_rest_to_animated(scene.rest_tets[ti], scene.animated_tets[ti]);
  }

  scene.classic_vb.resize(scene.classic_rest_vb.size());
  for (size_t i = 0; i < scene.classic_rest_vb.size(); ++i) {
    const uint32_t ti = scene.classic_vert_tet[i];
    const Mat34 xf = instance_transform_rest_to_animated(scene.rest_tets[ti], scene.animated_tets[ti]);
    if (!invert_mat3(rest_tet_basis(scene.rest_tets[ti]))) {
      scene.classic_vb[i] = scene.classic_rest_vb[i];
      continue;
    }
    scene.classic_vb[i] = transform_point(xf, scene.classic_rest_vb[i]);
  }
  return scene;
}

Vec3 instance_grid_offset(uint32_t index, uint32_t count, float spacing) {
  const uint32_t n = std::max(1u, count);
  const uint32_t side =
      std::max(1u, static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(n)))));
  const uint32_t x = index % side;
  const uint32_t z = index / side;
  const float cx = 0.5f * spacing * static_cast<float>(side - 1);
  return {static_cast<float>(x) * spacing - cx, 0.f, static_cast<float>(z) * spacing - cx};
}

} // namespace opencagert
