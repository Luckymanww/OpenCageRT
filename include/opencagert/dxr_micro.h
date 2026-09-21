#pragma once

#include "opencagert/demo_state.h"
#include "opencagert/micro_scene.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <vector>

namespace opencagert {

// M3: instanced plant field — Classic unique BLAS vs shared μBLAS + tetLAS.
class DxrMicro {
 public:
  bool initialize(ID3D12Device* device, ID3D12CommandQueue* queue, uint32_t width, uint32_t height,
                  std::string& error);
  void shutdown();
  void resize(uint32_t width, uint32_t height);
  void render(ID3D12GraphicsCommandList4* cmd_list, ID3D12Resource* backbuffer,
              D3D12_RESOURCE_STATES backbuffer_state, uint32_t frame_index, const DemoState& demo);

  bool is_active() const { return active_; }
  DemoMetrics metrics() const { return metrics_; }

 private:
  bool create_output(uint32_t width, uint32_t height, std::string& error);
  bool create_pipeline(std::string& error);
  bool create_geometry(std::string& error);
  bool create_acceleration(std::string& error);
  bool configure_instances(uint32_t count, std::string& error);
  uint64_t gpu_bytes(ID3D12Resource* res) const;
  void update_vram_metrics();
  void read_gpu_timestamps();
  void wait_for_gpu();
  void record_builds(ID3D12GraphicsCommandList4* cmd_list, const DemoState& demo, uint32_t frame_index);

  Microsoft::WRL::ComPtr<ID3D12Device5> device_;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> init_alloc_;
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> init_list_;
  Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
  HANDLE fence_event_ = nullptr;
  uint64_t fence_value_ = 0;

  Microsoft::WRL::ComPtr<ID3D12RootSignature> root_sig_;
  Microsoft::WRL::ComPtr<ID3D12StateObject> rt_pso_;
  Microsoft::WRL::ComPtr<ID3D12Resource> shader_table_;
  uint32_t shader_record_size_ = 0;

  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> uav_heap_;
  Microsoft::WRL::ComPtr<ID3D12Resource> constant_buffer_;
  void* constant_cpu_ = nullptr;
  Microsoft::WRL::ComPtr<ID3D12Resource> tet_buffer_;
  void* tet_cpu_ = nullptr;
  Microsoft::WRL::ComPtr<ID3D12Resource> output_texture_;

  Microsoft::WRL::ComPtr<ID3D12Resource> classic_vb_;
  Microsoft::WRL::ComPtr<ID3D12Resource> cage_vb_;
  Microsoft::WRL::ComPtr<ID3D12Resource> classic_ib_;
  Microsoft::WRL::ComPtr<ID3D12Resource> cage_ib_;
  void* classic_vb_cpu_ = nullptr;

  std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> classic_blas_;
  std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> cage_blas_;
  Microsoft::WRL::ComPtr<ID3D12Resource> classic_tlas_;
  Microsoft::WRL::ComPtr<ID3D12Resource> cage_tlas_;
  Microsoft::WRL::ComPtr<ID3D12Resource> scratch_;
  Microsoft::WRL::ComPtr<ID3D12Resource> classic_instances_;
  Microsoft::WRL::ComPtr<ID3D12Resource> cage_instances_;
  void* classic_instances_cpu_ = nullptr;
  void* cage_instances_cpu_ = nullptr;

  std::vector<CagePieceGpu> pieces_;
  uint32_t classic_vertex_count_ = 0;
  uint32_t classic_index_count_ = 0;

  uint64_t classic_blas_scratch_ = 0;
  uint64_t cage_tlas_scratch_ = 0;
  uint64_t classic_tlas_scratch_ = 0;

  uint32_t width_ = 0;
  uint32_t height_ = 0;
  bool active_ = false;
  MicroScene proto_{};
  uint32_t instance_count_ = 0;
  uint32_t proto_vertex_count_ = 0;
  uint32_t proto_index_count_ = 0;
  DemoMetrics metrics_{};
  Microsoft::WRL::ComPtr<ID3D12QueryHeap> timestamp_heap_;
  Microsoft::WRL::ComPtr<ID3D12Resource> timestamp_readback_;
  uint64_t timestamp_freq_ = 0;
  bool timestamps_ready_ = false;
};

} // namespace opencagert
