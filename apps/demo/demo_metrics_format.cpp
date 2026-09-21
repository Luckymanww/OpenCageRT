#include "demo_metrics.h"

#include <sstream>
#include <iomanip>

namespace {

std::wstring narrow_to_wide(const std::string& s) {
  return std::wstring(s.begin(), s.end());
}

const char* view_mode_label(opencagert::DemoViewMode mode) {
  switch (mode) {
    case opencagert::DemoViewMode::Split:
      return "SPLIT";
    case opencagert::DemoViewMode::SoloClassic:
      return "Classic";
    case opencagert::DemoViewMode::SoloCageRT:
      return "CageRT";
    default:
      return "?";
  }
}

} // namespace

std::wstring format_demo_title(const opencagert::DemoState& state, const opencagert::DemoMetrics& metrics,
                               const std::wstring& base_title) {
  std::wostringstream oss;
  oss << base_title << L" M3 field | inst=" << metrics.instance_count << L" tris="
      << metrics.triangle_count;
  if (metrics.cage.vram_mb > 0.001f) {
    oss << L" | x" << std::fixed << std::setprecision(1)
        << (metrics.classic.vram_mb / metrics.cage.vram_mb);
  }
  oss << L" | View " << narrow_to_wide(view_mode_label(state.view_mode));

  if (state.debug_flags & opencagert::DemoDebugShowCages) {
    oss << L" | cages";
  }
  if (state.debug_flags & opencagert::DemoDebugFreezeGeometry) {
    oss << L" | freeze";
  }
  if (state.debug_flags & opencagert::DemoDebugRayPath) {
    oss << L" | raydbg";
  }

  oss << L" || Classic VRAM=" << std::fixed << std::setprecision(2) << metrics.classic.vram_mb << L"MB"
      << L" AS=" << std::setprecision(2) << metrics.classic.as_update_ms << L"ms"
      << L" RT=" << metrics.classic.rt_ms << L"ms"
      << L" FPS=" << std::setprecision(0) << metrics.classic.fps;

  oss << L" || Cage VRAM=" << std::setprecision(2) << metrics.cage.vram_mb << L"MB"
      << L" tetLAS=" << metrics.cage.as_update_ms << L"ms"
      << L" RT=" << metrics.cage.rt_ms << L"ms"
      << L" FPS=" << std::setprecision(0) << metrics.cage.fps;

  if (metrics.using_placeholders) {
    oss << L" (HUD est.)";
  }

  return oss.str();
}
