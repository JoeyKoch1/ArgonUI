#include "argonui/Backend/Registry.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

#include "argonui/Core/Log.h"

namespace argon::ui {
#if defined(ARGON_WITH_D3D11)
IArgonBackend* CreateD3D11Backend();
#endif
#if defined(ARGON_WITH_D3D12)
IArgonBackend* CreateD3D12Backend();
#endif
#if defined(ARGON_WITH_VULKAN)
IArgonBackend* CreateVulkanBackend();
#endif
#if defined(ARGON_WITH_OPENGL)
IArgonBackend* CreateOpenGLBackend();
#endif
IArgonBackend* CreateNullBackend();

const char* GraphicsApiName(GraphicsApi api) {
    switch (api) {
    case GraphicsApi::D3D11:   return "D3D11";
    case GraphicsApi::D3D12:   return "D3D12";
    case GraphicsApi::Vulkan:  return "Vulkan";
    case GraphicsApi::OpenGL:  return "OpenGL";
    case GraphicsApi::Null:    return "Null";
    case GraphicsApi::None:    break;
    }
    return "None";
}

GraphicsApi ParseGraphicsApi(const char* name) {
    if (name == nullptr || name[0] == '\0') return GraphicsApi::None;
    std::string lowered;
    lowered.reserve(std::strlen(name));
    for (const char* p = name; *p; ++p) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*p))));
    }
    if (lowered == "d3d11" || lowered == "dx11") return GraphicsApi::D3D11;
    if (lowered == "d3d12" || lowered == "dx12") return GraphicsApi::D3D12;
    if (lowered == "vulkan" || lowered == "vk") return GraphicsApi::Vulkan;
    if (lowered == "gl" || lowered == "opengl" || lowered == "glfw") return GraphicsApi::OpenGL;
    if (lowered == "null" || lowered == "headless") return GraphicsApi::Null;
    return GraphicsApi::None;
}

bool IsBackendAvailable(GraphicsApi api) {
    switch (api) {
#if defined(ARGON_WITH_D3D11)
    case GraphicsApi::D3D11: return true;
#endif
#if defined(ARGON_WITH_D3D12)
    case GraphicsApi::D3D12: return true;
#endif
#if defined(ARGON_WITH_VULKAN)
    case GraphicsApi::Vulkan: return true;
#endif
#if defined(ARGON_WITH_OPENGL)
    case GraphicsApi::OpenGL: return true;
#endif
    case GraphicsApi::Null: return true;
    default: return false;
    }
}

std::vector<GraphicsApi> AvailableBackends() {
    // Ordered by how suitable each is for an in-game overlay: the DirectX
    // backends dominate real usage, GL is the portable fallback.
    std::vector<GraphicsApi> out;
#if defined(ARGON_WITH_D3D11)
    out.push_back(GraphicsApi::D3D11);
#endif
#if defined(ARGON_WITH_D3D12)
    out.push_back(GraphicsApi::D3D12);
#endif
#if defined(ARGON_WITH_VULKAN)
    out.push_back(GraphicsApi::Vulkan);
#endif
#if defined(ARGON_WITH_OPENGL)
    out.push_back(GraphicsApi::OpenGL);
#endif
    out.push_back(GraphicsApi::Null);
    return out;
}

IArgonBackend* CreateBackend(GraphicsApi api) {
    IArgonBackend* backend = nullptr;
    switch (api) {
#if defined(ARGON_WITH_D3D11)
    case GraphicsApi::D3D11:   backend = CreateD3D11Backend(); break;
#endif
#if defined(ARGON_WITH_D3D12)
    case GraphicsApi::D3D12:   backend = CreateD3D12Backend(); break;
#endif
#if defined(ARGON_WITH_VULKAN)
    case GraphicsApi::Vulkan:  backend = CreateVulkanBackend(); break;
#endif
#if defined(ARGON_WITH_OPENGL)
    case GraphicsApi::OpenGL:  backend = CreateOpenGLBackend(); break;
#endif
    case GraphicsApi::Null:    backend = CreateNullBackend(); break;
    default:                   backend = nullptr; break;
    }

    if (backend == nullptr) {
        ARGON_WARN("Registry", "backend '%s' requested but not compiled in", GraphicsApiName(api));
    }
    return backend;
}

GraphicsApi SelectBackend(GraphicsApi preferred) {
    if (preferred != GraphicsApi::None && IsBackendAvailable(preferred)) {
        return preferred;
    }
    if (const char* env = std::getenv("ARGONUI_BACKEND")) {
        const GraphicsApi parsed = ParseGraphicsApi(env);
        if (parsed != GraphicsApi::None && IsBackendAvailable(parsed)) {
            ARGON_INFO("Registry", "backend forced by ARGONUI_BACKEND: %s", GraphicsApiName(parsed));
            return parsed;
        }
    }
    const std::vector<GraphicsApi> all = AvailableBackends();
    return all.empty() ? GraphicsApi::None : all.front();
}

} // namespace argon::ui
