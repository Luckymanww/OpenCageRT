#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <cwchar>
#include <cstdlib>
#include <windows.h>

inline std::string wide_to_utf8(const wchar_t* s) {
  if (!s || !*s) {
    return {};
  }
  const int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
  if (n <= 1) {
    return {};
  }
  std::string out(static_cast<size_t>(n - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, s, -1, out.data(), n, nullptr, nullptr);
  return out;
}

struct BenchmarkCli {
  bool benchmark = false;
  bool parity = false;
  bool vsync = false;
  std::vector<uint32_t> instances{64, 1024, 4096, 25000};
  uint32_t warmup = 60;
  uint32_t frames = 240;
  uint32_t width = 1280;
  uint32_t height = 720;
  std::string csv = "results.csv";
};

inline bool parse_u32_list(const std::wstring& s, std::vector<uint32_t>& out) {
  out.clear();
  uint32_t cur = 0;
  bool any = false;
  for (size_t i = 0; i <= s.size(); ++i) {
    const wchar_t c = i < s.size() ? s[i] : L',';
    if (c >= L'0' && c <= L'9') {
      cur = cur * 10 + static_cast<uint32_t>(c - L'0');
      any = true;
    } else if (c == L',' || i == s.size()) {
      if (!any) {
        return false;
      }
      out.push_back(cur);
      cur = 0;
      any = false;
    } else {
      return false;
    }
  }
  return !out.empty();
}

inline bool parse_demo_cli(int argc, wchar_t** argv, BenchmarkCli& cli, std::string& error) {
  for (int i = 1; i < argc; ++i) {
    const std::wstring a = argv[i] ? argv[i] : L"";
    auto need = [&](const char* name) -> const wchar_t* {
      if (i + 1 >= argc) {
        error = std::string("missing value for ") + name;
        return nullptr;
      }
      return argv[++i];
    };
    if (a == L"--benchmark") {
      cli.benchmark = true;
    } else if (a == L"--parity") {
      cli.parity = true;
    } else if (a == L"--vsync") {
      cli.vsync = true;
    } else if (a == L"--instances") {
      const wchar_t* v = need("--instances");
      if (!v) {
        return false;
      }
      if (!parse_u32_list(v, cli.instances)) {
        error = "invalid --instances list";
        return false;
      }
    } else if (a == L"--frames") {
      const wchar_t* v = need("--frames");
      if (!v) {
        return false;
      }
      cli.frames = static_cast<uint32_t>(_wtoi(v));
    } else if (a == L"--warmup") {
      const wchar_t* v = need("--warmup");
      if (!v) {
        return false;
      }
      cli.warmup = static_cast<uint32_t>(_wtoi(v));
    } else if (a == L"--csv") {
      const wchar_t* v = need("--csv");
      if (!v) {
        return false;
      }
      cli.csv = wide_to_utf8(v);
    } else if (a == L"--width") {
      const wchar_t* v = need("--width");
      if (!v) {
        return false;
      }
      cli.width = static_cast<uint32_t>(_wtoi(v));
    } else if (a == L"--height") {
      const wchar_t* v = need("--height");
      if (!v) {
        return false;
      }
      cli.height = static_cast<uint32_t>(_wtoi(v));
    } else if (a == L"--help" || a == L"-h") {
      error = "OpenCageRTDemo [--benchmark] [--parity] [--instances 64,1024,4096,25000] "
              "[--frames 240] [--warmup 60] [--csv results.csv] [--vsync]";
      return false;
    }
  }
  if (cli.frames < 1) {
    error = "--frames must be >= 1";
    return false;
  }
  return true;
}
