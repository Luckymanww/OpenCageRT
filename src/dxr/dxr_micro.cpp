#include "opencagert/dxr_micro.h"

#include "opencagert/micro_scene.h"

#include <d3d12.h>
#include <dxgi1_6.h>

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
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

uint64_t align_up(uint64_t v, uint64_t a) { return (v + a - 1) & ~(a - 1); }

ComPtr<ID3D12Resource> create_buffer(ID3D12Device* device, uint64_t size, D3D12_HEAP_TYPE heap,
                                     D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags) {
  D3D12_HEAP_PROPERTIES hp{};
  hp.Type = heap;
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = size;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  desc.Flags = flags;
  ComPtr<ID3D12Resource> res;
  throw_if_failed(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                                  IID_PPV_ARGS(&res)),
                  "CreateCommittedResource buffer");
  return res;
}

void copy_instance_transform(D3D12_RAYTRACING_INSTANCE_DESC* inst, const Mat34& xf) {
  std::memcpy(inst->Transform, xf.m, sizeof(xf.m));
}

Mat34 identity_mat34() {
  Mat34 m{};
  m.m[0][0] = 1.f;
  m.m[1][1] = 1.f;
  m.m[2][2] = 1.f;
  return m;
}

struct MicroConstants {
  uint32_t output_size[2];
  uint32_t frame_index;
  uint32_t view_mode;
  uint32_t tri_level;
  uint32_t debug_flags;
  float time;
  uint32_t tet_count;
  float cam_origin[3];
  float cam_scale;
  float cam_target[3];
  uint32_t instance_count;
  float classic_vram_mb;
  float cage_vram_mb;
  float classic_as_ms;
  float cage_as_ms;
};

static_assert(offsetof(MicroConstants, cam_origin) == 32, "HLSL camOrigin packing");
static_assert(offsetof(MicroConstants, classic_vram_mb) == 64, "HLSL vram packing");
static_assert(sizeof(MicroConstants) == 80, "HLSL cbuffer size");

} // namespace

bool DxrMicro::initialize(ID3D12Device* device, ID3D12CommandQueue* queue, uint32_t width,
                          uint32_t height, std::string& error) {
  shutdown();
  width_ = width;
  height_ = height;
  try {
    throw_if_failed(device->QueryInterface(IID_PPV_ARGS(&device_)), "QueryInterface ID3D12Device5");
    queue_ = queue;

    throw_if_failed(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                    IID_PPV_ARGS(&init_alloc_)),
                    "CreateCommandAllocator");
    throw_if_failed(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, init_alloc_.Get(),
                                               nullptr, IID_PPV_ARGS(&init_list_)),
                    "CreateCommandList4");

    throw_if_failed(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)),
                    "CreateFence");
    fence_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fence_event_) {
      error = "CreateEvent failed";
      return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
    heap_desc.NumDescriptors = 1;
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    throw_if_failed(device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&uav_heap_)),
                    "CreateDescriptorHeap");

    constant_buffer_ =
        create_buffer(device_.Get(), 256, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
                      D3D12_RESOURCE_FLAG_NONE);
    throw_if_failed(constant_buffer_->Map(0, nullptr, &constant_cpu_), "Map CB");
    tet_buffer_ = create_buffer(device_.Get(), sizeof(float) * 4 * 4 * kMicroMaxTets, D3D12_HEAP_TYPE_UPLOAD,
                                D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
    throw_if_failed(tet_buffer_->Map(0, nullptr, &tet_cpu_), "Map tet buffer");

    if (!create_output(width, height, error)) {
      return false;
    }
    if (!create_pipeline(error)) {
      return false;
    }
    if (!create_geometry(error)) {
      return false;
    }
    if (!create_acceleration(error)) {
      return false;
    }

    D3D12_QUERY_HEAP_DESC qh{};
    qh.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    qh.Count = 6;
    throw_if_failed(device_->CreateQueryHeap(&qh, IID_PPV_ARGS(&timestamp_heap_)), "CreateQueryHeap");
    timestamp_readback_ =
        create_buffer(device_.Get(), sizeof(uint64_t) * 6, D3D12_HEAP_TYPE_READBACK,
                      D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE);
    throw_if_failed(queue_->GetTimestampFrequency(&timestamp_freq_), "GetTimestampFrequency");

    if (!configure_instances(tri_ladder_instances(TriLadder::K100), error)) {
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

bool DxrMicro::create_output(uint32_t width, uint32_t height, std::string& error) {
  (void)error;
  D3D12_HEAP_PROPERTIES default_heap{};
  default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC tex{};
  tex.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  tex.Width = width;
  tex.Height = height;
  tex.DepthOrArraySize = 1;
  tex.MipLevels = 1;
  tex.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  tex.SampleDesc.Count = 1;
  tex.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  throw_if_failed(device_->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &tex,
                                                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                    IID_PPV_ARGS(&output_texture_)),
                  "Create UAV texture");
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
  uav.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  device_->CreateUnorderedAccessView(output_texture_.Get(), nullptr, &uav,
                                     uav_heap_->GetCPUDescriptorHandleForHeapStart());
  return true;
}

bool DxrMicro::create_pipeline(std::string& error) {
  const std::vector<uint8_t> dxil = read_file_bytes(exe_directory() + L"m3_micro.dxil");
  if (dxil.empty()) {
    error = "m3_micro.dxil not found next to executable";
    return false;
  }

  D3D12_DESCRIPTOR_RANGE1 uav_range{};
  uav_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  uav_range.NumDescriptors = 1;
  uav_range.BaseShaderRegister = 0;
  uav_range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
  uav_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  D3D12_ROOT_PARAMETER1 params[5]{};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[0].DescriptorTable.NumDescriptorRanges = 1;
  params[0].DescriptorTable.pDescriptorRanges = &uav_range;
  params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  params[1].Descriptor.ShaderRegister = 0;
  params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  params[2].Descriptor.ShaderRegister = 1;
  params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  params[3].Descriptor.ShaderRegister = 0;
  params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  params[4].Descriptor.ShaderRegister = 2;
  params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_VERSIONED_ROOT_SIGNATURE_DESC rs{};
  rs.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
  rs.Desc_1_1.NumParameters = 5;
  rs.Desc_1_1.pParameters = params;
  ComPtr<ID3DBlob> blob;
  ComPtr<ID3DBlob> err;
  throw_if_failed(D3D12SerializeVersionedRootSignature(&rs, &blob, &err), "SerializeRootSignature");
  throw_if_failed(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                               IID_PPV_ARGS(&root_sig_)),
                  "CreateRootSignature");

  D3D12_EXPORT_DESC exports[3]{};
  exports[0].Name = L"RayGen";
  exports[1].Name = L"ClosestHit";
  exports[2].Name = L"Miss";

  D3D12_DXIL_LIBRARY_DESC lib{};
  lib.DXILLibrary.pShaderBytecode = dxil.data();
  lib.DXILLibrary.BytecodeLength = dxil.size();
  lib.NumExports = 3;
  lib.pExports = exports;

  D3D12_HIT_GROUP_DESC hit{};
  hit.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
  hit.ClosestHitShaderImport = L"ClosestHit";
  hit.HitGroupExport = L"HitGroup";

  D3D12_RAYTRACING_SHADER_CONFIG shader_cfg{};
  shader_cfg.MaxPayloadSizeInBytes = 32;
  shader_cfg.MaxAttributeSizeInBytes = 8;

  D3D12_RAYTRACING_PIPELINE_CONFIG pipe_cfg{};
  pipe_cfg.MaxTraceRecursionDepth = 1;

  D3D12_GLOBAL_ROOT_SIGNATURE global_rs{};
  global_rs.pGlobalRootSignature = root_sig_.Get();

  D3D12_STATE_SUBOBJECT subs[6]{};
  subs[0].Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
  subs[0].pDesc = &lib;
  subs[1].Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
  subs[1].pDesc = &hit;
  subs[2].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
  subs[2].pDesc = &shader_cfg;
  subs[3].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
  subs[3].pDesc = &pipe_cfg;
  subs[4].Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
  subs[4].pDesc = &global_rs;

  const wchar_t* assoc_exports[] = {L"RayGen", L"ClosestHit", L"Miss"};
  D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION assoc{};
  assoc.pSubobjectToAssociate = &subs[2];
  assoc.NumExports = 3;
  assoc.pExports = assoc_exports;
  subs[5].Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
  subs[5].pDesc = &assoc;

  D3D12_STATE_OBJECT_DESC so{};
  so.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
  so.NumSubobjects = 6;
  so.pSubobjects = subs;
  throw_if_failed(device_->CreateStateObject(&so, IID_PPV_ARGS(&rt_pso_)), "CreateStateObject");

  ComPtr<ID3D12StateObjectProperties> props;
  throw_if_failed(rt_pso_->QueryInterface(IID_PPV_ARGS(&props)), "StateObjectProperties");
  const void* id_ray = props->GetShaderIdentifier(L"RayGen");
  const void* id_miss = props->GetShaderIdentifier(L"Miss");
  const void* id_hit = props->GetShaderIdentifier(L"HitGroup");
  if (!id_ray || !id_miss || !id_hit) {
    error = "GetShaderIdentifier failed";
    return false;
  }

  shader_record_size_ = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
  const uint32_t table_bytes = shader_record_size_ * 3;
  shader_table_ = create_buffer(device_.Get(), table_bytes, D3D12_HEAP_TYPE_UPLOAD,
                                D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
  uint8_t* mapped = nullptr;
  throw_if_failed(shader_table_->Map(0, nullptr, reinterpret_cast<void**>(&mapped)), "Map SBT");
  std::memset(mapped, 0, table_bytes);
  std::memcpy(mapped + 0, id_ray, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
  std::memcpy(mapped + shader_record_size_, id_miss, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
  std::memcpy(mapped + shader_record_size_ * 2, id_hit, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
  shader_table_->Unmap(0, nullptr);
  return true;
}

bool DxrMicro::create_geometry(std::string& error) {
  proto_ = make_micro_scene(0.f, true);
  if (proto_.pieces.empty() || proto_.classic_ib.empty()) {
    error = "micro scene produced no triangles";
    return false;
  }
  pieces_ = proto_.pieces;
  proto_vertex_count_ = static_cast<uint32_t>(proto_.classic_rest_vb.size());
  proto_index_count_ = static_cast<uint32_t>(proto_.classic_ib.size());
  classic_vertex_count_ = proto_vertex_count_;
  classic_index_count_ = proto_index_count_;

  classic_vb_staging_ =
      create_buffer(device_.Get(), sizeof(Vec3) * proto_vertex_count_, D3D12_HEAP_TYPE_UPLOAD,
                    D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
  classic_vb_ = create_buffer(device_.Get(), sizeof(Vec3) * proto_vertex_count_, D3D12_HEAP_TYPE_DEFAULT,
                              D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE);
  classic_vb_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
  cage_vb_ = create_buffer(device_.Get(), sizeof(Vec3) * proto_.cage_rest_vb.size(), D3D12_HEAP_TYPE_UPLOAD,
                           D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
  classic_ib_ = create_buffer(device_.Get(), sizeof(uint32_t) * proto_index_count_, D3D12_HEAP_TYPE_UPLOAD,
                              D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
  cage_ib_ = create_buffer(device_.Get(), sizeof(uint32_t) * proto_.cage_ib.size(), D3D12_HEAP_TYPE_UPLOAD,
                           D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);

  throw_if_failed(classic_vb_staging_->Map(0, nullptr, &classic_vb_cpu_), "Map classic VB staging");
  void* cage_cpu = nullptr;
  void* classic_ib_cpu = nullptr;
  void* cage_ib_cpu = nullptr;
  throw_if_failed(cage_vb_->Map(0, nullptr, &cage_cpu), "Map cage VB");
  throw_if_failed(classic_ib_->Map(0, nullptr, &classic_ib_cpu), "Map classic IB");
  throw_if_failed(cage_ib_->Map(0, nullptr, &cage_ib_cpu), "Map cage IB");
  std::memcpy(classic_vb_cpu_, proto_.classic_rest_vb.data(), sizeof(Vec3) * proto_vertex_count_);
  std::memcpy(cage_cpu, proto_.cage_rest_vb.data(), sizeof(Vec3) * proto_.cage_rest_vb.size());
  std::memcpy(classic_ib_cpu, proto_.classic_ib.data(), sizeof(uint32_t) * proto_index_count_);
  std::memcpy(cage_ib_cpu, proto_.cage_ib.data(), sizeof(uint32_t) * proto_.cage_ib.size());
  cage_vb_->Unmap(0, nullptr);
  classic_ib_->Unmap(0, nullptr);
  cage_ib_->Unmap(0, nullptr);
  return true;
}

bool DxrMicro::create_acceleration(std::string& error) {
  (void)error;
  auto tri_geom = [](ID3D12Resource* vb, uint32_t vcount, uint64_t voff, ID3D12Resource* ib,
                     uint32_t icount, uint64_t ioff) {
    D3D12_RAYTRACING_GEOMETRY_DESC g{};
    g.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    g.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    g.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    g.Triangles.VertexCount = vcount;
    g.Triangles.VertexBuffer.StartAddress = vb->GetGPUVirtualAddress() + voff;
    g.Triangles.VertexBuffer.StrideInBytes = sizeof(Vec3);
    g.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
    g.Triangles.IndexCount = icount;
    g.Triangles.IndexBuffer = ib->GetGPUVirtualAddress() + ioff;
    return g;
  };

  init_list_->CopyBufferRegion(classic_vb_.Get(), 0, classic_vb_staging_.Get(), 0,
                               sizeof(Vec3) * proto_vertex_count_);
  {
    D3D12_RESOURCE_BARRIER vb{};
    vb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    vb.Transition.pResource = classic_vb_.Get();
    vb.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    vb.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    vb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    init_list_->ResourceBarrier(1, &vb);
    classic_vb_state_ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  }

  D3D12_RAYTRACING_GEOMETRY_DESC classic_geom =
      tri_geom(classic_vb_.Get(), classic_vertex_count_, 0, classic_ib_.Get(), classic_index_count_, 0);

  std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> cage_geoms;
  cage_geoms.reserve(pieces_.size());
  for (const CagePieceGpu& piece : pieces_) {
    cage_geoms.push_back(tri_geom(cage_vb_.Get(), piece.vertex_count, piece.vertex_offset * sizeof(Vec3),
                                  cage_ib_.Get(), piece.index_count, piece.index_offset * sizeof(uint32_t)));
  }

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blas_in{};
  blas_in.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
  blas_in.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  blas_in.NumDescs = 1;
  blas_in.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD |
                  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO classic_info{};
  blas_in.pGeometryDescs = &classic_geom;
  device_->GetRaytracingAccelerationStructurePrebuildInfo(&blas_in, &classic_info);

  blas_in.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
  uint64_t cage_scratch = 0;
  uint64_t cage_result = 0;
  std::vector<D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO> cage_infos(pieces_.size());
  for (size_t i = 0; i < pieces_.size(); ++i) {
    blas_in.pGeometryDescs = &cage_geoms[i];
    device_->GetRaytracingAccelerationStructurePrebuildInfo(&blas_in, &cage_infos[i]);
    cage_scratch = std::max(cage_scratch, cage_infos[i].ScratchDataSizeInBytes);
    cage_result = std::max(cage_result, cage_infos[i].ResultDataMaxSizeInBytes);
  }

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlas_in{};
  tlas_in.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
  tlas_in.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  tlas_in.NumDescs = 1;
  tlas_in.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO classic_tlas_info{};
  device_->GetRaytracingAccelerationStructurePrebuildInfo(&tlas_in, &classic_tlas_info);
  tlas_in.NumDescs = static_cast<UINT>(pieces_.size());
  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO cage_tlas_info{};
  device_->GetRaytracingAccelerationStructurePrebuildInfo(&tlas_in, &cage_tlas_info);

  const uint64_t classic_scratch =
      std::max(classic_info.ScratchDataSizeInBytes, classic_info.UpdateScratchDataSizeInBytes);
  const uint64_t blas_scratch = align_up(std::max(classic_scratch, cage_scratch),
                                         D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
  const uint64_t tlas_scratch = align_up(std::max(classic_tlas_info.ScratchDataSizeInBytes,
                                                   cage_tlas_info.ScratchDataSizeInBytes),
                                         D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
  classic_blas_scratch_ = 0;
  classic_tlas_scratch_ = blas_scratch;
  cage_tlas_scratch_ = blas_scratch + tlas_scratch;

  scratch_ = create_buffer(device_.Get(), blas_scratch + tlas_scratch * 2, D3D12_HEAP_TYPE_DEFAULT,
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                           D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

  auto make_as = [&](uint64_t bytes) {
    return create_buffer(device_.Get(),
                         align_up(bytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT),
                         D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  };
  classic_blas_.clear();
  classic_blas_.push_back(make_as(classic_info.ResultDataMaxSizeInBytes));
  cage_blas_.resize(pieces_.size());
  for (auto& blas : cage_blas_) {
    blas = make_as(cage_result);
  }
  classic_tlas_ = make_as(classic_tlas_info.ResultDataMaxSizeInBytes);
  cage_tlas_ = make_as(cage_tlas_info.ResultDataMaxSizeInBytes);

  classic_instances_ =
      create_buffer(device_.Get(), sizeof(D3D12_RAYTRACING_INSTANCE_DESC), D3D12_HEAP_TYPE_UPLOAD,
                    D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
  cage_instances_ = create_buffer(device_.Get(), sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * pieces_.size(),
                                  D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
                                  D3D12_RESOURCE_FLAG_NONE);
  throw_if_failed(classic_instances_->Map(0, nullptr, &classic_instances_cpu_), "Map classic inst");
  throw_if_failed(cage_instances_->Map(0, nullptr, &cage_instances_cpu_), "Map cage inst");

  auto fill_inst = [](D3D12_RAYTRACING_INSTANCE_DESC* inst, ID3D12Resource* blas, const Mat34& xf) {
    std::memset(inst, 0, sizeof(*inst));
    copy_instance_transform(inst, xf);
    inst->InstanceMask = 0xFF;
    inst->Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
    inst->AccelerationStructure = blas->GetGPUVirtualAddress();
  };
  fill_inst(static_cast<D3D12_RAYTRACING_INSTANCE_DESC*>(classic_instances_cpu_), classic_blas_[0].Get(),
            identity_mat34());
  auto* cage_insts = static_cast<D3D12_RAYTRACING_INSTANCE_DESC*>(cage_instances_cpu_);
  for (size_t i = 0; i < pieces_.size(); ++i) {
    fill_inst(&cage_insts[i], cage_blas_[i].Get(), identity_mat34());
  }

  auto build_blas = [&](const D3D12_RAYTRACING_GEOMETRY_DESC& geom, ID3D12Resource* dest) {
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS in = blas_in;
    in.pGeometryDescs = &geom;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
    build.DestAccelerationStructureData = dest->GetGPUVirtualAddress();
    build.Inputs = in;
    build.ScratchAccelerationStructureData = scratch_->GetGPUVirtualAddress();
    init_list_->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
    D3D12_RESOURCE_BARRIER uav{};
    uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uav.UAV.pResource = dest;
    init_list_->ResourceBarrier(1, &uav);
    uav.UAV.pResource = scratch_.Get();
    init_list_->ResourceBarrier(1, &uav);
  };
  build_blas(classic_geom, classic_blas_[0].Get());
  for (size_t i = 0; i < pieces_.size(); ++i) {
    build_blas(cage_geoms[i], cage_blas_[i].Get());
  }

  throw_if_failed(init_list_->Close(), "Close init list");
  ID3D12CommandList* lists[] = {init_list_.Get()};
  queue_->ExecuteCommandLists(1, lists);
  wait_for_gpu();
  return true;
}

uint64_t DxrMicro::gpu_bytes(ID3D12Resource* res) const {
  if (!res || !device_) {
    return 0;
  }
  const D3D12_RESOURCE_DESC desc = res->GetDesc();
  return device_->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
}

void DxrMicro::update_vram_metrics() {
  uint64_t classic_geom = gpu_bytes(classic_vb_.Get()) + gpu_bytes(classic_ib_.Get()) +
                          gpu_bytes(classic_vb_staging_.Get());
  uint64_t classic_as = gpu_bytes(classic_tlas_.Get()) + gpu_bytes(classic_instances_.Get());
  for (const auto& blas : classic_blas_) {
    classic_as += gpu_bytes(blas.Get());
  }
  uint64_t cage_geom = gpu_bytes(cage_vb_.Get()) + gpu_bytes(cage_ib_.Get());
  uint64_t cage_as = gpu_bytes(cage_tlas_.Get()) + gpu_bytes(cage_instances_.Get());
  for (const auto& blas : cage_blas_) {
    cage_as += gpu_bytes(blas.Get());
  }
  auto fill = [](PathMetrics& m, uint64_t geom, uint64_t as) {
    m.geom_mb = static_cast<float>(geom) / (1024.f * 1024.f);
    m.as_mb = static_cast<float>(as) / (1024.f * 1024.f);
    m.tracked_mb = m.geom_mb + m.as_mb;
    m.vram_mb = m.tracked_mb;
  };
  fill(metrics_.classic, classic_geom, classic_as);
  fill(metrics_.cage, cage_geom, cage_as);
  metrics_.instance_count = instance_count_;
  metrics_.tet_count = static_cast<uint32_t>(proto_.rest_tets.size());
  metrics_.triangle_count = (static_cast<uint64_t>(proto_index_count_) / 3) * instance_count_;
  metrics_.tri_level = TriLadder::K100;
  metrics_.using_placeholders = false;
}

bool DxrMicro::configure_instances(uint32_t count, std::string& error) {
  (void)error;
  count = std::max(1u, count);
  if (count == instance_count_ && cage_tlas_ &&
      ((count > kClassicUniqueBlasMax && classic_blas_.empty()) || classic_blas_.size() == count)) {
    return true;
  }
  wait_for_gpu();
  release_classic_vertices();
  if (classic_instances_ && classic_instances_cpu_) {
    classic_instances_->Unmap(0, nullptr);
    classic_instances_cpu_ = nullptr;
  }
  if (cage_instances_ && cage_instances_cpu_) {
    cage_instances_->Unmap(0, nullptr);
    cage_instances_cpu_ = nullptr;
  }
  classic_ib_.Reset();
  classic_blas_.clear();
  classic_tlas_.Reset();
  classic_instances_.Reset();
  cage_instances_.Reset();
  cage_tlas_.Reset();
  scratch_.Reset();
  classic_blas_allow_update_ = false;

  instance_count_ = count;
  const bool unique_classic = count <= kClassicUniqueBlasMax;
  classic_vertex_count_ = unique_classic ? proto_vertex_count_ * count : proto_vertex_count_;
  classic_index_count_ = proto_index_count_;

  if (unique_classic) {
    classic_vb_staging_ =
        create_buffer(device_.Get(), sizeof(Vec3) * classic_vertex_count_, D3D12_HEAP_TYPE_UPLOAD,
                      D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
    classic_vb_ = create_buffer(device_.Get(), sizeof(Vec3) * classic_vertex_count_, D3D12_HEAP_TYPE_DEFAULT,
                                D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE);
    classic_vb_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
    throw_if_failed(classic_vb_staging_->Map(0, nullptr, &classic_vb_cpu_), "Map classic VB staging");
  }

  classic_ib_ = create_buffer(device_.Get(), sizeof(uint32_t) * proto_index_count_, D3D12_HEAP_TYPE_UPLOAD,
                              D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
  void* ib_cpu = nullptr;
  throw_if_failed(classic_ib_->Map(0, nullptr, &ib_cpu), "Map shared classic IB");
  std::memcpy(ib_cpu, proto_.classic_ib.data(), sizeof(uint32_t) * proto_index_count_);
  classic_ib_->Unmap(0, nullptr);

  D3D12_RAYTRACING_GEOMETRY_DESC geom{};
  geom.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
  geom.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
  geom.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
  geom.Triangles.VertexCount = proto_vertex_count_;
  geom.Triangles.VertexBuffer.StartAddress =
      unique_classic ? classic_vb_->GetGPUVirtualAddress() : cage_vb_->GetGPUVirtualAddress();
  geom.Triangles.VertexBuffer.StrideInBytes = sizeof(Vec3);
  geom.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
  geom.Triangles.IndexCount = proto_index_count_;
  geom.Triangles.IndexBuffer = classic_ib_->GetGPUVirtualAddress();

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blas_in{};
  blas_in.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
  blas_in.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  blas_in.NumDescs = 1;
  blas_in.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD |
                  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
  blas_in.pGeometryDescs = &geom;
  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO plant_info{};
  device_->GetRaytracingAccelerationStructurePrebuildInfo(&blas_in, &plant_info);

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlas_in{};
  tlas_in.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
  tlas_in.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  tlas_in.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
  tlas_in.NumDescs = unique_classic ? count : 1;
  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO classic_tlas_info{};
  device_->GetRaytracingAccelerationStructurePrebuildInfo(&tlas_in, &classic_tlas_info);
  tlas_in.NumDescs = static_cast<UINT>(pieces_.size() * count);
  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO cage_tlas_info{};
  device_->GetRaytracingAccelerationStructurePrebuildInfo(&tlas_in, &cage_tlas_info);

  const uint64_t blas_scratch = align_up(
      std::max(plant_info.ScratchDataSizeInBytes, plant_info.UpdateScratchDataSizeInBytes),
      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
  const uint64_t tlas_scratch = align_up(std::max(classic_tlas_info.ScratchDataSizeInBytes,
                                                   cage_tlas_info.ScratchDataSizeInBytes),
                                         D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
  classic_blas_scratch_ = 0;
  classic_tlas_scratch_ = blas_scratch;
  cage_tlas_scratch_ = blas_scratch + tlas_scratch;
  scratch_ = create_buffer(device_.Get(), blas_scratch + tlas_scratch * 2, D3D12_HEAP_TYPE_DEFAULT,
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                           D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

  auto make_as = [&](uint64_t bytes) {
    return create_buffer(device_.Get(),
                         align_up(bytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT),
                         D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  };
  if (unique_classic) {
    classic_blas_.resize(count);
    for (auto& blas : classic_blas_) {
      blas = make_as(plant_info.ResultDataMaxSizeInBytes);
    }
  }
  classic_tlas_ = make_as(std::max<uint64_t>(classic_tlas_info.ResultDataMaxSizeInBytes, 256));
  cage_tlas_ = make_as(cage_tlas_info.ResultDataMaxSizeInBytes);

  if (unique_classic) {
    classic_instances_ =
        create_buffer(device_.Get(), sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * count, D3D12_HEAP_TYPE_UPLOAD,
                      D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
    throw_if_failed(classic_instances_->Map(0, nullptr, &classic_instances_cpu_), "Map classic inst N");
  }
  cage_instances_ =
      create_buffer(device_.Get(), sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * pieces_.size() * count,
                    D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
  throw_if_failed(cage_instances_->Map(0, nullptr, &cage_instances_cpu_), "Map cage inst N");
  update_vram_metrics();
  return true;
}

void DxrMicro::record_builds(ID3D12GraphicsCommandList4* cmd_list, const DemoState& demo,
                             uint32_t frame_index) {
  const uint32_t want = tri_ladder_instances(demo.tri_level);
  if (want != instance_count_) {
    std::string err;
    try {
      configure_instances(want, err);
    } catch (const std::exception&) {
      return;
    }
  }

  const bool freeze = (demo.debug_flags & DemoDebugFreezeGeometry) != 0;
  const bool need_classic =
      demo.view_mode != DemoViewMode::SoloCageRT && classic_blas_.size() == instance_count_ &&
      instance_count_ > 0;
  const bool need_cage = demo.view_mode != DemoViewMode::SoloClassic;
  const bool classic_update = demo.classic_blas_mode == ClassicBlasMode::Update;
  if (!need_classic || !classic_update) {
    classic_blas_allow_update_ = false;
  }
  const float time = freeze ? 0.f : static_cast<float>(frame_index) * 0.016f;
  const uint32_t n = instance_count_;
  auto* dst = static_cast<Vec3*>(classic_vb_cpu_);
  auto* classic_insts = static_cast<D3D12_RAYTRACING_INSTANCE_DESC*>(classic_instances_cpu_);
  auto* cage_insts = static_cast<D3D12_RAYTRACING_INSTANCE_DESC*>(cage_instances_cpu_);

  auto write_inst = [](D3D12_RAYTRACING_INSTANCE_DESC* inst, ID3D12Resource* blas, const Mat34& xf) {
    std::memset(inst, 0, sizeof(*inst));
    copy_instance_transform(inst, xf);
    inst->InstanceMask = 0xFF;
    inst->Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
    inst->AccelerationStructure = blas->GetGPUVirtualAddress();
  };

  std::vector<Tetrahedron> overlay_tets;
  std::vector<Tetrahedron> animated;
  animated.reserve(proto_.rest_tets.size());
  for (uint32_t inst = 0; inst < n; ++inst) {
    const float phase = time + static_cast<float>(inst) * 0.13f;
    animate_micro_tets_into(proto_.rest_tets, phase, freeze, animated);
    const Mat34 world = mat34_translate(instance_grid_offset(inst, n));
    if (inst == 0) {
      overlay_tets = animated;
      for (Tetrahedron& tet : overlay_tets) {
        tet.v0 = transform_point(world, tet.v0);
        tet.v1 = transform_point(world, tet.v1);
        tet.v2 = transform_point(world, tet.v2);
        tet.v3 = transform_point(world, tet.v3);
      }
    }
    if (need_classic) {
      write_inst(&classic_insts[inst], classic_blas_[inst].Get(), world);
    }
    if (need_cage) {
      for (size_t p = 0; p < pieces_.size(); ++p) {
        const uint32_t ti = pieces_[p].tet_index;
        const Mat34 tet_t = instance_transform_rest_to_animated(proto_.rest_tets[ti], animated[ti]);
        const Mat34 xf = multiply_mat34(world, tet_t);
        write_inst(&cage_insts[inst * pieces_.size() + p], cage_blas_[p].Get(), xf);
      }
    }
    if (need_classic) {
      for (uint32_t v = 0; v < proto_vertex_count_; ++v) {
        const uint32_t ti = proto_.classic_vert_tet[v];
        const Mat34 tet_t = instance_transform_rest_to_animated(proto_.rest_tets[ti], animated[ti]);
        dst[inst * proto_vertex_count_ + v] = transform_point(tet_t, proto_.classic_rest_vb[v]);
      }
    }
  }

  D3D12_RAYTRACING_GEOMETRY_DESC geom{};
  geom.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
  geom.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
  geom.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
  geom.Triangles.VertexCount = proto_vertex_count_;
  geom.Triangles.VertexBuffer.StrideInBytes = sizeof(Vec3);
  geom.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
  geom.Triangles.IndexCount = proto_index_count_;
  geom.Triangles.IndexBuffer = classic_ib_->GetGPUVirtualAddress();

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blas_in{};
  blas_in.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
  blas_in.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  blas_in.NumDescs = 1;
  blas_in.pGeometryDescs = &geom;
  blas_in.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;

  auto build_tlas = [&](ID3D12Resource* tlas, ID3D12Resource* instances, UINT num_descs,
                        uint64_t scratch_off) {
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS in{};
    in.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    in.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    in.NumDescs = num_descs;
    in.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    in.InstanceDescs = instances ? instances->GetGPUVirtualAddress() : 0;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC d{};
    d.DestAccelerationStructureData = tlas->GetGPUVirtualAddress();
    d.Inputs = in;
    d.ScratchAccelerationStructureData = scratch_->GetGPUVirtualAddress() + scratch_off;
    cmd_list->BuildRaytracingAccelerationStructure(&d, 0, nullptr);
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    b.UAV.pResource = tlas;
    cmd_list->ResourceBarrier(1, &b);
  };

  cmd_list->EndQuery(timestamp_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
  if (need_classic) {
    copy_classic_vertices(cmd_list);
    const bool perform_update = classic_update && classic_blas_allow_update_;
    if (classic_update) {
      blas_in.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD |
                      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
      if (perform_update) {
        blas_in.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
      }
    } else {
      blas_in.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    }
    for (uint32_t inst = 0; inst < n; ++inst) {
      geom.Triangles.VertexBuffer.StartAddress =
          classic_vb_->GetGPUVirtualAddress() + static_cast<uint64_t>(inst) * proto_vertex_count_ * sizeof(Vec3);
      D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC blas{};
      blas.DestAccelerationStructureData = classic_blas_[inst]->GetGPUVirtualAddress();
      blas.Inputs = blas_in;
      if (perform_update) {
        blas.SourceAccelerationStructureData = classic_blas_[inst]->GetGPUVirtualAddress();
      }
      blas.ScratchAccelerationStructureData = scratch_->GetGPUVirtualAddress() + classic_blas_scratch_;
      cmd_list->BuildRaytracingAccelerationStructure(&blas, 0, nullptr);
      D3D12_RESOURCE_BARRIER uavs[2]{};
      uavs[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
      uavs[0].UAV.pResource = classic_blas_[inst].Get();
      uavs[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
      uavs[1].UAV.pResource = scratch_.Get();
      cmd_list->ResourceBarrier(2, uavs);
    }
    classic_blas_allow_update_ = classic_update;
    build_tlas(classic_tlas_.Get(), classic_instances_.Get(), n, classic_tlas_scratch_);
  } else if (classic_tlas_) {
    build_tlas(classic_tlas_.Get(), nullptr, 0, classic_tlas_scratch_);
  }
  cmd_list->EndQuery(timestamp_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
  if (need_cage) {
    build_tlas(cage_tlas_.Get(), cage_instances_.Get(), static_cast<UINT>(pieces_.size() * n),
               cage_tlas_scratch_);
  }
  cmd_list->EndQuery(timestamp_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 2);

  const uint32_t side =
      std::max(1u, static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(n)))));
  const float extent = 0.55f * static_cast<float>(side);
  auto* cb = static_cast<MicroConstants*>(constant_cpu_);
  cb->output_size[0] = width_;
  cb->output_size[1] = height_;
  cb->frame_index = frame_index;
  cb->view_mode = static_cast<uint32_t>(demo.view_mode);
  cb->tri_level = static_cast<uint32_t>(demo.tri_level);
  cb->debug_flags = demo.debug_flags;
  cb->time = time;
  cb->tet_count = static_cast<uint32_t>(std::min(overlay_tets.size(), size_t{kMicroMaxTets}));
  cb->cam_origin[0] = 0.f;
  cb->cam_origin[1] = 0.55f + extent * 0.15f;
  cb->cam_origin[2] = 1.6f + extent * 0.95f;
  cb->cam_scale = 0.55f;
  cb->cam_target[0] = 0.f;
  cb->cam_target[1] = 0.18f;
  cb->cam_target[2] = 0.f;
  cb->instance_count = n;
  cb->classic_vram_mb = metrics_.classic.vram_mb;
  cb->cage_vram_mb = metrics_.cage.vram_mb;
  cb->classic_as_ms = metrics_.classic.as_update_ms;
  cb->cage_as_ms = metrics_.cage.as_update_ms;
  last_view_mode_ = demo.view_mode;

  auto* tet_out = static_cast<float*>(tet_cpu_);
  std::memset(tet_out, 0, sizeof(float) * 4 * 4 * kMicroMaxTets);
  for (uint32_t i = 0; i < cb->tet_count; ++i) {
    const Tetrahedron& tet = overlay_tets[i];
    const Vec3 vs[4] = {tet.v0, tet.v1, tet.v2, tet.v3};
    for (int v = 0; v < 4; ++v) {
      const uint32_t o = (i * 4 + static_cast<uint32_t>(v)) * 4;
      tet_out[o + 0] = vs[v].x;
      tet_out[o + 1] = vs[v].y;
      tet_out[o + 2] = vs[v].z;
      tet_out[o + 3] = 1.f;
    }
  }
  metrics_.tri_level = demo.tri_level;
}

void DxrMicro::read_gpu_timestamps() {
  if (!timestamps_ready_ || !timestamp_readback_ || timestamp_freq_ == 0) {
    return;
  }
  uint64_t stamps[6]{};
  void* mapped = nullptr;
  D3D12_RANGE range{0, sizeof(stamps)};
  if (FAILED(timestamp_readback_->Map(0, &range, &mapped)) || !mapped) {
    return;
  }
  std::memcpy(stamps, mapped, sizeof(stamps));
  const D3D12_RANGE empty{0, 0};
  timestamp_readback_->Unmap(0, &empty);

  auto ms = [this](uint64_t a, uint64_t b) -> float {
    if (b < a) {
      return 0.f;
    }
    return static_cast<float>((1000.0 * static_cast<double>(b - a)) /
                              static_cast<double>(timestamp_freq_));
  };
  metrics_.classic.as_update_ms = ms(stamps[0], stamps[1]);
  metrics_.cage.as_update_ms = ms(stamps[1], stamps[2]);
  const float rt = ms(stamps[3], stamps[4]);
  if (last_view_mode_ == DemoViewMode::Split) {
    metrics_.rt_times_combined = true;
    metrics_.classic.rt_ms = rt;
    metrics_.cage.rt_ms = rt;
  } else if (last_view_mode_ == DemoViewMode::SoloClassic) {
    metrics_.rt_times_combined = false;
    metrics_.classic.rt_ms = rt;
    metrics_.cage.rt_ms = 0.f;
  } else {
    metrics_.rt_times_combined = false;
    metrics_.classic.rt_ms = 0.f;
    metrics_.cage.rt_ms = rt;
  }
}

void DxrMicro::resize(uint32_t width, uint32_t height) {
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

void DxrMicro::render(ID3D12GraphicsCommandList4* cmd_list, ID3D12Resource* backbuffer,
                      D3D12_RESOURCE_STATES backbuffer_state, uint32_t frame_index,
                      const DemoState& demo) {
  if (!active_ || !output_texture_) {
    return;
  }

  read_gpu_timestamps();
  record_builds(cmd_list, demo, frame_index);

  ID3D12DescriptorHeap* heaps[] = {uav_heap_.Get()};
  cmd_list->SetDescriptorHeaps(1, heaps);
  cmd_list->SetComputeRootSignature(root_sig_.Get());
  cmd_list->SetPipelineState1(rt_pso_.Get());
  cmd_list->SetComputeRootDescriptorTable(0, uav_heap_->GetGPUDescriptorHandleForHeapStart());
  cmd_list->SetComputeRootShaderResourceView(1, classic_tlas_->GetGPUVirtualAddress());
  cmd_list->SetComputeRootShaderResourceView(2, cage_tlas_->GetGPUVirtualAddress());
  cmd_list->SetComputeRootConstantBufferView(3, constant_buffer_->GetGPUVirtualAddress());
  cmd_list->SetComputeRootShaderResourceView(4, tet_buffer_->GetGPUVirtualAddress());

  D3D12_DISPATCH_RAYS_DESC dispatch{};
  const UINT64 base = shader_table_->GetGPUVirtualAddress();
  dispatch.RayGenerationShaderRecord.StartAddress = base;
  dispatch.RayGenerationShaderRecord.SizeInBytes = shader_record_size_;
  dispatch.MissShaderTable.StartAddress = base + shader_record_size_;
  dispatch.MissShaderTable.SizeInBytes = shader_record_size_;
  dispatch.MissShaderTable.StrideInBytes = shader_record_size_;
  dispatch.HitGroupTable.StartAddress = base + shader_record_size_ * 2;
  dispatch.HitGroupTable.SizeInBytes = shader_record_size_;
  dispatch.HitGroupTable.StrideInBytes = shader_record_size_;
  dispatch.Width = width_;
  dispatch.Height = height_;
  dispatch.Depth = 1;
  cmd_list->EndQuery(timestamp_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 3);
  cmd_list->DispatchRays(&dispatch);
  cmd_list->EndQuery(timestamp_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 4);
  cmd_list->ResolveQueryData(timestamp_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 6,
                             timestamp_readback_.Get(), 0);
  timestamps_ready_ = true;

  D3D12_RESOURCE_BARRIER barriers[2]{};
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

  D3D12_RESOURCE_BARRIER restore[2]{};
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

void DxrMicro::release_classic_vertices() {
  if (classic_vb_staging_ && classic_vb_cpu_) {
    classic_vb_staging_->Unmap(0, nullptr);
    classic_vb_cpu_ = nullptr;
  }
  classic_vb_staging_.Reset();
  classic_vb_.Reset();
  classic_vb_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
}

void DxrMicro::transition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
                          D3D12_RESOURCE_STATES& state, D3D12_RESOURCE_STATES after) {
  if (!cmd || !res || state == after) {
    return;
  }
  D3D12_RESOURCE_BARRIER b{};
  b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition.pResource = res;
  b.Transition.StateBefore = state;
  b.Transition.StateAfter = after;
  b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  cmd->ResourceBarrier(1, &b);
  state = after;
}

void DxrMicro::copy_classic_vertices(ID3D12GraphicsCommandList* cmd) {
  if (!cmd || !classic_vb_ || !classic_vb_staging_ || classic_vertex_count_ == 0) {
    return;
  }
  transition(cmd, classic_vb_.Get(), classic_vb_state_, D3D12_RESOURCE_STATE_COPY_DEST);
  cmd->CopyBufferRegion(classic_vb_.Get(), 0, classic_vb_staging_.Get(), 0,
                        sizeof(Vec3) * classic_vertex_count_);
  transition(cmd, classic_vb_.Get(), classic_vb_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}

bool DxrMicro::ensure_output_readback(std::string& error) {
  if (!device_ || !output_texture_) {
    error = "no output texture";
    return false;
  }
  const D3D12_RESOURCE_DESC desc = output_texture_->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT num_rows = 0;
  UINT64 row_size = 0;
  UINT64 total = 0;
  device_->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &num_rows, &row_size, &total);
  output_readback_pitch_ = footprint.Footprint.RowPitch;
  if (output_readback_ && output_readback_->GetDesc().Width >= total) {
    return true;
  }
  output_readback_.Reset();
  try {
    output_readback_ = create_buffer(device_.Get(), std::max<uint64_t>(total, 256), D3D12_HEAP_TYPE_READBACK,
                                     D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE);
  } catch (const std::exception& ex) {
    error = ex.what();
    return false;
  }
  return true;
}

bool DxrMicro::readback_output_rgba(std::vector<uint8_t>& rgba, uint32_t& width, uint32_t& height,
                                    uint32_t& row_pitch, std::string& error) {
  if (!active_ || !init_alloc_ || !init_list_) {
    error = "DXR not active";
    return false;
  }
  wait_for_gpu();
  if (!ensure_output_readback(error)) {
    return false;
  }
  const D3D12_RESOURCE_DESC desc = output_texture_->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT num_rows = 0;
  UINT64 row_size = 0;
  UINT64 total = 0;
  device_->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &num_rows, &row_size, &total);

  throw_if_failed(init_alloc_->Reset(), "Reset readback allocator");
  throw_if_failed(init_list_->Reset(init_alloc_.Get(), nullptr), "Reset readback list");

  D3D12_RESOURCE_BARRIER to_copy{};
  to_copy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  to_copy.Transition.pResource = output_texture_.Get();
  to_copy.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  to_copy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  to_copy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  init_list_->ResourceBarrier(1, &to_copy);

  D3D12_TEXTURE_COPY_LOCATION dst{};
  dst.pResource = output_readback_.Get();
  dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  dst.PlacedFootprint = footprint;
  D3D12_TEXTURE_COPY_LOCATION src{};
  src.pResource = output_texture_.Get();
  src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  src.SubresourceIndex = 0;
  init_list_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

  D3D12_RESOURCE_BARRIER to_uav = to_copy;
  to_uav.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
  to_uav.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  init_list_->ResourceBarrier(1, &to_uav);
  throw_if_failed(init_list_->Close(), "Close readback list");
  ID3D12CommandList* lists[] = {init_list_.Get()};
  queue_->ExecuteCommandLists(1, lists);
  wait_for_gpu();

  width = width_;
  height = height_;
  row_pitch = static_cast<uint32_t>(output_readback_pitch_);
  rgba.assign(static_cast<size_t>(row_pitch) * height_, 0);
  void* mapped = nullptr;
  D3D12_RANGE range{0, static_cast<SIZE_T>(total)};
  if (FAILED(output_readback_->Map(0, &range, &mapped)) || !mapped) {
    error = "Map output readback failed";
    return false;
  }
  std::memcpy(rgba.data(), mapped, static_cast<size_t>(row_pitch) * height_);
  const D3D12_RANGE empty{0, 0};
  output_readback_->Unmap(0, &empty);
  return true;
}

void DxrMicro::wait_for_gpu() {
  if (!queue_ || !fence_) {
    return;
  }
  queue_->Signal(fence_.Get(), ++fence_value_);
  if (fence_->GetCompletedValue() < fence_value_) {
    fence_->SetEventOnCompletion(fence_value_, fence_event_);
    WaitForSingleObject(fence_event_, INFINITE);
  }
}

void DxrMicro::shutdown() {
  wait_for_gpu();
  release_classic_vertices();
  if (classic_instances_ && classic_instances_cpu_) {
    classic_instances_->Unmap(0, nullptr);
    classic_instances_cpu_ = nullptr;
  }
  if (cage_instances_ && cage_instances_cpu_) {
    cage_instances_->Unmap(0, nullptr);
    cage_instances_cpu_ = nullptr;
  }
  if (constant_buffer_ && constant_cpu_) {
    constant_buffer_->Unmap(0, nullptr);
    constant_cpu_ = nullptr;
  }
  if (tet_buffer_ && tet_cpu_) {
    tet_buffer_->Unmap(0, nullptr);
    tet_cpu_ = nullptr;
  }
  if (fence_event_) {
    CloseHandle(fence_event_);
    fence_event_ = nullptr;
  }
  output_texture_.Reset();
  output_readback_.Reset();
  output_readback_pitch_ = 0;
  constant_buffer_.Reset();
  tet_buffer_.Reset();
  shader_table_.Reset();
  rt_pso_.Reset();
  root_sig_.Reset();
  uav_heap_.Reset();
  classic_vb_.Reset();
  classic_vb_staging_.Reset();
  cage_vb_.Reset();
  classic_ib_.Reset();
  cage_ib_.Reset();
  classic_blas_.clear();
  cage_blas_.clear();
  classic_tlas_.Reset();
  cage_tlas_.Reset();
  scratch_.Reset();
  classic_instances_.Reset();
  cage_instances_.Reset();
  init_list_.Reset();
  init_alloc_.Reset();
  timestamp_heap_.Reset();
  timestamp_readback_.Reset();
  timestamp_freq_ = 0;
  timestamps_ready_ = false;
  fence_.Reset();
  queue_.Reset();
  device_.Reset();
  active_ = false;
}

} // namespace opencagert
