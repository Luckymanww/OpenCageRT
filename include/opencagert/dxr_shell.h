#pragma once

#include "opencagert/demo_state.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>

namespace opencagert {

// M1: minimal DXR — raygen writes a UAV; no BLAS/TLAS.
class DxrShell {
 public:
  bool initialize(ID3D12Device* device, ID3D12CommandQueue* queue, uint32_t width, uint32_t height,
                  std::string& error);
  void shutdown();
  void resize(uint32_t width, uint32_t height);
  void render(ID3D12GraphicsCommandList4* cmd_list, ID3D12Resource* backbuffer,
              D3D12_RESOURCE_STATES backbuffer_state, uint32_t frame_index, const DemoState& demo);

  bool is_active() const { return active_; }
  D3D12_RAYTRACING_TIER tier() const { return tier_; }

 private:
  bool create_descriptors(std::string& error);
  bool create_output(uint32_t width, uint32_t height, std::string& error);
  bool create_pipeline(std::string& error);
  void wait_for_gpu();

  Microsoft::WRL::ComPtr<ID3D12Device5> device_;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> upload_alloc_;
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> upload_list_;
  Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
  HANDLE fence_event_ = nullptr;
  uint64_t fence_value_ = 0;

  Microsoft::WRL::ComPtr<ID3D12RootSignature> root_sig_;
  Microsoft::WRL::ComPtr<ID3D12StateObject> rt_pso_;
  Microsoft::WRL::ComPtr<ID3D12Resource> shader_table_;
  uint32_t shader_record_size_ = 0;

  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> cbv_uav_heap_;
  Microsoft::WRL::ComPtr<ID3D12Resource> constant_buffer_;
  void* constant_cpu_ = nullptr;
  Microsoft::WRL::ComPtr<ID3D12Resource> output_texture_;

  uint32_t width_ = 0;
  uint32_t height_ = 0;
  D3D12_RAYTRACING_TIER tier_ = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
  bool active_ = false;
};

} // namespace opencagert
