// ArgonUI - backends/opengl/GLBackend.cpp
#include "backends/opengl/GLBackend.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "argonui/Core/Log.h"

namespace argon::ui {

namespace {

constexpr const char* kTag = "OpenGL";

// ---------------------------------------------------------------------------
// GLSL - a line-by-line port of shaders/argonui.hlsl
// ---------------------------------------------------------------------------
// Both stages are stored here, in the open, because the maths has to match the
// HLSL exactly or the backends will visibly disagree. Keep them in sync with
// backends/common/ArgonHLSL.h and with every other API's copy.
//
// BLEND STATE IS PREMULTIPLIED ALPHA: glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA).
// Both stages therefore work in, and the fragment stage emits, rgb * a.

constexpr const char* kArgonGlslVertex = R"GLSL(#version 330 core
//
// ArgonUI vertex stage. Direct port of vs_ArgonUI in shaders/argonui.hlsl.
//
// The four colour/flag words are `flat` - GLSL's spelling of HLSL's
// `nointerpolation`. All four corners of a quad carry identical values, so
// interpolating them would be both slower and wrong at the seams.

// Attribute offsets are dictated by argon::ui::Vertex:
//   0  aPos     vec2  @0   (x, y)
//   1  aUV      vec2  @8
//   2  aHalf    vec2  @16  (hx, hy)
//   3  aRadii   vec4  @24  (rtl, rtr, rbr, rbl)
//   4  aEdge    vec2  @40  (borderWidth, glowWidth)
//   5  aFill    uint  @48 | 6 aBorder @52 | 7 aGlow @56 | 8 aFlags @60
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec2 aHalf;
layout(location = 3) in vec4 aRadii;
layout(location = 4) in vec2 aEdge;
layout(location = 5) in uint aFill;
layout(location = 6) in uint aBorder;
layout(location = 7) in uint aGlow;
layout(location = 8) in uint aFlags;
//
// aFill..aFlags are integer attributes and MUST be fed with
// glVertexAttribIPointer + GL_UNSIGNED_INT, never glVertexAttribPointer: the
// float entry point reinterprets the bits (as a normalised float, or as four
// 8-bit components) and every colour in the UI comes out as noise. That is the
// classic GL vertex-layout mistake and the reason this backend owns its VAO.

uniform vec4 uViewport;   // xy = pixel size, zw = 1 / pixel size

out vec2 vUV;
out vec2 vHalf;
out vec4 vRadii;
out vec2 vEdge;
flat out uint vFill;
flat out uint vBorder;
flat out uint vGlow;
flat out uint vFlags;

void main() {
    // x / width  -> [-1, 1]
    // y / height -> [ 1, -1]  (screen Y grows downward)
    // Identical maths to the HLSL: pos.x * invW - 1 and 1 - pos.y * invH.
    gl_Position = vec4(aPos.x * uViewport.z * 2.0 - 1.0,
                       1.0 - aPos.y * uViewport.w * 2.0,
                       0.0,
                       1.0);

    vUV    = aUV;
    vHalf  = aHalf;
    vRadii = aRadii;
    vEdge  = aEdge;
    vFill   = aFill;
    vBorder = aBorder;
    vGlow   = aGlow;
    vFlags  = aFlags;
}
)GLSL";

constexpr const char* kArgonGlslFragment = R"GLSL(#version 330 core
//
// ArgonUI fragment stage. Direct port of ps_ArgonUI in shaders/argonui.hlsl.
// One pipeline draws everything: each quad carries a rounded-box SDF
// description, evaluated here analytically - no tessellation, no CPU
// rasterisation, nothing to rebuild when a radius changes.

in vec2 vUV;
in vec2 vHalf;
in vec4 vRadii;
in vec2 vEdge;
flat in uint vFill;
flat in uint vBorder;
flat in uint vGlow;
flat in uint vFlags;

uniform sampler2D uTexture;   // always unit 0

out vec4 oColor;

#define ARGON_VF_GRADIENT_V      1u
#define ARGON_VF_GRADIENT_H      2u
#define ARGON_VF_GRADIENT_RADIAL 4u
#define ARGON_VF_INSET_SHADOW    8u
#define ARGON_VF_TEX_COVERAGE   16u

vec4 UnpackRGBA(uint packed) {
    return vec4(
        float( packed        & 255u),
        float((packed >>  8) & 255u),
        float((packed >> 16) & 255u),
        float((packed >> 24) & 255u)) * (1.0 / 255.0);
}

/// Signed distance to a rounded box centred on the origin.
float SdfRoundedBox(vec2 p, vec2 halfExtent, float radius) {
    vec2 q = abs(p) - (halfExtent - radius);
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

void main() {
    const float hx = vHalf.x;
    const float hy = vHalf.y;
    const float borderWidth = vEdge.x;
    const float glowWidth   = vEdge.y;

    const vec2 expanded = vec2(hx, hy) + glowWidth;

    // Position relative to the core box centre, in pixels.
    const vec2 p = (vUV * 2.0 - 1.0) * expanded;

    // Screen Y grows downward, so p.y > 0 is the bottom half.
    const float radius = (p.x > 0.0)
        ? ((p.y > 0.0) ? vRadii.z : vRadii.y)   // br : tr
        : ((p.y > 0.0) ? vRadii.w : vRadii.x);  // bl : tl

    const float d = SdfRoundedBox(p, vec2(hx, hy), radius);

    // Analytic anti-aliasing: one device pixel wide, whatever the scale.
    // The `f` suffixes matter: in GLSL an unsuffixed literal like 1e-4 is not
    // exactly representable and therefore becomes a double, which will not
    // implicitly convert back to a float before 4.00.
    const float aa = max(fwidth(d), 1e-4f) * 0.5;

    const float fillA   = 1.0 - smoothstep(-aa, aa, d + borderWidth);
    const float borderA = 1.0 - smoothstep(-aa, aa, abs(d + borderWidth * 0.5) - borderWidth * 0.5);
    const float shapeA  = max(fillA, borderA);

    const vec4 fillC   = UnpackRGBA(vFill);
    const vec4 borderC = UnpackRGBA(vBorder);
    const vec4 glowC   = UnpackRGBA(vGlow);

    // --- colour -----------------------------------------------------------
    vec3 shapeRGB = (fillC.rgb * fillA + borderC.rgb * borderA) / max(shapeA, 1e-4f);

    const uint gflags = vFlags & 7u;
    if (gflags != 0u) {
        float t;
        if      ((vFlags & ARGON_VF_GRADIENT_V) != 0u) t = vUV.y;
        else if ((vFlags & ARGON_VF_GRADIENT_H) != 0u) t = vUV.x;
        else                                            t = clamp(length(p / max(expanded, 1e-4f)), 0.0, 1.0);
        // Gradient overrides the border mix; the border colour is the far stop.
        shapeRGB = mix(fillC.rgb, borderC.rgb, t);
    }

    // --- texture ----------------------------------------------------------
    // ArgonUI hands us STRAIGHT (non-premultiplied) data; see TextureDesc.
    const bool coverageOnly = (vFlags & ARGON_VF_TEX_COVERAGE) != 0u;
    const vec4 texel = texture(uTexture, vUV);
    const float coverage = coverageOnly ? texel.r : texel.a;
    const vec3 texRGB  = coverageOnly ? vec3(1.0, 1.0, 1.0) : texel.rgb;

    const float alpha = shapeA * fillC.a * coverage;
    vec3 rgbPremult = shapeRGB * texRGB * alpha;

    // --- glow -------------------------------------------------------------
    // Box-space falloff against the EXPANDED extent (not the core box), so the
    // halo reaches zero exactly at the expanded quad edge and is never clipped
    // at the corners the way a euclidean falloff is.
    float alphaOut = alpha;
    if (glowWidth > 0.0) {
        const float gx = 1.0 - clamp(abs(p.x) / max(expanded.x, 1e-4f), 0.0, 1.0);
        const float gy = 1.0 - clamp(abs(p.y) / max(expanded.y, 1e-4f), 0.0, 1.0);
        const float glowShape = pow(clamp(min(gx, gy), 0.0, 1.0), 2.0) * step(0.0, d);
        const float glowA = glowShape * glowC.a;
        // Composite the halo *behind* the shape: premultiplied "over".
        rgbPremult += glowC.rgb * glowA * (1.0 - alphaOut);
        alphaOut    += glowA * (1.0 - alphaOut);
    }

    // Fully outside the shape and its halo: nothing to blend.
    if (alphaOut <= 0.0005f) discard;

    oColor = vec4(rgbPremult, alphaOut);
}
)GLSL";

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

GLenum ToGlInternalFormat(TextureFormat f) {
    switch (f) {
    case TextureFormat::R8:    return GL_R8;
    case TextureFormat::RGBA8:
    case TextureFormat::BGRA8:
    default:                   return GL_RGBA8;
    }
}

GLenum ToGlFormat(TextureFormat f) {
    switch (f) {
    case TextureFormat::R8:    return GL_RED;
    case TextureFormat::BGRA8: return GL_BGRA;   // the only swizzle ArgonUI needs
    case TextureFormat::RGBA8:
    default:                   return GL_RGBA;
    }
}

const char* FramebufferStatusName(GLenum status) {
    switch (status) {
    case GL_FRAMEBUFFER_COMPLETE:              return "GL_FRAMEBUFFER_COMPLETE";
    case 0x8CD6:                               return "GL_FRAMEBUFFER_UNDEFINED";
    case 0x8CD7:                               return "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT";
    case 0x8CDB:                               return "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT";
    case 0x8CDD:                               return "GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER";
    case 0x8CDC:                               return "GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER";
    case 0x8DCA:                               return "GL_FRAMEBUFFER_UNSUPPORTED";
    case 0x8D56:                               return "GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE";
    default:                                   return "unknown framebuffer status";
    }
}

void SetError(std::string* err, const char* fmt, ...) {
    if (err == nullptr) return;
    char buf[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    *err = buf;
}

/// Compile one stage and pull the driver's log out on failure.
bool CompileStage(const gl::Api& api, GLuint shader, const char* source, const char* label,
                  std::string* err) {
    api.ShaderSource(shader, 1, &source, nullptr);
    api.CompileShader(shader);

    GLint status = GL_FALSE;
    api.GetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_FALSE) return true;

    GLint length = 0;
    api.GetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<size_t>(length > 0 ? length : 1), '\0');
    if (length > 0) api.GetShaderInfoLog(shader, length, nullptr, &log[0]);
    SetError(err, "%s failed to compile: %s", label, log.c_str());
    return false;
}

// ---------------------------------------------------------------------------
// WGL context creation - only used by DeviceSource::CreateInternal
// ---------------------------------------------------------------------------
// Windows cannot ask wglCreateContext for "3.3 core"; it always returns a 1.1
// context. The documented dance is: make ANY legacy context current so the ICD
// has something to dispatch against, ask wglGetProcAddress for
// wglCreateContextAttribsARB, then call it with a version/profile attribute
// list. The legacy bootstrap runs on a hidden helper window rather than on the
// host's, so probing does not disturb the host's pixel format or context.

#ifndef WGL_CONTEXT_MAJOR_VERSION_ARB
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x9092
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
#endif

using PfnWglCreateContextAttribsARB = HGLRC(WINAPI*)(HDC, HGLRC, const int*);

constexpr const char* kWglHelperClass = "ArgonUI_WglBootstrap";

bool WglCreateModernContext(HWND window, HGLRC* outContext, HGLRC* outPrevious, HDC* outDC,
                            std::string* err) {
    *outContext = nullptr;
    *outPrevious = nullptr;
    *outDC = nullptr;

    const HMODULE module = GetModuleHandleA(nullptr);
    const HINSTANCE instance = module != nullptr ? module : GetModuleHandleA("opengl32.dll");

    WNDCLASSA wc{};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = instance;
    wc.lpszClassName = kWglHelperClass;
    bool ownsClass = RegisterClassA(&wc) != 0;
    if (!ownsClass && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        SetError(err, "RegisterClassA failed for the WGL bootstrap window (%lu)",
                 static_cast<unsigned long>(GetLastError()));
        return false;
    }

    HWND helper = CreateWindowExA(0, kWglHelperClass, "", WS_POPUP, 0, 0, 1, 1, nullptr,
                                  nullptr, instance, nullptr);
    if (helper == nullptr) {
        SetError(err, "CreateWindowExA failed for the WGL bootstrap window (%lu)",
                 static_cast<unsigned long>(GetLastError()));
        if (ownsClass) UnregisterClassA(kWglHelperClass, instance);
        return false;
    }

    HDC helperDC = GetDC(helper);
    HDC targetDC = nullptr;
    HGLRC legacy = nullptr;
    HGLRC modern = nullptr;
    HGLRC previous = nullptr;

    // One cleanup path for every failure below. No exceptions anywhere: GL
    // creation failures are reported through `err` like everything else.
    auto cleanup = [&]() {
        wglMakeCurrent(nullptr, nullptr);
        if (legacy != nullptr) { wglDeleteContext(legacy); legacy = nullptr; }
        if (modern != nullptr) { wglDeleteContext(modern); modern = nullptr; }
        if (targetDC != nullptr) { ReleaseDC(window, targetDC); targetDC = nullptr; }
        if (helperDC != nullptr) { ReleaseDC(helper, helperDC); helperDC = nullptr; }
        if (helper != nullptr) { DestroyWindow(helper); helper = nullptr; }
        if (ownsClass) UnregisterClassA(kWglHelperClass, instance);
    };

    // -- 1. legacy bootstrap on the hidden window -------------------------
    if (helperDC != nullptr) legacy = wglCreateContext(helperDC);
    if (legacy == nullptr) {
        SetError(err, "wglCreateContext failed for the bootstrap window - is there a "
                      "GPU driver installed at all?");
        cleanup();
        return false;
    }

    // Whatever the host had current, we are about to replace it. WGL gives us no
    // way back to its device context, so this is only informational.
    previous = wglGetCurrentContext();

    if (wglMakeCurrent(helperDC, legacy) == FALSE) {
        SetError(err, "wglMakeCurrent failed for the bootstrap context");
        cleanup();
        return false;
    }

    // -- 2. resolve the modern entry point --------------------------------
    auto createAttribs = reinterpret_cast<PfnWglCreateContextAttribsARB>(
        wglGetProcAddress("wglCreateContextAttribsARB"));
    if (createAttribs == nullptr) {
        SetError(err, "WGL_ARB_create_context is unavailable, so a 3.3 core profile "
                      "context cannot be created (ArgonUI requires OpenGL 3.3)");
        cleanup();
        return false;
    }

    // -- 3. the real context, on the host window's DC ---------------------
    targetDC = GetDC(window);
    if (targetDC == nullptr) {
        SetError(err, "GetDC failed for the window passed to DeviceDesc::windowHandle");
        cleanup();
        return false;
    }

    PIXELFORMATDESCRIPTOR pfd{};
    if (DescribePixelFormat(targetDC, 1, sizeof(pfd), &pfd) == 0) {
        // The window has never been given a pixel format (nobody else uses GL on
        // it). Pick the conventional one: double buffered RGBA8 + depth.
        std::memset(&pfd, 0, sizeof(pfd));
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;
        pfd.cAlphaBits = 8;
        pfd.cDepthBits = 24;
        pfd.cStencilBits = 8;
        pfd.iLayerType = PFD_MAIN_PLANE;
        const int format = ChoosePixelFormat(targetDC, &pfd);
        if (format == 0 || SetPixelFormat(targetDC, format, &pfd) == FALSE) {
            SetError(err, "SetPixelFormat failed on the target window (%lu)",
                     static_cast<unsigned long>(GetLastError()));
            cleanup();
            return false;
        }
    }

    const int attribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
        WGL_CONTEXT_MINOR_VERSION_ARB, 3,
        WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0
    };
    modern = createAttribs(targetDC, nullptr, attribs);
    if (modern == nullptr) {
        SetError(err, "wglCreateContextAttribsARB refused a 3.3 core profile context "
                      "(the driver may be software or the pixel format unsupported)");
        cleanup();
        return false;
    }

    wglDeleteContext(legacy);
    legacy = nullptr;

    if (wglMakeCurrent(targetDC, modern) == FALSE) {
        SetError(err, "wglMakeCurrent failed for the 3.3 core context");
        cleanup();
        return false;
    }

    // The bootstrap window has done its job.
    ReleaseDC(helper, helperDC);
    helperDC = nullptr;
    DestroyWindow(helper);
    helper = nullptr;
    if (ownsClass) UnregisterClassA(kWglHelperClass, instance);

    *outContext = modern;
    *outPrevious = previous;
    *outDC = targetDC;
    return true;
}

} // namespace

// ---------------------------------------------------------------------------

GLBackend::GLBackend() = default;
GLBackend::~GLBackend() { Shutdown(); }

void GLBackend::Fail(const char* what) {
    ARGON_ERROR(kTag, "%s", what);
    lastError_ = what;
}

/// Drain the GL error queue after a call whose failure would leave us in an
/// unknown state. Cheap next to a pipeline stall and it turns "black overlay"
/// into a sentence in the log.
bool GLBackend::CheckGl(const char* what) {
    const gl::Api& api = gl::Get();
    if (!api.loaded) return true;
    const GLenum first = api.GetError();
    if (first == GL_NO_ERROR) return true;

    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s failed (glGetError 0x%04X)", what,
                  static_cast<unsigned>(first));
    Fail(buf);
    return false;
}

// ---------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------

bool GLBackend::Initialize(const DeviceDesc& desc) {
    info_ = BackendInfo{};
    info_.api = GraphicsApi::OpenGL;
    info_.shadingLanguage = "GLSL 3.30 core";
    // OpenGL never owns a swapchain: the window system does. We only own a
    // context in CreateInternal mode.
    info_.ownsSwapchain = false;
    info_.supportsPremultipliedBlending = true;

    windowHandle_ = desc.windowHandle;
    deviceWidth_ = desc.width;
    deviceHeight_ = desc.height;

    if (desc.deviceSource == DeviceSource::CreateInternal) {
        HWND window = static_cast<HWND>(windowHandle_);
        if (window == nullptr) {
            lastError_ =
                "DeviceSource::CreateInternal needs DeviceDesc::windowHandle (an HWND)";
            ARGON_ERROR(kTag, "%s", lastError_.c_str());
            return false;
        }
        if (!CreateModernContext(window)) return false;
        ARGON_INFO(kTag, "created a 3.3 core context for hwnd %p", windowHandle_);
    } else {
        // The host made its context current before calling us. Adopt it, probe
        // it, and never touch its lifetime.
        ARGON_INFO(kTag, "using the OpenGL context the host made current");
    }

    // DeviceDesc::enableVsync / transparentBackground / msaaSamples have no
    // meaning here: GL has no present path (the host's SwapInterval governs
    // vsync), window transparency belongs to the compositor, and multisampling
    // would double-filter the shader's analytic anti-aliasing.

    std::string err;
    if (!gl::Load(&err)) {
        lastError_ = err;
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return false;
    }

    const gl::Api& api = gl::Get();
    if (!api.has_3_3_core) {
        lastError_ = "ArgonUI needs an OpenGL 3.3 core profile context (VAOs and "
                     "#version 330 core); this one reports '" +
                     std::string(api.version) + "'";
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return false;
    }

    info_.deviceName = api.renderer;
    info_.driverVersion = api.version;
    info_.maxTextureSize = static_cast<u32>(api.max_texture_size);
    info_.ownsDevice = ownsContext_;
    ARGON_INFO(kTag, "%s | %s | %s (OpenGL %d.%d)", api.renderer, api.vendor, api.version,
               api.major, api.minor);

    if (!CompileShaders()) return false;
    if (!CreatePipeline()) return false;
    if (!CreateStaticResources()) return false;

    initialized_ = true;
    return true;
}

bool GLBackend::CreateModernContext(HWND window) {
    HGLRC context = nullptr;
    HGLRC previous = nullptr;
    HDC dc = nullptr;
    if (!WglCreateModernContext(window, &context, &previous, &dc, &lastError_)) {
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return false;
    }
    context_ = context;
    previousContext_ = previous;
    contextDC_ = dc;
    ownsContext_ = true;
    if (previousContext_ != nullptr) {
        // WGL will not give us back the device context the host's context was
        // made current on, so it cannot be restored at shutdown - only noted.
        ARGON_WARN(kTag, "replaced the caller's current context (%p); it cannot be "
                         "restored automatically", static_cast<void*>(previousContext_));
    }
    return true;
}

void GLBackend::DestroyModernContext() {
    // Only ever touches a context this backend created. A host's context is
    // left exactly as we found it.
    if (context_ != nullptr) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(context_);
        context_ = nullptr;
    }
    if (contextDC_ != nullptr) {
        ReleaseDC(static_cast<HWND>(windowHandle_), contextDC_);
        contextDC_ = nullptr;
    }
    previousContext_ = nullptr;
    ownsContext_ = false;
}

void GLBackend::Shutdown() {
    DestroyTarget();

    const gl::Api& api = gl::Get();
    if (api.loaded) {
        for (auto& kv : textures_) {
            if (kv.second.name != 0) api.DeleteTextures(1, &kv.second.name);
        }
        textures_.clear();
        whiteTexture_ = TextureRecord{};
        nextTextureId_ = 2;

        if (vertexBuffer_ != 0) api.DeleteBuffers(1, &vertexBuffer_);
        if (indexBuffer_ != 0) api.DeleteBuffers(1, &indexBuffer_);
        vertexBufferSize_ = 0;
        indexBufferSize_ = 0;
        if (vertexArray_ != 0) api.DeleteVertexArrays(1, &vertexArray_);

        if (linearSampler_ != 0) api.DeleteSamplers(1, &linearSampler_);
        if (nearestSampler_ != 0) api.DeleteSamplers(1, &nearestSampler_);
        if (program_ != 0) api.DeleteProgram(program_);
        if (vertexShader_ != 0) api.DeleteShader(vertexShader_);
        if (fragmentShader_ != 0) api.DeleteShader(fragmentShader_);

        // Leave the context in a predictable state for whoever draws next.
        api.UseProgram(0);
        api.BindVertexArray(0);
        api.BindBuffer(GL_ARRAY_BUFFER, 0);
        api.BindTexture(GL_TEXTURE_2D, 0);
        api.BindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    program_ = vertexShader_ = fragmentShader_ = 0;
    vertexArray_ = vertexBuffer_ = indexBuffer_ = 0;
    linearSampler_ = nearestSampler_ = 0;
    uniformViewport_ = uniformTexture_ = -1;
    initialized_ = false;
    frameBegun_ = false;
    boundTexture_ = 0;

    DestroyModernContext();
}

// ---------------------------------------------------------------------------
// Shaders and pipeline
// ---------------------------------------------------------------------------

bool GLBackend::CompileShaders() {
    const gl::Api& api = gl::Get();

    vertexShader_ = api.CreateShader(GL_VERTEX_SHADER);
    if (vertexShader_ == 0) { Fail("glCreateShader(GL_VERTEX_SHADER) failed"); return false; }
    if (!CompileStage(api, vertexShader_, kArgonGlslVertex, "argonui.vert", &lastError_)) {
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return false;
    }

    fragmentShader_ = api.CreateShader(GL_FRAGMENT_SHADER);
    if (fragmentShader_ == 0) { Fail("glCreateShader(GL_FRAGMENT_SHADER) failed"); return false; }
    if (!CompileStage(api, fragmentShader_, kArgonGlslFragment, "argonui.frag", &lastError_)) {
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return false;
    }
    return true;
}

bool GLBackend::LinkProgram() {
    const gl::Api& api = gl::Get();

    program_ = api.CreateProgram();
    if (program_ == 0) { Fail("glCreateProgram failed"); return false; }
    api.AttachShader(program_, vertexShader_);
    api.AttachShader(program_, fragmentShader_);
    api.LinkProgram(program_);

    GLint status = GL_FALSE;
    api.GetProgramiv(program_, GL_LINK_STATUS, &status);
    if (status == GL_FALSE) {
        GLint length = 0;
        api.GetProgramiv(program_, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<size_t>(length > 0 ? length : 1), '\0');
        if (length > 0) api.GetProgramInfoLog(program_, length, nullptr, &log[0]);

        char buf[64];
        std::snprintf(buf, sizeof(buf), "glLinkProgram failed: %s", log.c_str());
        Fail(buf);
        return false;
    }

    // The program holds its own reference to both stages now.
    api.DeleteShader(vertexShader_);
    vertexShader_ = 0;
    api.DeleteShader(fragmentShader_);
    fragmentShader_ = 0;

    uniformViewport_ = api.GetUniformLocation(program_, "uViewport");
    if (uniformViewport_ < 0) { Fail("uViewport was optimised out of the program"); return false; }
    uniformTexture_ = api.GetUniformLocation(program_, "uTexture");
    if (uniformTexture_ < 0) { Fail("uTexture was optimised out of the program"); return false; }
    return true;
}

bool GLBackend::CreatePipeline() {
    const gl::Api& api = gl::Get();
    if (!LinkProgram()) return false;

    // One VAO, describing the exact 8-attribute layout of argon::ui::Vertex
    // (64 bytes). Created empty here; ConfigureVertexArray() fills it once the
    // buffers exist.
    api.GenVertexArrays(1, &vertexArray_);
    if (vertexArray_ == 0) { Fail("glGenVertexArrays failed"); return false; }

    // Seed both buffers so the VAO has something valid to describe, and so the
    // first frame never has to wait for a grow.
    if (!EnsureVertexCapacity(1024, 1536)) return false;
    if (!ConfigureVertexArray()) return false;
    api.BindVertexArray(0);

    // Sampler objects. They override the texture's own parameters, which is
    // exactly what we want: a host that binds one of our textures on another
    // sampler, or sets its own filters, cannot change how ArgonUI draws.
    api.GenSamplers(1, &linearSampler_);
    api.GenSamplers(1, &nearestSampler_);
    if (linearSampler_ == 0 || nearestSampler_ == 0) { Fail("glGenSamplers failed"); return false; }

    const GLint linearFilter = static_cast<GLint>(GL_LINEAR);
    const GLint nearestFilter = static_cast<GLint>(GL_NEAREST);
    const GLint clamp = static_cast<GLint>(GL_CLAMP_TO_EDGE);

    api.SamplerParameteri(linearSampler_, GL_TEXTURE_MIN_FILTER, linearFilter);
    api.SamplerParameteri(linearSampler_, GL_TEXTURE_MAG_FILTER, linearFilter);
    api.SamplerParameteri(linearSampler_, GL_TEXTURE_WRAP_S, clamp);
    api.SamplerParameteri(linearSampler_, GL_TEXTURE_WRAP_T, clamp);
    api.SamplerParameteri(nearestSampler_, GL_TEXTURE_MIN_FILTER, nearestFilter);
    api.SamplerParameteri(nearestSampler_, GL_TEXTURE_MAG_FILTER, nearestFilter);
    api.SamplerParameteri(nearestSampler_, GL_TEXTURE_WRAP_S, clamp);
    api.SamplerParameteri(nearestSampler_, GL_TEXTURE_WRAP_T, clamp);

    return true;
}

bool GLBackend::CreateStaticResources() {
    // Texture id 1 is the built-in 1x1 opaque white every shape draws with.
    const u32 white = 0xFFFFFFFFu;
    whiteTexture_.desc.width = 1;
    whiteTexture_.desc.height = 1;
    whiteTexture_.desc.format = TextureFormat::RGBA8;
    if (!CreateTextureInternal(whiteTexture_.desc, &white, whiteTexture_)) return false;
    textures_.emplace(1u, whiteTexture_);
    return true;
}

bool GLBackend::ConfigureVertexArray() {
    const gl::Api& api = gl::Get();
    if (vertexArray_ == 0 || vertexBuffer_ == 0 || indexBuffer_ == 0) return false;

    const GLsizei stride = static_cast<GLsizei>(sizeof(Vertex));
    static_assert(sizeof(Vertex) == 64, "ArgonUI vertex layout must be exactly 64 bytes");

    // Attribute pointers and the element buffer binding are VAO state, so this
    // has to happen with OUR VAO bound.
    api.BindVertexArray(vertexArray_);
    api.BindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);

    // Floats: locations 0-4.
    api.EnableVertexAttribArray(0);
    api.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride,
                            reinterpret_cast<const void*>(offsetof(Vertex, x)));
    api.EnableVertexAttribArray(1);
    api.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                            reinterpret_cast<const void*>(offsetof(Vertex, u)));
    api.EnableVertexAttribArray(2);
    api.VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                            reinterpret_cast<const void*>(offsetof(Vertex, hx)));
    api.EnableVertexAttribArray(3);
    api.VertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride,
                            reinterpret_cast<const void*>(offsetof(Vertex, rtl)));
    api.EnableVertexAttribArray(4);
    api.VertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, stride,
                            reinterpret_cast<const void*>(offsetof(Vertex, borderWidth)));

    // Integers: glVertexAttribIPointer, never glVertexAttribPointer. The float
    // entry point would reinterpret each packed RGBA word as a normalised float
    // and every colour would come out as noise.
    api.EnableVertexAttribArray(5);
    api.VertexAttribIPointer(5, 1, GL_UNSIGNED_INT, stride,
                             reinterpret_cast<const void*>(offsetof(Vertex, fill)));
    api.EnableVertexAttribArray(6);
    api.VertexAttribIPointer(6, 1, GL_UNSIGNED_INT, stride,
                             reinterpret_cast<const void*>(offsetof(Vertex, border)));
    api.EnableVertexAttribArray(7);
    api.VertexAttribIPointer(7, 1, GL_UNSIGNED_INT, stride,
                             reinterpret_cast<const void*>(offsetof(Vertex, glowColor)));
    api.EnableVertexAttribArray(8);
    api.VertexAttribIPointer(8, 1, GL_UNSIGNED_INT, stride,
                             reinterpret_cast<const void*>(offsetof(Vertex, flags)));

    // The element buffer binding is captured by whichever VAO is bound, so it
    // goes in here too - otherwise glDrawElements would read the default VAO's
    // index buffer.
    api.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer_);

    return CheckGl("glVertexAttribPointer setup");
}

bool GLBackend::EnsureVertexCapacity(size_t vertices, size_t indices) {
    const gl::Api& api = gl::Get();
    const size_t vertexBytes = vertices * sizeof(Vertex);
    const size_t indexBytes = indices * sizeof(u32);

    // Done with our VAO bound so both bindings are recorded into it.
    api.BindVertexArray(vertexArray_);

    if (vertexBuffer_ == 0 || vertexBytes > vertexBufferSize_) {
        if (vertexBuffer_ != 0) api.DeleteBuffers(1, &vertexBuffer_);
        // Grow geometrically: a menu that doubles its text length should not
        // reallocate every frame after that.
        vertexBufferSize_ = Max<size_t>(vertexBytes * 2, 64 * 1024);
        api.GenBuffers(1, &vertexBuffer_);
        api.BindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
        api.BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertexBufferSize_), nullptr,
                       GL_DYNAMIC_DRAW);
    }

    if (indexBuffer_ == 0 || indexBytes > indexBufferSize_) {
        if (indexBuffer_ != 0) api.DeleteBuffers(1, &indexBuffer_);
        indexBufferSize_ = Max<size_t>(indexBytes * 2, 32 * 1024);
        api.GenBuffers(1, &indexBuffer_);
        api.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer_);
        api.BufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indexBufferSize_), nullptr,
                       GL_DYNAMIC_DRAW);
    }

    // A fresh buffer has a fresh name, so the VAO's attribute pointers have to
    // be re-pointed at it.
    if (!ConfigureVertexArray()) return false;
    api.BindVertexArray(0);

    info_.vertexBufferSize = static_cast<u32>(vertexBufferSize_);
    info_.indexBufferSize = static_cast<u32>(indexBufferSize_);
    return vertexBuffer_ != 0 && indexBuffer_ != 0;
}

// ---------------------------------------------------------------------------
// Textures
// ---------------------------------------------------------------------------

bool GLBackend::CreateTextureInternal(const TextureDesc& desc, const void* pixels,
                                      TextureRecord& out) {
    const gl::Api& api = gl::Get();
    if (desc.width == 0 || desc.height == 0) return false;

    out.desc = desc;
    if (out.name == 0) api.GenTextures(1, &out.name);
    if (out.name == 0) { Fail("glGenTextures failed"); return false; }

    api.BindTexture(GL_TEXTURE_2D, out.name);

    // No mips: the shader only ever samples level 0, and asking for a mip on a
    // single-level texture makes the texture incomplete (black).
    const GLint filter = static_cast<GLint>(desc.filterLinear ? GL_LINEAR : GL_NEAREST);
    const GLint wrap = static_cast<GLint>(desc.repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    api.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    api.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    api.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    api.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    api.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

    // TextureDesc::srgb is deliberately ignored: ArgonUI composites unlit
    // colours and the reference D3D11 backend ignores it too, so honouring it
    // here would make the two disagree.
    api.PixelStorei(GL_UNPACK_ALIGNMENT, 1);  // R8 rows are not 4-byte aligned
    api.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(ToGlInternalFormat(desc.format)),
                   static_cast<GLsizei>(desc.width), static_cast<GLsizei>(desc.height), 0,
                   ToGlFormat(desc.format), GL_UNSIGNED_BYTE, pixels);
    api.PixelStorei(GL_UNPACK_ALIGNMENT, 4);  // the value the host left behind
    api.BindTexture(GL_TEXTURE_2D, 0);

    // glTexImage2D already consumed `pixels`; UpdateTexture's recreate path
    // reuses this with fresh data, so there is nothing left to upload here.
    return CheckGl("glTexImage2D");
}

bool GLBackend::UploadTextureData(const TextureDesc& desc, const void* pixels,
                                  TextureRecord& rec) {
    const gl::Api& api = gl::Get();
    if (pixels == nullptr || rec.name == 0) return false;

    api.BindTexture(GL_TEXTURE_2D, rec.name);
    api.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    api.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(desc.width),
                      static_cast<GLsizei>(desc.height), ToGlFormat(desc.format),
                      GL_UNSIGNED_BYTE, pixels);
    api.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    api.BindTexture(GL_TEXTURE_2D, 0);
    return CheckGl("glTexSubImage2D");
}

u32 GLBackend::CreateTexture(const TextureDesc& desc, const void* pixels) {
    if (!initialized_) return 0;
    if (desc.width == 0 || desc.height == 0) return 0;

    TextureRecord rec;
    if (!CreateTextureInternal(desc, pixels, rec)) return 0;

    const u32 id = nextTextureId_++;
    textures_.emplace(id, rec);
    return id;
}

bool GLBackend::UpdateTexture(u32 handle, const TextureDesc& desc, const void* pixels) {
    if (!initialized_ || pixels == nullptr) return false;
    auto it = textures_.find(handle);
    if (it == textures_.end() || handle == 1) return false;  // 1 is the built-in white

    TextureRecord& rec = it->second;
    if (desc.width != rec.desc.width || desc.height != rec.desc.height ||
        desc.format != rec.desc.format) {
        // A size or format change is a new texture; glTexImage2D re-specifies
        // the storage on the same name, so every binding stays valid.
        return CreateTextureInternal(desc, pixels, rec);
    }
    return UploadTextureData(desc, pixels, rec);
}

void GLBackend::DestroyTexture(u32 handle) {
    if (handle == 0 || handle == 1) return;  // the white 1x1 is not ours to free here
    auto it = textures_.find(handle);
    if (it == textures_.end()) return;
    if (it->second.name != 0) gl::Get().DeleteTextures(1, &it->second.name);
    textures_.erase(it);
}

const GLBackend::TextureRecord* GLBackend::FindTexture(u32 handle) const {
    if (handle == 0) handle = 1;  // 0 and 1 both mean the built-in white 1x1
    auto it = textures_.find(handle);
    if (it != textures_.end()) return &it->second;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Target
// ---------------------------------------------------------------------------

void GLBackend::ApplyTargetSize(u32 width, u32 height) {
    targetWidth_ = width;
    targetHeight_ = height;
    targetSize_ = Vec2(static_cast<f32>(width), static_cast<f32>(height));
    // The cached scissor belongs to the old size.
    boundScissor_ = Rect(-1.0f, -1.0f, -1.0f, -1.0f);
}

bool GLBackend::QueryDefaultFramebufferSize(u32& width, u32& height) {
    const gl::Api& api = gl::Get();
    width = 0;
    height = 0;

    HWND window = static_cast<HWND>(windowHandle_);
    if (window != nullptr) {
        RECT rc{};
        if (GetClientRect(window, &rc) != FALSE) {
            const LONG w = rc.right - rc.left;
            const LONG h = rc.bottom - rc.top;
            if (w > 0 && h > 0) {
                width = static_cast<u32>(w);
                height = static_cast<u32>(h);
            }
        }
    }

    if ((width == 0 || height == 0) && api.loaded) {
        // No window to ask. GL has no "default framebuffer size" query, but the
        // viewport the host last set is the best proxy available.
        GLint viewport[4] = {0, 0, 0, 0};
        api.GetIntegerv(GL_VIEWPORT, viewport);
        if (viewport[2] > 0 && viewport[3] > 0) {
            width = static_cast<u32>(viewport[2]);
            height = static_cast<u32>(viewport[3]);
        }
    }
    return width > 0 && height > 0;
}

bool GLBackend::CreateDefaultFramebufferTarget(u32 width, u32 height) {
    const gl::Api& api = gl::Get();

    targetSource_ = TargetSource::OwnSwapchain;
    targetFramebuffer_ = 0;  // the host's window system owns the swap chain
    externalFramebuffer_ = 0;
    framebufferView_ = 0;

    // The window knows its own size best; fall back to whatever the host told
    // us in TargetDesc / DeviceDesc.
    u32 w = 0;
    u32 h = 0;
    if (QueryDefaultFramebufferSize(w, h)) {
        width = w;
        height = h;
    }
    if (width == 0 || height == 0) {
        lastError_ = "cannot size the default framebuffer: pass TargetDesc::width/height";
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return false;
    }

    ApplyTargetSize(width, height);
    hasTarget_ = true;

    // ArgonUI anti-aliases analytically in the shader, so a multisampled
    // framebuffer would filter every edge twice. Warn rather than fight it:
    // the window's framebuffer is not ours to change.
    GLint sampleBuffers = 0;
    GLint samples = 0;
    api.GetIntegerv(GL_SAMPLE_BUFFERS, &sampleBuffers);
    api.GetIntegerv(GL_SAMPLES, &samples);
    if (sampleBuffers > 0 && samples > 1) {
        ARGON_WARN(kTag, "the default framebuffer is %dx MSAA: ArgonUI's analytic AA will "
                         "soften edges slightly. Render into a single-sample FBO for "
                         "pixel-exact UI.", static_cast<int>(samples));
    }
    return true;
}

bool GLBackend::RebuildFramebufferView(u32 fallbackWidth, u32 fallbackHeight) {
    const gl::Api& api = gl::Get();
    if (externalFramebuffer_ == 0) return false;

    // The host's name never changes; ours may, when the host re-creates its
    // framebuffer or swaps the colour attachment underneath us.
    const gl::GLuint host = externalFramebuffer_;

    api.BindFramebuffer(GL_FRAMEBUFFER, host);

    GLint attachment = 0;
    GLint attachWidth = 0;
    GLint attachHeight = 0;
    api.GetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                             GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &attachment);
    api.GetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                             GL_FRAMEBUFFER_ATTACHMENT_WIDTH, &attachWidth);
    api.GetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                             GL_FRAMEBUFFER_ATTACHMENT_HEIGHT, &attachHeight);
    const gl::GLuint colorAttachment = static_cast<gl::GLuint>(attachment);
    if (colorAttachment == 0) (void)api.GetError();  // not a texture FBO

    u32 width = fallbackWidth;
    u32 height = fallbackHeight;
    if (attachWidth > 0 && attachHeight > 0) {
        width = static_cast<u32>(attachWidth);
        height = static_cast<u32>(attachHeight);
    }
    if (width == 0 || height == 0) return false;
    ApplyTargetSize(width, height);

    if (colorAttachment == 0) {
        // A renderbuffer FBO (or nothing readable at all). Drawing straight
        // into the host's framebuffer is fine, but we cannot wrap it.
        ARGON_INFO(kTag, "framebuffer %u has no colour texture; drawing into it directly",
                   static_cast<unsigned>(host));
        api.BindFramebuffer(GL_FRAMEBUFFER, 0);
        return true;
    }

    // Wrap the host's colour texture in an FBO we own. Two wins: we verify
    // completeness ourselves, and we keep one name to bind every frame no
    // matter what the host does to its own framebuffer objects.
    gl::GLuint view = 0;
    api.GenFramebuffers(1, &view);
    api.BindFramebuffer(GL_FRAMEBUFFER, view);
    api.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                             colorAttachment, 0);
    const gl::GLenum status = api.CheckFramebufferStatus(GL_FRAMEBUFFER);
    api.BindFramebuffer(GL_FRAMEBUFFER, 0);
    (void)api.GetError();

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        // Expected when the host's attachment is a multisample renderbuffer: a
        // texture view of it is not legal. Fall back rather than fail.
        api.DeleteFramebuffers(1, &view);
        ARGON_WARN(kTag, "could not wrap framebuffer %u (%s); drawing into it directly",
                   static_cast<unsigned>(host), FramebufferStatusName(status));
        return true;
    }

    if (framebufferView_ != 0) api.DeleteFramebuffers(1, &framebufferView_);
    framebufferView_ = view;
    targetFramebuffer_ = view;

    ARGON_INFO(kTag, "wrapped framebuffer %u as view %u (%ux%u)", static_cast<unsigned>(host),
               static_cast<unsigned>(view), targetWidth_, targetHeight_);
    return true;
}

bool GLBackend::AdoptTarget(const TargetDesc& desc) {
    if (!initialized_) {
        lastError_ = "AdoptTarget was called before Initialize()";
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return false;
    }

    // Switching targets is allowed: a host that changes render backends
    // mid-session should not have to tear the backend down first.
    DestroyTarget();

    // externalSwapchain carries a GLuint framebuffer name. 0 - which is also
    // what a null pointer becomes - is the default framebuffer, a perfectly
    // legal thing to be handed.
    const gl::GLuint fbo =
        static_cast<gl::GLuint>(reinterpret_cast<std::uintptr_t>(desc.externalSwapchain));

    targetSource_ = TargetSource::ExternalSwapchain;
    externalFramebuffer_ = fbo;
    targetFramebuffer_ = fbo;
    framebufferView_ = 0;

    if (fbo == 0) {
        u32 w = desc.width;
        u32 h = desc.height;
        u32 queryW = 0;
        u32 queryH = 0;
        if (QueryDefaultFramebufferSize(queryW, queryH)) {
            w = queryW;
            h = queryH;
        }
        ApplyTargetSize(w, h);
    } else if (!RebuildFramebufferView(desc.width, desc.height)) {
        lastError_ = "the adopted framebuffer reports no size and TargetDesc gave none";
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        DestroyTarget();
        return false;
    }

    if (targetWidth_ == 0 || targetHeight_ == 0) {
        lastError_ = "could not determine the size of the adopted framebuffer";
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        DestroyTarget();
        return false;
    }

    hasTarget_ = true;
    ARGON_INFO(kTag, "adopted framebuffer %u (%ux%u)", static_cast<unsigned>(targetFramebuffer_),
               targetWidth_, targetHeight_);
    return true;
}

bool GLBackend::CreateOffscreenTarget(u32 width, u32 height) {
    const gl::Api& api = gl::Get();
    if (width == 0 || height == 0) {
        lastError_ = "an offscreen target needs a non-zero size";
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return false;
    }

    // Colour attachment: a plain texture, so the host can sample the result
    // (menu -> composite pass) after we are done.
    if (offscreenTexture_ == 0) api.GenTextures(1, &offscreenTexture_);
    if (offscreenTexture_ == 0) { Fail("glGenTextures failed for the offscreen target"); return false; }
    api.BindTexture(GL_TEXTURE_2D, offscreenTexture_);
    api.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_LINEAR));
    api.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_LINEAR));
    api.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    api.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    api.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    api.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    api.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), static_cast<GLsizei>(width),
                   static_cast<GLsizei>(height), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    api.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    api.BindTexture(GL_TEXTURE_2D, 0);

    // Depth-stencil. ArgonUI never enables depth testing, so this attachment is
    // inert for our own draws; it exists so a host that samples the colour
    // texture into a 3D pass has a matching depth buffer to test against.
    if (offscreenDepth_ == 0) api.GenRenderbuffers(1, &offscreenDepth_);
    if (offscreenDepth_ == 0) { Fail("glGenRenderbuffers failed for the offscreen target"); return false; }
    api.BindRenderbuffer(GL_RENDERBUFFER, offscreenDepth_);
    api.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, static_cast<GLsizei>(width),
                            static_cast<GLsizei>(height));
    api.BindRenderbuffer(GL_RENDERBUFFER, 0);

    if (framebufferView_ == 0) api.GenFramebuffers(1, &framebufferView_);
    if (framebufferView_ == 0) { Fail("glGenFramebuffers failed for the offscreen target"); return false; }

    api.BindFramebuffer(GL_FRAMEBUFFER, framebufferView_);
    api.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                             offscreenTexture_, 0);
    api.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER,
                                offscreenDepth_);
    const gl::GLenum status = api.CheckFramebufferStatus(GL_FRAMEBUFFER);
    api.BindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        lastError_ = std::string("the offscreen framebuffer is ") +
                     FramebufferStatusName(status);
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return false;
    }

    targetSource_ = TargetSource::OffscreenTexture;
    // Deliberately single sampled: a multisample attachment would resolve on top
    // of the shader's analytic AA and every edge would be filtered twice. On
    // desktop GL GL_MULTISAMPLE cannot be disabled, so single-sample storage is
    // the only way to get it.
    targetFramebuffer_ = framebufferView_;
    ApplyTargetSize(width, height);
    hasTarget_ = true;
    ARGON_INFO(kTag, "offscreen target %u (%ux%u)", static_cast<unsigned>(framebufferView_),
               width, height);
    return true;
}

bool GLBackend::CreateTarget(const TargetDesc& desc) {
    DestroyTarget();
    if (!initialized_) {
        lastError_ = "CreateTarget called before Initialize()";
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return false;
    }

    u32 width = desc.width != 0 ? desc.width : deviceWidth_;
    u32 height = desc.height != 0 ? desc.height : deviceHeight_;

    bool ok = false;
    switch (desc.targetSource) {
    case TargetSource::ExternalSwapchain:
        ok = AdoptTarget(desc);
        break;
    case TargetSource::OffscreenTexture:
        ok = CreateOffscreenTarget(width, height);
        break;
    case TargetSource::OwnSwapchain:
    default:
        // GL has no swapchain of ours to make: the host's window system owns
        // the swap chain, so the target is the default framebuffer.
        ok = CreateDefaultFramebufferTarget(width, height);
        break;
    }

    // Never true for GL - there is no swapchain to own.
    info_.ownsSwapchain = false;
    return ok;
}

bool GLBackend::ResizeTarget(u32 width, u32 height) {
    if (!hasTarget_) return false;
    if (width == 0 || height == 0) return true;  // minimised
    if (width == targetWidth_ && height == targetHeight_) return true;

    switch (targetSource_) {
    case TargetSource::OffscreenTexture:
        // Our own storage: re-specify it at the new size.
        return CreateOffscreenTarget(width, height);

    case TargetSource::ExternalSwapchain:
        // The host already resized its attachment; rebuild the view over it.
        return RebuildFramebufferView(width, height);

    case TargetSource::OwnSwapchain:
    default:
        // The default framebuffer is resized by the window system. There is
        // nothing to recreate, only numbers to remember.
        ARGON_DEBUG(kTag, "target resized to %ux%u", width, height);
        ApplyTargetSize(width, height);
        return true;
    }
}

void GLBackend::DestroyTarget() {
    const gl::Api& api = gl::Get();
    if (api.loaded) {
        if (framebufferView_ != 0) { api.DeleteFramebuffers(1, &framebufferView_); }
        if (offscreenTexture_ != 0) { api.DeleteTextures(1, &offscreenTexture_); }
        if (offscreenDepth_ != 0) { api.DeleteRenderbuffers(1, &offscreenDepth_); }
        api.BindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    framebufferView_ = 0;
    offscreenTexture_ = 0;
    offscreenDepth_ = 0;
    externalFramebuffer_ = 0;
    targetFramebuffer_ = 0;
    hasTarget_ = false;
    ApplyTargetSize(0, 0);
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

bool GLBackend::BeginFrame(const FrameState& state) {
    if (frameBegun_) return false;                 // unbalanced Begin/End
    if (!initialized_) return false;
    if (!hasTarget_) return false;                // no framebuffer: nothing to draw into
    if (state.occluded) return false;             // minimised / hidden

    // The default framebuffer belongs to the window system and changes size
    // without telling anybody. GL hosts that forget to call ResizeTarget get a
    // stretched or invisible overlay; GetClientRect is cheap enough to ask
    // every frame.
    if (targetSource_ == TargetSource::OwnSwapchain) {
        u32 width = 0;
        u32 height = 0;
        if (QueryDefaultFramebufferSize(width, height) &&
            (width != targetWidth_ || height != targetHeight_)) {
            ARGON_DEBUG(kTag, "default framebuffer resized %ux%u -> %ux%u", targetWidth_,
                        targetHeight_, width, height);
            ApplyTargetSize(width, height);
        }
    }

    frameBegun_ = true;
    drawCallCount_ = 0;
    return true;
}

void GLBackend::SetBlend(BlendMode mode) {
    if (!blendDirty_ && boundBlend_ == mode) return;
    const gl::Api& api = gl::Get();

    // The fragment stage emits premultiplied alpha, so alpha blending is
    // (ONE, ONE_MINUS_SRC_ALPHA) - exactly the D3D11 blend state, including the
    // alpha channel, which glBlendFunc applies to RGB and A alike.
    if (mode == BlendMode::Additive) {
        api.BlendFunc(GL_ONE, GL_ONE);
    } else {
        api.BlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }
    boundBlend_ = mode;
    blendDirty_ = false;
}

void GLBackend::SetTexture(u32 handle) {
    if (!textureDirty_ && boundTexture_ == handle) return;
    const gl::Api& api = gl::Get();

    const TextureRecord* rec = FindTexture(handle);
    const gl::GLuint name = rec != nullptr ? rec->name : whiteTexture_.name;
    const bool linear = rec == nullptr || rec->desc.filterLinear;

    api.BindTexture(GL_TEXTURE_2D, name);
    api.BindSampler(0, linear ? linearSampler_ : nearestSampler_);
    boundTexture_ = handle;
    textureDirty_ = false;
}

bool GLBackend::SetScissor(const Rect& clip) {
    // Same clamp/floor/ceil the D3D11 backend uses, so both clip identically at
    // the pixel boundary. The Max(+1) keeps a sub-pixel sliver from vanishing.
    const f32 l = Max(0.0f, std::floor(clip.min.x));
    const f32 t = Max(0.0f, std::floor(clip.min.y));
    const f32 r = Min(static_cast<f32>(targetWidth_), std::ceil(clip.max.x));
    const f32 b = Min(static_cast<f32>(targetHeight_), std::ceil(clip.max.y));

    const f32 right = Max(l + 1.0f, r);
    const f32 bottom = Max(t + 1.0f, b);
    if (right <= l || bottom <= t) return false;

    const Rect want(l, t, right, bottom);
    if (want != boundScissor_) {
        gl::Get().Scissor(static_cast<GLint>(l), static_cast<GLint>(t),
                          static_cast<GLsizei>(right - l), static_cast<GLsizei>(bottom - t));
        boundScissor_ = want;
    }
    return true;
}

void GLBackend::Render(const DrawList& list, const FrameState&) {
    if (!frameBegun_ || !initialized_) return;
    const gl::Api& api = gl::Get();
    if (!api.loaded || targetWidth_ == 0 || targetHeight_ == 0) return;
    if (list.CmdCount() == 0) return;

    const size_t vertexBytes = list.VertexCount() * sizeof(Vertex);
    const size_t indexBytes = list.IndexCount() * sizeof(u32);
    if (!EnsureVertexCapacity(list.VertexCount(), list.IndexCount())) return;

    // -- Upload ------------------------------------------------------------
    // Both buffers are bound inside our VAO, so nothing here touches the
    // default VAO's state. GL_STREAM_DRAW + a sub-data copy is the standard
    // "this data is used exactly once" pattern and keeps us off a sync point.
    api.BindVertexArray(vertexArray_);
    api.BindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
    api.BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertexBytes), nullptr,
                   GL_STREAM_DRAW);
    if (vertexBytes > 0) {
        api.BufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(vertexBytes),
                          list.Vertices());
    }
    api.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer_);
    api.BufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indexBytes), nullptr,
                   GL_STREAM_DRAW);
    if (indexBytes > 0) {
        api.BufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(indexBytes),
                          list.Indices());
    }

    // -- Framebuffer and fixed state --------------------------------------
    api.BindFramebuffer(GL_FRAMEBUFFER, targetFramebuffer_);
    api.Viewport(0, 0, static_cast<GLsizei>(targetWidth_), static_cast<GLsizei>(targetHeight_));

    api.Enable(GL_SCISSOR_TEST);   // clipping is per batch, resolved on the CPU
    api.Enable(GL_BLEND);
    api.Disable(GL_DEPTH_TEST);    // an unlit 2D overlay has no depth
    api.Disable(GL_CULL_FACE);     // the SDF is winding independent
    api.Disable(GL_STENCIL_TEST);
    api.Disable(GL_DITHER);
    // ArgonUI composites in whatever colour space the widget colours are in,
    // exactly like the D3D11 backend. Leave the sRGB encode off or every colour
    // on an sRGB default framebuffer is gamma'd twice.
    api.Disable(GL_FRAMEBUFFER_SRGB);

    api.UseProgram(program_);
    {
        // The shader turns pixel coordinates into clip space with this; xy is
        // the pixel size and zw its reciprocal, matching the D3D11 constant
        // buffer byte for byte.
        const f32 width = static_cast<f32>(Max(1u, targetWidth_));
        const f32 height = static_cast<f32>(Max(1u, targetHeight_));
        const f32 viewport[4] = {width, height, 1.0f / width, 1.0f / height};
        if (uniformViewport_ >= 0) api.Uniform4fv(uniformViewport_, 1, viewport);
    }
    if (uniformTexture_ >= 0) api.Uniform1i(uniformTexture_, 0);
    api.ActiveTexture(GL_TEXTURE0);

    // A host may have changed our pipeline between frames, so re-assert the
    // per-batch state on the first command instead of trusting the caches.
    blendDirty_ = true;
    textureDirty_ = true;

    // -- Draw every batch, switching only what actually differs -------------
    const DrawCmd* cmds = list.Cmds();
    const u32 cmdCount = list.CmdCount();

    for (u32 i = 0; i < cmdCount; ++i) {
        const DrawCmd& cmd = cmds[i];
        if (cmd.indexCount == 0) continue;
        if (!SetScissor(cmd.clip)) continue;  // entirely off target

        SetBlend(cmd.blend);
        SetTexture(cmd.textureId);

        // GL_UNSIGNED_INT indices are core since 3.0, so there is no reason to
        // squeeze them into 16 bits.
        api.DrawElements(GL_TRIANGLES, static_cast<GLsizei>(cmd.indexCount), GL_UNSIGNED_INT,
                         reinterpret_cast<const void*>(static_cast<size_t>(cmd.indexOffset) *
                                                        sizeof(u32)));
        ++drawCallCount_;
    }
}

bool GLBackend::Present() {
    // OpenGL has no swapchain and no Present - the host owns SwapBuffers, and
    // an overlay must never present somebody else's window from inside a hook.
    // Returning false is the contract-correct answer for every target mode.
    return false;
}

void GLBackend::EndFrame() {
    if (!frameBegun_) return;
    const gl::Api& api = gl::Get();

    if (api.loaded && hasTarget_) {
        api.Scissor(0, 0, static_cast<GLsizei>(targetWidth_),
                    static_cast<GLsizei>(targetHeight_));
        api.Disable(GL_SCISSOR_TEST);
    }
    if (api.loaded) {
        // Leave the host's context how we would like to find it: nothing bound
        // to a VAO, no texture, no framebuffer of ours.
        api.BindVertexArray(0);
        api.BindBuffer(GL_ARRAY_BUFFER, 0);
        api.BindTexture(GL_TEXTURE_2D, 0);  // unit 0; still the active unit
        api.BindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    blendDirty_ = true;
    textureDirty_ = true;
    boundScissor_ = Rect(-1.0f, -1.0f, -1.0f, -1.0f);
    frameBegun_ = false;
}

const char* GLBackend::GlVersion() const {
    return gl::Get().loaded ? gl::Get().version : "";
}

bool GLBackend::ReadTargetPixels(u32 x, u32 y, u32 width, u32 height, u32 channels, void* dst,
                                 bool flipY) {
    const gl::Api& api = gl::Get();
    if (!api.loaded || dst == nullptr || width == 0 || height == 0) return false;
    if (channels != 3 && channels != 4) return false;

    const GLenum format = (channels == 3) ? GL_RGB : GL_RGBA;
    api.ReadPixels(static_cast<GLint>(x), static_cast<GLint>(y), static_cast<GLsizei>(width),
                   static_cast<GLsizei>(height), format, GL_UNSIGNED_BYTE, dst);
    if (api.GetError() != GL_NO_ERROR) return false;
    if (!flipY) return true;

    // GL hands back bottom-up rows; everything above ArgonUI thinks top-down.
    // Swap them in place through a fixed stack buffer - no allocation, no
    // exception.
    const size_t stride = static_cast<size_t>(width) * channels;
    u8* rows = static_cast<u8*>(dst);
    u8 scratch[64];
    for (u32 row = 0; row < height / 2; ++row) {
        u8* top = rows + static_cast<size_t>(row) * stride;
        u8* bottom = rows + static_cast<size_t>(height - 1 - row) * stride;
        for (size_t offset = 0; offset < stride; offset += sizeof(scratch)) {
            const size_t n = Min(sizeof(scratch), stride - offset);
            std::memcpy(scratch, top + offset, n);
            std::memcpy(top + offset, bottom + offset, n);
            std::memcpy(bottom + offset, scratch, n);
        }
    }
    return true;
}

// ---------------------------------------------------------------------------

IArgonBackend* CreateOpenGLBackend() { return new GLBackend(); }

} // namespace argon::ui