// ArgonUI - Backend/Registry.h
//
// Runtime backend selection. Each backend lives in its own static library
// target; CMake decides which ones exist, and this registry exposes the ones
// that were compiled in. Applications can pick by enum, by name, or by env
// var (ARGONUI_BACKEND=d3d11|vulkan|gl|d3d12) without touching link config.
#pragma once

#include <string>
#include <vector>

#include "Backend.h"

namespace argon::ui {

/// Creates a backend instance. Returns nullptr when that backend was not
/// compiled in. Caller owns the result and must delete it (or pass it to
/// Context::Shutdown()).
IArgonBackend* CreateBackend(GraphicsApi api);

bool IsBackendAvailable(GraphicsApi api);

/// Every API compiled into this binary, in preference order for overlays.
std::vector<GraphicsApi> AvailableBackends();

const char* GraphicsApiName(GraphicsApi api);

/// Case-insensitive: "d3d11", "dx11", "vulkan", "vk", "gl", "opengl", "d3d12",
/// "dx12", "null".
GraphicsApi ParseGraphicsApi(const char* name);

/// Picks a backend from, in order: `preferred` if non-None, the
/// ARGONUI_BACKEND environment variable, then the first available.
GraphicsApi SelectBackend(GraphicsApi preferred = GraphicsApi::None);

} // namespace argon::ui