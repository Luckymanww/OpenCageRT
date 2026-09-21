#include "demo_metrics.h"

#include "opencagert/demo_state.h"

#include <algorithm>
#include <cmath>

namespace {

float ladder_stress(opencagert::TriLadder level) {
  switch (level) {
    case opencagert::TriLadder::K100:
      return 0.0f;
    case opencagert::TriLadder::M1:
      return 0.35f;
    case opencagert::TriLadder::M10:
      return 0.65f;
    case opencagert::TriLadder::M50:
      return 1.0f;
    default:
      return 0.f;
  }
}

} // namespace

opencagert::DemoMetrics compute_placeholder_metrics(opencagert::TriLadder level, float frame_ms) {
  opencagert::DemoMetrics m{};
  m.tri_level = level;
  m.using_placeholders = true;

  const float s = ladder_stress(level);
  const float fps = frame_ms > 0.f ? 1000.f / frame_ms : 0.f;

  m.classic.vram_mb = 800.f + s * 11200.f;
  m.classic.as_update_ms = 0.4f + s * 14.f;
  m.classic.rt_ms = 1.2f + s * 22.f;
  m.classic.fps = std::max(8.f, 120.f - s * 92.f);

  m.cage.vram_mb = 180.f + s * 420.f;
  m.cage.as_update_ms = 0.15f + s * 1.2f;
  m.cage.rt_ms = 1.1f + s * 4.5f;
  m.cage.fps = std::max(24.f, 118.f - s * 28.f);

  // Until real timers exist, nudge FPS from measured frame time slightly.
  const float blend = 0.15f;
  m.classic.fps = m.classic.fps * (1.f - blend) + fps * (1.f - s * 0.5f) * blend;
  m.cage.fps = m.cage.fps * (1.f - blend) + fps * blend;

  return m;
}
