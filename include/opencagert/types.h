#pragma once

#include <cstdint>
#include <vector>

namespace opencagert {

struct Vec3 {
  float x = 0.f;
  float y = 0.f;
  float z = 0.f;

  Vec3() = default;
  Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

  Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
  Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
  Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
};

struct Triangle {
  Vec3 v0;
  Vec3 v1;
  Vec3 v2;
};

struct Tetrahedron {
  Vec3 v0;
  Vec3 v1;
  Vec3 v2;
  Vec3 v3;
};

struct Mesh {
  std::vector<Vec3> positions;
  std::vector<uint32_t> indices; // triplets
};

struct MicroMesh {
  std::vector<Vec3> positions;
  std::vector<uint32_t> indices;
  uint32_t parent_tet_index = 0;
};

struct CageAsset {
  std::vector<Tetrahedron> rest_tets;
  std::vector<MicroMesh> micro_meshes;
  Mesh source_mesh;
};

} // namespace opencagert
