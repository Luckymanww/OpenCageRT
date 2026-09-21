#include "opencagert/image_parity.h"

#include <iostream>
#include <vector>

namespace {

bool check(bool cond, const char* msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << "\n";
  }
  return cond;
}

} // namespace

int run_parity_tests() {
  int failed = 0;

  const uint32_t w = 4;
  const uint32_t h = 2;
  std::vector<uint8_t> a(w * h * 4, 10);
  std::vector<uint8_t> b = a;
  b[0] = 12; // +2 on first pixel R, under epsilon 8
  opencagert::ImageParityOptions opts{};
  opts.max_channel_delta = 8;
  auto ok = opencagert::compare_rgba8(a.data(), b.data(), w, h, w * 4, opts);
  if (!check(ok.compared == 8 && ok.mismatches == 0 && ok.max_channel_delta == 2,
             "small RGB delta is a match")) {
    ++failed;
  }

  b[4] = 40; // +30 on second pixel
  auto bad = opencagert::compare_rgba8(a.data(), b.data(), w, h, w * 4, opts);
  if (!check(bad.mismatches == 1 && bad.max_channel_delta == 30, "large RGB delta is a mismatch")) {
    ++failed;
  }

  opts.skip_top_px = 1;
  auto skipped = opencagert::compare_rgba8(a.data(), b.data(), w, h, w * 4, opts);
  if (!check(skipped.compared == 4 && skipped.mismatches == 0,
             "skip_top_px drops first row")) {
    ++failed;
  }

  const float med = opencagert::percentile_sorted({1.f, 5.f, 3.f, 4.f, 2.f}, 0.5f);
  if (!check(med >= 2.99f && med <= 3.01f, "median of 1..5 is 3")) {
    ++failed;
  }
  const float p95 = opencagert::percentile_sorted({1.f, 2.f, 3.f, 4.f, 100.f}, 0.95f);
  if (!check(p95 >= 80.f, "p95 is near the tail")) {
    ++failed;
  }

  return failed == 0 ? 0 : 1;
}
