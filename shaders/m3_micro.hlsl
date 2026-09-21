// M3 micro: plant field, Classic unique BLAS vs CageRT rest + instance T.

RaytracingAccelerationStructure g_tlasClassic : register(t0);
RaytracingAccelerationStructure g_tlasCage : register(t1);
StructuredBuffer<float4> g_tetVerts : register(t2);
RWTexture2D<float4> g_output : register(u0);

cbuffer MicroParams : register(b0) {
  uint2 outputSize;
  uint frameIndex;
  uint viewMode;
  uint triLevel;
  uint debugFlags;
  float time;
  uint tetCount;
  float3 camOrigin;
  float camScale;
  float3 camTarget;
  uint instanceCount;
  float classicVramMb;
  float cageVramMb;
  float classicAsMs;
  float cageAsMs;
};

static const uint kSplit = 0;
static const uint kSoloClassic = 1;
static const uint kSoloCage = 2;
static const uint kShowCages = 1u << 0;
static const uint kFreezeGeom = 1u << 1;
static const uint kRayDebug = 1u << 2;

struct Payload {
  float3 color;
  float hit_t;
  uint hit;
};

float sd_segment(float2 p, float2 a, float2 b, float w) {
  float2 pa = p - a;
  float2 ba = b - a;
  float h = saturate(dot(pa, ba) / max(dot(ba, ba), 1e-8));
  return length(pa - ba * h) - w;
}

void camera_basis(out float3 origin, out float3 look, out float3 right, out float3 upv) {
  origin = camOrigin;
  float3 target = camTarget;
  look = normalize(target - origin);
  right = normalize(cross(look, float3(0, 1, 0)));
  upv = cross(right, look);
}

float2 project_world(float3 p, float aspect) {
  float3 origin, look, right, upv;
  camera_basis(origin, look, right, upv);
  float3 d = p - origin;
  float z = max(dot(d, look), 1e-4);
  float scale = max(camScale, 0.05);
  float x = dot(d, right) / (z * scale);
  float y = dot(d, upv) / (z * scale / aspect);
  return float2(x * 0.5 + 0.5, -y * 0.5 + 0.5);
}

float sd_box(float2 p, float2 b) {
  float2 q = abs(p) - b;
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
}

uint seven_mask(uint d) {
  switch (d) {
    case 0: return 0x3F;
    case 1: return 0x06;
    case 2: return 0x5B;
    case 3: return 0x4F;
    case 4: return 0x66;
    case 5: return 0x6D;
    case 6: return 0x7D;
    case 7: return 0x07;
    case 8: return 0x7F;
    case 9: return 0x6F;
    default: return 0;
  }
}

// p in [0,1], y grows down. Thick calculator digits.
float seven_digit(float2 p, uint mask) {
  const float t = 0.09;
  const float hw = 0.30;
  const float vh = 0.17;
  float d = 1e5;
  if (mask & 1) {
    d = min(d, sd_box(p - float2(0.50, 0.10), float2(hw, t)));
  }
  if (mask & 2) {
    d = min(d, sd_box(p - float2(0.82, 0.30), float2(t, vh)));
  }
  if (mask & 4) {
    d = min(d, sd_box(p - float2(0.82, 0.70), float2(t, vh)));
  }
  if (mask & 8) {
    d = min(d, sd_box(p - float2(0.50, 0.90), float2(hw, t)));
  }
  if (mask & 16) {
    d = min(d, sd_box(p - float2(0.18, 0.70), float2(t, vh)));
  }
  if (mask & 32) {
    d = min(d, sd_box(p - float2(0.18, 0.30), float2(t, vh)));
  }
  if (mask & 64) {
    d = min(d, sd_box(p - float2(0.50, 0.50), float2(hw, t)));
  }
  return d;
}

float seven_dot(float2 p) {
  return sd_box(p - float2(0.50, 0.88), float2(0.13, 0.13));
}

void push_glyph(inout uint ids[10], inout uint n, uint id) {
  if (n < 10) {
    ids[n] = id;
    n += 1;
  }
}

void format_fixed(float v, uint decimals, out uint ids[10], out uint n) {
  n = 0;
  ids[0] = 0;
  v = max(v, 0.0);
  uint div = (decimals == 2) ? 100u : 10u;
  uint scaled = (uint)(v * (float)div + 0.5);
  uint whole = scaled / div;
  uint frac = scaled % div;
  if (whole >= 100) {
    push_glyph(ids, n, min(whole / 100, 9u));
    push_glyph(ids, n, (whole / 10) % 10);
    push_glyph(ids, n, whole % 10);
    return;
  }
  if (whole >= 10) {
    push_glyph(ids, n, (whole / 10) % 10);
  }
  push_glyph(ids, n, whole % 10);
  push_glyph(ids, n, 10);
  if (decimals == 2) {
    push_glyph(ids, n, (frac / 10) % 10);
    push_glyph(ids, n, frac % 10);
  } else {
    push_glyph(ids, n, frac % 10);
  }
}

float draw_seven(float2 panel, float2 origin, float2 cell, uint ids[10], uint n) {
  float acc = 0.0;
  float x = origin.x;
  for (uint i = 0; i < n; ++i) {
    float w = (ids[i] == 10) ? cell.x * 0.42 : cell.x;
    float2 p = (panel - float2(x, origin.y)) / float2(w, cell.y);
    if (p.x >= -0.08 && p.x <= 1.08 && p.y >= -0.08 && p.y <= 1.08) {
      float d = (ids[i] == 10) ? seven_dot(p) : seven_digit(p, seven_mask(ids[i]));
      acc = max(acc, 1.0 - smoothstep(0.0, 0.035, d));
    }
    x += w * 1.14;
  }
  return acc;
}

[shader("raygeneration")]
void RayGen() {
  uint2 pixel = DispatchRaysIndex().xy;
  uint2 dim = DispatchRaysDimensions().xy;
  float2 uv = (float2(pixel) + 0.5) / float2(max(dim, uint2(1, 1)));
  float aspect = float(dim.x) / max(float(dim.y), 1.0);

  bool left = uv.x < 0.5;
  bool classic = true;
  float2 panel = uv;
  float panel_aspect = aspect;
  if (viewMode == kSplit) {
    classic = left;
    panel.x = left ? uv.x * 2.0 : (uv.x - 0.5) * 2.0;
    panel_aspect = aspect * 0.5;
  } else {
    classic = (viewMode == kSoloClassic);
  }

  float2 ndc = panel * 2.0 - 1.0;
  ndc.y = -ndc.y;

  float3 origin, look, right, upv;
  camera_basis(origin, look, right, upv);
  float scale = max(camScale, 0.05);
  float3 dir = normalize(look + right * (ndc.x * scale) + upv * (ndc.y * scale / panel_aspect));

  Payload p;
  p.color = 0;
  p.hit_t = 0;
  p.hit = 0;

  RayDesc ray;
  ray.Origin = origin;
  ray.Direction = dir;
  ray.TMin = 0.001;
  ray.TMax = 250.0;

  if (classic) {
    TraceRay(g_tlasClassic, RAY_FLAG_FORCE_OPAQUE, 0xFF, 0, 1, 0, ray, p);
  } else {
    TraceRay(g_tlasCage, RAY_FLAG_FORCE_OPAQUE, 0xFF, 0, 1, 0, ray, p);
  }

  float3 col = p.hit ? p.color : float3(0.045, 0.05, 0.07);

  if (!classic && (debugFlags & kShowCages) != 0) {
    float acc = 1.0;
    uint n = min(tetCount, 48u);
    for (uint i = 0; i < n; ++i) {
      float2 p0 = project_world(g_tetVerts[i * 4 + 0].xyz, panel_aspect);
      float2 p1 = project_world(g_tetVerts[i * 4 + 1].xyz, panel_aspect);
      float2 p2 = project_world(g_tetVerts[i * 4 + 2].xyz, panel_aspect);
      float2 p3 = project_world(g_tetVerts[i * 4 + 3].xyz, panel_aspect);
      float d = min(sd_segment(panel, p0, p1, 0.0028), sd_segment(panel, p1, p2, 0.0028));
      d = min(d, sd_segment(panel, p2, p0, 0.0028));
      d = min(d, sd_segment(panel, p0, p3, 0.0024));
      d = min(d, sd_segment(panel, p1, p3, 0.0024));
      d = min(d, sd_segment(panel, p2, p3, 0.0024));
      acc = min(acc, d);
    }
    col += float3(0.7, 0.92, 1.0) * smoothstep(0.0035, 0.0, acc);
  }

  if ((debugFlags & kRayDebug) != 0 && p.hit) {
    float3 hitp = origin + dir * p.hit_t;
    float2 hp = project_world(hitp, panel_aspect);
    float2 cam = project_world(origin, panel_aspect);
    float beam = sd_segment(panel, cam, hp, 0.003);
    col += float3(0.2, 0.85, 1.0) * smoothstep(0.004, 0.0, beam);
  }

  if (panel.y > 0.80) {
    float3 head = classic ? float3(0.28, 0.07, 0.06) : float3(0.03, 0.18, 0.14);
    col = lerp(col, head, 0.92);
    float vmax = max(max(classicVramMb, cageVramMb), 0.05);
    float v = classic ? classicVramMb : cageVramMb;
    float t = saturate(v / vmax);
    bool in_bar = panel.y > 0.92 && panel.y < 0.985 && panel.x > 0.04 && panel.x < (0.04 + 0.92 * t);
    if (in_bar) {
      col = classic ? float3(0.95, 0.32, 0.20) : float3(0.18, 0.92, 0.52);
    }

    uint ids[10];
    uint n = 0;
    format_fixed(v, 2, ids, n);
    float ink = draw_seven(panel, float2(0.05, 0.812), float2(0.075, 0.095), ids, n);
    format_fixed(classic ? classicAsMs : cageAsMs, 1, ids, n);
    ink = max(ink, draw_seven(panel, float2(0.58, 0.812), float2(0.075, 0.095), ids, n));
    col = lerp(col, float3(1.0, 1.0, 1.0), ink);
  }

  if (viewMode == kSplit) {
    col = lerp(col, float3(0.92, 0.92, 0.92), smoothstep(0.0035, 0.0, abs(uv.x - 0.5)));
  }

  g_output[pixel] = float4(col, 1.0);
}

[shader("closesthit")]
void ClosestHit(inout Payload p, in BuiltInTriangleIntersectionAttributes attr) {
  float3 bary = float3(1.0 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x,
                       attr.barycentrics.y);
  p.color = bary * 0.85 + 0.12;
  p.hit_t = RayTCurrent();
  p.hit = 1;
}

[shader("miss")]
void Miss(inout Payload p) {
  p.color = 0;
  p.hit_t = 0;
  p.hit = 0;
}
