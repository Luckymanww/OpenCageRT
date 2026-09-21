#include "d3d12_app.h"
#include "demo_metrics.h"
#include "git_version.h"

#include "opencagert/image_parity.h"

#include <dxgi1_6.h>
#include <d3d12.h>

#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <cwchar>

using Microsoft::WRL::ComPtr;

namespace {

void check_hr(HRESULT hr, const char* msg) {
  if (FAILED(hr)) {
    throw std::runtime_error(msg);
  }
}

} // namespace

bool D3D12App::initialize(HWND hwnd, uint32_t width, uint32_t height, std::string& error) {
  hwnd_ = hwnd;
  width_ = width;
  height_ = height;
  try {
    if (!create_device(error)) {
      return false;
    }
    if (!create_swapchain(hwnd, width, height, error)) {
      return false;
    }

    check_hr(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmd_alloc_)),
             "CreateCommandAllocator");
    check_hr(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmd_alloc_.Get(), nullptr,
                                         IID_PPV_ARGS(&cmd_list_)),
             "CreateCommandList");
    check_hr(cmd_list_.As(&cmd_list4_), "QueryInterface ID3D12GraphicsCommandList4");
    cmd_list_->Close();

    std::string dxr_error;
    if (dxr_supported_) {
      if (!dxr_micro_.initialize(device_.Get(), queue_.Get(), width_, height_, dxr_error)) {
        dxr_supported_ = false;
      }
    }

    check_hr(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "CreateFence");
    fence_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fence_event_) {
      error = "CreateEvent failed";
      return false;
    }

    QueryPerformanceFrequency(&qpc_freq_);
    QueryPerformanceCounter(&qpc_last_);

    if (adapter_) {
      DXGI_QUERY_VIDEO_MEMORY_INFO info{};
      if (SUCCEEDED(adapter_->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) {
        dxgi_baseline_mb_ = static_cast<float>(info.CurrentUsage) / (1024.f * 1024.f);
      }
    }

    std::ostringstream oss;
    oss << "OpenCageRT M3 micro | DXR "
        << (dxr_micro_.is_active() ? "ON" : (dxr_error.empty() ? "OFF" : dxr_error));
    status_line_ = oss.str();
    refresh_title();
    return true;
  } catch (const std::exception& ex) {
    error = ex.what();
    return false;
  }
}

bool D3D12App::create_device(std::string& error) {
  UINT flags = 0;
#if defined(_DEBUG)
  flags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
  if (FAILED(CreateDXGIFactory2(flags, IID_PPV_ARGS(&factory_)))) {
    error = "CreateDXGIFactory2 failed";
    return false;
  }

  ComPtr<IDXGIAdapter1> adapter;
  for (UINT i = 0; factory_->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
    DXGI_ADAPTER_DESC1 desc{};
    adapter->GetDesc1(&desc);
    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
      continue;
    }
    if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&device_)))) {
      adapter.As(&adapter_);
      gpu_name_ = wide_to_utf8(desc.Description);
      LARGE_INTEGER umd{};
      if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd))) {
        const uint64_t ver = static_cast<uint64_t>(umd.QuadPart);
        std::ostringstream drv;
        drv << ((ver >> 48) & 0xFFFF) << '.' << ((ver >> 32) & 0xFFFF) << '.'
            << ((ver >> 16) & 0xFFFF) << '.' << (ver & 0xFFFF);
        driver_version_ = drv.str();
      }
      break;
    }
    device_.Reset();
  }
  if (!device_) {
    error = "D3D12CreateDevice failed (need DX12 Ultimate class GPU)";
    return false;
  }

  D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
  if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)))) {
    dxr_supported_ = options5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
  }

  D3D12_COMMAND_QUEUE_DESC qdesc{};
  qdesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (FAILED(device_->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&queue_)))) {
    error = "CreateCommandQueue failed";
    return false;
  }
  return true;
}

bool D3D12App::create_swapchain(HWND hwnd, uint32_t width, uint32_t height, std::string& error) {
  DXGI_SWAP_CHAIN_DESC1 sc_desc{};
  sc_desc.Width = width;
  sc_desc.Height = height;
  sc_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sc_desc.SampleDesc.Count = 1;
  sc_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sc_desc.BufferCount = 2;
  sc_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

  ComPtr<IDXGISwapChain1> swapchain1;
  if (FAILED(factory_->CreateSwapChainForHwnd(queue_.Get(), hwnd, &sc_desc, nullptr, nullptr,
                                              &swapchain1))) {
    error = "CreateSwapChainForHwnd failed";
    return false;
  }
  swapchain1.As(&swapchain_);

  D3D12_DESCRIPTOR_HEAP_DESC rtv_desc{};
  rtv_desc.NumDescriptors = 2;
  rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  if (FAILED(device_->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&rtv_heap_)))) {
    error = "CreateDescriptorHeap RTV failed";
    return false;
  }

  rtv_stride_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
  for (UINT i = 0; i < 2; ++i) {
    if (FAILED(swapchain_->GetBuffer(i, IID_PPV_ARGS(&render_targets_[i])))) {
      error = "Swapchain GetBuffer failed";
      return false;
    }
    device_->CreateRenderTargetView(render_targets_[i].Get(), nullptr, rtv_handle);
    rtv_handle.ptr += rtv_stride_;
  }
  frame_index_ = swapchain_->GetCurrentBackBufferIndex();
  return true;
}

void D3D12App::resize(uint32_t width, uint32_t height) {
  if (!swapchain_ || width == 0 || height == 0) {
    return;
  }
  wait_for_gpu();
  for (auto& rt : render_targets_) {
    rt.Reset();
  }
  if (FAILED(swapchain_->ResizeBuffers(2, width, height, DXGI_FORMAT_UNKNOWN, 0))) {
    return;
  }
  width_ = width;
  height_ = height;
  D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
  for (UINT i = 0; i < 2; ++i) {
    swapchain_->GetBuffer(i, IID_PPV_ARGS(&render_targets_[i]));
    device_->CreateRenderTargetView(render_targets_[i].Get(), nullptr, rtv_handle);
    rtv_handle.ptr += rtv_stride_;
  }
  frame_index_ = swapchain_->GetCurrentBackBufferIndex();
  if (dxr_micro_.is_active()) {
    dxr_micro_.resize(width_, height_);
  }
}

void D3D12App::render_frame() {
  if (!device_) {
    return;
  }
  wait_for_gpu();
  cmd_alloc_->Reset();
  cmd_list_->Reset(cmd_alloc_.Get(), nullptr);

  ID3D12Resource* backbuffer = render_targets_[frame_index_].Get();
  if (dxr_micro_.is_active() && cmd_list4_) {
    dxr_micro_.render(cmd_list4_.Get(), backbuffer, D3D12_RESOURCE_STATE_PRESENT, frame_counter_,
                      demo_);
  } else {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = backbuffer;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd_list_->ResourceBarrier(1, &barrier);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += frame_index_ * rtv_stride_;
    const float clear[4] = {0.05f, 0.07f, 0.12f, 1.f};
    cmd_list_->ClearRenderTargetView(rtv, clear, 0, nullptr);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    cmd_list_->ResourceBarrier(1, &barrier);
  }
  cmd_list_->Close();

  ID3D12CommandList* lists[] = {cmd_list_.Get()};
  queue_->ExecuteCommandLists(1, lists);
  swapchain_->Present(vsync_ ? 1 : 0, 0);
  ++fence_value_;
  queue_->Signal(fence_.Get(), fence_value_);
  frame_index_ = swapchain_->GetCurrentBackBufferIndex();
  ++frame_counter_;

  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  if (qpc_freq_.QuadPart > 0) {
    frame_ms_ = 1000.f * static_cast<float>(now.QuadPart - qpc_last_.QuadPart) /
                static_cast<float>(qpc_freq_.QuadPart);
  }
  qpc_last_ = now;
  if ((frame_counter_ % 20) == 0) {
    refresh_title();
  }
  tick_tour();
}

void D3D12App::handle_key(WPARAM key) {
  switch (key) {
    case 'C':
    case 'c':
      if (opencagert::tri_ladder_instances(demo_.tri_level) > opencagert::kClassicUniqueBlasMax) {
        demo_.view_mode = opencagert::DemoViewMode::SoloCageRT;
      } else {
        demo_.toggle_classic_cage();
      }
      break;
    case 'S':
    case 's':
      if (opencagert::tri_ladder_instances(demo_.tri_level) > opencagert::kClassicUniqueBlasMax) {
        demo_.view_mode = opencagert::DemoViewMode::SoloCageRT;
      } else {
        demo_.view_mode = opencagert::DemoViewMode::Split;
      }
      break;
    case 'G':
    case 'g':
      demo_.toggle_debug(opencagert::DemoDebugShowCages);
      break;
    case 'F':
    case 'f':
      demo_.toggle_debug(opencagert::DemoDebugFreezeGeometry);
      break;
    case 'R':
    case 'r':
      demo_.toggle_debug(opencagert::DemoDebugRayPath);
      break;
    case 'U':
    case 'u':
      demo_.toggle_classic_blas_mode();
      break;
    case VK_LEFT:
    case VK_OEM_4: // [
      demo_.step_tri_level(-1);
      break;
    case VK_RIGHT:
    case VK_OEM_6: // ]
      demo_.step_tri_level(1);
      break;
    case '1':
      demo_.tri_level = opencagert::TriLadder::K100;
      break;
    case '2':
      demo_.tri_level = opencagert::TriLadder::M1;
      break;
    case '3':
      demo_.tri_level = opencagert::TriLadder::M10;
      break;
    case '4':
      demo_.tri_level = opencagert::TriLadder::M50;
      demo_.view_mode = opencagert::DemoViewMode::SoloCageRT;
      break;
    case 'T':
    case 't':
      if (tour_active_) {
        tour_active_ = false;
      } else {
        start_tour();
      }
      break;
    default:
      return;
  }
  refresh_title();
}

void D3D12App::refresh_title() {
  if (!hwnd_) {
    return;
  }
  opencagert::DemoMetrics metrics = dxr_micro_.is_active()
                                        ? dxr_micro_.metrics()
                                        : compute_placeholder_metrics(demo_.tri_level, frame_ms_);
  fill_adapter_metrics(metrics);
  const float fps = frame_ms_ > 0.f ? 1000.f / frame_ms_ : 0.f;
  if (!metrics.using_placeholders) {
    if (demo_.view_mode == opencagert::DemoViewMode::SoloClassic) {
      metrics.classic.fps = fps;
      metrics.cage.fps = 0.f;
    } else if (demo_.view_mode == opencagert::DemoViewMode::SoloCageRT) {
      metrics.classic.fps = 0.f;
      metrics.cage.fps = fps;
    } else {
      metrics.classic.fps = fps;
      metrics.cage.fps = fps;
    }
  }
  const std::wstring title = format_demo_title(demo_, metrics, L"OpenCageRT");
  if (tour_active_) {
    SetWindowTextW(hwnd_, (L"TOUR " + std::to_wstring(tour_step_ + 1) + L"/5 | " + title).c_str());
  } else {
    SetWindowTextW(hwnd_, title.c_str());
  }
  log_ladder_row(metrics);
}

void D3D12App::log_ladder_row(const opencagert::DemoMetrics& metrics) {
  if (metrics.using_placeholders || metrics.instance_count == 0 ||
      (metrics.classic.tracked_mb <= 0.f && metrics.classic.vram_mb <= 0.f &&
       metrics.cage.tracked_mb <= 0.f && metrics.cage.vram_mb <= 0.f)) {
    return;
  }
  if (logged_ladder_ == metrics.tri_level && logged_instances_ == metrics.instance_count &&
      logged_view_ == demo_.view_mode && logged_classic_mode_ == demo_.classic_blas_mode) {
    return;
  }
  logged_ladder_ = metrics.tri_level;
  logged_instances_ = metrics.instance_count;
  logged_view_ = demo_.view_mode;
  logged_classic_mode_ = demo_.classic_blas_mode;

  wchar_t exe_path[MAX_PATH]{};
  GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
  std::wstring file(exe_path);
  const size_t slash = file.find_last_of(L"\\/");
  if (slash != std::wstring::npos) {
    file.resize(slash + 1);
  }
  file += L"ladder.csv";

  const bool write_header = GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES;
  std::ofstream out(file, std::ios::app);
  if (!out) {
    return;
  }
  if (write_header) {
    out << "view,classic_mode,instances,tris,classic_tracked_mb,classic_geom_mb,classic_as_mb,"
           "cage_tracked_mb,cage_geom_mb,cage_as_mb,ratio,classic_as_ms,cage_tetlas_ms,"
           "classic_rt_ms,cage_rt_ms,rt_combined,fps,dxgi_local_mb\n";
  }
  const char* view = "split";
  if (demo_.view_mode == opencagert::DemoViewMode::SoloClassic) {
    view = "classic";
  } else if (demo_.view_mode == opencagert::DemoViewMode::SoloCageRT) {
    view = "cage";
  }
  const float ratio =
      metrics.cage.tracked_mb > 0.001f ? metrics.classic.tracked_mb / metrics.cage.tracked_mb : 0.f;
  out << view << ',' << opencagert::classic_blas_mode_label(demo_.classic_blas_mode) << ','
      << std::fixed << std::setprecision(3) << metrics.instance_count << ','
      << metrics.triangle_count << ',' << metrics.classic.tracked_mb << ',' << metrics.classic.geom_mb
      << ',' << metrics.classic.as_mb << ',' << metrics.cage.tracked_mb << ',' << metrics.cage.geom_mb
      << ',' << metrics.cage.as_mb << ',' << ratio << ',' << metrics.classic.as_update_ms << ','
      << metrics.cage.as_update_ms << ',' << metrics.classic.rt_ms << ',' << metrics.cage.rt_ms << ','
      << (metrics.rt_times_combined ? 1 : 0) << ',' << metrics.classic.fps << ','
      << metrics.dxgi_local_mb << '\n';
}

void D3D12App::start_tour() {
  tour_active_ = true;
  tour_step_ = 0;
  QueryPerformanceCounter(&tour_step_start_);
  demo_.view_mode = opencagert::DemoViewMode::Split;
  demo_.tri_level = opencagert::TriLadder::K100;
  demo_.debug_flags |= opencagert::DemoDebugShowCages;
  refresh_title();
}

void D3D12App::tick_tour() {
  if (!tour_active_ || qpc_freq_.QuadPart <= 0) {
    return;
  }
  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  const double elapsed = static_cast<double>(now.QuadPart - tour_step_start_.QuadPart) /
                         static_cast<double>(qpc_freq_.QuadPart);
  const double hold = (tour_step_ == 2) ? 5.0 : 4.0;
  if (elapsed < hold) {
    return;
  }

  ++tour_step_;
  QueryPerformanceCounter(&tour_step_start_);
  switch (tour_step_) {
    case 1:
      demo_.tri_level = opencagert::TriLadder::M1;
      demo_.view_mode = opencagert::DemoViewMode::Split;
      break;
    case 2:
      demo_.tri_level = opencagert::TriLadder::M10;
      demo_.view_mode = opencagert::DemoViewMode::Split;
      break;
    case 3:
      demo_.tri_level = opencagert::TriLadder::M10;
      demo_.view_mode = opencagert::DemoViewMode::SoloCageRT;
      break;
    case 4:
      demo_.tri_level = opencagert::TriLadder::M10;
      demo_.view_mode = opencagert::DemoViewMode::SoloClassic;
      break;
    default:
      tour_active_ = false;
      demo_.view_mode = opencagert::DemoViewMode::Split;
      break;
  }
  refresh_title();
}

void D3D12App::fill_adapter_metrics(opencagert::DemoMetrics& metrics) const {
  metrics.gpu_name = gpu_name_;
  metrics.driver_version = driver_version_;
  if (!adapter_) {
    return;
  }
  DXGI_QUERY_VIDEO_MEMORY_INFO info{};
  if (SUCCEEDED(adapter_->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) {
    metrics.dxgi_local_mb = static_cast<float>(info.CurrentUsage) / (1024.f * 1024.f);
    metrics.dxgi_local_delta_mb = metrics.dxgi_local_mb - dxgi_baseline_mb_;
  }
}

opencagert::TriLadder D3D12App::ladder_for_instances(uint32_t instances) const {
  for (uint32_t i = 0; i < static_cast<uint32_t>(opencagert::TriLadder::Count); ++i) {
    const auto level = static_cast<opencagert::TriLadder>(i);
    if (opencagert::tri_ladder_instances(level) == instances) {
      return level;
    }
  }
  return opencagert::TriLadder::Count;
}

int D3D12App::run_benchmark(const BenchmarkCli& cli, std::string& error) {
  if (!dxr_micro_.is_active()) {
    error = "DXR is not active; cannot benchmark";
    return 2;
  }
  vsync_ = cli.vsync;
  demo_.debug_flags &= ~opencagert::DemoDebugShowCages;
  demo_.debug_flags |= opencagert::DemoDebugHideHud;

  std::ofstream out(cli.csv, std::ios::out | std::ios::trunc);
  if (!out) {
    error = "failed to open CSV: " + cli.csv;
    return 3;
  }
  out << "mode,instances,tris,tracked_mb,geom_mb,as_mb,as_update_ms_median,as_update_ms_p95,"
         "rt_ms_median,rt_ms_p95,fps_median,fps_p95,dxgi_local_mb,dxgi_delta_mb,gpu,driver,"
         "resolution,commit,warmup,frames\n";

  struct Mode {
    const char* name;
    opencagert::DemoViewMode view;
    opencagert::ClassicBlasMode classic;
    bool needs_classic;
  };
  const Mode modes[] = {
      {"classic_rebuild", opencagert::DemoViewMode::SoloClassic, opencagert::ClassicBlasMode::Rebuild,
       true},
      {"classic_update", opencagert::DemoViewMode::SoloClassic, opencagert::ClassicBlasMode::Update, true},
      {"cagert", opencagert::DemoViewMode::SoloCageRT, opencagert::ClassicBlasMode::Rebuild, false},
  };

  for (uint32_t instances : cli.instances) {
    demo_.tri_level = ladder_for_instances(instances);
    if (demo_.tri_level == opencagert::TriLadder::Count) {
      error = "unsupported --instances value (use 64,1024,4096,25000)";
      return 7;
    }
    for (const Mode& mode : modes) {
      if (mode.needs_classic && instances > opencagert::kClassicUniqueBlasMax) {
        continue;
      }
      demo_.view_mode = mode.view;
      demo_.classic_blas_mode = mode.classic;
      for (uint32_t i = 0; i < cli.warmup; ++i) {
        render_frame();
      }
      std::vector<float> as_ms;
      std::vector<float> rt_ms;
      std::vector<float> fps;
      as_ms.reserve(cli.frames);
      rt_ms.reserve(cli.frames);
      fps.reserve(cli.frames);
      opencagert::DemoMetrics last{};
      for (uint32_t i = 0; i < cli.frames; ++i) {
        render_frame();
        last = dxr_micro_.metrics();
        fill_adapter_metrics(last);
        const opencagert::PathMetrics& path = mode.needs_classic ? last.classic : last.cage;
        as_ms.push_back(path.as_update_ms);
        rt_ms.push_back(path.rt_ms);
        const float frame_fps = frame_ms_ > 0.f ? 1000.f / frame_ms_ : 0.f;
        fps.push_back(frame_fps);
      }
      const opencagert::PathMetrics& path = mode.needs_classic ? last.classic : last.cage;
      out << mode.name << ',' << last.instance_count << ',' << last.triangle_count << ','
          << std::fixed << std::setprecision(4) << path.tracked_mb << ',' << path.geom_mb << ','
          << path.as_mb << ',' << opencagert::percentile_sorted(as_ms, 0.5f) << ','
          << opencagert::percentile_sorted(as_ms, 0.95f) << ','
          << opencagert::percentile_sorted(rt_ms, 0.5f) << ','
          << opencagert::percentile_sorted(rt_ms, 0.95f) << ','
          << opencagert::percentile_sorted(fps, 0.5f) << ','
          << opencagert::percentile_sorted(fps, 0.95f) << ',' << last.dxgi_local_mb << ','
          << last.dxgi_local_delta_mb << ',' << '"' << gpu_name_ << '"' << ',' << driver_version_
          << ',' << width_ << 'x' << height_ << ',' << OPENCAGERT_GIT_HASH << ',' << cli.warmup
          << ',' << cli.frames << '\n';
    }
  }
  out.flush();
  return 0;
}

int D3D12App::run_parity(std::string& error) {
  if (!dxr_micro_.is_active()) {
    error = "DXR is not active; cannot run parity";
    return 2;
  }
  demo_.tri_level = opencagert::TriLadder::K100;
  demo_.debug_flags = opencagert::DemoDebugFreezeGeometry | opencagert::DemoDebugHideHud;
  demo_.classic_blas_mode = opencagert::ClassicBlasMode::Update;
  vsync_ = false;

  demo_.view_mode = opencagert::DemoViewMode::SoloClassic;
  for (int i = 0; i < 8; ++i) {
    render_frame();
  }
  std::vector<uint8_t> classic;
  uint32_t w = 0;
  uint32_t h = 0;
  uint32_t pitch = 0;
  try {
    if (!dxr_micro_.readback_output_rgba(classic, w, h, pitch, error)) {
      return 4;
    }
  } catch (const std::exception& ex) {
    error = ex.what();
    return 4;
  }

  demo_.view_mode = opencagert::DemoViewMode::SoloCageRT;
  for (int i = 0; i < 8; ++i) {
    render_frame();
  }
  std::vector<uint8_t> cage;
  uint32_t w2 = 0;
  uint32_t h2 = 0;
  uint32_t pitch2 = 0;
  try {
    if (!dxr_micro_.readback_output_rgba(cage, w2, h2, pitch2, error)) {
      return 4;
    }
  } catch (const std::exception& ex) {
    error = ex.what();
    return 4;
  }
  if (w != w2 || h != h2 || pitch != pitch2) {
    error = "parity readback size mismatch";
    return 5;
  }

  opencagert::ImageParityOptions opts{};
  opts.max_channel_delta = 8;
  const auto stats = opencagert::compare_rgba8(classic.data(), cage.data(), w, h, pitch, opts);
  std::ostringstream oss;
  oss << "parity compared=" << stats.compared << " mismatches=" << stats.mismatches
      << " rate=" << std::fixed << std::setprecision(6) << stats.mismatch_rate
      << " max_delta=" << stats.max_channel_delta;
  status_line_ = oss.str();
  if (stats.mismatch_rate > 0.002f) {
    error = status_line_;
    return 6;
  }
  return 0;
}

void D3D12App::wait_for_gpu() {
  if (!fence_) {
    return;
  }
  queue_->Signal(fence_.Get(), ++fence_value_);
  if (fence_->GetCompletedValue() < fence_value_) {
    fence_->SetEventOnCompletion(fence_value_, fence_event_);
    WaitForSingleObject(fence_event_, INFINITE);
  }
}

void D3D12App::shutdown() {
  dxr_micro_.shutdown();
  wait_for_gpu();
  if (fence_event_) {
    CloseHandle(fence_event_);
    fence_event_ = nullptr;
  }
  for (auto& rt : render_targets_) {
    rt.Reset();
  }
  swapchain_.Reset();
  cmd_list_.Reset();
  cmd_alloc_.Reset();
  rtv_heap_.Reset();
  fence_.Reset();
  queue_.Reset();
  device_.Reset();
  adapter_.Reset();
  factory_.Reset();
}
