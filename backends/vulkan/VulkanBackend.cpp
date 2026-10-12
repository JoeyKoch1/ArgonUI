// ArgonUI - backends/vulkan/VulkanBackend.cpp
//
// The Vulkan 1.0 backend. Read the .h first; this file is organised as
//
//     1. WSI platform selection (must happen before vulkan.h is parsed)
//     2. the GLSL the precompiled SPIR-V is built from
//     3. small helpers
//     4. the backend, in the same order as the D3D11 backend: device, target,
//        textures, frame, shutdown.
//
// No exceptions, no shader compiler, no third-party dependency beyond the
// Vulkan headers. Vulkan 1.0 core symbols come from vulkan-1.lib; every
// KHR_swapchain / surface / debug-utils entry point is resolved through
// vkGetInstanceProcAddr because those live in the loader, not in the library.

// ---------------------------------------------------------------------------
// 1. WSI platform
//
// vulkan.h only declares Vk*SurfaceCreateInfoKHR for the platforms that are
// enabled *before* it is parsed, so the choice has to be made here. The values
// below are the ones vulkan_core.h assigns to its own VK_USE_PLATFORM_*
// constants; spelling them out identically keeps including vulkan.h afterwards
// a no-op redefinition rather than a conflicting one.
//
// ARGON_VK_SURFACE_EXT overrides the extension name (e.g.
// -DARGON_VK_SURFACE_EXT=VK_KHR_xcb_surface). It must name a platform whose
// VK_USE_PLATFORM_* macro is defined above - that macro is what actually
// selects the struct and the creation entry point used below.
// ---------------------------------------------------------------------------

#if defined(_WIN32)
#  if !defined(VK_USE_PLATFORM_WIN32_KHR)
#    define VK_USE_PLATFORM_WIN32_KHR 1
#  endif
#elif defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#  if !defined(VK_USE_PLATFORM_XLIB_KHR)
#    define VK_USE_PLATFORM_XLIB_KHR 2
#  endif
#endif

#include "VulkanBackend.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "argonui/Core/Log.h"

#if defined(_WIN32)
#  include <windows.h>
#endif

#if !defined(ARGON_VK_SURFACE_EXT)
#  if defined(VK_USE_PLATFORM_WIN32_KHR)
#    define ARGON_VK_SURFACE_EXT VK_KHR_WIN32_SURFACE_EXTENSION_NAME
#  elif defined(VK_USE_PLATFORM_XLIB_KHR)
#    define ARGON_VK_SURFACE_EXT VK_KHR_XLIB_SURFACE_EXTENSION_NAME
#  elif defined(VK_USE_PLATFORM_XCB_KHR)
#    define ARGON_VK_SURFACE_EXT VK_KHR_XCB_SURFACE_EXTENSION_NAME
#  else
#    error "Vulkan backend: no WSI platform. Define VK_USE_PLATFORM_WIN32_KHR, \
VK_USE_PLATFORM_XLIB_KHR or VK_USE_PLATFORM_XCB_KHR."
#  endif
#endif

namespace argon::ui {

namespace {

constexpr const char* kTag = "Vulkan";

// ---------------------------------------------------------------------------
// 2. The GLSL
//
// These are the sources the shipped SPIR-V is compiled from, kept here so the
// shader can be reviewed next to the code that binds it. They are a
// line-by-line port of shaders/argonui.hlsl; if one changes, all of them
// change, or the backends will visually disagree.
//
// Compile with:
//
//   glslc -O -fshader-stage=vertex argonui.vert.glsl -o argonui.vert.spv
//   glslc -O -fshader-stage=fragment argonui.frag.glsl -o argonui.frag.spv
//
// (extract the two blocks below verbatim into .glsl files; they are identical
// to what ships in <shader dir>/argonui.vert.spv and argonui.frag.spv.)
//
// Blend state is PREMULTIPLIED ALPHA: (ONE, ONE_MINUS_SRC_ALPHA), so the
// fragment shader returns rgb already multiplied by a.
// ---------------------------------------------------------------------------

constexpr const char* kArgonVertGlsl = R"GLSL(#version 450

// Attribute layout mirrors argon::ui::Vertex (64 bytes) exactly:
//   0 inPos    vec2  @0   (x, y)
//   1 inUv     vec2  @8
//   2 inHalf   vec2  @16  (hx, hy)
//   3 inRadii  vec4  @24  (rtl, rtr, rbr, rbl)
//   4 inEdge   vec2  @40  (borderWidth, glowWidth)
//   5 inFill   uint  @48 | 6 inBorder @52 | 7 inGlow @56 | 8 inFlags @60
// See backends/d3d11/D3D11Backend.cpp for the equivalent DXGI input description.
layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec2 inHalf;
layout(location = 3) in vec4 inRadii;
layout(location = 4) in vec2 inEdge;
layout(location = 5) in uint inFill;
layout(location = 6) in uint inBorder;
layout(location = 7) in uint inGlow;
layout(location = 8) in uint inFlags;

layout(location = 0) out vec2 vsUv;
layout(location = 1) out vec2 vsHalf;
layout(location = 2) out vec4 vsRadii;
layout(location = 3) out vec2 vsEdge;
// HLSL marks these `nointerpolation`. GLSL requires the fragment side of an
// integer varying to be `flat`, and `flat` is exactly equivalent here: all
// four words are constant across a quad (they are per-quad, not per-corner),
// and Vulkan's provoking vertex is the FIRST vertex of the primitive, which
// is the quad's top-left corner and carries the same value as the other three.
// So interpolating them would be a no-op too - but `flat` says that, costs
// nothing, and is the only spelling glslc accepts.
layout(location = 4) flat out uint vsFill;
layout(location = 5) flat out uint vsBorder;
layout(location = 6) flat out uint vsGlow;
layout(location = 7) flat out uint vsFlags;

// 16 bytes: xy = pixel size, zw = 1 / pixel size.
layout(set = 0, binding = 0, std140) uniform ViewportCB {
    vec4 viewport;
} gViewport;

void main() {
    // Pixel space -> clip space. Y is flipped because Vulkan's framebuffer
    // origin is top-left, exactly like the D3D11 backend's viewport.
    vec2 ndc = vec2(inPos.x * gViewport.viewport.z * 2.0 - 1.0,
                    1.0 - inPos.y * gViewport.viewport.w * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);

    vsUv     = inUv;
    vsHalf   = inHalf;
    vsRadii  = inRadii;
    vsEdge   = inEdge;
    vsFill   = inFill;
    vsBorder = inBorder;
    vsGlow   = inGlow;
    vsFlags  = inFlags;
}
)GLSL";

constexpr const char* kArgonFragGlsl = R"GLSL(#version 450

layout(location = 0) in vec2 vsUv;
layout(location = 1) in vec2 vsHalf;
layout(location = 2) in vec4 vsRadii;
layout(location = 3) in vec2 vsEdge;
layout(location = 4) flat in uint vsFill;
layout(location = 5) flat in uint vsBorder;
layout(location = 6) flat in uint vsGlow;
layout(location = 7) flat in uint vsFlags;

layout(location = 0) out vec4 outColor;

// HLSL splits this into Texture2D + SamplerState; Vulkan's combined image
// sampler (set 0, binding 1) is the same thing in one binding. The shader
// asks for straight (non-premultiplied) texels, which is the format the backend
// uploads - see TextureDesc.
layout(set = 0, binding = 1) uniform sampler2D gTexture;

const uint ARGON_VF_GRADIENT_V      = 1u;
const uint ARGON_VF_GRADIENT_H      = 2u;
const uint ARGON_VF_GRADIENT_RADIAL = 4u;
const uint ARGON_VF_INSET_SHADOW    = 8u;
const uint ARGON_VF_TEX_COVERAGE    = 16u;

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
    const float hx = vsHalf.x;
    const float hy = vsHalf.y;
    const float borderWidth = vsEdge.x;
    const float glowWidth   = vsEdge.y;

    const vec2 expanded = vec2(hx, hy) + glowWidth;

    // Position relative to the core box centre, in pixels.
    const vec2 p = (vsUv * 2.0 - 1.0) * expanded;

    // Screen Y grows downward, so p.y > 0 is the bottom half.
    const float radius = (p.x > 0.0)
        ? ((p.y > 0.0) ? vsRadii.z : vsRadii.y)   // br : tr
        : ((p.y > 0.0) ? vsRadii.w : vsRadii.x);  // bl : tl

    const float d = SdfRoundedBox(p, vec2(hx, hy), radius);

    // Analytic anti-aliasing: one device pixel wide, whatever the scale.
    const float aa = max(fwidth(d), 1e-4) * 0.5;

    const float fillA   = 1.0 - smoothstep(-aa, aa, d + borderWidth);
    const float borderA = 1.0 - smoothstep(-aa, aa, abs(d + borderWidth * 0.5) - borderWidth * 0.5);
    const float shapeA  = max(fillA, borderA);

    const vec4 fillC   = UnpackRGBA(vsFill);
    const vec4 borderC = UnpackRGBA(vsBorder);
    const vec4 glowC   = UnpackRGBA(vsGlow);

    // --- colour ---------------------------------------------------------
    vec3 shapeRGB = (fillC.rgb * fillA + borderC.rgb * borderA) / max(shapeA, 1e-4);

    const uint gflags = vsFlags & 7u;
    if (gflags != 0u) {
        float t;
        if ((vsFlags & ARGON_VF_GRADIENT_V) != 0u)      t = vsUv.y;
        else if ((vsFlags & ARGON_VF_GRADIENT_H) != 0u) t = vsUv.x;
        else                                             t = clamp(length(p / max(expanded, 1e-4)), 0.0, 1.0);
        // Gradient overrides the border mix; the border colour is the far stop.
        shapeRGB = mix(fillC.rgb, borderC.rgb, t);
    }

    // --- texture --------------------------------------------------------
    const bool coverageOnly = (vsFlags & ARGON_VF_TEX_COVERAGE) != 0u;
    const vec4 texel = texture(gTexture, vsUv);
    const float coverage = coverageOnly ? texel.r : texel.a;
    const vec3 texRGB  = coverageOnly ? vec3(1.0, 1.0, 1.0) : texel.rgb;

    const float alpha = shapeA * fillC.a * coverage;
    vec3 rgbPremult = shapeRGB * texRGB * alpha;

    // --- glow -------------------------------------------------------------
    // Box-space falloff so the halo reaches zero exactly at the expanded quad
    // edge and is never clipped at the corners (a euclidean falloff is).
    float alphaOut = alpha;
    if (glowWidth > 0.0) {
        const float gx = 1.0 - clamp(abs(p.x) / max(expanded.x, 1e-4), 0.0, 1.0);
        const float gy = 1.0 - clamp(abs(p.y) / max(expanded.y, 1e-4), 0.0, 1.0);
        const float glowShape = pow(clamp(min(gx, gy), 0.0, 1.0), 2.0) * step(0.0, d);
        const float glowA = glowShape * glowC.a;
        // Composite the halo *behind* the shape: premultiplied "over".
        rgbPremult += glowC.rgb * glowA * (1.0 - alphaOut);
        alphaOut    += glowA * (1.0 - alphaOut);
    }

    // Fully outside the shape and its halo: nothing to blend.
    if (alphaOut <= 0.0005) discard;

    // Premultiplied output - the blend state is (ONE, ONE_MINUS_SRC_ALPHA).
    outColor = vec4(rgbPremult, alphaOut);
}
)GLSL";

// ---------------------------------------------------------------------------
// 3. Helpers
// ---------------------------------------------------------------------------

/// Generous starting sizes for the mapped vertex/index pair; both double on
/// demand, so these only decide how often that happens.
constexpr size_t kVertexBufferBytes = 4u * 1024u * 1024u;
constexpr size_t kIndexBufferBytes = 3u * 1024u * 1024u;

/// One descriptor pool for the whole backend: a set per swapchain image, the
/// white 1x1 set, and one set per user texture. 1024 is far beyond any real
/// menu and costs nothing to reserve.
constexpr u32 kMaxDescriptorSets = 1024;

/// Acquiring can only block when the host is not presenting our images (an
/// adopted swapchain whose owner never calls Present, for example). A second
/// is far longer than any healthy frame; past it we drop the frame instead of
/// hanging the host's render thread.
constexpr u64 kAcquireTimeoutNs = 1000000000ull;

/// SPIR-V magic number, little-endian.
constexpr u32 kSpirvMagic = 0x07230203u;

const char* ResultName(VkResult r) {
    switch (r) {
    case VK_SUCCESS:                        return "VK_SUCCESS";
    case VK_NOT_READY:                      return "VK_NOT_READY";
    case VK_TIMEOUT:                        return "VK_TIMEOUT";
    case VK_EVENT_SET:                      return "VK_EVENT_SET";
    case VK_EVENT_RESET:                    return "VK_EVENT_RESET";
    case VK_INCOMPLETE:                     return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY:       return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:     return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:    return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:              return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED:        return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT:        return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:    return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:      return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:      return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS:         return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED:     return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_FRAGMENTED_POOL:          return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_OUT_OF_POOL_MEMORY:       return "VK_ERROR_OUT_OF_POOL_MEMORY";
    case VK_ERROR_INVALID_EXTERNAL_HANDLE:  return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
    case VK_ERROR_SURFACE_LOST_KHR:         return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_SUBOPTIMAL_KHR:                 return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR:          return "VK_ERROR_OUT_OF_DATE_KHR";
    default:                                return "unknown VkResult";
    }
}

const char* ToString(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R8_UNORM:          return "VK_FORMAT_R8_UNORM";
    case VK_FORMAT_R8G8B8A8_UNORM:    return "VK_FORMAT_R8G8B8A8_UNORM";
    case VK_FORMAT_R8G8B8A8_SRGB:     return "VK_FORMAT_R8G8B8A8_SRGB";
    case VK_FORMAT_B8G8R8A8_UNORM:    return "VK_FORMAT_B8G8R8A8_UNORM";
    case VK_FORMAT_B8G8R8A8_SRGB:     return "VK_FORMAT_B8G8R8A8_SRGB";
    default:                         return "an unsupported format";
    }
}

bool HasName(const VkExtensionProperties* props, u32 count, const char* name) {
    if (props == nullptr || name == nullptr) return false;
    for (u32 i = 0; i < count; ++i) {
        if (std::strcmp(props[i].extensionName, name) == 0) return true;
    }
    return false;
}

bool HasLayer(const VkLayerProperties* props, u32 count, const char* name) {
    if (props == nullptr || name == nullptr) return false;
    for (u32 i = 0; i < count; ++i) {
        if (std::strcmp(props[i].layerName, name) == 0) return true;
    }
    return false;
}

bool ReadFileBytes(const char* path, std::vector<u8>& out) {
    if (path == nullptr || path[0] == '\0') return false;
    std::FILE* f = std::fopen(path, "rb");
    if (f == nullptr) return false;
    if (std::fseek(f, 0, SEEK_END) != 0) { std::fclose(f); return false; }
    const long size = std::ftell(f);
    if (size <= 0) { std::fclose(f); return false; }
    std::rewind(f);
    out.resize(static_cast<size_t>(size));
    const size_t read = std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    if (read != out.size()) { out.clear(); return false; }
    return true;
}

std::string JoinPath(const std::string& dir, const char* leaf) {
    if (dir.empty()) return std::string(leaf);
    char last = dir[dir.size() - 1];
    if (last == '/' || last == '\\') return dir + leaf;
    return dir + "/" + leaf;
}

/// Directory of the running executable, "" when it cannot be determined.
std::string ExecutableDirectory() {
#if defined(_WIN32)
    wchar_t wbuf[4096] = {};
    const DWORD n = GetModuleFileNameW(nullptr, wbuf, static_cast<DWORD>(sizeof(wbuf) / sizeof(wbuf[0])));
    if (n == 0 || n >= sizeof(wbuf) / sizeof(wbuf[0])) return std::string();
    char nbuf[4096] = {};
    const int m = WideCharToMultiByte(CP_UTF8, 0, wbuf, static_cast<int>(n), nbuf,
                                      static_cast<int>(sizeof(nbuf) - 1), nullptr, nullptr);
    if (m <= 0) return std::string();
    nbuf[m] = '\0';
    const std::string path(nbuf);
#elif defined(__linux__) || defined(__APPLE__)
    char buf[4096] = {};
    const long n = static_cast<long>(readlink("/proc/self/exe", buf, sizeof(buf) - 1));
    if (n <= 0) return std::string();
    buf[n] = '\0';
    const std::string path(buf);
#else
    return std::string();
#endif
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

VkFormat ToVkFormat(const TextureDesc& desc) {
    switch (desc.format) {
    case TextureFormat::R8:
        return VK_FORMAT_R8_UNORM;
    case TextureFormat::BGRA8:
        return desc.srgb ? VK_FORMAT_B8G8R8A8_SRGB : VK_FORMAT_B8G8R8A8_UNORM;
    case TextureFormat::RGBA8:
    default:
        return desc.srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    }
}

u32 BytesPerTexel(TextureFormat f) { return f == TextureFormat::R8 ? 1u : 4u; }

VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data,
                                             void*) {
    const char* label = "Vulkan";
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) {
        label = "Vulkan(error)";
    }
    if (data != nullptr && data->pMessage != nullptr) {
        ARGON_ERROR(label, "%s", data->pMessage);
    }
    return VK_FALSE;   // never abort the host: an overlay must not take the game down
}

/// First queue family that can do graphics (and present, when asked).
i32 FindGraphicsFamily(VkPhysicalDevice pd, VkSurfaceKHR surface, bool requirePresent,
                       const std::function<PFN_vkVoidFunction(VkInstance, const char*)>& loader) {
    u32 count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &count, nullptr);
    if (count == 0) return -1;
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &count, families.data());

    const PFN_vkGetPhysicalDeviceSurfaceSupportKHR support =
        loader ? loader(VK_NULL_HANDLE, "vkGetPhysicalDeviceSurfaceSupportKHR") : nullptr;
    if (support == nullptr) support = loader ? loader(pd, "vkGetPhysicalDeviceSurfaceSupportKHR") : nullptr;

    for (u32 i = 0; i < count; ++i) {
        if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) continue;
        if (requirePresent && surface != VK_NULL_HANDLE && support != nullptr) {
            VkBool32 canPresent = VK_FALSE;
            if (support(pd, i, surface, &canPresent) != VK_SUCCESS || canPresent == VK_FALSE) continue;
        }
        return static_cast<i32>(i);
    }
    return -1;
}

} // namespace

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

VulkanBackend::VulkanBackend() = default;

VulkanBackend::~VulkanBackend() { Shutdown(); }

const char* VulkanBackend::VertexGlsl() { return kArgonVertGlsl; }

const char* VulkanBackend::FragmentGlsl() { return kArgonFragGlsl; }

// ---------------------------------------------------------------------------
// Shader hooks
// ---------------------------------------------------------------------------

void VulkanBackend::SetShaderPaths(const char* vertexSpv, const char* fragmentSpv) {
    shaderVertPath_ = (vertexSpv != nullptr) ? vertexSpv : "";
    shaderFragPath_ = (fragmentSpv != nullptr) ? fragmentSpv : "";
}

void VulkanBackend::SetShaderSpv(const void* vertex, size_t vertexSize,
                                 const void* fragment, size_t fragmentSize) {
    shaderVertSpv_.clear();
    shaderFragSpv_.clear();
    if (vertex != nullptr && vertexSize > 0) {
        const u8* bytes = static_cast<const u8*>(vertex);
        shaderVertSpv_.assign(bytes, bytes + vertexSize);
    }
    if (fragment != nullptr && fragmentSize > 0) {
        const u8* bytes = static_cast<const u8*>(fragment);
        shaderFragSpv_.assign(bytes, bytes + fragmentSize);
    }
}

// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------

void VulkanBackend::Fail(const char* what, VkResult result) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s failed: %s (%d)", what, ResultName(result),
                  static_cast<int>(result));
    lastError_ = buf;
    ARGON_ERROR(kTag, "%s", buf);
}

// ---------------------------------------------------------------------------
// Instance
// ---------------------------------------------------------------------------

bool VulkanBackend::CreateInstance(bool wantValidation) {
    u32 extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> available(extensionCount);
    if (extensionCount > 0) {
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, available.data());
    }

    std::vector<const char*> extensions;
    extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    extensions.push_back(ARGON_VK_SURFACE_EXT);
    const bool wantDebug = wantValidation &&
                           HasName(available.data(), extensionCount, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (wantDebug) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    // Optional: only used to read the driver version string out of
    // VkPhysicalDeviceDriverProperties. Everything else is plain Vulkan 1.0.
    const bool wantProps2 =
        HasName(available.data(), extensionCount, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    if (wantProps2) {
        extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    }

    std::vector<const char*> layers;
    u32 layerCount = 0;
    if (wantValidation) {
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> availableLayers(layerCount);
        if (layerCount > 0) {
            vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
        }
        if (HasLayer(availableLayers.data(), layerCount, "VK_LAYER_KHRONOS_validation")) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
        } else {
            ARGON_WARN(kTag, "validation requested but VK_LAYER_KHRONOS_validation is not installed");
        }
        if (!wantDebug) {
            ARGON_WARN(kTag, "validation requested but VK_EXT_debug_utils is unavailable");
        }
    }

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "ArgonUI";
    app.applicationVersion = ARGONUI_VERSION_NUM;
    app.pEngineName = "ArgonUI";
    app.engineVersion = ARGONUI_VERSION_NUM;
    // 1.1 is requested because the validation/debug paths read driver
    // properties through it, but every feature the backend actually draws
    // with is Vulkan 1.0. Drivers that only speak 1.0 get a retry.
    app.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = static_cast<u32>(extensions.size());
    ici.ppEnabledExtensionNames = extensions.data();
    ici.enabledLayerCount = static_cast<u32>(layers.size());
    ici.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();

    VkResult r = vkCreateInstance(&ici, nullptr, &instance_);
    if (r == VK_ERROR_INCOMPATIBLE_DRIVER && app.apiVersion >= VK_API_VERSION_1_1) {
        ARGON_WARN(kTag, "Vulkan 1.1 driver not available; retrying as 1.0");
        app.apiVersion = VK_API_VERSION_1_0;
        r = vkCreateInstance(&ici, nullptr, &instance_);
    }
    if (r != VK_SUCCESS || instance_ == VK_NULL_HANDLE) {
        instance_ = VK_NULL_HANDLE;
        Fail("vkCreateInstance", r);
        return false;
    }

    ownsInstance_ = true;
    validationEnabled_ = wantDebug;

    pfnGetPhysicalDeviceProperties2 = nullptr;
    if (wantProps2) {
        pfnGetPhysicalDeviceProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
            vkGetInstanceProcAddr(instance_, "vkGetPhysicalDeviceProperties2KHR"));
        if (pfnGetPhysicalDeviceProperties2 == nullptr) {
            pfnGetPhysicalDeviceProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
                vkGetInstanceProcAddr(instance_, "vkGetPhysicalDeviceProperties2"));
        }
    }
    return true;
}

void VulkanBackend::CreateDebugMessenger() {
    if (!validationEnabled_) return;
    pfnCreateDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
    pfnDestroyDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
    if (pfnCreateDebugUtilsMessengerEXT == nullptr) {
        ARGON_WARN(kTag, "VK_EXT_debug_utils entry points missing; validation messages are dropped");
        return;
    }

    VkDebugUtilsMessengerCreateInfoEXT ci{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    ci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    ci.pfnUserCallback = DebugCallback;
    const VkResult r = pfnCreateDebugUtilsMessengerEXT(instance_, &ci, nullptr, &debugMessenger_);
    if (r != VK_SUCCESS) {
        debugMessenger_ = VK_NULL_HANDLE;
        ARGON_WARN(kTag, "debug messenger creation failed: %s", ResultName(r));
    }
}

bool VulkanBackend::LoadInstanceFunctions() {
    struct Entry {
        PFN_vkVoidFunction* slot;
        const char* name;
        bool required;
    };
    // PFN_vkVoidFunction* is the only common denominator between a dozen
    // signatures; the reinterpret_cast back is the usual Vulkan-loader idiom.
    const Entry entries[] = {
        {reinterpret_cast<PFN_vkVoidFunction*>(&pfnDestroySurfaceKHR), "vkDestroySurfaceKHR", false},
        {reinterpret_cast<PFN_vkVoidFunction*>(&pfnGetPhysicalDeviceSurfaceSupportKHR),
         "vkGetPhysicalDeviceSurfaceSupportKHR", true},
        {reinterpret_cast<PFN_vkVoidFunction*>(&pfnGetPhysicalDeviceSurfaceCapabilitiesKHR),
         "vkGetPhysicalDeviceSurfaceCapabilitiesKHR", true},
        {reinterpret_cast<PFN_vkVoidFunction*>(&pfnGetPhysicalDeviceSurfaceFormatsKHR),
         "vkGetPhysicalDeviceSurfaceFormatsKHR", true},
        {reinterpret_cast<PFN_vkVoidFunction*>(&pfnGetPhysicalDeviceSurfacePresentModesKHR),
         "vkGetPhysicalDeviceSurfacePresentModesKHR", true},
        {reinterpret_cast<PFN_vkVoidFunction*>(&pfnCreateSwapchainKHR), "vkCreateSwapchainKHR", true},
        {reinterpret_cast<PFN_vkVoidFunction*>(&pfnDestroySwapchainKHR), "vkDestroySwapchainKHR", true},
        {reinterpret_cast<PFN_vkVoidFunction*>(&pfnGetSwapchainImagesKHR), "vkGetSwapchainImagesKHR", true},
        {reinterpret_cast<PFN_vkVoidFunction*>(&pfnAcquireNextImageKHR), "vkAcquireNextImageKHR", true},
        {reinterpret_cast<PFN_vkVoidFunction*>(&pfnQueuePresentKHR), "vkQueuePresentKHR", true},
    };

    for (const Entry& e : entries) {
        *e.slot = vkGetInstanceProcAddr(instance_, e.name);
        if (*e.slot == nullptr && e.required) {
            lastError_ = std::string("the Vulkan instance does not export ") + e.name +
                         " - the host instance was probably created without VK_KHR_surface / VK_KHR_swapchain";
            ARGON_ERROR(kTag, "%s", lastError_.c_str());
            return false;
        }
    }
    return true;
}

bool VulkanBackend::PickPhysicalDevice(bool requireSurface) {
    const auto loader = [this](VkInstance inst, const char* name) -> PFN_vkVoidFunction {
        return vkGetInstanceProcAddr(inst, name);
    };

    // When the host gave us a VkDevice, use its driver properties to recognise
    // the adapter it belongs to - Vulkan has no reverse device -> physical
    // device lookup, and guessing wrong would pick the wrong queue family.
    VkPhysicalDeviceProperties deviceProps{};
    VkPhysicalDeviceProperties wantProps{};
    bool haveDeviceProps = false;
    if (device_ != VK_NULL_HANDLE) {
        vkGetDeviceProperties(device_, &wantProps);
        haveDeviceProps = true;
    }

    u32 count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    if (count == 0) {
        lastError_ = "no Vulkan physical devices (is a GPU driver installed?)";
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return false;
    }
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());

    VkPhysicalDevice best = VK_NULL_HANDLE;
    i32 bestFamily = -1;
    i32 bestScore = -1;

    for (VkPhysicalDevice pd : devices) {
        vkGetPhysicalDeviceProperties(pd, &deviceProps);
        const i32 family = FindGraphicsFamily(pd, surface_, requireSurface, loader);
        if (family < 0) continue;

        i32 score = 1;
        switch (deviceProps.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   score = 3; break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: score = 2; break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    score = 2; break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:           score = 0; break;
        default: break;
        }
        if (haveDeviceProps) {
            const bool sameDriver =
                deviceProps.vendorID == wantProps.vendorID &&
                deviceProps.deviceID == wantProps.deviceID &&
                (deviceProps.apiVersion < VK_API_VERSION_1_2 ||
                 deviceProps.driverID == wantProps.driverID);
            if (sameDriver) score += 4;   // the host's own adapter
        }

        if (score > bestScore) {
            bestScore = score;
            best = pd;
            bestFamily = family;
        }
    }

    if (best == VK_NULL_HANDLE) {
        lastError_ = "no Vulkan physical device with a usable graphics queue family";
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return false;
    }
    if (haveDeviceProps && bestScore < 4) {
        ARGON_WARN(kTag,
                   "could not match the external VkDevice to a physical device; "
                   "falling back to '%s'", deviceProps.deviceName);
    }

    physicalDevice_ = best;
    queueFamily_ = static_cast<u32>(bestFamily);
    vkGetPhysicalDeviceProperties(physicalDevice_, &deviceProperties_);
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties_);
    return true;
}

bool VulkanBackend::CreateDevice() {
    VkPhysicalDeviceFeatures features{};
    // Nothing here needs a feature beyond Vulkan 1.0, and asking for nothing
    // lets ArgonUI ride along on whatever device the game created.
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = queueFamily_;
    qci.queueCount = 1;
    const f32 priority = 1.0f;
    qci.pQueuePriorities = &priority;

    u32 extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> available(extensionCount);
    if (extensionCount > 0) {
        vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &extensionCount,
                                             available.data());
    }
    if (!HasName(available.data(), extensionCount, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
        ARGON_WARN(kTag, "%s is unavailable; swapchain presentation will not work",
                   VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }

    const char* extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = extensions;
    dci.pEnabledFeatures = &features;

    VkResult r = vkCreateDevice(physicalDevice_, &dci, nullptr, &device_);
    if (r != VK_SUCCESS || device_ == VK_NULL_HANDLE) {
        device_ = VK_NULL_HANDLE;
        Fail("vkCreateDevice", r);
        return false;
    }
    ownsDevice_ = true;
    vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
    return queue_ != VK_NULL_HANDLE;
}

bool VulkanBackend::CreateSurface() {
    if (surface_ != VK_NULL_HANDLE) return true;
    if (windowHandle_ == nullptr) {
        lastError_ = "CreateTarget needs DeviceDesc::windowHandle to create a surface";
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return false;
    }

    VkResult r = VK_ERROR_INITIALIZATION_FAILED;
#if defined(VK_USE_PLATFORM_WIN32_KHR)
    VkWin32SurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    sci.hinstance = GetModuleHandleW(nullptr);
    sci.hwnd = static_cast<HWND>(windowHandle_);
    auto create = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
        vkGetInstanceProcAddr(instance_, "vkCreateWin32SurfaceKHR"));
    if (create == nullptr) {
        lastError_ = "vkCreateWin32SurfaceKHR is unavailable; the adopted instance lacks "
                     VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return false;
    }
    r = create(instance_, &sci, nullptr, &surface_);
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
    // Xlib needs two handles and DeviceDesc only has one pointer, so on this
    // platform `windowHandle` must point at
    //     struct { void* display; u64 window; };
    // (any layout-compatible struct works; nothing is dereferenced past those
    // two members). A bare Display* cannot make a surface, so say so plainly.
    struct XlibTarget {
        void* display;
        u64 window;
    };
    const XlibTarget* target = static_cast<const XlibTarget*>(windowHandle_);
    if (target == nullptr || target->display == nullptr) {
        lastError_ = "CreateTarget needs DeviceDesc::windowHandle to point at "
                     "{void* display; u64 window;} for the Xlib WSI";
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return false;
    }

    VkXlibSurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR};
    sci.dpy = static_cast<Display*>(target->display);
    sci.window = static_cast<::Window>(target->window);
    auto create = reinterpret_cast<PFN_vkCreateXlibSurfaceKHR>(
        vkGetInstanceProcAddr(instance_, "vkCreateXlibSurfaceKHR"));
    if (create == nullptr) {
        lastError_ = "vkCreateXlibSurfaceKHR is unavailable; the adopted instance lacks "
                     VK_KHR_XLIB_SURFACE_EXTENSION_NAME;
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return false;
    }
    r = create(instance_, &sci, nullptr, &surface_);
#else
#  error "Vulkan backend: no WSI surface for this platform (see ARGON_VK_SURFACE_EXT)"
#endif

    if (r != VK_SUCCESS || surface_ == VK_NULL_HANDLE) {
        surface_ = VK_NULL_HANDLE;
        Fail("vkCreate*SurfaceKHR", r);
        return false;
    }
    ownsSurface_ = true;
    return true;
}

// ---------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------

bool VulkanBackend::Initialize(const DeviceDesc& desc) {
    Shutdown();   // re-Initialize must not leak the previous target

    info_ = BackendInfo{};
    info_.api = GraphicsApi::Vulkan;
    info_.shadingLanguage = "GLSL / SPIR-V";
    info_.ownsDevice = false;
    info_.ownsSwapchain = false;

    transparentBackground_ = desc.transparentBackground;
    windowHandle_ = desc.windowHandle;
    enableVsync_ = desc.enableVsync;
    validationEnabled_ = desc.enableValidation;
    if (desc.msaaSamples > 1) {
        // A single-sample render pass and framebuffer is all this backend
        // builds; MSAA would need a resolve attachment and would not match the
        // shader's one-sample coverage.
        ARGON_WARN(kTag, "MSAA is not implemented; falling back to 1 sample");
    }

    if (desc.deviceSource == DeviceSource::External) {
        if (desc.externalDevice == nullptr || desc.externalContext == nullptr) {
            lastError_ = "External device source requires externalDevice and externalContext";
            ARGON_ERROR(kTag, "%s", lastError_.c_str());
            return false;
        }
        device_ = static_cast<VkDevice>(desc.externalDevice);
        queue_ = static_cast<VkQueue>(desc.externalContext);
        instance_ = static_cast<VkInstance>(desc.externalInstance);
        surface_ = static_cast<VkSurfaceKHR>(desc.externalSurface);
        ownsDevice_ = false;
        ownsInstance_ = false;   // adopted
        ownsSurface_ = false;    // adopted
        info_.ownsDevice = false;
        ARGON_INFO(kTag, "adopted external VkDevice %p", desc.externalDevice);

        if (instance_ == VK_NULL_HANDLE) {
            // We still need an instance to enumerate physical devices with.
            if (!CreateInstance(desc.enableValidation)) return false;
            ownsInstance_ = true;
        } else {
            CreateDebugMessenger();
        }
        if (surface_ == VK_NULL_HANDLE && desc.windowHandle != nullptr) {
            if (!CreateSurface()) return false;
        }

        if (!PickPhysicalDevice(surface_ != VK_NULL_HANDLE)) return false;

        // Which queue family does the host's queue belong to? Vulkan has no
        // query for it; asking the driver for every family's queue 0 and
        // comparing handles is what every host integration does, because the
        // dispatcher hands back the same object for the same (device, family).
        if (!FindQueueFamilyForQueue()) return false;
    } else {
        if (!CreateInstance(desc.enableValidation)) return false;
        CreateDebugMessenger();
        if (desc.externalSurface != nullptr) {
            surface_ = static_cast<VkSurfaceKHR>(desc.externalSurface);
            ownsSurface_ = false;
        } else if (!CreateSurface()) {
            return false;
        }
        if (!LoadInstanceFunctions()) return false;
        if (!PickPhysicalDevice(true)) return false;
        if (!CreateDevice()) return false;
    }

    if (device_ == nullptr || queue_ == VK_NULL_HANDLE || physicalDevice_ == VK_NULL_HANDLE) {
        lastError_ = "device, queue or physical device is null after setup";
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return false;
    }
    if (pfnQueuePresentKHR == nullptr) {
        if (!LoadInstanceFunctions()) return false;
    }

    // Adapter name for Info(). Cheap, one-off.
    {
        std::snprintf(deviceName_, sizeof(deviceName_), "%s", deviceProperties_.deviceName);
        std::snprintf(driverVersion_, sizeof(driverVersion_), "api %u.%u.%u",
                      VK_API_VERSION_MAJOR(deviceProperties_.apiVersion),
                      VK_API_VERSION_MINOR(deviceProperties_.apiVersion),
                      VK_API_VERSION_PATCH(deviceProperties_.apiVersion));
        if (pfnGetPhysicalDeviceProperties2 != nullptr &&
            deviceProperties_.apiVersion >= VK_API_VERSION_1_2) {
            VkPhysicalDeviceDriverProperties driverProps{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
            VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
            props.pNext = &driverProps;
            pfnGetPhysicalDeviceProperties2(physicalDevice_, &props);
            const char* name = driverProps.driverName;
            if (name != nullptr && name[0] != '\0') {
                std::snprintf(driverVersion_, sizeof(driverVersion_), "%s %s", name,
                              driverProps.driverInfo);
            }
        }
        info_.deviceName = deviceName_;
        info_.driverVersion = driverVersion_;
        info_.maxTextureSize = deviceProperties_.limits.maxImageDimension2D;
    }

    if (!ResolveShaders()) return false;
    if (!CreateCommandPool()) return false;
    if (!CreateStaticResources()) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Shaders
// ---------------------------------------------------------------------------

bool VulkanBackend::ResolveShaders() {
    // An embedded blob always wins; it is the only option that does not touch
    // the filesystem at all.
    if (!shaderVertSpv_.empty() && !shaderFragSpv_.empty()) {
        ARGON_INFO(kTag, "using embedded SPIR-V (%zu + %zu bytes)", shaderVertSpv_.size(),
                   shaderFragSpv_.size());
        return true;
    }

    struct Candidate {
        std::string vert;
        std::string frag;
        const char* origin;
    };
    std::vector<Candidate> candidates;

    if (!shaderVertPath_.empty() || !shaderFragPath_.empty()) {
        candidates.push_back({shaderVertPath_.empty() ? std::string("argonui.vert.spv") : shaderVertPath_,
                              shaderFragPath_.empty() ? std::string("argonui.frag.spv") : shaderFragPath_,
                              "SetShaderPaths"});
    }
    if (const char* dir = std::getenv("ARGONUI_VULKAN_SHADER_DIR")) {
        candidates.push_back({JoinPath(dir, "argonui.vert.spv"),
                              JoinPath(dir, "argonui.frag.spv"),
                              "$ARGONUI_VULKAN_SHADER_DIR"});
    }
    const std::string exeDir = ExecutableDirectory();
    if (!exeDir.empty()) {
        candidates.push_back({JoinPath(JoinPath(exeDir, "shaders"), "argonui.vert.spv"),
                              JoinPath(JoinPath(exeDir, "shaders"), "argonui.frag.spv"),
                              "<executable>/shaders"});
    }
    candidates.push_back({"argonui.vert.spv", "argonui.frag.spv", "<working directory>"});

    std::string tried;
    for (const Candidate& c : candidates) {
        if (c.vert.empty() || c.frag.empty()) continue;
        std::vector<u8> vert;
        std::vector<u8> frag;
        const bool haveVert = ReadFileBytes(c.vert.c_str(), vert);
        const bool haveFrag = ReadFileBytes(c.frag.c_str(), frag);
        if (haveVert && haveFrag) {
            shaderVertSpv_ = vert;
            shaderFragSpv_ = frag;
            ARGON_INFO(kTag, "loaded SPIR-V from %s ('%s' + '%s')", c.origin, c.vert.c_str(),
                       c.frag.c_str());
            return true;
        }
        detail::Appendf(tried, "\n  %-24s %s + %s (%s%s%s)", c.origin, c.vert.c_str(), c.frag.c_str(),
                        haveVert ? "vertex ok" : "vertex missing",
                        haveFrag ? ", " : ", ", haveFrag ? "fragment ok" : "fragment missing");
    }

    lastError_ = "could not load ArgonUI's precompiled SPIR-V. Tried:" + tried +
                 "\nCall SetShaderSpv() to embed the modules, SetShaderPaths() to name them, "
                 "or set $ARGONUI_VULKAN_SHADER_DIR to the directory holding them.";
    ARGON_ERROR(kTag, "%s", lastError_.c_str());
    return false;
}

bool VulkanBackend::CreateShaderModule(const std::vector<u8>& spirv, VkShaderModule& out,
                                       const char* what) {
    if (spirv.empty() || (spirv.size() % 4u) != 0u) {
        lastError_ = std::string("ArgonUI ") + what + " SPIR-V is empty or not word aligned";
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return false;
    }
    u32 magic = 0;
    std::memcpy(&magic, spirv.data(), sizeof(magic));
    if (magic != kSpirvMagic) {
        lastError_.clear();
        detail::Appendf(lastError_,
                        "ArgonUI %s module is not SPIR-V (magic 0x%08X, expected 0x%08X); "
                        "it was probably handed a GLSL or HLSL source file",
                        what, magic, kSpirvMagic);
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return false;
    }

    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = spirv.size();
    ci.pCode = reinterpret_cast<const u32*>(spirv.data());

    VkResult r = vkCreateShaderModule(device_, &ci, nullptr, &out);
    if (r != VK_SUCCESS) {
        out = VK_NULL_HANDLE;
        Fail("vkCreateShaderModule", r);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Descriptor plumbing, sampler and the white 1x1
// ---------------------------------------------------------------------------

bool VulkanBackend::CreateCommandPool() {
    // One pool for every command buffer we ever allocate: the per-image ones
    // and the throwaway uploads. RESET_COMMAND_BUFFER is not set because each
    // buffer is re-recorded with vkResetCommandBuffer explicitly.
    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = 0;
    pci.queueFamilyIndex = queueFamily_;
    const VkResult r = vkCreateCommandPool(device_, &pci, nullptr, &commandPool_);
    if (r != VK_SUCCESS) {
        commandPool_ = VK_NULL_HANDLE;
        Fail("vkCreateCommandPool", r);
        return false;
    }
    return true;
}

bool VulkanBackend::CreateStaticResources() {
    // set 0, binding 0 -> uniform buffer (viewport), binding 1 -> sampler.
    const VkDescriptorSetLayoutBinding bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    };
    VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    lci.bindingCount = 2;
    lci.pBindings = bindings;
    VkResult r = vkCreateDescriptorSetLayout(device_, &lci, nullptr, &setLayout_);
    if (r != VK_SUCCESS) { Fail("vkCreateDescriptorSetLayout", r); return false; }

    const VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxDescriptorSets},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxDescriptorSets},
    };
    VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pci.maxSets = kMaxDescriptorSets;
    pci.poolSizeCount = 2;
    pci.pPoolSizes = poolSizes;
    r = vkCreateDescriptorPool(device_, &pci, nullptr, &descriptorPool_);
    if (r != VK_SUCCESS) { Fail("vkCreateDescriptorPool", r); return false; }

    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &setLayout_;
    r = vkCreatePipelineLayout(device_, &plci, nullptr, &pipelineLayout_);
    if (r != VK_SUCCESS) { Fail("vkCreatePipelineLayout", r); return false; }

    if (!CreateShaderModule(shaderVertSpv_, vsModule_, "vertex")) return false;
    if (!CreateShaderModule(shaderFragSpv_, fsModule_, "fragment")) return false;

    // One clamp-to-edge, mip-less, linear sampler for everything, which is
    // what the reference HLSL sampler state does. maxLod is pinned to 0 because
    // no texture here has mips, so the sampler never depends on mipmap
    // completeness, which keeps NPOT font atlases safe.
    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.anisotropyEnable = VK_FALSE;
    sci.maxAnisotropy = 1.0f;
    sci.compareEnable = VK_FALSE;
    sci.minLod = 0.0f;
    sci.maxLod = 0.0f;
    sci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    sci.unnormalizedCoordinates = VK_FALSE;
    r = vkCreateSampler(device_, &sci, nullptr, &sampler_);
    if (r != VK_SUCCESS) { Fail("vkCreateSampler", r); return false; }

    // Texture id 1 is the built-in white 1x1 every shape batch samples.
    TextureDesc white{};
    white.width = 1;
    white.height = 1;
    white.format = TextureFormat::RGBA8;
    const u32 opaqueWhite = 0xFFFFFFFFu;
    TextureRecord rec;
    if (!CreateTextureInternal(white, &opaqueWhite, rec)) {
        DestroyTextureRecord(rec);
        return false;
    }
    whiteView_ = rec.view;
    textures_.emplace(1u, rec);
    return true;
}

// ---------------------------------------------------------------------------
// Render pass and pipelines
// ---------------------------------------------------------------------------

bool VulkanBackend::CreateRenderPass(VkFormat format) {
    DestroyRenderPass();

    VkAttachmentDescription attachment{};
    attachment.format = format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    // Our own images start undefined and get cleared; an adopted swapchain is
    // LOADed so the frame the host just drew survives.
    attachment.loadOp = clearTarget_ ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    // Our own images are freshly undefined and get cleared. An adopted image is
    // LOADed from whatever layout it came back in - PRESENT_SRC_KHR, because
    // that is the layout the presentation engine leaves it in, and the host's
    // frame must survive.
    attachment.initialLayout = clearTarget_ ? VK_IMAGE_LAYOUT_UNDEFINED
                                            : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    // An offscreen target is never presented, so it stays colour-attachment
    // optimal for the host to read back.
    attachment.finalLayout = offscreenTarget_ ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                              : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = clearTarget_ ? 0u : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = 1;
    rpci.pAttachments = &attachment;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dependency;

    VkResult r = vkCreateRenderPass(device_, &rpci, nullptr, &renderPass_);
    if (r != VK_SUCCESS) { Fail("vkCreateRenderPass", r); return false; }
    return true;
}

void VulkanBackend::DestroyRenderPass() {
    if (device_ == VK_NULL_HANDLE) {
        renderPass_ = VK_NULL_HANDLE;
        return;
    }
    // A pipeline is only valid for the render pass it was built against, and
    // the load op / final layout can change even when the format does not
    // (our own chain vs. an adopted one), so the two always die together.
    DestroyPipelines();
    if (renderPass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device_, renderPass_, nullptr);
        renderPass_ = VK_NULL_HANDLE;
    }
}

bool VulkanBackend::EnsurePipelines(VkFormat format) {
    if (renderPass_ == VK_NULL_HANDLE && !CreateRenderPass(format)) return false;
    if (pipelineAlpha_ != VK_NULL_HANDLE && pipelineFormat_ == format) return true;
    DestroyPipelines();
    if (!CreatePipeline(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, pipelineAlpha_)) {
        return false;
    }
    if (!CreatePipeline(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, pipelineAdditive_)) {
        return false;
    }
    pipelineFormat_ = format;
    return true;
}

void VulkanBackend::DestroyPipelines() {
    if (device_ != VK_NULL_HANDLE) {
        if (pipelineAlpha_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, pipelineAlpha_, nullptr);
            pipelineAlpha_ = VK_NULL_HANDLE;
        }
        if (pipelineAdditive_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, pipelineAdditive_, nullptr);
            pipelineAdditive_ = VK_NULL_HANDLE;
        }
    }
    pipelineAlpha_ = VK_NULL_HANDLE;
    pipelineAdditive_ = VK_NULL_HANDLE;
    pipelineFormat_ = VK_FORMAT_UNDEFINED;
}

bool VulkanBackend::CreatePipeline(VkBlendFactor srcColor, VkBlendFactor dstColor, VkPipeline& out) {
    // Vertex input - mirrors argon::ui::Vertex (64 bytes) exactly.
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = static_cast<u32>(sizeof(Vertex));
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    // Offsets come straight from argon::ui::Vertex:
    //   0 x/y, 8 uv, 16 hx/hy, 24 radii, 40 border/glow, 48 fill, 52 border,
    //   56 glow, 60 flags.
    VkVertexInputAttributeDescription attributes[9]{};
    attributes[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, x)};
    attributes[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, u)};
    attributes[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, hx)};
    attributes[3] = {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, rtl)};
    attributes[4] = {4, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, borderWidth)};
    attributes[5] = {5, 0, VK_FORMAT_R32_UINT, offsetof(Vertex, fill)};
    attributes[6] = {6, 0, VK_FORMAT_R32_UINT, offsetof(Vertex, border)};
    attributes[7] = {7, 0, VK_FORMAT_R32_UINT, offsetof(Vertex, glowColor)};
    attributes[8] = {8, 0, VK_FORMAT_R32_UINT, offsetof(Vertex, flags)};

    VkPipelineVertexInputStateCreateInfo vertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 9;
    vertexInput.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo assembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    assembly.primitiveRestartEnable = VK_FALSE;

    // Dynamic viewport and scissor: clipping is per batch and set from the
    // DrawCmd clip rect, so the pipeline never has to be rebuilt for it.
    VkPipelineViewportStateCreateInfo viewport{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.depthClampEnable = VK_FALSE;
    raster.rasterizerDiscardEnable = VK_FALSE;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;   // the SDF is winding-independent
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.depthBiasEnable = VK_FALSE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisample.sampleShadingEnable = VK_FALSE;

    // No depth stencil at all: the attachment list has no depth buffer, so
    // pDepthStencilState must be null rather than an "everything disabled" one.

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.srcColorBlendFactor = srcColor;
    blendAttachment.dstColorBlendFactor = dstColor;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = srcColor;
    blendAttachment.dstAlphaBlendFactor = dstColor;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.logicOpEnable = VK_FALSE;
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;

    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vsModule_;
    stages[0].pName = "main";
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fsModule_;
    stages[1].pName = "main";

    VkGraphicsPipelineCreateInfo gpci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gpci.stageCount = 2;
    gpci.pStages = stages;
    gpci.pVertexInputState = &vertexInput;
    gpci.pInputAssemblyState = &assembly;
    gpci.pViewportState = &viewport;
    gpci.pRasterizationState = &raster;
    gpci.pMultisampleState = &multisample;
    gpci.pDepthStencilState = nullptr;
    gpci.pColorBlendState = &blend;
    gpci.pDynamicState = &dynamic;
    gpci.layout = pipelineLayout_;
    gpci.renderPass = renderPass_;     // must stay compatible with the framebuffer
    gpci.subpass = 0;

    VkResult r = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gpci, nullptr, &out);
    if (r != VK_SUCCESS) {
        out = VK_NULL_HANDLE;
        Fail("vkCreateGraphicsPipelines", r);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Queue family for an adopted queue
// ---------------------------------------------------------------------------

bool VulkanBackend::FindQueueFamilyForQueue() {
    u32 count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    if (count > 0) {
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &count, families.data());
    }

    for (u32 i = 0; i < count; ++i) {
        VkQueue candidate = VK_NULL_HANDLE;
        vkGetDeviceQueue(device_, i, 0, &candidate);
        if (candidate != queue_) continue;
        if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) {
            lastError_ = "the adopted VkQueue belongs to a family without VK_QUEUE_GRAPHICS_BIT";
            ARGON_ERROR(kTag, "%s", lastError_.c_str());
            return false;
        }
        queueFamily_ = i;
        return true;
    }

    // No match. Carry on with the family PickPhysicalDevice chose rather than
    // failing, but say so: a wrong family makes every submit fail validation.
    ARGON_WARN(kTag,
               "could not match the adopted VkQueue to a queue family; assuming family %u",
               queueFamily_);
    return true;
}

// ---------------------------------------------------------------------------
// Target
// ---------------------------------------------------------------------------

bool VulkanBackend::CreateTarget(const TargetDesc& desc) {
    DestroyTarget();
    if (device_ == VK_NULL_HANDLE) {
        lastError_ = "CreateTarget called before Initialize()";
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return false;
    }
    if (desc.externalSwapchain != nullptr) return AdoptTarget(desc);

    const u32 w = Max(1u, desc.width);
    const u32 h = Max(1u, desc.height);

    if (desc.targetSource == TargetSource::OffscreenTexture) {
        return CreateOffscreenTarget(w, h);
    }
    if (pfnCreateSwapchainKHR == nullptr) {
        lastError_ = "VK_KHR_swapchain entry points are not available on this instance";
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return false;
    }
    return CreateSwapchainForWindow(w, h);
}

bool VulkanBackend::AdoptTarget(const TargetDesc& desc) {
    if (desc.externalSwapchain == nullptr) {
        lastError_ = "AdoptTarget was given a null swapchain";
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return false;
    }
    if (!images_.empty()) {
        lastError_ = "a target is already attached; call DestroyTarget() first";
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return false;
    }
    if (pfnGetSwapchainImagesKHR == nullptr || pfnGetPhysicalDeviceSurfaceFormatsKHR == nullptr) {
        lastError_ = "VK_KHR_swapchain entry points are not available on this instance";
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return false;
    }

    swapChain_ = static_cast<VkSwapchainKHR>(desc.externalSwapchain);
    ownsSwapchain_ = false;         // never ours to destroy
    offscreenTarget_ = false;
    clearTarget_ = false;           // the host drew into this image; do not wipe it
    info_.ownsSwapchain = false;

    // The extent and format of somebody else's chain cannot be queried
    // directly, so we go through the surface the chain was created against.
    if (surface_ == VK_NULL_HANDLE && !CreateSurface()) return false;
    VkSurfaceCapabilitiesKHR caps{};
    if (pfnGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &caps) != VK_SUCCESS) {
        lastError_ = "could not query surface capabilities for the adopted swapchain";
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return false;
    }
    PickSurfaceFormat();
    PickExtent(caps, desc.width, desc.height);

    if (!CreateRenderPass(surfaceFormat_)) return false;
    if (!EnsurePipelines(surfaceFormat_)) return false;

    u32 count = 0;
    VkResult r = pfnGetSwapchainImagesKHR(device_, swapChain_, &count, nullptr);
    if (r != VK_SUCCESS) { Fail("vkGetSwapchainImagesKHR", r); return false; }
    if (count == 0) {
        lastError_ = "the adopted swapchain reports no images";
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return false;
    }
    std::vector<VkImage> images(count);
    r = pfnGetSwapchainImagesKHR(device_, swapChain_, &count, images.data());
    if (r != VK_SUCCESS) { Fail("vkGetSwapchainImagesKHR", r); return false; }

    ARGON_INFO(kTag, "adopted external swapchain %p (%u images, format %d)",
               desc.externalSwapchain, count, static_cast<int>(surfaceFormat_));
    if (!BuildTargetResources(images)) return false;
    ARGON_WARN(kTag,
               "ArgonUI acquires and renders its own swapchain images; the host must not "
               "acquire from this swapchain in the same frame");
    return true;
}

void VulkanBackend::PickSurfaceFormat() {
    u32 formatCount = 0;
    pfnGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    if (formatCount > 0) {
        pfnGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount,
                                              formats.data());
    }

    // ArgonUI's colours are hand-authored sRGB values, so an _SRGB surface is
    // the honest choice when one is offered; UNORM is the fallback. Either way
    // the render pass, the framebuffers and the pipelines all use exactly this
    // format.
    const VkFormat preferred[] = {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM};

    surfaceFormat_ = formats.empty() ? VK_FORMAT_R8G8B8A8_UNORM : formats[0].format;
    surfaceColorSpace_ =
        formats.empty() ? VK_COLOR_SPACE_SRGB_NONLINEAR_KHR : formats[0].colorSpace;

    for (int pass = 0; pass < 2 && !formats.empty(); ++pass) {
        for (VkFormat want : preferred) {
            for (const VkSurfaceFormatKHR& f : formats) {
                if (f.format != want) continue;
                if (pass == 0 && f.colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) continue;
                surfaceFormat_ = f.format;
                surfaceColorSpace_ = f.colorSpace;
                if (pass == 1 && want != preferred[0]) {
                    ARGON_WARN(kTag, "the surface only offers %s, not an sRGB format",
                               ToString(f.format));
                }
                return;
            }
        }
    }
}

void VulkanBackend::PickExtent(const VkSurfaceCapabilitiesKHR& caps, u32 width, u32 height) {
    targetExtent_.width = Max(1u, width);
    targetExtent_.height = Max(1u, height);
    // A surface that is driven by a window dictates its own size.
    if (caps.currentExtent.width != 0xFFFFFFFFu) {
        targetExtent_.width = Max(1u, caps.currentExtent.width);
        targetExtent_.height = Max(1u, caps.currentExtent.height);
    }
    targetWidth_ = targetExtent_.width;
    targetHeight_ = targetExtent_.height;
    targetSize_ = Vec2(static_cast<f32>(targetWidth_), static_cast<f32>(targetHeight_));
}

bool VulkanBackend::CreateSwapchainForWindow(u32 width, u32 height) {
    if (surface_ == VK_NULL_HANDLE && !CreateSurface()) return false;
    if (pfnCreateSwapchainKHR == nullptr) {
        lastError_ = "VK_KHR_swapchain entry points are not available on this instance";
        return false;
    }

    VkSurfaceCapabilitiesKHR caps{};
    VkResult r = pfnGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &caps);
    if (r != VK_SUCCESS) { Fail("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", r); return false; }
    PickExtent(caps, width, height);
    PickSurfaceFormat();

    if ((caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
        lastError_ = "the surface cannot be used as a colour attachment";
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return false;
    }

    u32 imageCount = caps.minImageCount + 1u;   // one in flight beyond the minimum
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

    u32 modeCount = 0;
    pfnGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &modeCount, nullptr);
    std::vector<VkPresentModeKHR> modes(modeCount);
    if (modeCount > 0) {
        pfnGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &modeCount,
                                                   modes.data());
    }
    VkPresentModeKHR presentMode = modes.empty() ? VK_PRESENT_MODE_FIFO_KHR : modes[0];
    const VkPresentModeKHR wanted[] = {
        enableVsync_ ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR,
        enableVsync_ ? VK_PRESENT_MODE_IMMEDIATE_KHR : VK_PRESENT_MODE_MAILBOX_KHR,
        VK_PRESENT_MODE_FIFO_KHR};
    for (VkPresentModeKHR want : wanted) {
        if (std::find(modes.begin(), modes.end(), want) != modes.end()) {
            presentMode = want;
            break;
        }
    }

    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    const VkCompositeAlphaFlagBitsKHR alphaWanted[] = {
        transparentBackground_ ? VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR
                              : VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR};
    for (VkCompositeAlphaFlagBitsKHR want : alphaWanted) {
        if ((caps.supportedCompositeAlpha & want) != 0) {
            compositeAlpha = want;
            break;
        }
    }

    VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sci.surface = surface_;
    sci.minImageCount = imageCount;
    sci.imageFormat = surfaceFormat_;
    sci.imageColorSpace = surfaceColorSpace_;
    sci.imageExtent = targetExtent_;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = compositeAlpha;
    sci.presentMode = presentMode;
    sci.clipped = VK_TRUE;
    // previousSwapChain_ is owned by RecreateSwapchain, which destroys the old
    // handle once the new chain exists - clearing it here would leak it.
    sci.oldSwapchain = previousSwapChain_;

    VkSwapchainKHR swapChain = VK_NULL_HANDLE;
    r = pfnCreateSwapchainKHR(device_, &sci, nullptr, &swapChain);
    // VK_SUBOPTIMAL_KHR is a success code: the chain works, but it wants
    // recreating (usually because the window resized between the capability
    // query and here).
    if ((r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) || swapChain == VK_NULL_HANDLE) {
        Fail("vkCreateSwapchainKHR", r);
        return false;
    }
    swapchainOutOfDate_ = (r == VK_SUBOPTIMAL_KHR);

    swapChain_ = swapChain;
    ownsSwapchain_ = true;
    info_.ownsSwapchain = true;
    offscreenTarget_ = false;
    clearTarget_ = true;
    DestroyRenderPass();
    if (!CreateRenderPass(surfaceFormat_)) return false;
    if (!EnsurePipelines(surfaceFormat_)) return false;

    u32 count = 0;
    r = pfnGetSwapchainImagesKHR(device_, swapChain_, &count, nullptr);
    if (r != VK_SUCCESS || count == 0) { Fail("vkGetSwapchainImagesKHR", r); return false; }
    std::vector<VkImage> images(count);
    r = pfnGetSwapchainImagesKHR(device_, swapChain_, &count, images.data());
    if (r != VK_SUCCESS) { Fail("vkGetSwapchainImagesKHR", r); return false; }

    return BuildTargetResources(images);
}

bool VulkanBackend::CreateOffscreenTarget(u32 width, u32 height) {
    const VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    surfaceFormat_ = format;
    targetExtent_.width = Max(1u, width);
    targetExtent_.height = Max(1u, height);
    targetWidth_ = targetExtent_.width;
    targetHeight_ = targetExtent_.height;
    targetSize_ = Vec2(static_cast<f32>(targetWidth_), static_cast<f32>(targetHeight_));
    offscreenTarget_ = true;
    ownsSwapchain_ = false;
    clearTarget_ = true;
    swapChain_ = VK_NULL_HANDLE;
    imageCount_ = 1;

    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = format;
    ici.extent = {targetExtent_.width, targetExtent_.height, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage image = VK_NULL_HANDLE;
    VkResult r = vkCreateImage(device_, &ici, nullptr, &image);
    if (r != VK_SUCCESS) { Fail("vkCreateImage(offscreen)", r); return false; }

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(device_, image, &req);
    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (!AllocateMemory(req.size, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &memory)) {
        vkDestroyImage(device_, image, nullptr);
        return false;
    }
    if (vkBindImageMemory(device_, image, memory, 0) != VK_SUCCESS) {
        lastError_ = "vkBindImageMemory failed for the offscreen target";
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        vkFreeMemory(device_, memory, nullptr);
        vkDestroyImage(device_, image, nullptr);
        return false;
    }

    DestroyRenderPass();
    if (!CreateRenderPass(format)) {
        vkDestroyImage(device_, image, nullptr);
        vkFreeMemory(device_, memory, nullptr);
        return false;
    }
    if (!EnsurePipelines(format)) {
        vkDestroyImage(device_, image, nullptr);
        vkFreeMemory(device_, memory, nullptr);
        return false;
    }

    // BuildTargetResources resets ownsImages_ (a swapchain's images are not
    // ours); it is only true again once our own image is actually in place.
    const bool ok = BuildTargetResources(std::vector<VkImage>(1u, image));
    if (!ok) {
        vkDestroyImage(device_, image, nullptr);
        vkFreeMemory(device_, memory, nullptr);
        return false;
    }
    ownsImages_ = true;
    // Stash the allocation next to the image so DestroyTargetResources frees it.
    images_[0].memory = memory;
    ARGON_INFO(kTag, "offscreen target %ux%u", targetWidth_, targetHeight_);
    return true;
}

bool VulkanBackend::BuildTargetResources(const std::vector<VkImage>& images) {
    images_.assign(images.size(), ImageResource{});
    ownsImages_ = false;

    for (size_t i = 0; i < images.size(); ++i) {
        ImageResource& res = images_[i];
        res.image = images[i];

        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vci.image = res.image;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = surfaceFormat_;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkResult r = vkCreateImageView(device_, &vci, nullptr, &res.view);
        if (r != VK_SUCCESS) { Fail("vkCreateImageView(swapchain)", r); return false; }

        VkFramebufferCreateInfo fbci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fbci.renderPass = renderPass_;
        fbci.attachmentCount = 1;
        fbci.pAttachments = &res.view;
        fbci.width = targetExtent_.width;
        fbci.height = targetExtent_.height;
        fbci.layers = 1;
        r = vkCreateFramebuffer(device_, &fbci, nullptr, &res.framebuffer);
        if (r != VK_SUCCESS) {
            Fail("vkCreateFramebuffer", r);
            if (r == VK_ERROR_FORMAT_NOT_SUPPORTED) {
                lastError_ += " - the render pass format does not match the image (a swapped-in "
                              "swapchain format needs matching host state)";
            }
            return false;
        }

        VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cbai.commandPool = commandPool_;
        cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = 1;
        r = vkAllocateCommandBuffers(device_, &cbai, &res.commandBuffer);
        if (r != VK_SUCCESS) { Fail("vkAllocateCommandBuffers", r); return false; }

        if (!offscreenTarget_) {
            VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            r = vkCreateSemaphore(device_, &sci, nullptr, &res.acquireSemaphore);
            if (r != VK_SUCCESS) { Fail("vkCreateSemaphore(acquire)", r); return false; }
            r = vkCreateSemaphore(device_, &sci, nullptr, &res.presentSemaphore);
            if (r != VK_SUCCESS) { Fail("vkCreateSemaphore(present)", r); return false; }
        }

        VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        r = vkCreateFence(device_, &fci, nullptr, &res.fence);
        if (r != VK_SUCCESS) { Fail("vkCreateFence", r); return false; }

        // Per-image uniform buffer. One per image rather than one shared buffer
        // so a frame never overwrites the 16 bytes the previous frame is still
        // reading.
        if (!CreateHostVisibleBuffer(16, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                     &res.viewportBuffer, &res.viewportMemory,
                                     &res.viewportMapped)) {
            return false;
        }

        VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        dai.descriptorPool = descriptorPool_;
        dai.descriptorSetCount = 1;
        dai.pSetLayouts = &setLayout_;
        r = vkAllocateDescriptorSets(device_, &dai, &res.descriptorSet);
        if (r != VK_SUCCESS) { Fail("vkAllocateDescriptorSets", r); return false; }

        // Binding 0 never changes for this set; binding 1 is rewritten as the
        // batches come in (see BindTexture).
        VkDescriptorBufferInfo uboInfo{res.viewportBuffer, 0, VK_WHOLE_SIZE};
        VkDescriptorImageInfo imageInfo{sampler_, whiteView_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet writes[2]{};
        writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[0].dstSet = res.descriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo = &uboInfo;
        writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[1].dstSet = res.descriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);
    }

    currentImage_ = 0;
    currentCommandBuffer_ = images_.empty() ? VK_NULL_HANDLE : images_[0].commandBuffer;
    imageCount_ = static_cast<u32>(images_.size());
    boundSet_ = VK_NULL_HANDLE;
    return true;
}

void VulkanBackend::DestroyTargetResources() {
    if (device_ == VK_NULL_HANDLE || images_.empty()) {
        images_.clear();
        return;
    }

    std::vector<VkDescriptorSet> sets;
    sets.reserve(images_.size());
    for (ImageResource& res : images_) {
        if (res.viewportMapped != nullptr && res.viewportMemory != VK_NULL_HANDLE) {
            vkUnmapMemory(device_, res.viewportMemory);
            res.viewportMapped = nullptr;
        }
        if (res.viewportBuffer != VK_NULL_HANDLE) vkDestroyBuffer(device_, res.viewportBuffer, nullptr);
        if (res.viewportMemory != VK_NULL_HANDLE) vkFreeMemory(device_, res.viewportMemory, nullptr);
        if (res.fence != VK_NULL_HANDLE) vkDestroyFence(device_, res.fence, nullptr);
        if (res.acquireSemaphore != VK_NULL_HANDLE) vkDestroySemaphore(device_, res.acquireSemaphore, nullptr);
        if (res.presentSemaphore != VK_NULL_HANDLE) vkDestroySemaphore(device_, res.presentSemaphore, nullptr);
        if (res.commandBuffer != VK_NULL_HANDLE && commandPool_ != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(device_, commandPool_, 1, &res.commandBuffer);
        }
        if (res.framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(device_, res.framebuffer, nullptr);
        if (res.view != VK_NULL_HANDLE) vkDestroyImageView(device_, res.view, nullptr);
        // Swapchain images belong to the swapchain; only our own offscreen
        // image (and its memory) is ours to delete.
        if (ownsImages_ && res.image != VK_NULL_HANDLE) vkDestroyImage(device_, res.image, nullptr);
        if (res.memory != VK_NULL_HANDLE) vkFreeMemory(device_, res.memory, nullptr);
        if (res.descriptorSet != VK_NULL_HANDLE) sets.push_back(res.descriptorSet);
    }
    if (!sets.empty()) vkFreeDescriptorSets(device_, descriptorPool_, static_cast<u32>(sets.size()),
                                            sets.data());

    images_.clear();
    ownsImages_ = false;
}

void VulkanBackend::DestroyTarget() {
    DestroyTargetResources();
    DestroyRenderPass();

    if (swapChain_ != VK_NULL_HANDLE) {
        // An adopted chain is not ours to release.
        if (ownsSwapchain_ && pfnDestroySwapchainKHR != nullptr) {
            pfnDestroySwapchainKHR(device_, swapChain_, nullptr);
        }
        swapChain_ = VK_NULL_HANDLE;
    }
    previousSwapChain_ = VK_NULL_HANDLE;
    ownsSwapchain_ = false;
    offscreenTarget_ = false;
    clearTarget_ = true;
    swapchainOutOfDate_ = false;
    surfaceFormat_ = VK_FORMAT_UNDEFINED;
    surfaceColorSpace_ = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    targetExtent_ = VkExtent2D{0, 0};
    targetWidth_ = 0;
    targetHeight_ = 0;
    targetSize_ = Vec2(0.0f, 0.0f);
    imageCount_ = 0;
    currentImage_ = 0;
    currentCommandBuffer_ = VK_NULL_HANDLE;
    boundSet_ = VK_NULL_HANDLE;
    boundView_ = VK_NULL_HANDLE;
    boundSlot_ = 0;
    frameBegun_ = false;
    frameSubmitted_ = false;
    info_.ownsSwapchain = false;
}

bool VulkanBackend::RecreateSwapchain(u32 width, u32 height) {
    // Every image must be idle before its views, framebuffers and memory go
    // away, and before the swapchain handle itself does.
    vkDeviceWaitIdle(device_);

    previousSwapChain_ = swapChain_;
    swapChain_ = VK_NULL_HANDLE;
    DestroyTargetResources();

    const bool ok = CreateSwapchainForWindow(width, height);
    if (pfnDestroySwapchainKHR != nullptr && previousSwapChain_ != VK_NULL_HANDLE) {
        pfnDestroySwapchainKHR(device_, previousSwapChain_, nullptr);
        previousSwapChain_ = VK_NULL_HANDLE;
    }
    swapchainOutOfDate_ = false;
    if (!ok) lastError_ += " (while recreating the swapchain)";
    return ok;
}

bool VulkanBackend::RecreateFramebuffers() {
    // Somebody else's swapchain cannot be resized, but our framebuffers can be
    // rebuilt for a window that changed size underneath us.
    vkDeviceWaitIdle(device_);
    DestroyTargetResources();

    if (surface_ != VK_NULL_HANDLE && pfnGetPhysicalDeviceSurfaceCapabilitiesKHR != nullptr) {
        VkSurfaceCapabilitiesKHR caps{};
        if (pfnGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &caps) == VK_SUCCESS) {
            PickExtent(caps, targetWidth_, targetHeight_);
        }
    }

    u32 count = 0;
    if (pfnGetSwapchainImagesKHR == nullptr ||
        pfnGetSwapchainImagesKHR(device_, swapChain_, &count, nullptr) != VK_SUCCESS || count == 0) {
        lastError_ = "could not re-query the adopted swapchain's images";
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return false;
    }
    std::vector<VkImage> images(count);
    if (pfnGetSwapchainImagesKHR(device_, swapChain_, &count, images.data()) != VK_SUCCESS) {
        lastError_ = "could not re-query the adopted swapchain's images";
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return false;
    }
    if (!CreateRenderPass(surfaceFormat_)) return false;
    if (!EnsurePipelines(surfaceFormat_)) return false;
    return BuildTargetResources(images);
}

bool VulkanBackend::ResizeTarget(u32 width, u32 height) {
    if (images_.empty()) return false;
    if (width == 0 || height == 0) return true;              // minimised
    if (width == targetWidth_ && height == targetHeight_ && !swapchainOutOfDate_) return true;

    if (offscreenTarget_) {
        vkDeviceWaitIdle(device_);
        DestroyTargetResources();
        DestroyRenderPass();
        return CreateOffscreenTarget(width, height);
    }
    if (!ownsSwapchain_) {
        // Not ours to resize; rebuild what we can against the new window size.
        return RecreateFramebuffers();
    }
    return RecreateSwapchain(width, height);
}

// ---------------------------------------------------------------------------
// Memory
// ---------------------------------------------------------------------------

u32 VulkanBackend::FindMemoryType(u32 typeBits, VkMemoryPropertyFlags required) const {
    for (u32 i = 0; i < memoryProperties_.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) == 0) continue;
        if ((memoryProperties_.memoryTypes[i].propertyFlags & required) == required) return i;
    }
    return UINT32_MAX;   // == VK_ATTACHMENT_UNUSED
}

bool VulkanBackend::AllocateMemory(VkDeviceSize size, u32 typeBits, VkMemoryPropertyFlags required,
                                   VkDeviceMemory* out) {
    const u32 index = FindMemoryType(typeBits, required);
    if (index == UINT32_MAX) {
        lastError_ = "no Vulkan memory type satisfies the requested properties";
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return false;
    }
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = size;
    mai.memoryTypeIndex = index;
    const VkResult r = vkAllocateMemory(device_, &mai, nullptr, out);
    if (r != VK_SUCCESS) {
        *out = VK_NULL_HANDLE;
        Fail("vkAllocateMemory", r);
        return false;
    }
    return true;
}

bool VulkanBackend::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                 VkMemoryPropertyFlags properties, VkBuffer* buffer,
                                 VkDeviceMemory* memory) {
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = Max<VkDeviceSize>(size, 16);
    bci.usage = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult r = vkCreateBuffer(device_, &bci, nullptr, buffer);
    if (r != VK_SUCCESS) {
        *buffer = VK_NULL_HANDLE;
        Fail("vkCreateBuffer", r);
        return false;
    }

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device_, *buffer, &req);
    // A host-visible buffer is much better off device-local when the driver
    // offers both (BAR-backed discrete GPUs); fall back otherwise.
    if ((properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0) {
        const VkMemoryPropertyFlags ideal = properties | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        if (FindMemoryType(req.memoryTypeBits, ideal) != UINT32_MAX) properties = ideal;
    }
    if (!AllocateMemory(req.size, req.memoryTypeBits, properties, memory)) {
        vkDestroyBuffer(device_, *buffer, nullptr);
        *buffer = VK_NULL_HANDLE;
        return false;
    }

    r = vkBindBufferMemory(device_, *buffer, *memory, 0);
    if (r != VK_SUCCESS) {
        Fail("vkBindBufferMemory", r);
        vkFreeMemory(device_, *memory, nullptr);
        *memory = VK_NULL_HANDLE;
        vkDestroyBuffer(device_, *buffer, nullptr);
        *buffer = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

bool VulkanBackend::CreateHostVisibleBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                            VkBuffer* buffer, VkDeviceMemory* memory,
                                            void** mapped) {
    const VkMemoryPropertyFlags properties =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (!CreateBuffer(size, usage, properties, buffer, memory)) return false;

    const VkResult r = vkMapMemory(device_, *memory, 0, VK_WHOLE_SIZE, 0, mapped);
    if (r != VK_SUCCESS) {
        *mapped = nullptr;
        Fail("vkMapMemory", r);
        vkDestroyBuffer(device_, *buffer, nullptr);
        *buffer = VK_NULL_HANDLE;
        vkFreeMemory(device_, *memory, nullptr);
        *memory = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

void VulkanBackend::DestroyBuffer(VkBuffer* buffer, VkDeviceMemory* memory) {
    if (device_ == VK_NULL_HANDLE) return;
    if (*buffer != VK_NULL_HANDLE) vkDestroyBuffer(device_, *buffer, nullptr);
    if (*memory != VK_NULL_HANDLE) vkFreeMemory(device_, *memory, nullptr);
    *buffer = VK_NULL_HANDLE;
    *memory = VK_NULL_HANDLE;
}

bool VulkanBackend::EnsureBufferCapacity(size_t vertexBytes, size_t indexBytes) {
    if (vertexBuffer_ == VK_NULL_HANDLE || vertexBytes > vertexCapacity_) {
        if (vertexMapped_ != nullptr && vertexMemory_ != VK_NULL_HANDLE) {
            vkUnmapMemory(device_, vertexMemory_);
            vertexMapped_ = nullptr;
        }
        DestroyBuffer(&vertexBuffer_, &vertexMemory_);
        vertexCapacity_ = Max<size_t>(vertexBytes * 2, kVertexBufferBytes);
        if (!CreateHostVisibleBuffer(vertexCapacity_, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                     &vertexBuffer_, &vertexMemory_, &vertexMapped_)) {
            vertexCapacity_ = 0;
            return false;
        }
    }

    if (indexBuffer_ == VK_NULL_HANDLE || indexBytes > indexCapacity_) {
        if (indexMapped_ != nullptr && indexMemory_ != VK_NULL_HANDLE) {
            vkUnmapMemory(device_, indexMemory_);
            indexMapped_ = nullptr;
        }
        DestroyBuffer(&indexBuffer_, &indexMemory_);
        indexCapacity_ = Max<size_t>(indexBytes * 2, kIndexBufferBytes);
        if (!CreateHostVisibleBuffer(indexCapacity_, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                     &indexBuffer_, &indexMemory_, &indexMapped_)) {
            indexCapacity_ = 0;
            return false;
        }
    }
    info_.vertexBufferSize = static_cast<u32>(Min<size_t>(vertexCapacity_, 0xFFFFFFFFu));
    info_.indexBufferSize = static_cast<u32>(Min<size_t>(indexCapacity_, 0xFFFFFFFFu));
    return true;
}

bool VulkanBackend::RunOneTimeCommand(const std::function<void(VkCommandBuffer)>& body) {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = commandPool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkResult r = vkAllocateCommandBuffers(device_, &ai, &cmd);
    if (r != VK_SUCCESS) { Fail("vkAllocateCommandBuffers", r); return false; }

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    r = vkBeginCommandBuffer(cmd, &bi);
    if (r != VK_SUCCESS) {
        vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
        Fail("vkBeginCommandBuffer", r);
        return false;
    }
    body(cmd);
    r = vkEndCommandBuffer(cmd);
    if (r != VK_SUCCESS) {
        vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
        Fail("vkEndCommandBuffer", r);
        return false;
    }

    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence = VK_NULL_HANDLE;
    r = vkCreateFence(device_, &fci, nullptr, &fence);
    if (r != VK_SUCCESS) {
        vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
        Fail("vkCreateFence", r);
        return false;
    }

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    r = vkQueueSubmit(queue_, 1, &si, fence);
    if (r != VK_SUCCESS) {
        vkDestroyFence(device_, fence, nullptr);
        vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
        Fail("vkQueueSubmit", r);
        return false;
    }

    vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(device_, fence, nullptr);
    vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
    return true;
}

// ---------------------------------------------------------------------------
// Textures
// ---------------------------------------------------------------------------

bool VulkanBackend::CreateTextureInternal(const TextureDesc& desc, const void* pixels,
                                          TextureRecord& out) {
    out = TextureRecord{};
    out.desc = desc;
    desc.width = Max(1u, desc.width);
    desc.height = Max(1u, desc.height);
    out.desc.width = desc.width;
    out.desc.height = desc.height;

    // Optimal tiling, one mip, no cube: non-power-of-two dimensions are legal
    // for this combination with a clamp sampler, which lets ArgonUI use
    // R8 glyph atlases of any size.
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = ToVkFormat(desc);
    ici.extent = {desc.width, desc.height, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkResult r = vkCreateImage(device_, &ici, nullptr, &out.image);
    if (r != VK_SUCCESS) { out.image = VK_NULL_HANDLE; Fail("vkCreateImage", r); return false; }

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(device_, out.image, &req);
    if (!AllocateMemory(req.size, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        &out.memory)) {
        return false;
    }
    r = vkBindImageMemory(device_, out.image, out.memory, 0);
    if (r != VK_SUCCESS) { Fail("vkBindImageMemory", r); return false; }

    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = out.image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = ici.format;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    r = vkCreateImageView(device_, &vci, nullptr, &out.view);
    if (r != VK_SUCCESS) { Fail("vkCreateImageView", r); return false; }

    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = descriptorPool_;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &setLayout_;
    r = vkAllocateDescriptorSets(device_, &dai, &out.descriptorSet);
    if (r != VK_SUCCESS) {
        out.descriptorSet = VK_NULL_HANDLE;
        Fail("vkAllocateDescriptorSets", r);
        return false;
    }

    // Binding 1 is the texture itself and never changes; binding 0 (the frame's
    // viewport buffer) is filled in by BindTexture, because the texture does not
    // know yet which swapchain image it will be drawn with.
    VkDescriptorImageInfo imageInfo{sampler_, out.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = out.descriptorSet;
    write.dstBinding = 1;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

    if (pixels != nullptr) {
        if (!UploadTextureData(desc, pixels, out)) return false;
    }
    return true;
}

bool VulkanBackend::UploadTextureData(const TextureDesc& desc, const void* pixels,
                                      TextureRecord& rec) {
    const VkDeviceSize rowBytes =
        static_cast<VkDeviceSize>(desc.width) * static_cast<VkDeviceSize>(BytesPerTexel(desc.format));
    const VkDeviceSize totalBytes = rowBytes * desc.height;

    // The staging buffer is tightly packed and rowLength stays 0 in the copy
    // region, so the CPU-side layout is exactly the D3D11 one: width * bytes
    // per texel, no padding. That also means no NPT row-padding maths.
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    if (!CreateHostVisibleBuffer(totalBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &staging,
                                 &stagingMemory, &mapped)) {
        return false;
    }
    std::memcpy(mapped, pixels, static_cast<size_t>(totalBytes));

    const bool ok = RunOneTimeCommand([&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = rec.image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;       // 0 == (widthTexel * texelBlockSize)
        region.bufferImageHeight = 0;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {desc.width, desc.height, 1};
        vkCmdCopyBufferToImage(cmd, staging, rec.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                               &region);

        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                             &barrier);
    });

    vkUnmapMemory(device_, stagingMemory);
    vkDestroyBuffer(device_, staging, nullptr);
    vkFreeMemory(device_, stagingMemory, nullptr);
    return ok;
}

void VulkanBackend::DestroyTextureRecord(TextureRecord& rec) {
    if (device_ == VK_NULL_HANDLE) return;
    if (rec.descriptorSet != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(device_, descriptorPool_, 1, &rec.descriptorSet);
        rec.descriptorSet = VK_NULL_HANDLE;
    }
    if (rec.view != VK_NULL_HANDLE) vkDestroyImageView(device_, rec.view, nullptr);
    if (rec.image != VK_NULL_HANDLE) vkDestroyImage(device_, rec.image, nullptr);
    if (rec.memory != VK_NULL_HANDLE) vkFreeMemory(device_, rec.memory, nullptr);
    rec = TextureRecord{};
}

TextureRecord* VulkanBackend::ResolveTexture(u32 handle) {
    auto it = textures_.find(handle == 0u ? 1u : handle);
    if (it == textures_.end()) return nullptr;
    return &it->second;
}

u32 VulkanBackend::CreateTexture(const TextureDesc& desc, const void* pixels) {
    if (device_ == VK_NULL_HANDLE || desc.width == 0 || desc.height == 0) return 0;

    TextureRecord rec;
    if (!CreateTextureInternal(desc, pixels, rec)) {
        DestroyTextureRecord(rec);
        return 0;
    }
    const u32 id = nextTextureId_++;
    textures_.emplace(id, rec);
    return id;
}

bool VulkanBackend::UpdateTexture(u32 handle, const TextureDesc& desc, const void* pixels) {
    auto it = textures_.find(handle);
    if (it == textures_.end() || pixels == nullptr || device_ == VK_NULL_HANDLE) return false;

    if (desc.width != it->second.desc.width || desc.height != it->second.desc.height ||
        desc.format != it->second.desc.format || desc.srgb != it->second.desc.srgb) {
        // A format or size change needs a full re-create.
        TextureRecord fresh;
        if (!CreateTextureInternal(desc, pixels, fresh)) {
            DestroyTextureRecord(fresh);
            return false;
        }
        DestroyTextureRecord(it->second);
        it->second = fresh;
        return true;
    }
    return UploadTextureData(desc, pixels, it->second);
}

void VulkanBackend::DestroyTexture(u32 handle) {
    if (handle == 1 || handle == 0) return;   // the white 1x1 is not ours to free here
    auto it = textures_.find(handle);
    if (it == textures_.end()) return;
    DestroyTextureRecord(it->second);
    textures_.erase(it);
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

bool VulkanBackend::BindTexture(VkCommandBuffer cmd, VkDescriptorSet set, VkImageView view,
                                u32 slot) {
    if (slot >= images_.size() || set == VK_NULL_HANDLE) return false;
    if (set == boundSet_ && view == boundView_ && slot == boundSlot_) return true;

    const ImageResource& image = images_[slot];
    if (image.viewportBuffer == VK_NULL_HANDLE) return false;

    VkDescriptorBufferInfo ubo{image.viewportBuffer, 0, VK_WHOLE_SIZE};
    VkDescriptorImageInfo samplerImage{sampler_, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    VkWriteDescriptorSet writes[2]{};
    writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    writes[0].dstSet = set;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &ubo;
    writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    writes[1].dstSet = set;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = &samplerImage;
    vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &set, 0,
                            nullptr);

    boundSet_ = set;
    boundView_ = view;
    boundSlot_ = slot;
    return true;
}

bool VulkanBackend::BeginFrame(const FrameState& state) {
    if (frameBegun_) return false;                     // unbalanced Begin/End
    if (device_ == VK_NULL_HANDLE) return false;
    if (images_.empty() || renderPass_ == VK_NULL_HANDLE) return false;   // no target
    if (state.occluded) return false;                  // minimised / hidden

    // A swapchain that went out of date is rebuilt lazily: hosts that only
    // resize when their window does would otherwise keep dropping frames.
    if (swapchainOutOfDate_ && ownsSwapchain_) {
        if (!RecreateSwapchain(targetWidth_, targetHeight_)) return false;
    }

    u32 image = 0;
    if (!offscreenTarget_) {
        if (pfnAcquireNextImageKHR == nullptr) return false;
        const VkResult r = pfnAcquireNextImageKHR(device_, swapChain_, kAcquireTimeoutNs, VK_NULL_HANDLE,
                                                   &image);
        if (r == VK_ERROR_OUT_OF_DATE_KHR) {
            swapchainOutOfDate_ = true;
            ARGON_WARN(kTag, "swapchain is out of date; skipping the frame");
            return false;
        }
        if (r == VK_TIMEOUT) {
            ARGON_WARN(kTag,
                       "vkAcquireNextImageKHR timed out - is the host presenting the swapchain "
                       "ArgonUI acquired?");
            return false;
        }
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
            Fail("vkAcquireNextImageKHR", r);
            return false;
        }
        if (r == VK_SUBOPTIMAL_KHR) swapchainOutOfDate_ = true;
        if (image >= images_.size()) {
            lastError_ = "the swapchain returned an image index we do not have resources for";
            ARGON_ERROR(kTag, "%s", lastError_.c_str());
            return false;
        }
    }

    ImageResource& res = images_[image];
    // Nothing else may touch this image until the frame that used it has
    // finished: its command buffer, its fence and its descriptor set are all
    // reused below.
    vkWaitForFences(device_, 1, &res.fence, VK_TRUE, UINT64_MAX);
    vkResetFences(device_, 1, &res.fence);

    currentImage_ = image;
    currentCommandBuffer_ = res.commandBuffer;
    boundSet_ = VK_NULL_HANDLE;     // the descriptor cache dies with the target
    drawCallCount_ = 0;
    frameSubmitted_ = false;
    frameBegun_ = true;
    return true;
}

void VulkanBackend::Render(const DrawList& list, const FrameState&) {
    if (!frameBegun_ || frameSubmitted_ || device_ == VK_NULL_HANDLE) return;
    if (images_.empty() || renderPass_ == VK_NULL_HANDLE) {
        // Nothing to submit into; Present() will decline.
        return;
    }

    ImageResource& image = images_[currentImage_];
    VkCommandBuffer cmd = currentCommandBuffer_;

    // Upload. The DrawList owns the arrays for the whole frame, so a plain
    // memcpy into the persistently mapped pair is all the "upload" is.
    const size_t vertexBytes = list.VertexCount() * sizeof(Vertex);
    const size_t indexBytes = list.IndexCount() * sizeof(u32);
    if (list.CmdCount() > 0) {
        if (!EnsureBufferCapacity(vertexBytes, indexBytes)) return;
        if (vertexBytes > 0 && vertexMapped_ != nullptr) {
            std::memcpy(vertexMapped_, list.Vertices(), vertexBytes);
        }
        if (indexBytes > 0 && indexMapped_ != nullptr) {
            std::memcpy(indexMapped_, list.Indices(), indexBytes);
        }
    }

    // The shader turns pixel coordinates into clip space from this constant:
    // xy = pixel size, zw = 1 / pixel size.
    if (image.viewportMapped != nullptr) {
        const f32 w = static_cast<f32>(Max(1u, targetWidth_));
        const f32 h = static_cast<f32>(Max(1u, targetHeight_));
        const f32 viewportConst[4] = {w, h, 1.0f / w, 1.0f / h};
        std::memcpy(image.viewportMapped, viewportConst, sizeof(viewportConst));
    }

    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VkResult r = vkBeginCommandBuffer(cmd, &bi);
    if (r != VK_SUCCESS) { Fail("vkBeginCommandBuffer", r); return; }

    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = renderPass_;
    rp.framebuffer = image.framebuffer;
    rp.renderArea = {{0, 0}, {targetExtent_.width, targetExtent_.height}};
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    VkPipeline boundPipeline = VK_NULL_HANDLE;
    VkRect2D boundScissor{};
    bool scissorValid = false;

    if (list.CmdCount() > 0) {
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<f32>(targetWidth_);
        viewport.height = static_cast<f32>(targetHeight_);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        const VkDeviceSize zeroOffset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer_, &zeroOffset);
        vkCmdBindIndexBuffer(cmd, indexBuffer_, 0, VK_INDEX_TYPE_UINT32);

        const DrawCmd* cmds = list.Cmds();
        const u32 cmdCount = list.CmdCount();

        for (u32 i = 0; i < cmdCount; ++i) {
            const DrawCmd& draw = cmds[i];
            if (draw.indexCount == 0) continue;

            // Scissor: clamp the CPU-side clip to the target, floor/ceil so
            // partial pixel coverage is never cut off. Unlike D3D11, a Vulkan
            // scissor outside the framebuffer is invalid, so a fully clipped
            // batch is dropped instead of being widened to a phantom pixel.
            const f32 left = Max(0.0f, floor(draw.clip.min.x));
            const f32 top = Max(0.0f, floor(draw.clip.min.y));
            const f32 right = Min(static_cast<f32>(targetWidth_), ceil(draw.clip.max.x));
            const f32 bottom = Min(static_cast<f32>(targetHeight_), ceil(draw.clip.max.y));
            if (right <= left || bottom <= top) continue;

            VkRect2D scissor{};
            scissor.offset = {static_cast<i32>(left), static_cast<i32>(top)};
            scissor.extent = {static_cast<u32>(right - left), static_cast<u32>(bottom - top)};

            VkPipeline want = (draw.blend == BlendMode::Additive) ? pipelineAdditive_
                                                                  : pipelineAlpha_;
            if (want == VK_NULL_HANDLE) continue;
            if (want != boundPipeline) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, want);
                boundPipeline = want;
            }

            // Textures carry their own descriptor set; shapes and anything
            // unrecognised use the white 1x1 through this frame's set.
            TextureRecord* texture = nullptr;
            if (draw.textureId > 1) texture = ResolveTexture(draw.textureId);
            const VkDescriptorSet set =
                texture != nullptr ? texture->descriptorSet : image.descriptorSet;
            const VkImageView view = texture != nullptr ? texture->view : whiteView_;
            if (!BindTexture(cmd, set, view, currentImage_)) continue;

            if (!scissorValid || boundScissor.offset.x != scissor.offset.x ||
                boundScissor.offset.y != scissor.offset.y ||
                boundScissor.extent.width != scissor.extent.width ||
                boundScissor.extent.height != scissor.extent.height) {
                vkCmdSetScissor(cmd, 0, 1, &scissor);
                boundScissor = scissor;
                scissorValid = true;
            }

            vkCmdDrawIndexed(cmd, draw.indexCount, 1, static_cast<u32>(draw.indexOffset), 0, 0);
            ++drawCallCount_;
        }
    }

    // Recorded even with no geometry: the render pass is what clears the
    // target, and an empty UI frame still has to paint its background.
    vkCmdEndRenderPass(cmd);
    r = vkEndCommandBuffer(cmd);
    if (r != VK_SUCCESS) { Fail("vkEndCommandBuffer", r); return; }

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    if (!offscreenTarget_) {
        // Wait for the acquire. This also makes it legal to touch the
        // image at all: the fence above only guarantees our *previous* frame is
        // done, not that the presentation engine has released the image.
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &image.acquireSemaphore;
        si.pWaitDstStageMask = &waitStage;
        // Only signal a present semaphore we are actually going to wait on. An
        // adopted swapchain is presented by the host, and a binary semaphore
        // that nobody waits on would still be signalled next frame - which is
        // not a legal thing to do to it.
        if (ownsSwapchain_) {
            si.signalSemaphoreCount = 1;
            si.pSignalSemaphores = &image.presentSemaphore;
        }
    }
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;

    r = vkQueueSubmit(queue_, 1, &si, image.fence);
    if (r != VK_SUCCESS) { Fail("vkQueueSubmit", r); return; }
    frameSubmitted_ = true;
}

bool VulkanBackend::Present() {
    if (!ownsSwapchain_ || swapChain_ == VK_NULL_HANDLE) return false;
    if (!frameBegun_ || !frameSubmitted_ || pfnQueuePresentKHR == nullptr) return false;

    const ImageResource& image = images_[currentImage_];
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSwapchainKHR swapChain = swapChain_;

    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &image.presentSemaphore;
    pi.pWaitDstStageMask = &waitStage;
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapChain;

    const VkResult r = pfnQueuePresentKHR(queue_, &pi);
    // The wait semaphore has been consumed one way or another; a second
    // Present() in the same frame must not wait on it again.
    frameSubmitted_ = false;
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
        swapchainOutOfDate_ = true;
        return true;
    }
    if (r != VK_SUCCESS) {
        Fail("vkQueuePresentKHR", r);
        return false;
    }
    return true;
}

void VulkanBackend::EndFrame() {
    // Nothing is allocated per frame, and an adopted device must be left as we
    // found it: every object we touched belongs to this backend and is rebuilt
    // or rebound at the start of the next frame.
    frameBegun_ = false;
    frameSubmitted_ = false;
    currentCommandBuffer_ = VK_NULL_HANDLE;
}

// ---------------------------------------------------------------------------
// Shutdown
// ---------------------------------------------------------------------------

void VulkanBackend::Shutdown() {
    DestroyTarget();

    for (auto& kv : textures_) DestroyTextureRecord(kv.second);
    textures_.clear();
    whiteView_ = VK_NULL_HANDLE;
    nextTextureId_ = 2;

    if (vertexMapped_ != nullptr && vertexMemory_ != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE) {
        vkUnmapMemory(device_, vertexMemory_);
    }
    vertexMapped_ = nullptr;
    if (indexMapped_ != nullptr && indexMemory_ != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE) {
        vkUnmapMemory(device_, indexMemory_);
    }
    indexMapped_ = nullptr;
    DestroyBuffer(&vertexBuffer_, &vertexMemory_);
    DestroyBuffer(&indexBuffer_, &indexMemory_);
    vertexCapacity_ = 0;
    indexCapacity_ = 0;

    DestroyPipelines();
    if (device_ != VK_NULL_HANDLE) {
        if (sampler_ != VK_NULL_HANDLE) vkDestroySampler(device_, sampler_, nullptr);
        if (pipelineLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        if (setLayout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device_, setLayout_, nullptr);
        // The pool owns every descriptor set that is still alive; freeing it
        // last means nothing above ever referenced a dead set.
        if (descriptorPool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        if (vsModule_ != VK_NULL_HANDLE) vkDestroyShaderModule(device_, vsModule_, nullptr);
        if (fsModule_ != VK_NULL_HANDLE) vkDestroyShaderModule(device_, fsModule_, nullptr);
        if (commandPool_ != VK_NULL_HANDLE) vkDestroyCommandPool(device_, commandPool_, nullptr);
    }
    sampler_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
    setLayout_ = VK_NULL_HANDLE;
    descriptorPool_ = VK_NULL_HANDLE;
    vsModule_ = VK_NULL_HANDLE;
    fsModule_ = VK_NULL_HANDLE;
    commandPool_ = VK_NULL_HANDLE;

    if (ownsDevice_ && device_ != VK_NULL_HANDLE) vkDestroyDevice(device_, nullptr);
    device_ = VK_NULL_HANDLE;
    queue_ = VK_NULL_HANDLE;
    physicalDevice_ = VK_NULL_HANDLE;
    ownsDevice_ = false;

    if (ownsSurface_ && surface_ != VK_NULL_HANDLE && pfnDestroySurfaceKHR != nullptr) {
        pfnDestroySurfaceKHR(instance_, surface_, nullptr);
    }
    surface_ = VK_NULL_HANDLE;
    ownsSurface_ = false;

    if (debugMessenger_ != VK_NULL_HANDLE && pfnDestroyDebugUtilsMessengerEXT != nullptr) {
        pfnDestroyDebugUtilsMessengerEXT(instance_, debugMessenger_, nullptr);
    }
    debugMessenger_ = VK_NULL_HANDLE;

    if (ownsInstance_ && instance_ != VK_NULL_HANDLE) vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
    ownsInstance_ = false;
    validationEnabled_ = false;

    deviceProperties_ = VkPhysicalDeviceProperties{};
    memoryProperties_ = VkPhysicalDeviceMemoryProperties{};
    frameBegun_ = false;
    frameSubmitted_ = false;
    drawCallCount_ = 0;
}

// ---------------------------------------------------------------------------

IArgonBackend* CreateVulkanBackend() { return new VulkanBackend(); }

} // namespace argon::ui

