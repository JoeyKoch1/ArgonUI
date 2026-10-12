// ArgonUI - backends/common/ArgonHLSL.h
//
// The HLSL compiled by the D3D11 and D3D12 backends. This is a byte-for-byte
// copy of shaders/argonui.hlsl; keep the two in sync (the D3D12 backend
// additionally prepends its own root-signature-independent prefix).
#pragma once

namespace argon::ui {

inline constexpr const char* kArgonHlslSource = R"HLSL(
// Attribute offsets are dictated by argon::ui::Vertex:
//   0  pos     vec2  (x, y)
//   8  uv      vec2
//  16  half    vec2  (hx, hy)
//  24  radii   vec4  (rtl, rtr, rbr, rbl)
//  40  edge    vec2  (borderWidth, glowWidth)
//  48  fill    uint | 52 border uint | 56 glow uint | 60 flags uint
struct VSIn {
    float2 pos     : POSITION;
    float2 uv      : TEXCOORD0;
    float2 half    : TEXCOORD1;
    float4 radii   : TEXCOORD2;
    float2 edge    : TEXCOORD3;
    uint   fill    : TEXCOORD4;
    uint   border  : TEXCOORD5;
    uint   glow    : TEXCOORD6;
    uint   flags   : TEXCOORD7;
};

struct VSOut {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
    float2 half : TEXCOORD1;
    float4 radii : TEXCOORD2;
    float2 edge  : TEXCOORD3;
    nointerpolation uint fill   : TEXCOORD4;
    nointerpolation uint border : TEXCOORD5;
    nointerpolation uint glow   : TEXCOORD6;
    nointerpolation uint flags  : TEXCOORD7;
};

cbuffer ArgonViewportCB : register(b0) {
    float4 gViewport;   // xy = pixel size, zw = 1 / pixel size
};

VSOut VSMain(VSIn input) {
    VSOut o;
    // Screen pixels -> clip space. Note the *2.0: gViewport.z is 1/pixelSize,
    // so this maps [0, w] onto [-1, +1]. Dropping the 2.0 collapses the whole
    // UI into the top-left quadrant.
    o.pos = float4(input.pos.x * gViewport.z * 2.0 - 1.0,
                   1.0 - input.pos.y * gViewport.w * 2.0,
                   0.0,
                   1.0);
    o.uv    = input.uv;
    o.half  = input.half;
    o.radii = input.radii;
    o.edge  = input.edge;
    o.fill   = input.fill;
    o.border = input.border;
    o.glow   = input.glow;
    o.flags  = input.flags;
    return o;
}

Texture2D    gTexture;
SamplerState gSampler;

#define ARGON_VF_GRADIENT_V      1u
#define ARGON_VF_GRADIENT_H      2u
#define ARGON_VF_GRADIENT_RADIAL 4u
#define ARGON_VF_TEX_COVERAGE   16u

float4 UnpackRGBA(uint packed) {
    return float4(
        float( packed        & 255u),
        float((packed >>  8) & 255u),
        float((packed >> 16) & 255u),
        float((packed >> 24) & 255u)) * (1.0 / 255.0);
}

float SdfRoundedBox(float2 p, float2 halfExtent, float radius) {
    float2 q = abs(p) - (halfExtent - radius);
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

float4 PSMain(VSOut i) : SV_TARGET {
    const float hx = i.half.x;
    const float hy = i.half.y;
    const float borderWidth = i.edge.x;
    const float glowWidth   = i.edge.y;

    const float2 expanded = float2(hx, hy) + glowWidth;
    const float2 p = (i.uv * 2.0 - 1.0) * expanded;

    // Screen Y grows downward: p.y > 0 is the bottom half.
    const float radius = (p.x > 0.0)
        ? ((p.y > 0.0) ? i.radii.z : i.radii.y)
        : ((p.y > 0.0) ? i.radii.w : i.radii.x);

    const float d = SdfRoundedBox(p, float2(hx, hy), radius);

    const float aa = max(fwidth(d), 1e-4) * 0.5;

    const float fillA   = 1.0 - smoothstep(-aa, aa, d + borderWidth);
    const float borderA = 1.0 - smoothstep(-aa, aa, abs(d + borderWidth * 0.5) - borderWidth * 0.5);
    const float shapeA  = max(fillA, borderA);

    const float4 fillC   = UnpackRGBA(i.fill);
    const float4 borderC = UnpackRGBA(i.border);
    const float4 glowC   = UnpackRGBA(i.glow);

    float3 shapeRGB = (fillC.rgb * fillA + borderC.rgb * borderA) / max(shapeA, 1e-4);

    const uint gflags = i.flags & 7u;
    if (gflags != 0u) {
        float t;
        if ((i.flags & ARGON_VF_GRADIENT_V) != 0u)      t = i.uv.y;
        else if ((i.flags & ARGON_VF_GRADIENT_H) != 0u) t = i.uv.x;
        else                                             t = saturate(length(p / max(expanded, 1e-4)));
        shapeRGB = lerp(fillC.rgb, borderC.rgb, t);
    }

    const bool coverageOnly = (i.flags & ARGON_VF_TEX_COVERAGE) != 0u;
    const float4 texel = gTexture.Sample(gSampler, i.uv);
    const float coverage = coverageOnly ? texel.r : texel.a;
    const float3 texRGB  = coverageOnly ? float3(1.0, 1.0, 1.0) : texel.rgb;

    const float alpha = shapeA * fillC.a * coverage;
    float3 rgbPremult = shapeRGB * texRGB * alpha;

    float alphaOut = alpha;
    if (glowWidth > 0.0) {
        const float gx = 1.0 - saturate(abs(p.x) / max(expanded.x, 1e-4));
        const float gy = 1.0 - saturate(abs(p.y) / max(expanded.y, 1e-4));
        const float glowShape = pow(saturate(min(gx, gy)), 2.0) * step(0.0, d);
        const float glowA = glowShape * glowC.a;
        rgbPremult += glowC.rgb * glowA * (1.0 - alphaOut);
        alphaOut    += glowA * (1.0 - alphaOut);
    }

    if (alphaOut <= 0.0005) discard;

    return float4(rgbPremult, alphaOut);
}
)HLSL";

} // namespace argon::ui