#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <windows.h>

#include <opencagert/demo_state.h>
#include <opencagert/dxr_micro.h>

#include <cstdint>
#include <string>
#include <wrl/client.h>

class D3D12App {
 public:
  bool initialize(HWND hwnd, uint32_t width, uint32_t height, std::string& error);
  void resize(uint32_t width, uint32_t height);
  void render_frame();
  void shutdown();
  void handle_key(WPARAM key);

  bool dxr_supported() const { return dxr_supported_; }
  const std::string& status_line() const { return status_line_; }

 private:
  bool create_device(std::string& error);
  bool create_swapchain(HWND hwnd, uint32_t width, uint32_t height, std::string& error);
  void wait_for_gpu();
  void refresh_title();
  void log_ladder_row(const opencagert::DemoMetrics& metrics);
  void start_tour();
  void tick_tour();

  HWND hwnd_ = nullptr;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  bool dxr_supported_ = false;
  std::string status_line_;
  opencagert::DemoState demo_{};

  Microsoft::WRL::ComPtr<IDXGIFactory4> factory_;
  Microsoft::WRL::ComPtr<ID3D12Device> device_;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
  Microsoft::WRL::ComPtr<IDXGISwapChain3> swapchain_;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap_;
  Microsoft::WRL::ComPtr<ID3D12Resource> render_targets_[2];
  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> cmd_alloc_;
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmd_list_;
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> cmd_list4_;
  opencagert::DxrMicro dxr_micro_;
  Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
  HANDLE fence_event_ = nullptr;
  uint64_t fence_value_ = 0;
  uint32_t frame_index_ = 0;
  uint32_t frame_counter_ = 0;
  uint32_t rtv_stride_ = 0;
  LARGE_INTEGER qpc_freq_{};
  LARGE_INTEGER qpc_last_{};
  float frame_ms_ = 16.f;
  opencagert::TriLadder logged_ladder_ = opencagert::TriLadder::Count;
  uint32_t logged_instances_ = 0;
  opencagert::DemoViewMode logged_view_ = opencagert::DemoViewMode::Split;
  bool tour_active_ = false;
  uint32_t tour_step_ = 0;
  LARGE_INTEGER tour_step_start_{};
};
