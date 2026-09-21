#pragma once

#include <cstdint>
#include <vector>

namespace opencagert {

struct ImageParityStats {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t compared = 0;
  uint32_t mismatches = 0;
  uint32_t max_channel_delta = 0;
  float mismatch_rate = 0.f;
};

struct ImageParityOptions {
  uint32_t skip_top_px = 0;
  uint32_t skip_bottom_px = 0;
  uint32_t max_channel_delta = 8;
};

// Tightly packed RGBA8 (row_pitch == width * 4) or pitched rows.
ImageParityStats compare_rgba8(const uint8_t* a, const uint8_t* b, uint32_t width, uint32_t height,
                               uint32_t row_pitch, const ImageParityOptions& opts);

float percentile_sorted(std::vector<float> values, float p);

} // namespace opencagert
