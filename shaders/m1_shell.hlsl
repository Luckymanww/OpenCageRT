// Compare-demo chrome (pre-BLAS): split Classic | CageRT, debug overlays.
// Geometry is still a stand-in; keys drive the same HUD contract as the real demo.

RWTexture2D<float4> g_output : register(u0);

cbuffer DemoParams : register(b0) {
  uint2 outputSize;
  uint frameIndex;
  uint viewMode;   // 0 split, 1 classic, 2 cage
  uint triLevel;   // 0..3
  uint debugFlags;
  uint padding;
};

static const uint kSplit = 0;
static const uint kSoloClassic = 1;
static const uint kSoloCage = 2;
static const uint kShowCages = 1u << 0;
static const uint kFreezeGeom = 1u << 1;
static const uint kRayDebug = 1u << 2;

float hash21(float2 p) {
  return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}

float sd_segment(float2 p, float2 a, float2 b, float w) {
  float2 pa = p - a;
  float2 ba = b - a;
  float h = saturate(dot(pa, ba) / dot(ba, ba));
  return length(pa - ba * h) - w;
}

float3 foliage(float2 uv, float t, float dens) {
  float ground = smoothstep(0.55, 0.52, uv.y);
  float3 col = lerp(float3(0.07, 0.09, 0.12), float3(0.10, 0.22, 0.12), ground);
  float wind = 0.03 * sin(t * 2.2 + uv.x * 14.0);
  for (int i = 0; i < 6; ++i) {
    float id = float(i);
    float x = frac(uv.x * (9.0 + dens * 6.0) + id * 0.17);
    float blade = abs(x - 0.5 - wind * (0.4 + 0.1 * id));
    float h = 0.18 + 0.12 * hash21(float2(id, dens));
    float g = smoothstep(0.025, 0.0, blade) * smoothstep(0.52 - h, 0.52, uv.y);
    col += float3(0.08, 0.22, 0.06) * g;
  }
  return col;
}

float3 tet_wire(float2 uv, float t) {
  float2 c = float2(0.52, 0.42);
  float2 p0 = c + float2(-0.16, 0.14);
  float2 p1 = c + float2(0.18, 0.12);
  float2 p2 = c + float2(0.00, -0.18);
  float2 p3 = c + float2(0.04, 0.02) + 0.06 * float2(sin(t), cos(t * 0.7));
  float d = min(sd_segment(uv, p0, p1, 0.004), sd_segment(uv, p1, p2, 0.004));
  d = min(d, sd_segment(uv, p2, p0, 0.004));
  d = min(d, sd_segment(uv, p0, p3, 0.0035));
  d = min(d, sd_segment(uv, p1, p3, 0.0035));
  d = min(d, sd_segment(uv, p2, p3, 0.0035));
  return float3(0.85, 0.95, 1.0) * smoothstep(0.004, 0.0, d);
}

float3 ray_path(float2 uv) {
  float2 cam = float2(0.08, 0.88);
  float2 hit = float2(0.52, 0.42);
  float2 rest = float2(0.82, 0.38);
  float d = min(sd_segment(uv, cam, hit, 0.0035), sd_segment(uv, hit, rest, 0.003));
  d = min(d, length(uv - cam) - 0.012);
  return float3(0.25, 0.9, 1.0) * smoothstep(0.004, 0.0, d);
}

float vram_bar(float2 uv, float fill, bool left_panel) {
  float x0 = left_panel ? 0.08 : 0.08;
  float x1 = x0 + 0.84;
  float y0 = 0.08;
  float y1 = 0.13;
  if (uv.x < x0 || uv.x > x1 || uv.y < y0 || uv.y > y1) {
    return 0.0;
  }
  float t = saturate((uv.x - x0) / (x1 - x0));
  return t < fill ? 1.0 : 0.25;
}

[shader("raygeneration")]
void RayGen() {
  uint2 pixel = DispatchRaysIndex().xy;
  uint2 dim = DispatchRaysDimensions().xy;
  float2 uv = (float2(pixel) + 0.5) / float2(max(dim, uint2(1, 1)));

  float t = float(frameIndex) * 0.016;
  if ((debugFlags & kFreezeGeom) != 0) {
    t = 0.0;
  }

  bool left = uv.x < 0.5;
  bool classic = true;
  float2 panel = uv;
  if (viewMode == kSplit) {
    classic = left;
    panel.x = left ? uv.x * 2.0 : (uv.x - 0.5) * 2.0;
  } else {
    classic = (viewMode == kSoloClassic);
  }

  float dens = 0.15 + float(triLevel) * 0.28;
  float3 col = foliage(panel, t, dens);
  if (!classic) {
    col = foliage(panel + float2(0.004, 0.0), t, dens);
    col = lerp(col, float3(0.06, 0.14, 0.16), 0.08);
  }

  if (!classic && (debugFlags & kShowCages) != 0) {
    col += tet_wire(panel, t) * 0.9;
    float2 g = panel * (6.0 + float(triLevel));
    float2 f = abs(frac(g) - 0.5);
    col += float3(0.2, 0.45, 0.55) * smoothstep(0.03, 0.0, min(f.x, f.y)) * 0.35;
  }
  if (!classic && (debugFlags & kRayDebug) != 0) {
    col += ray_path(panel);
    col += tet_wire(panel, t) * 0.5;
  }

  float stress = float(triLevel) / 3.0;
  float classic_fill = 0.12 + stress * 0.82;
  float cage_fill = 0.10 + stress * 0.12;
  float bar = vram_bar(panel, classic ? classic_fill : cage_fill, classic);
  if (bar > 0.0) {
    float3 bar_col = classic ? float3(0.85, 0.28, 0.18) : float3(0.18, 0.78, 0.55);
    col = lerp(col, bar_col, 0.35 + 0.55 * step(0.3, bar));
  }

  if (panel.y > 0.90) {
    float3 head = classic ? float3(0.72, 0.22, 0.16) : float3(0.10, 0.55, 0.42);
    col = lerp(col, head, 0.82);
  }

  if (viewMode == kSplit) {
    col = lerp(col, float3(0.95, 0.95, 0.95), smoothstep(0.004, 0.0, abs(uv.x - 0.5)));
  }

  // Top triangle-count slider.
  if (uv.y > 0.965) {
    col = lerp(col, float3(0.12, 0.14, 0.18), 0.85);
    float knob = 0.12 + float(triLevel) * 0.25;
    col = lerp(col, float3(0.35, 0.65, 1.0), smoothstep(0.018, 0.0, abs(uv.x - knob)));
  }

  g_output[pixel] = float4(col, 1.0);
}
