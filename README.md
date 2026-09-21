# OpenCageRT

Independent MIT **DXR** demo of tetrahedral-cage animated ray tracing, based on:

> Gruen, Benthin, Kern, McAllister — *Ray Tracing Massive Amounts of Animated Geometry*, HPG 2026, [DOI 10.1145/3820014](https://doi.org/10.1145/3820014).

Not affiliated with AMD GPUOpen.

Same plants. **Classic Rebuild** rebuilds a unique BLAS per instance every frame. **Classic Update** keeps those unique BLASes and refits them with `ALLOW_UPDATE` / `PERFORM_UPDATE`. **CageRT** keeps a **shared set of static μBLASes** and updates tetLAS instance transforms (`A M⁻¹`).

`ASmem` on the HUD is **tracked RT memory** (geometry + acceleration structures the demo owns). It is not the process working-set from Task Manager. DXGI local-adapter usage is logged as a delta in CSV.

## How to reproduce numbers

Interactive captures are not the published method. Clone, build, then:

```powershell
.\OpenCageRTDemo.exe --benchmark --instances 64,1024,4096,25000 --warmup 60 --frames 240 --csv results.csv
.\OpenCageRTDemo.exe --parity
```

`--benchmark` runs three solo paths (Classic Rebuild, Classic Update, CageRT), vsync off, and writes median / p95 plus GPU, driver, resolution, and git hash. `--parity` freezes a 64-instance field and compares Classic vs CageRT barycentric RGB (HUD off). Do not quote a speedup unless `--parity` exits 0.

Classic unique BLAS is skipped above 4096 instances (TDR risk). 25k is CageRT-only.

## Measured on RTX 3090

Solo views, same scene, GPU timestamps from the HUD. Cage overlay (`G`) was on, so CageRT RT/FPS are slightly pessimistic. Tracked RT memory includes the Classic staging+DEFAULT vertex buffers.

`--parity` passed on this machine. Flagship numbers below are the **1024** interactive solo capture. 4096 Classic Rebuild is **preliminary** (HUD showed ~61 ms vs 151 ms at 1024 — that does not scale, so it is not a published result until `results.csv` confirms it).

### 1024 plants (flagship)

| Mode            | Tracked RT mem | AS update | RT | FPS |
|-----------------|---------------:|----------:|---:|----:|
| Classic Rebuild | 73.06 MB | **150.6 ms** | 0.10 ms | 6 |
| Classic Update  | 73.06 MB | **14.8 ms** | 0.10 ms | 33 |
| CageRT          | **1.94 MB** | **0.55 ms** | 0.58 ms | **138** |

Even against DXR refit, CageRT is **×38** less tracked RT memory and **×27** faster AS update.

Classic Update 1024:

![Classic Update 1024](docs/screenshots/classic-update-1024.png)

Classic Rebuild 1024:

![Classic Rebuild 1024](docs/screenshots/classic-rebuild-1024.png)

CageRT 1024 (shared static μBLASes):

![CageRT 1024](docs/screenshots/cagert-1024.png)

### Full ladder

| Instances | Mode            | Tracked RT mem | AS update | FPS |
|----------:|-----------------|---------------:|----------:|----:|
| 64 | Classic Rebuild | 4.81 MB | 9.3 ms | 70 |
| 64 | Classic Update | 4.81 MB | 2.0 ms | 141 |
| 64 | CageRT | 0.69 MB | 0.4 ms | 124 |
| 1024 | Classic Rebuild | 73.06 MB | 150.6 ms | 6 |
| 1024 | Classic Update | 73.06 MB | 14.8 ms | 33 |
| 1024 | CageRT | 1.94 MB | 0.55 ms | 138 |
| 4096 | Classic Rebuild | 291.75 MB | *preliminary* | 8 |
| 4096 | Classic Update | 291.75 MB | *preliminary* | 8 |
| 4096 | CageRT | 6.12 MB | 0.36 ms | 154 |
| 25000 | CageRT | 34.56 MB | 10.8 ms | 26 |

4096 Classic unique BLAS is the last Classic rung (`kClassicUniqueBlasMax`). Do not quote 4096 Rebuild/Update AS ms from the HUD: 4× instances cannot be faster than 1024 Rebuild until the CSV harness says so. 25k is CageRT-only.

Classic Update 4096 (292 MB / 60 ms / 8 FPS):

![Classic Update 4096](docs/screenshots/classic-update-4096.png)

CageRT 4096 (6.12 MB / 0.4 ms / 154 FPS):

![CageRT 4096](docs/screenshots/cagert-4096.png)

CageRT 25 000 plants (~9.7M triangles):

![CageRT 25k](docs/screenshots/cagert-25k.png)

Split 64:

![Split 64](docs/screenshots/split-64.png)

Split 1024:

![Split 1024](docs/screenshots/split-1024.png)

## Keys

| Key | Action |
|-----|--------|
| **S** | Split Classic \| CageRT |
| **C** | Toggle Classic / CageRT fullscreen |
| **U** | Toggle Classic **Rebuild** / **Update (refit)** |
| **1–4** | 64 → 1024 → 4096 → 25k plants |
| **T** | Auto tour for a capture |
| **G / F / R** | Cages / freeze / ray debug |

**4** stays on CageRT. Do not toggle Classic there (TDR risk from tens of thousands of unique BLASes).

## Build (Windows)

VS 2022 Build Tools, Windows SDK 10+, CMake 3.24+, DXR GPU.

```powershell
cmake --preset windows-x64
cmake --build "build/windows-x64" --config Release
ctest -C Release --test-dir build/windows-x64 --output-on-failure
```

Demo: `build/windows-x64/apps/demo/Release/OpenCageRTDemo.exe`

CPU tests run in CI. `--parity` / `--benchmark` need a DXR GPU and are not run on GitHub-hosted runners.

## License

MIT — see [LICENSE](LICENSE).
