// ArgonUI - shaders/argonui.hlsl  (reference implementation)
//
// ONE pipeline draws everything. Each quad carries a rounded-box SDF
// description (see Draw/Vertex.h); the fragment shader evaluates it
// analytically, so there is no tessellation, no CPU rasterisation and no
// geometry to rebuild when a radius changes.
//
// The GLSL, SPIR-V and HLSL backends are all line-by-line ports of this file.
// If you change one, change all four, or the backends will visually disagree.
//
//  vs_ArgonUI   : pixel-space vertices -> clip space
//  ps_ArgonUI   : rounded-box SDF + gradient + glow + texture modulation
//
// Blend state is PREMULTIPLIED ALPHA: (ONE, ONE_MINUS_SRC_ALPHA).
// The shader therefore returns rgb already multiplied by a.

static const float2 kViewportMin = float2(-1.0, -1.0);
static const float2 kViewportMax = float2( 1.0,  1.0);

 
// Vertex
 

// Attribute offsets are dictated by argon::ui::Vertex:
//   0  pos     vec2  (x, y)
//   8  uv      vec2
//  16  half    vec2  (hx, hy)
//  24  radii   vec4  (rtl, rtr, rbr, rbl)
//  40  edge    vec2  (borderWidth, glowWidth)
//  48  fill    uint | 52 border uint | 56 glow uint | 60 flags uint
struct VSIn {
    float2 pos     : POSITION;   // screen pixels
    float2 uv      : TEXCOORD0;  // 0..1 across the glow-expanded box
    float2 half    : TEXCOORD1;  // hx, hy
    float4 radii   : TEXCOORD2;  // rtl, rtr, rbr, rbl
    float2 edge    : TEXCOORD3;  // borderWidth, glowWidth
    uint   fill    : TEXCOORD4;  // packed RGBA
    uint   border  : TEXCOORD5;  // packed RGBA
    uint   glow    : TEXCOORD6;  // packed RGBA
    uint   flags   : TEXCOORD7;  // VertexFlags
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

// CBUFFER layout must be 16-byte aligned in every register:
//   register(b0) -> Viewport (x, y, invW, invH)
//   register(b1) -> unused
struct ViewportCB {
    float4 viewport;  // xy = pixel size, zw = 1 / pixel size
};

VSOut VSMain(VSIn input, uniform ViewportCB vp) {
    VSOut o;
    // Screen pixels -> clip space. Note the *2.0: vp.viewport.z is 1/pixelSize,
    // so this maps [0, w] onto [-1, +1]. Dropping the 2.0 collapses the whole
    // UI into the top-left quadrant.
    o.pos = float4(input.pos.x * vp.viewport.z * 2.0 - 1.0,
                  1.0 - input.pos.y * vp.viewport.w * 2.0,
                  0.0,
                  1.0);
    o.uv     = input.uv;
    o.half   = input.half;
    o.radii  = input.radii;
    o.edge   = input.edge;
    o.fill   = input.fill;
    o.border = input.border;
    o.glow   = input.glow;
    o.flags  = input.flags;
    return o;
}

 
// Fragment
 

Texture2D    gTexture;
SamplerState gSampler;

#define ARGON_VF_GRADIENT_V     1u
#define ARGON_VF_GRADIENT_H     2u
#define ARGON_VF_GRADIENT_RADIAL 4u
#define ARGON_VF_INSET_SHADOW   8u
#define ARGON_VF_TEX_COVERAGE  16u

float4 UnpackRGBA(uint packed) {
    return float4(
        float( packed        & 255u),
        float((packed >>  8) & 255u),
        float((packed >> 16) & 255u),
        float((packed >> 24) & 255u)) * (1.0 / 255.0);
}

/// Signed distance to a rounded box centred on the origin.
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

    // Position relative to the core box centre, in pixels.
    const float2 p = (i.uv * 2.0 - 1.0) * expanded;

    // Screen Y grows downward, so p.y > 0 is the bottom half.
    const float radius = (p.x > 0.0)
        ? ((p.y > 0.0) ? i.radii.z : i.radii.y)   // br : tr
        : ((p.y > 0.0) ? i.radii.w : i.radii.x);  // bl : tl

    const float d = SdfRoundedBox(p, float2(hx, hy), radius);

    // Analytic anti-aliasing: one device pixel wide, whatever the scale.
    const float aa = max(fwidth(d), 1e-4) * 0.5;

    const float fillA   = 1.0 - smoothstep(-aa, aa, d + borderWidth);
    const float borderA = 1.0 - smoothstep(-aa, aa, abs(d + borderWidth * 0.5) - borderWidth * 0.5);
    const float shapeA  = max(fillA, borderA);

    const float4 fillC   = UnpackRGBA(i.fill);
    const float4 borderC = UnpackRGBA(i.border);
    const float4 glowC   = UnpackRGBA(i.glow);

    // --- colour ---------------------------------------------------------
    float3 shapeRGB = (fillC.rgb * fillA + borderC.rgb * borderA) / max(shapeA, 1e-4);

    const uint gflags = i.flags & 7u;
    if (gflags != 0u) {
        float t;
        if ((i.flags & ARGON_VF_GRADIENT_V) != 0u)      t = i.uv.y;
        else if ((i.flags & ARGON_VF_GRADIENT_H) != 0u) t = i.uv.x;
        else                                             t = saturate(length(p / max(expanded, 1e-4)));
        // Gradient overrides the border mix; the border colour is the far stop.
        shapeRGB = lerp(fillC.rgb, borderC.rgb, t);
    }

    // --- texture --------------------------------------------------------
    const bool coverageOnly = (i.flags & ARGON_VF_TEX_COVERAGE) != 0u;
    const float4 texel = gTexture.Sample(gSampler, i.uv);
    const float coverage = coverageOnly ? texel.r : texel.a;
    const float3 texRGB  = coverageOnly ? float3(1.0, 1.0, 1.0) : texel.rgb;

    const float alpha = shapeA * fillC.a * coverage;
    float3 rgbPremult = shapeRGB * texRGB * alpha;

    // --- glow -------------------------------------------------------------
    // Box-space falloff so the halo reaches zero exactly at the expanded quad
    // edge and is never clipped at the corners (a euclidean falloff is).
    float alphaOut = alpha;
    if (glowWidth > 0.0) {
        const float gx = 1.0 - saturate(abs(p.x) / max(expanded.x, 1e-4));
        const float gy = 1.0 - saturate(abs(p.y) / max(expanded.y, 1e-4));
        const float glowShape = pow(saturate(min(gx, gy)), 2.0) * step(0.0, d);
        const float glowA = glowShape * glowC.a;
        // Composite the halo *behind* the shape: premultiplied "over".
        rgbPremult += glowC.rgb * glowA * (1.0 - alphaOut);
        alphaOut    += glowA * (1.0 - alphaOut);
    }

    // Fully outside the shape and its halo: nothing to blend.
    if (alphaOut <= 0.0005) discard;

    return float4(rgbPremult, alphaOut);
}