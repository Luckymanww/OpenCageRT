# OpenCageRT

Vendor-neutral **tetrahedral cage** acceleration for massive animated ray-traced geometry, based on:

> Gruen, Benthin, Kern, McAllister — *Ray Tracing Massive Amounts of Animated Geometry*, HPG 2026, [DOI 10.1145/3820014](https://doi.org/10.1145/3820014).

This is an **independent** implementation. It is not affiliated with AMD GPUOpen.

## v0.1 scope

- CPU: regular voxel cage, triangle clipping, static μ-mesh partitions, deformation matrices (`A M⁻¹`).
- GPU: D3D12 / DXR demo on RTX-class hardware — **Classic** (animated BLAS) vs **CageRT** (static μBLAS + animated tetLAS / instance transforms).
- On-screen counters: VRAM estimate, AS build time, trace time, FPS, triangle / tetrahedron counts.

## Build (Windows)

Requires Visual Studio 2022 Build Tools (MSVC), Windows SDK 10+, CMake 3.24+.

```powershell
cmake --preset windows-x64-ninja
cmake --build --preset windows-x64-ninja
ctest --test-dir build/ninja --output-on-failure
```

Demo binary: `build/ninja/apps/demo/OpenCageRTDemo.exe`

## License

MIT — see [LICENSE](LICENSE).
