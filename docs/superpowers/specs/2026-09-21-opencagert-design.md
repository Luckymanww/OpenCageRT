# OpenCageRT v0.1 — design specification

**Status:** Draft for review (2026-09-21)  
**Reference:** Gruen, Benthin, Kern, McAllister — *Ray Tracing Massive Amounts of Animated Geometry*, HPG 2026, [DOI 10.1145/3820014](https://doi.org/10.1145/3820014).  
**License (project code):** MIT. Third-party deps checked separately.  
**Independence:** No AMD sample code, no author-version PDF in repo, no affiliation claim.

---

## 1. Goal

Build a **vendor-neutral DXR proof-of-concept** (NVIDIA RTX + AMD Radeon) that renders the **same animated scene** two ways and proves **correctness before scale**:

| Path | Idea |
|------|------|
| **A. Classic DXR** | Animate triangle vertices → update/rebuild **BLAS** per deformed object. |
| **B. CageRT** | Keep clipped geometry **static** in rest space → **immutable μBLAS** per tet cell → animate only **tetrahedral cage** → update **tetLAS** + **instance transforms** (`A M⁻¹`) so rays hit the same surfaces in world space. |

**v0.1 success:** HUD shows Classic vs CageRT **metrics** (VRAM, AS memory, build/update, trace, frame, FPS, tri/tet counts) and **correctness gates** pass on small scenes before the scale ladder.

---

## 2. Non-goals (v0.1)

- Unreal Engine / Unity plugins  
- Production-quality adaptive cage builder (SGD bones, keyframes)  
- Watertight **4D BVH** variant (paper’s 19–80× slower reference path)  
- **500M** triangle AMD demo parity  
- Complex skeletal animation as first-class feature  
- Performance tuning before **Classic hit ≈ Cage hit** on micro scenes  

---

## 3. Environment baseline (host)

Verified on primary dev machine (2026-09-21):

| Component | Status |
|-----------|--------|
| Windows | 10.0.26200 |
| GPU | NVIDIA RTX 3090, 24 GB, driver 616.92 |
| CMake | 4.4.3 |
| Git | 2.51.0 |
| VS Build Tools 2022 | 17.14.x, MSVC 14.44 |
| Windows SDK | 10.0.26100.0 |
| DXC | `Windows Kits\10\bin\10.0.26100.0\x64\dxc.exe` |
| DX12 / DXR | Tier check at runtime in demo (device supports DXR) |

**Not required yet:** Ninja (VS generator works), Vulkan, UE5.

---

## 4. Architecture overview

```
                    ┌─────────────────────────────────────┐
                    │           Scene (shared)            │
                    │  rest mesh, materials, animation    │
                    └─────────────────┬───────────────────┘
                                      │
              ┌───────────────────────┴───────────────────────┐
              ▼                                               ▼
    ┌──────────────────┐                         ┌──────────────────┐
    │  Classic path    │                         │  CageRT path     │
    │  (src/classic)   │                         │  (src/cage)      │
    └────────┬─────────┘                         └────────┬─────────┘
             │                                            │
             │ animate all vertices                       │ animate cage verts only
             │ rebuild/refit BLAS                         │ static μBLAS (immutable)
             ▼                                            ▼
    ┌──────────────────┐                         ┌──────────────────┐
    │ TLAS (1 inst/obj)│                         │ tetLAS (1 inst/tet)│
    │ world triangles  │                         │ inst xform A·M⁻¹   │
    └────────┬─────────┘                         └────────┬─────────┘
             │                                            │
             └────────────────────┬───────────────────────┘
                                  ▼
                    ┌─────────────────────────────────────┐
                    │  DXR pipeline (src/dxr)             │
                    │  shared raygen / hit / miss         │
                    │  mode switch + same camera/light    │
                    └─────────────────┬───────────────────┘
                                      ▼
                    ┌─────────────────────────────────────┐
                    │  HUD + benchmarks                   │
                    └─────────────────────────────────────┘
```

**Principle:** Classic and CageRT are **two backends** over one **scene description**. They must not share BLAS handles or confuse ownership.

---

## 5. Target repository layout

Current repo is a **prototype** (`opencagert_core`, `apps/demo`). v0.1 **target** layout (migrate incrementally):

```
OpenCageRT/
  docs/
    superpowers/specs/          # this file
    correctness.md              # tolerance tables, oracle procedure
  include/opencagert/           # public CPU API (geometry, cage, math)
  src/
    geometry/                   # mesh I/O, AABB, upload layouts
    cage/                       # voxel cage, clipper, μ-mesh build
    classic/                    # animated mesh → BLAS update path
    dxr/                        # device, descriptors, PSO, SBT, AS builds
    validation/                 # CPU ray oracle, hit compare
  shaders/
    common.hlsli
    raygen.hlsl
    closesthit.hlsl
    miss.hlsl
  apps/
    demo/                       # Win32 + HUD + mode toggle (Tab)
  benchmarks/
    scale_ladder/               # scripted 1K→50M scenes
  tests/
    unit/                       # math, clip, barycentrics, transforms
    integration/                # single-tet DXR parity (headless optional)
  CMakeLists.txt
  LICENSE (MIT)
  README.md
```

**Note:** `apps/demo/dxr_renderer.h` and `apps/demo/shaders/raytracing.hlsl` are **premature WIP** — do not extend until **M3**; logic moves under `src/dxr/` per this layout.

---

## 6. Data structures (CPU)

### 6.1 Shared scene

```cpp
struct Mesh {
  std::vector<Vec3> positions;
  std::vector<uint32_t> indices;  // triangles
};

struct Tetrahedron {
  Vec3 v0, v1, v2, v3;  // rest pose, counter-clockwise faces
};

struct MicroMesh {
  std::vector<Vec3> positions;    // rest, inside parent tet
  std::vector<uint32_t> indices;
  uint32_t parent_tet_index;
};

struct CageAsset {
  Mesh source_rest;
  std::vector<Tetrahedron> rest_tets;
  std::vector<MicroMesh> micro_meshes;  // 1:1 with tets that contain geometry
};
```

### 6.2 Animation (v0.1)

- **Connectivity-preserving** deformation only (wind, rigid bend, cage vertex lerp).
- `animate_cage(rest_tets, t) → animated_tets`
- Classic: `animate_mesh(rest, t) → deformed positions`

### 6.3 Math (CageRT instance transform)

Rest tet columns: `M = [v1-v0 | v2-v0 | v3-v0]` (3×3).  
Animated tet: `A` = 3×4 with translation `a0` (paper Eq. 2).  
**DXR instance transform (row-major 3×4):** `T = A · extend(M⁻¹, 0)`.

Ray in world space → for hit tet `i`, transformed ray for μBLAS `i`:

- **Hardware path:** set `InstanceDesc.Transform = T_i` (DXR applies inverse to ray into BLAS space).
- **Validation path:** CPU oracle applies same transform explicitly and compares hits.

**Requirements:**

- Reject singular `M` or `A` at build time (skip tet or fallback to Classic for that object).
- Store both `T` and `T⁻¹` only if needed for normals; normals: inverse-transpose of linear part.

---

## 7. GPU resource ownership

| Resource | Classic owner | CageRT owner | Lifetime |
|----------|---------------|--------------|----------|
| Vertex/index buffers (deformed) | `classic/` | — | Updated every frame |
| Vertex/index buffers (rest μ) | — | `cage/` | **Immutable** after build |
| BLAS (dense) | `classic/` | — | Rebuild/refit per frame |
| BLAS (μ, per tet) | — | `cage/` | **Build once** |
| TLAS / tetLAS | `classic/` | `cage/` | Rebuild/update per frame |
| Instance buffer | 1 per object | 1 per tet | Cage: update transforms only |
| Shader table | `dxr/` shared | `dxr/` shared | Mode uniform selects shading tint only |

**Rule:** CageRT **never** writes μBLAS triangle memory after first build.

---

## 8. Clipping, cracks, tolerances

### 8.1 Clipping

- Split source triangles against **four half-spaces** of each tetrahedron.
- **Ownership:** each clipped triangle belongs to **exactly one** tet (after inflate-ε overlap at shared faces for non-watertight DXR path — paper §4.1, ε ≈ 2.5×10⁻⁶ in their units).
- **Shared boundary vertices:** quantize/snap to plane; dedupe keys `(quantized position, tet pair)` for edge ownership.

### 8.2 Failure criteria (automatic)

| Symptom | Likely cause |
|---------|----------------|
| Cracks / light leaks | Missing clip, wrong ε, inconsistent instance transform |
| Missing hits | Singular `M`, wrong `T`, BLAS not bound to instance |
| Duplicate hits | Triangle spans tets without clip; overlapping instances |
| Shimmer | Watertightness / different local rays per tet |
| Exploding mesh | Inverted tet, `M⁻¹` numerically unstable |

### 8.3 Numerical tolerances (initial)

| Check | Tolerance |
|-------|-----------|
| Point inside tet | plane distance ≥ −1e-5 |
| Vertex quantize | 1e-6 world units (scene-dependent) |
| Hit distance compare | relative 1e-4, abs 1e-5 |
| Hit position compare | 1e-4 (same units as mesh) |
| Normal compare | cos angle ≥ 0.999 |

Tune per scene; document in `docs/correctness.md`.

---

## 9. Validation and debug modes

1. **CPU oracle:** trace ray against deformed triangles (Classic ground truth for small meshes).  
2. **CPU cage trace:** tet walk + transform ray + μ triangle test in rest space.  
3. **DXR parity:** same rays through Classic DXR vs CageRT DXR → compare hit/miss, `t`, `bary`, primitive id.  
4. **Debug views:** tet wireframe, μBLAS bounds, instance index, transform determinant.  
5. **Headless:** `--parity` exit code compares Classic vs CageRT barycentric RGB (HUD off). GPU required; not run on GitHub-hosted CI.

---

## 10. Metrics and HUD

Per frame, per mode (Classic / CageRT):

| Metric | Definition |
|--------|------------|
| Tracked RT memory | Sum of demo-owned VB/IB/BLAS/TLAS/instance buffers (not process VRAM) |
| Geometry memory | Vertex/index buffers used as AS inputs |
| AS memory | BLAS + TLAS + instance desc buffers |
| DXGI local usage | `IDXGIAdapter3::QueryVideoMemoryInfo` (process-visible adapter local) |
| BLAS build/update ms | GPU timestamp or CPU fence wait around AS commands |
| TLAS build/update ms | same |
| Ray trace ms | DispatchRays → fence |
| Frame ms | full frame |
| FPS | smoothed |
| Triangle count | rendered / traced |
| Tetrahedron count | cage tets (CageRT only) |

HUD must show **both modes side-by-side** with **identical camera and time**. This split compare view is a **flagship GitHub feature**, not a later polish pass.

### Flagship demo layout

```
[ slider: 100K → 1M → 10M → 50M ]

| CLASSIC DXR                         | CageRT                              |
| same grass/trees, camera, rays      | same grass/trees, camera, rays      |
| BLAS rebuilt as vertices animate    | static μ-geometry, animated cage    |
| Tracked RT mem / AS update / RT / FPS | Tracked RT mem / tetLAS update / RT / FPS     |
```

**Controls (v0.1 chrome, then real backends):**

| Key | Action |
|-----|--------|
| **C** | Toggle **Classic ↔ CageRT** (picture nearly identical; HUD numbers change) |
| **S** | Split view (both panels) |
| **← / →** or **[ / ]** | Triangle ladder 100K → 1M → 10M → 50M |
| **1–4** | Jump to a ladder rung |
| **G** | Show cages (tet wireframe over plants) |
| **F** | Freeze geometry (wind stops; later: mesh verts stay still while cage would still explain motion) |
| **R** | Ray debug: camera → deformed tet → rest space → μBLAS hit |
| **U** | Classic BLAS **Rebuild** vs **Update/Refit** (`ALLOW_UPDATE` / `PERFORM_UPDATE`) |

**Numbers:** HUD `ASmem` is tracked RT memory. Quote `--benchmark` CSV (warmup 60, 240 frames, median/p95, vsync off) rather than interactive split `rt_ms`. Split view still traces both paths in one `DispatchRays`, so RT is combined there; solo / `--benchmark` times Classic and CageRT separately.

**GitHub clip (later):** 15–20 s of Classic Update ↔ CageRT. First visual prototype is the split chrome + **single-tet debug** (`R`).

---

## 11. Benchmark methodology

- Fixed resolution (1080p default), primary + optional shadow ray (later).  
- Warmup 60 frames, measure 240 frames, report median and p95.  
- Scale ladder scenes **generated procedurally** with seeded RNG.  
- Report **speedup only** when correctness gate passed for that scene size.  
- Log CSV: `{scene, mode, tris, tets, vram_mb, blas_ms, tlas_ms, trace_ms, fps}`.

---

## 12. Test scenes (order)

1. **Micro:** single tet / single voxel (5–6 tets) — correctness.  
2. **Rigid bend:** one bar, coarse cage.  
3. **Grass patch:** many tris, moderate cage (paper-like).  
4. **Many independent objects:** unique cage per instance (memory story).

---

## 13. Scale ladder (after correctness)

```
1K → 100K → 1M → 10M → 50M triangles
```

Do **not** advance rung until:

- Parity tests pass at current rung.  
- No monotonic VRAM explosion vs Classic on **independently animated copies** benchmark.

---

## 14. Milestones (implementation order)

| ID | Milestone | Done when |
|----|-----------|-----------|
| **M0** | Spec approved (this doc) | User sign-off |
| **M1** | Minimal DXR project shell | Raygen gradient on GPU; DXR ON in title — **done 2026-09-21** |
| **M1b** | Flagship compare chrome | Split Classic\|CageRT, keys C/S/G/F/R, ladder, HUD format (placeholders OK) |
| **M2** | Repo layout + move CPU core to `geometry/` + `cage/` | Tests green; no duplicate demo DXR |
| **M3** | **Micro correctness:** 1 mesh, voxel cage, μBLAS, simple deform | **done 2026-09-21** (visual + `--parity` bary RGB gate) |
| **M4** | HUD metrics (both paths) | **done** — tracked RT mem / AS / RT; Classic Rebuild vs Update |
| **M5** | Clipper hardening + oracle tests | Unit tests for shared edges |
| **M6** | Scale 100K → 1M | `--benchmark` CSV + parity |
| **M7** | Scale 10M → 50M | Document where Classic OOMs / CageRT survives |

**First runnable milestone after spec:** **M1** (DXR shell only).  
**First scientific milestone:** **M3** (parity) — **landed**; remaining work is honest Classic Update numbers, CSV methodology, then AMD GPU CSV.

---

## 15. Technical risks

| Risk | Mitigation |
|------|------------|
| DXR instance transform convention (row vs col, inverse) | Unit test `transform_point`; compare to DXR doc; single-tet scene |
| Clipper bugs at scale | Oracle + micro scene before 1M |
| TLAS instance count limits | Partitioned TLAS (DXR spec part 2) later; start < 65k tets |
| μBLAS count / build time | Batch builds; show build once in metrics |
| False parity (same color, wrong hit) | Compare depth + bary, not just RGB |
| Premature optimization | Gate scale ladder on M3 |
| Legal | MIT + citation only; no AMD PDF/code |

---

## 16. Current codebase snapshot (2026-09-21)

Already present (keep, refactor per §5):

- `include/opencagert/*` — math, cage builder (voxel + clip), animation  
- `tests/*` — CPU tests (math, clipper, cage build) — **passing**  
- `apps/demo` — D3D12/DXR flagship: Classic Rebuild, Classic Update/Refit, CageRT, `--benchmark`, `--parity`
- CPU tests include image-parity helper + percentile

---

## 17. Smallest runnable milestone (recommendation)

**M1 — DXR shell (≈0.5–1 day)**

1. New target `src/dxr/` with device init, DXIL compile via DXC in CMake.  
2. Raygen only → gradient or clear UAV → copy to swapchain.  
3. Log: adapter, DXR tier, driver.  
4. **No** BLAS, **no** cage, **no** Classic.

Then **M3 micro parity** before any foliage / 1M tri work.

---

## 18. Approval

Implementation of M1+ starts **only after** this spec is reviewed.  
Requested review: confirm milestones, tolerances, and repo layout.
