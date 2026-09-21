#include "opencagert/dxr_shell.h"
#include "opencagert/demo_state.h"

#include <d3d12.h>
#include <dxgi1_6.h>

#include <Windows.h>

#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace opencagert {
namespace {

void throw_if_failed(HRESULT hr, const char* msg) {
  if (FAILED(hr)) {
    throw std::runtime_error(msg);
  }
}

std::wstring exe_directory() {
  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  std::wstring s(path);
  const size_t pos = s.find_last_of(L"\\/");
  if (pos != std::wstring::npos) {
    s.resize(pos + 1);
  }
  return s;
}

std::vector<uint8_t> read_file_bytes(const std::wstring& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

struct ShellConstants {
  uint32_t output_size[2];
  uint32_t frame_index;
  uint32_t view_mode;
  uint32_t tri_level;
  uint32_t debug_flags;
  uint32_t padding;
};

} // namespace

bool DxrShell::initialize(ID3D12Device* device, ID3D12CommandQueue* queue, uint32_t width,
                          uint32_t height, std::string& error) {
  shutdown();
  width_ = width;
  height_ = height;

  try {
    throw_if_failed(device->QueryInterface(IID_PPV_ARGS(&device_)), "QueryInterface ID3D12Device5");
    queue_ = queue;

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
    throw_if_failed(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5,
                                                 sizeof(options5)),
                    "CheckFeatureSupport OPTIONS5");
    tier_ = options5.RaytracingTier;
    if (tier_ == D3D12_RAYTRACING_TIER_NOT_SUPPORTED) {
      error = "DXR tier not supported";
      return false;
    }

    throw_if_failed(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                    IID_PPV_ARGS(&upload_alloc_)),
                    "CreateCommandAllocator");
    throw_if_failed(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, upload_alloc_.Get(),
                                               nullptr, IID_PPV_ARGS(&upload_list_)),
                    "CreateCommandList4");
    upload_list_->Close();

    throw_if_failed(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)),
                    "CreateFence");
    fence_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fence_event_) {
      error = "CreateEvent failed";
      return false;
    }

    if (!create_descriptors(error)) {
      return false;
    }
    if (!create_output(width, height, error)) {
      return false;
    }
    if (!create_pipeline(error)) {
      return false;
    }

    active_ = true;
    return true;
  } catch (const std::exception& ex) {
    error = ex.what();
    shutdown();
    return false;
  }
}

bool DxrShell::create_descriptors(std::string& error) {
  (void)error;
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
  heap_desc.NumDescriptors = 1;
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  throw_if_failed(device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&cbv_uav_heap_)),
                  "CreateDescriptorHeap CBV_SRV_UAV");

  D3D12_HEAP_PROPERTIES upload_heap = {};
  upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
  D3D12_RESOURCE_DESC cb_desc = {};
  cb_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  cb_desc.Width = (sizeof(ShellConstants) + 255) & ~255u;
  cb_desc.Height = 1;
  cb_desc.DepthOrArraySize = 1;
  cb_desc.MipLevels = 1;
  cb_desc.Format = DXGI_FORMAT_UNKNOWN;
  cb_desc.SampleDesc.Count = 1;
  cb_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  throw_if_failed(device_->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &cb_desc,
                                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                    IID_PPV_ARGS(&constant_buffer_)),
                  "CreateCommittedResource CB");
  D3D12_RANGE read_range{};
  throw_if_failed(constant_buffer_->Map(0, &read_range, &constant_cpu_), "Map CB");
  return true;
}

bool DxrShell::create_output(uint32_t width, uint32_t height, std::string& error) {
  (void)error;
  D3D12_HEAP_PROPERTIES default_heap = {};
  default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC tex_desc = {};
  tex_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  tex_desc.Width = width;
  tex_desc.Height = height;
  tex_desc.DepthOrArraySize = 1;
  tex_desc.MipLevels = 1;
  tex_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  tex_desc.SampleDesc.Count = 1;
  tex_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  throw_if_failed(device_->CreateCommittedResource(
                      &default_heap, D3D12_HEAP_FLAG_NONE, &tex_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                      nullptr, IID_PPV_ARGS(&output_texture_)),
                  "CreateCommittedResource UAV tex");

  D3D12_CPU_DESCRIPTOR_HANDLE cpu = cbv_uav_heap_->GetCPUDescriptorHandleForHeapStart();
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
  uav_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  device_->CreateUnorderedAccessView(output_texture_.Get(), nullptr, &uav_desc, cpu);
  return true;
}

bool DxrShell::create_pipeline(std::string& error) {
  const std::wstring dxil_path = exe_directory() + L"m1_shell.dxil";
  const std::vector<uint8_t> dxil = read_file_bytes(dxil_path);
  if (dxil.empty()) {
    error = "m1_shell.dxil not found next to executable";
    return false;
  }

  D3D12_DESCRIPTOR_RANGE1 uav_range = {};
  uav_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  uav_range.NumDescriptors = 1;
  uav_range.BaseShaderRegister = 0;
  uav_range.RegisterSpace = 0;
  uav_range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
  uav_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  D3D12_ROOT_PARAMETER1 root_params[2] = {};
  root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  root_params[0].DescriptorTable.NumDescriptorRanges = 1;
  root_params[0].DescriptorTable.pDescriptorRanges = &uav_range;
  root_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  root_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  root_params[1].Descriptor.RegisterSpace = 0;
  root_params[1].Descriptor.ShaderRegister = 0;
  root_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_VERSIONED_ROOT_SIGNATURE_DESC root_sig_desc = {};
  root_sig_desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
  root_sig_desc.Desc_1_1.NumParameters = 2;
  root_sig_desc.Desc_1_1.pParameters = root_params;
  root_sig_desc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

  ComPtr<ID3DBlob> sig_blob;
  ComPtr<ID3DBlob> sig_error;
  throw_if_failed(D3D12SerializeVersionedRootSignature(&root_sig_desc, &sig_blob, &sig_error),
                  "D3D12SerializeVersionedRootSignature");
  throw_if_failed(device_->CreateRootSignature(0, sig_blob->GetBufferPointer(),
                                               sig_blob->GetBufferSize(),
                                               IID_PPV_ARGS(&root_sig_)),
                  "CreateRootSignature");

  D3D12_DXIL_LIBRARY_DESC lib_desc = {};
  lib_desc.DXILLibrary.pShaderBytecode = dxil.data();
  lib_desc.DXILLibrary.BytecodeLength = dxil.size();

  D3D12_EXPORT_DESC export_desc = {};
  export_desc.Name = L"RayGen";
  export_desc.Flags = D3D12_EXPORT_FLAG_NONE;
  lib_desc.NumExports = 1;
  lib_desc.pExports = &export_desc;

  D3D12_RAYTRACING_SHADER_CONFIG shader_config = {};
  shader_config.MaxPayloadSizeInBytes = 16;
  shader_config.MaxAttributeSizeInBytes = 8;
  D3D12_RAYTRACING_PIPELINE_CONFIG pipeline_config = {};
  pipeline_config.MaxTraceRecursionDepth = 1;

  D3D12_GLOBAL_ROOT_SIGNATURE global_rs = {};
  global_rs.pGlobalRootSignature = root_sig_.Get();

  D3D12_STATE_SUBOBJECT subobjects[4] = {};
  subobjects[0].Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
  subobjects[0].pDesc = &lib_desc;
  subobjects[1].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
  subobjects[1].pDesc = &shader_config;
  subobjects[2].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
  subobjects[2].pDesc = &pipeline_config;
  subobjects[3].Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
  subobjects[3].pDesc = &global_rs;

  D3D12_STATE_OBJECT_DESC rt_desc = {};
  rt_desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
  rt_desc.NumSubobjects = 4;
  rt_desc.pSubobjects = subobjects;
  throw_if_failed(device_->CreateStateObject(&rt_desc, IID_PPV_ARGS(&rt_pso_)), "CreateStateObject");

  ComPtr<ID3D12StateObjectProperties> props;
  throw_if_failed(rt_pso_->QueryInterface(IID_PPV_ARGS(&props)), "StateObjectProperties");
  const void* shader_id = props->GetShaderIdentifier(L"RayGen");
  if (!shader_id) {
    error = "GetShaderIdentifier(RayGen) returned null";
    return false;
  }

  shader_record_size_ = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;

  D3D12_HEAP_PROPERTIES upload = {};
  upload.Type = D3D12_HEAP_TYPE_UPLOAD;
  D3D12_RESOURCE_DESC buf = {};
  buf.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  buf.Width = shader_record_size_;
  buf.Height = 1;
  buf.DepthOrArraySize = 1;
  buf.MipLevels = 1;
  buf.Format = DXGI_FORMAT_UNKNOWN;
  buf.SampleDesc.Count = 1;
  buf.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  throw_if_failed(device_->CreateCommittedResource(&upload, D3D12_HEAP_FLAG_NONE, &buf,
                                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                    IID_PPV_ARGS(&shader_table_)),
                  "CreateCommittedResource SBT");

  void* mapped = nullptr;
  throw_if_failed(shader_table_->Map(0, nullptr, &mapped), "Map SBT");
  std::memset(mapped, 0, shader_record_size_);
  std::memcpy(mapped, shader_id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
  shader_table_->Unmap(0, nullptr);
  return true;
}

void DxrShell::resize(uint32_t width, uint32_t height) {
  if (!active_ || (width == width_ && height == height_)) {
    return;
  }
  wait_for_gpu();
  width_ = width;
  height_ = height;
  output_texture_.Reset();
  std::string err;
  create_output(width, height, err);
}

void DxrShell::render(ID3D12GraphicsCommandList4* cmd_list, ID3D12Resource* backbuffer,
                      D3D12_RESOURCE_STATES backbuffer_state, uint32_t frame_index,
                      const DemoState& demo) {
  if (!active_ || !output_texture_) {
    return;
  }

  auto* constants = static_cast<ShellConstants*>(constant_cpu_);
  constants->output_size[0] = width_;
  constants->output_size[1] = height_;
  constants->frame_index = frame_index;
  constants->view_mode = static_cast<uint32_t>(demo.view_mode);
  constants->tri_level = static_cast<uint32_t>(demo.tri_level);
  constants->debug_flags = demo.debug_flags;
  constants->padding = 0;

  ID3D12DescriptorHeap* heaps[] = {cbv_uav_heap_.Get()};
  cmd_list->SetDescriptorHeaps(1, heaps);
  cmd_list->SetComputeRootSignature(root_sig_.Get());
  cmd_list->SetPipelineState1(rt_pso_.Get());
  cmd_list->SetComputeRootDescriptorTable(0, cbv_uav_heap_->GetGPUDescriptorHandleForHeapStart());
  cmd_list->SetComputeRootConstantBufferView(1, constant_buffer_->GetGPUVirtualAddress());

  D3D12_RESOURCE_BARRIER uav_barrier = {};
  uav_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  uav_barrier.UAV.pResource = output_texture_.Get();
  cmd_list->ResourceBarrier(1, &uav_barrier);

  D3D12_DISPATCH_RAYS_DESC dispatch = {};
  dispatch.RayGenerationShaderRecord.StartAddress = shader_table_->GetGPUVirtualAddress();
  dispatch.RayGenerationShaderRecord.SizeInBytes = shader_record_size_;
  dispatch.Width = width_;
  dispatch.Height = height_;
  dispatch.Depth = 1;
  cmd_list->DispatchRays(&dispatch);

  D3D12_RESOURCE_BARRIER barriers[2] = {};
  barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barriers[0].Transition.pResource = output_texture_.Get();
  barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

  barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barriers[1].Transition.pResource = backbuffer;
  barriers[1].Transition.StateBefore = backbuffer_state;
  barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
  barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  cmd_list->ResourceBarrier(2, barriers);

  cmd_list->CopyResource(backbuffer, output_texture_.Get());

  D3D12_RESOURCE_BARRIER restore[2] = {};
  restore[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  restore[0].Transition.pResource = output_texture_.Get();
  restore[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
  restore[0].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  restore[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

  restore[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  restore[1].Transition.pResource = backbuffer;
  restore[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  restore[1].Transition.StateAfter = backbuffer_state;
  restore[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  cmd_list->ResourceBarrier(2, restore);
}

void DxrShell::wait_for_gpu() {
  if (!queue_ || !fence_) {
    return;
  }
  queue_->Signal(fence_.Get(), ++fence_value_);
  if (fence_->GetCompletedValue() < fence_value_) {
    fence_->SetEventOnCompletion(fence_value_, fence_event_);
    WaitForSingleObject(fence_event_, INFINITE);
  }
}

void DxrShell::shutdown() {
  wait_for_gpu();
  if (constant_buffer_ && constant_cpu_) {
    constant_buffer_->Unmap(0, nullptr);
    constant_cpu_ = nullptr;
  }
  if (fence_event_) {
    CloseHandle(fence_event_);
    fence_event_ = nullptr;
  }
  output_texture_.Reset();
  constant_buffer_.Reset();
  shader_table_.Reset();
  rt_pso_.Reset();
  root_sig_.Reset();
  cbv_uav_heap_.Reset();
  upload_list_.Reset();
  upload_alloc_.Reset();
  fence_.Reset();
  queue_.Reset();
  device_.Reset();
  active_ = false;
}

} // namespace opencagert
