# OpenCageRT

Independent MIT **DXR** demo of tetrahedral-cage animated ray tracing, based on:

> Gruen, Benthin, Kern, McAllister — *Ray Tracing Massive Amounts of Animated Geometry*, HPG 2026, [DOI 10.1145/3820014](https://doi.org/10.1145/3820014).

Not affiliated with AMD GPUOpen.

Same plants. Classic rebuilds a unique BLAS per instance. CageRT keeps one μBLAS and updates tetLAS instance transforms (`A M⁻¹`).

## Measured on RTX 3090

| Instances | Tris | Classic VRAM / AS | CageRT VRAM / tetLAS | FPS (solo) |
|-----------|------|-------------------|----------------------|------------|
| 64 | 25K | 4.50 MB / 9 ms | 0.69 MB / 0.1 ms | — |
| 1024 | 397K | **68.69 MB / 150 ms** | **1.94 MB / 0.3 ms** | Classic ~6 / CageRT **390** |
| 25000 | 9.7M | (Classic unique BLAS skipped) | **34.56 MB / 11.6 ms** | CageRT **30** |

Split 64 — same field, VRAM bars:

![Split 64](docs/screenshots/split-64.png)

Split 1024 — Classic 68.69 MB vs CageRT 1.94 MB (**x35**):

![Split 1024](docs/screenshots/split-1024.png)

Classic solo 1024 (unique BLAS rebuild):

![Classic 1024](docs/screenshots/classic-1024.png)

CageRT solo 1024 (shared μBLAS):

![CageRT 1024](docs/screenshots/cagert-1024.png)

CageRT 25 000 plants (~9.7M triangles):

![CageRT 25k](docs/screenshots/cagert-25k.png)

## Keys

| Key | Action |
|-----|--------|
| **S** | Split Classic \| CageRT |
| **C** | Toggle Classic / CageRT fullscreen |
| **1–4** | 64 → 1024 → 4096 → 25k plants |
| **T** | Auto tour for a capture |
| **G / F / R** | Cages / freeze / ray debug |

**4** stays on CageRT. Do not toggle Classic there (TDR risk from tens of thousands of BLAS rebuilds).

## Build (Windows)

VS 2022 Build Tools, Windows SDK 10+, CMake 3.24+, DXR GPU.

```powershell
cmake --preset windows-x64
cmake --build "build/windows-x64" --config Release
ctest -C Release --test-dir build/windows-x64 --output-on-failure
```

Demo: `build/windows-x64/apps/demo/Release/OpenCageRTDemo.exe`

## License

MIT — see [LICENSE](LICENSE).
