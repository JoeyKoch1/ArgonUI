// ArgonUI - Core/Version.h
#pragma once

#define ARGONUI_VERSION_MAJOR 0
#define ARGONUI_VERSION_MINOR 1
#define ARGONUI_VERSION_PATCH 0
#define ARGONUI_VERSION "0.1.0"

#define ARGONUI_VERSION_NUM                                                      \
    ((ARGONUI_VERSION_MAJOR << 16) | (ARGONUI_VERSION_MINOR << 8) | ARGONUI_VERSION_PATCH)

/// Monotonic integer, bumped whenever the DrawList/Vertex/Backend contract
/// changes in a way that requires every backend to be recompiled.
#define ARGONUI_ABI_VERSION 1
