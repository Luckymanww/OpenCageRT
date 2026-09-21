#include "opencagert/image_parity.h"

#include <algorithm>
#include <cmath>

namespace opencagert {

ImageParityStats compare_rgba8(const uint8_t* a, const uint8_t* b, uint32_t width, uint32_t height,
                               uint32_t row_pitch, const ImageParityOptions& opts) {
  ImageParityStats stats{};
  stats.width = width;
  stats.height = height;
  if (!a || !b || width == 0 || height == 0 || row_pitch < width * 4u) {
    return stats;
  }

  const uint32_t y0 = std::min(opts.skip_top_px, height);
  const uint32_t y1 = height - std::min(opts.skip_bottom_px, height - y0);
  for (uint32_t y = y0; y < y1; ++y) {
    const uint8_t* ra = a + static_cast<size_t>(y) * row_pitch;
    const uint8_t* rb = b + static_cast<size_t>(y) * row_pitch;
    for (uint32_t x = 0; x < width; ++x) {
      const uint8_t* pa = ra + x * 4u;
      const uint8_t* pb = rb + x * 4u;
      uint32_t delta = 0;
      for (int c = 0; c < 3; ++c) {
        const uint32_t d = pa[c] > pb[c] ? pa[c] - pb[c] : pb[c] - pa[c];
        delta = std::max(delta, d);
      }
      ++stats.compared;
      stats.max_channel_delta = std::max(stats.max_channel_delta, delta);
      if (delta > opts.max_channel_delta) {
        ++stats.mismatches;
      }
    }
  }
  if (stats.compared > 0) {
    stats.mismatch_rate = static_cast<float>(stats.mismatches) / static_cast<float>(stats.compared);
  }
  return stats;
}

float percentile_sorted(std::vector<float> values, float p) {
  if (values.empty()) {
    return 0.f;
  }
  std::sort(values.begin(), values.end());
  const float clamped = std::clamp(p, 0.f, 1.f);
  const double idx = clamped * static_cast<double>(values.size() - 1);
  const size_t lo = static_cast<size_t>(idx);
  const size_t hi = std::min(lo + 1, values.size() - 1);
  const float t = static_cast<float>(idx - static_cast<double>(lo));
  return values[lo] * (1.f - t) + values[hi] * t;
}

} // namespace opencagert
