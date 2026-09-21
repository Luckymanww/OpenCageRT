#include "opencagert/cage_builder.h"

#include "opencagert/clipper.h"
#include "opencagert/math.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace opencagert {
namespace {

struct GridKey {
  int x;
  int y;
  int z;
  bool operator==(const GridKey& o) const { return x == o.x && y == o.y && z == o.z; }
};

struct GridKeyHash {
  size_t operator()(const GridKey& k) const {
    return (static_cast<size_t>(k.x) * 73856093u) ^ (static_cast<size_t>(k.y) * 19349663u) ^
           (static_cast<size_t>(k.z) * 83492791u);
  }
};

Aabb mesh_bounds(const Mesh& mesh) {
  Aabb b{{1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}};
  for (const Vec3& p : mesh.positions) {
    b.min.x = std::min(b.min.x, p.x);
    b.min.y = std::min(b.min.y, p.y);
    b.min.z = std::min(b.min.z, p.z);
    b.max.x = std::max(b.max.x, p.x);
    b.max.y = std::max(b.max.y, p.y);
    b.max.z = std::max(b.max.z, p.z);
  }
  return b;
}

Vec3 cell_corner(const Aabb& b, int gx, int gy, int gz, int nx, int ny, int nz, float pad) {
  const Vec3 extent = b.max - b.min;
  const Vec3 min = b.min - extent * pad;
  const Vec3 max = b.max + extent * pad;
  const float fx = static_cast<float>(gx) / static_cast<float>(nx);
  const float fy = static_cast<float>(gy) / static_cast<float>(ny);
  const float fz = static_cast<float>(gz) / static_cast<float>(nz);
  return {min.x + (max.x - min.x) * fx, min.y + (max.y - min.y) * fy, min.z + (max.z - min.z) * fz};
}

void inflate_tet(Tetrahedron& tet, float eps) {
  const Vec3 center = (tet.v0 + tet.v1 + tet.v2 + tet.v3) * 0.25f;
  auto push = [&](Vec3& v) {
    const Vec3 dir = normalize(v - center);
    v = v + dir * eps;
  };
  push(tet.v0);
  push(tet.v1);
  push(tet.v2);
  push(tet.v3);
}

std::array<Tetrahedron, 6> freudenthal_tets(const Vec3& c000, const Vec3& c100, const Vec3& c010,
                                            const Vec3& c110, const Vec3& c001, const Vec3& c101,
                                            const Vec3& c011, const Vec3& c111) {
  // Kuhn split along the cube diagonal c000–c111 (all 6 tets have volume).
  return {Tetrahedron{c000, c100, c110, c111}, Tetrahedron{c000, c110, c010, c111},
          Tetrahedron{c000, c010, c011, c111}, Tetrahedron{c000, c011, c001, c111},
          Tetrahedron{c000, c001, c101, c111}, Tetrahedron{c000, c101, c100, c111}};
}

bool voxel_intersects_mesh(const Aabb& cell, const Mesh& mesh) {
  for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
    const Vec3& a = mesh.positions[mesh.indices[i]];
    const Vec3& b = mesh.positions[mesh.indices[i + 1]];
    const Vec3& c = mesh.positions[mesh.indices[i + 2]];
    Aabb tri{{std::min({a.x, b.x, c.x}), std::min({a.y, b.y, c.y}), std::min({a.z, b.z, c.z})},
             {std::max({a.x, b.x, c.x}), std::max({a.y, b.y, c.y}), std::max({a.z, b.z, c.z})}};
    if (tri.max.x < cell.min.x || tri.min.x > cell.max.x) {
      continue;
    }
    if (tri.max.y < cell.min.y || tri.min.y > cell.max.y) {
      continue;
    }
    if (tri.max.z < cell.min.z || tri.min.z > cell.max.z) {
      continue;
    }
    return true;
  }
  return false;
}

struct QuantizedKey {
  int64_t x;
  int64_t y;
  int64_t z;
  bool operator==(const QuantizedKey& o) const { return x == o.x && y == o.y && z == o.z; }
};

struct QuantizedHash {
  size_t operator()(const QuantizedKey& k) const {
    return (static_cast<size_t>(k.x) * 73856093u) ^ (static_cast<size_t>(k.y) * 19349663u) ^
           (static_cast<size_t>(k.z) * 83492791u);
  }
};

QuantizedKey quantize(const Vec3& v, float scale = 1e6f) {
  return {static_cast<int64_t>(std::llround(v.x * scale)),
          static_cast<int64_t>(std::llround(v.y * scale)),
          static_cast<int64_t>(std::llround(v.z * scale))};
}

uint32_t get_or_add_vertex(MicroMesh& mesh, std::unordered_map<QuantizedKey, uint32_t, QuantizedHash>& map,
                           const Vec3& v) {
  const QuantizedKey key = quantize(v);
  const auto it = map.find(key);
  if (it != map.end()) {
    return it->second;
  }
  const uint32_t index = static_cast<uint32_t>(mesh.positions.size());
  mesh.positions.push_back(v);
  map[key] = index;
  return index;
}

void append_triangle(MicroMesh& mesh, std::unordered_map<QuantizedKey, uint32_t, QuantizedHash>& map,
                     const Triangle& tri) {
  mesh.indices.push_back(get_or_add_vertex(mesh, map, tri.v0));
  mesh.indices.push_back(get_or_add_vertex(mesh, map, tri.v1));
  mesh.indices.push_back(get_or_add_vertex(mesh, map, tri.v2));
}

} // namespace

CageAsset build_voxel_cage(const Mesh& mesh, const CageBuildOptions& opts) {
  CageAsset asset;
  asset.source_mesh = mesh;
  if (mesh.indices.size() < 3) {
    return asset;
  }

  const Aabb bounds = mesh_bounds(mesh);
  const int nx = std::max(1, opts.grid_x);
  const int ny = std::max(1, opts.grid_y);
  const int nz = std::max(1, opts.grid_z);

  std::vector<Tetrahedron> tets;
  tets.reserve(static_cast<size_t>(nx * ny * nz * 6));

  for (int z = 0; z < nz; ++z) {
    for (int y = 0; y < ny; ++y) {
      for (int x = 0; x < nx; ++x) {
        const Vec3 c000 = cell_corner(bounds, x, y, z, nx, ny, nz, opts.padding);
        const Vec3 c100 = cell_corner(bounds, x + 1, y, z, nx, ny, nz, opts.padding);
        const Vec3 c010 = cell_corner(bounds, x, y + 1, z, nx, ny, nz, opts.padding);
        const Vec3 c110 = cell_corner(bounds, x + 1, y + 1, z, nx, ny, nz, opts.padding);
        const Vec3 c001 = cell_corner(bounds, x, y, z + 1, nx, ny, nz, opts.padding);
        const Vec3 c101 = cell_corner(bounds, x + 1, y, z + 1, nx, ny, nz, opts.padding);
        const Vec3 c011 = cell_corner(bounds, x, y + 1, z + 1, nx, ny, nz, opts.padding);
        const Vec3 c111 = cell_corner(bounds, x + 1, y + 1, z + 1, nx, ny, nz, opts.padding);

        Aabb cell;
        cell.min = c000;
        cell.max = c111;
        if (!voxel_intersects_mesh(cell, mesh)) {
          continue;
        }

        for (Tetrahedron tet : freudenthal_tets(c000, c100, c010, c110, c001, c101, c011, c111)) {
          inflate_tet(tet, opts.tet_inflate_epsilon);
          tets.push_back(tet);
        }
      }
    }
  }

  asset.rest_tets = std::move(tets);
  asset.micro_meshes.resize(asset.rest_tets.size());

  for (size_t t = 0; t < asset.rest_tets.size(); ++t) {
    MicroMesh& micro = asset.micro_meshes[t];
    micro.parent_tet_index = static_cast<uint32_t>(t);
    std::unordered_map<QuantizedKey, uint32_t, QuantizedHash> vmap;

    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
      Triangle tri{mesh.positions[mesh.indices[i]], mesh.positions[mesh.indices[i + 1]],
                   mesh.positions[mesh.indices[i + 2]]};
      for (const Triangle& clipped : clip_triangle_to_tetrahedron(tri, asset.rest_tets[t])) {
        append_triangle(micro, vmap, clipped);
      }
    }
  }

  asset.micro_meshes.erase(
      std::remove_if(asset.micro_meshes.begin(), asset.micro_meshes.end(),
                     [](const MicroMesh& m) { return m.indices.empty(); }),
      asset.micro_meshes.end());

  return asset;
}

} // namespace opencagert
