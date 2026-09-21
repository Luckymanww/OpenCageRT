// Compare demo shell (M1+): split Classic | CageRT, debug overlays — no BLAS yet.

RWTexture2D<float4> g_output : register(u0);

cbuffer DemoParams : register(b0) {
  uint2 outputSize;
  uint frameIndex;
  uint viewMode;   // 0 split, 1 solo classic, 2 solo cage
  uint triLevel;   // 0..3 ladder
  uint debugFlags; // DemoDebug*
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

float3 classic_shade(float2 uv, float t) {
  float3 base = float3(0.12 + 0.55 * uv.x, 0.18 + 0.35 * uv.y, 0.22);
  float blade = smoothstep(0.02, 0.0, abs(frac(uv.x * 40.0) - 0.5) - 0.08 + 0.04 * sin(t + uv.y * 20.0));
  base += float3(0.15, 0.35, 0.08) * blade;
  return base;
}

float3 cage_shade(float2 uv, float t) {
  float3 base = float3(0.10 + 0.45 * uv.y, 0.22 + 0.40 * uv.x, 0.28);
  float blade = smoothstep(0.02, 0.0, abs(frac(uv.x * 40.0 + 0.07) - 0.5) - 0.08 + 0.04 * sin(t + uv.y * 19.0));
  base += float3(0.10, 0.32, 0.18) * blade;
  return base;
}

float3 wire_cage_overlay(float2 uv, float t) {
  float2 g = uv * 12.0;
  float2 f = abs(frac(g) - 0.5);
  float line = min(f.x, f.y);
  float tet = smoothstep(0.04, 0.0, line);
  float pulse = 0.5 + 0.5 * sin(t * 2.0 + uv.x * 8.0);
  return float3(0.9, 0.95, 1.0) * tet * (0.35 + 0.25 * pulse);
}

float3 ray_debug_overlay(float2 uv) {
  float2 c = uv - float2(0.35, 0.55);
  float ang = atan2(c.y, c.x);
  float beam = smoothstep(0.08, 0.0, abs(frac(ang / 6.28318 + 0.25) - 0.5) - 0.02);
  return float3(0.2, 0.85, 1.0) * beam * 0.6;
}

float top_slider_mask(float2 uv, uint level) {
  if (uv.y > 0.08) {
    return 0.0;
  }
  float track = smoothstep(0.012, 0.0, abs(uv.y - 0.04));
  float knob_x = 0.15 + float(level) * (0.70 / 3.0);
  float knob = smoothstep(0.025, 0.0, length(uv - float2(knob_x, 0.04)));
  return saturate(track * 0.35 + knob);
}

[shader("raygeneration")]
void RayGen() {
  uint2 pixel = DispatchRaysIndex().xy;
  if (pixel.x >= outputSize.x || pixel.y >= outputSize.y) {
    return;
  }

  float2 res = float2(outputSize);
  float2 uv = (float2(pixel) + 0.5) / res;
  float anim_t = float(frameIndex) * 0.016;
  if ((debugFlags & kFreezeGeom) != 0) {
    anim_t = 0.0;
  }

  float split_x = 0.5;
  bool left = uv.x < split_x;
  bool show_left = left;
  bool show_right = !left;

  if (viewMode == kSoloClassic) {
    show_left = show_right = true;
    left = true;
  } else if (viewMode == kSoloCage) {
    show_left = show_right = true;
    left = false;
  }

  float2 panel_uv = uv;
  if (viewMode == kSplit) {
    panel_uv.x = left ? (uv.x / split_x) : ((uv.x - split_x) / (1.0 - split_x));
  }

  float3 col = left ? classic_shade(panel_uv, anim_t) : cage_shade(panel_uv, anim_t);

  if ((debugFlags & kShowCages) != 0 && !left) {
    col += wire_cage_overlay(panel_uv, anim_t);
  }
  if ((debugFlags & kRayDebug) != 0 && !left) {
    col += ray_debug_overlay(panel_uv);
  }

  if (viewMode == kSplit) {
    float div = smoothstep(0.003, 0.0, abs(uv.x - split_x));
    col = lerp(col, float3(0.95, 0.95, 0.95), div);
  }

  float slider = top_slider_mask(uv, triLevel);
  col = lerp(col, float3(0.25, 0.55, 0.95), slider);

  // Header tint per panel (visual "CLASSIC" / "CageRT" bands until GPU text HUD).
  if (uv.y < 0.10 && uv.y > 0.08) {
    if (viewMode == kSplit) {
      if (left) {
        col = lerp(col, float3(0.85, 0.35, 0.25), 0.85);
      } else {
        col = lerp(col, float3(0.25, 0.75, 0.55), 0.85);
      }
    } else if (viewMode == kSoloClassic) {
      col = lerp(col, float3(0.85, 0.35, 0.25), 0.85);
    } else {
      col = lerp(col, float3(0.25, 0.75, 0.55), 0.85);
    }
  }

  g_output[pixel] = float4(col, 1.0);
}
