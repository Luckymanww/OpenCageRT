#pragma once

#include <cstdint>

namespace opencagert {

enum class DemoViewMode : uint32_t {
  Split = 0,
  SoloClassic = 1,
  SoloCageRT = 2,
};

enum DemoDebugFlag : uint32_t {
  DemoDebugShowCages = 1u << 0,
  DemoDebugFreezeGeometry = 1u << 1,
  DemoDebugRayPath = 1u << 2,
};

// Scale ladder presets (triangle counts are targets for procedural scenes).
enum class TriLadder : uint32_t {
  K100 = 0,
  M1 = 1,
  M10 = 2,
  M50 = 3,
  Count = 4,
};

inline uint32_t tri_ladder_instances(TriLadder level) {
  switch (level) {
    case TriLadder::K100:
      return 64;
    case TriLadder::M1:
      return 1024;
    case TriLadder::M10:
      return 4096;
    case TriLadder::M50:
      return 25000;
    default:
      return 64;
  }
}

inline constexpr uint32_t kClassicUniqueBlasMax = 4096;

inline uint64_t tri_ladder_target(TriLadder level) {
  switch (level) {
    case TriLadder::K100:
      return 100'000;
    case TriLadder::M1:
      return 1'000'000;
    case TriLadder::M10:
      return 10'000'000;
    case TriLadder::M50:
      return 50'000'000;
    default:
      return 100'000;
  }
}

inline const char* tri_ladder_label(TriLadder level) {
  switch (level) {
    case TriLadder::K100:
      return "64";
    case TriLadder::M1:
      return "1024";
    case TriLadder::M10:
      return "4096";
    case TriLadder::M50:
      return "25k";
    default:
      return "?";
  }
}

struct PathMetrics {
  float vram_mb = 0.f;
  float as_update_ms = 0.f;
  float rt_ms = 0.f;
  float fps = 0.f;
};

struct DemoMetrics {
  PathMetrics classic{};
  PathMetrics cage{};
  TriLadder tri_level = TriLadder::K100;
  uint64_t triangle_count = 0;
  uint32_t instance_count = 0;
  uint32_t tet_count = 0;
  bool using_placeholders = true;
};

struct DemoState {
  DemoViewMode view_mode = DemoViewMode::Split;
  TriLadder tri_level = TriLadder::K100;
  uint32_t debug_flags = DemoDebugShowCages;

  void cycle_view_mode() {
    const auto v = static_cast<uint32_t>(view_mode);
    view_mode = static_cast<DemoViewMode>((v + 1) % 3);
  }

  void step_tri_level(int delta) {
    int level = static_cast<int>(tri_level) + delta;
    if (level < 0) {
      level = 0;
    }
    if (level >= static_cast<int>(TriLadder::Count)) {
      level = static_cast<int>(TriLadder::Count) - 1;
    }
    tri_level = static_cast<TriLadder>(level);
  }

  void toggle_debug(DemoDebugFlag flag) { debug_flags ^= static_cast<uint32_t>(flag); }

  void toggle_classic_cage() {
    view_mode = (view_mode == DemoViewMode::SoloClassic) ? DemoViewMode::SoloCageRT
                                                         : DemoViewMode::SoloClassic;
  }
};

} // namespace opencagert
