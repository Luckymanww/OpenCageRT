#include "d3d12_app.h"
#include "cli.h"

#include "opencagert/cage_builder.h"
#include "opencagert/types.h"

#include <windows.h>
#include <shellapi.h>

#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>

namespace {

D3D12App* g_app = nullptr;
opencagert::CageAsset g_cage;
std::wstring g_title = L"OpenCageRT v0.1";

opencagert::Mesh make_test_foliage_mesh(int segments) {
  opencagert::Mesh mesh;
  for (int z = 0; z < segments; ++z) {
    for (int x = 0; x < segments; ++x) {
      const float fx = static_cast<float>(x) / static_cast<float>(segments);
      const float fz = static_cast<float>(z) / static_cast<float>(segments);
      const float h = 0.4f + 0.2f * sinf(fx * 6.f) * cosf(fz * 5.f);
      const uint32_t base = static_cast<uint32_t>(mesh.positions.size());
      mesh.positions.push_back({fx, 0.f, fz});
      mesh.positions.push_back({fx + 0.04f, h, fz});
      mesh.positions.push_back({fx, h, fz + 0.04f});
      mesh.indices.push_back(base);
      mesh.indices.push_back(base + 1);
      mesh.indices.push_back(base + 2);
    }
  }
  return mesh;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  switch (msg) {
    case WM_SIZE: {
      if (g_app && wparam != SIZE_MINIMIZED) {
        const uint32_t w = LOWORD(lparam);
        const uint32_t h = HIWORD(lparam);
        g_app->resize(w, h);
      }
      return 0;
    }
    case WM_KEYDOWN:
      if (g_app) {
        g_app->handle_key(wparam);
      }
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      break;
  }
  return DefWindowProc(hwnd, msg, wparam, lparam);
}

void attach_console() {
  if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
    AllocConsole();
  }
  FILE* fp = nullptr;
  freopen_s(&fp, "CONOUT$", "w", stdout);
  freopen_s(&fp, "CONOUT$", "w", stderr);
  freopen_s(&fp, "CONIN$", "r", stdin);
}

} // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_cmd) {
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  BenchmarkCli cli;
  std::string cli_error;
  const bool cli_ok = argv && parse_demo_cli(argc, argv, cli, cli_error);
  if (argv) {
    LocalFree(argv);
  }
  if (!cli_ok) {
    if (!cli_error.empty()) {
      attach_console();
      std::fprintf(stderr, "%s\n", cli_error.c_str());
      MessageBoxA(nullptr, cli_error.c_str(), "OpenCageRT", MB_ICONINFORMATION);
    }
    return 1;
  }
  if (cli.benchmark || cli.parity) {
    attach_console();
  }

  opencagert::CageBuildOptions opts;
  opts.grid_x = opts.grid_y = 6;
  opts.grid_z = 8;
  g_cage = opencagert::build_voxel_cage(make_test_foliage_mesh(24), opts);

  size_t micro_tris = 0;
  for (const auto& m : g_cage.micro_meshes) {
    micro_tris += m.indices.size() / 3;
  }

  std::wstringstream title;
  title << L"OpenCageRT v0.1 | tets=" << g_cage.rest_tets.size() << L" micro_tris=" << micro_tris;
  g_title = title.str();

  WNDCLASS wc{};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = instance;
  wc.lpszClassName = L"OpenCageRTDemoWnd";
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  RegisterClass(&wc);

  const int kWidth = static_cast<int>(cli.width);
  const int kHeight = static_cast<int>(cli.height);
  RECT wr{0, 0, kWidth, kHeight};
  AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
  HWND hwnd = CreateWindow(wc.lpszClassName, g_title.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                           CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top, nullptr, nullptr,
                           instance, nullptr);
  if (!hwnd) {
    return 1;
  }

  D3D12App app;
  g_app = &app;
  std::string error;
  if (!app.initialize(hwnd, cli.width, cli.height, error)) {
    attach_console();
    std::fprintf(stderr, "init failed: %s\n", error.c_str());
    MessageBoxA(hwnd, error.c_str(), "OpenCageRT D3D12 init failed", MB_ICONERROR);
    return 1;
  }

  app.set_vsync(cli.benchmark || cli.parity ? cli.vsync : true);

  std::wstring final_title = g_title + L" | " +
                             std::wstring(app.status_line().begin(), app.status_line().end());
  SetWindowText(hwnd, final_title.c_str());

  ShowWindow(hwnd, show_cmd);
  UpdateWindow(hwnd);

  if (cli.parity) {
    const int code = app.run_parity(error);
    std::fprintf(code == 0 ? stdout : stderr, "%s\n",
                 error.empty() ? app.status_line().c_str() : error.c_str());
    if (code != 0 || !cli.benchmark) {
      app.shutdown();
      g_app = nullptr;
      return code;
    }
  }

  if (cli.benchmark) {
    const int code = app.run_benchmark(cli, error);
    if (code != 0) {
      std::fprintf(stderr, "benchmark failed: %s\n", error.c_str());
    } else {
      std::fprintf(stdout, "wrote %s\n", cli.csv.c_str());
    }
    app.shutdown();
    g_app = nullptr;
    return code;
  }

  MSG msg{};
  while (msg.message != WM_QUIT) {
    if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    } else {
      app.render_frame();
    }
  }

  app.shutdown();
  g_app = nullptr;
  return static_cast<int>(msg.wParam);
}
