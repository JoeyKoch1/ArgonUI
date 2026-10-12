# Installation

ArgonUI has three layers and you choose how much of it to take.

| Target | What you get | What it needs |
|---|---|---|
| `argonui_core` | The whole UI. Draw list, widgets, text, theming. | A C++17 compiler. No GPU SDK. |
| `argonui-d3d11`, `-d3d12`, `-vulkan`, `-opengl` | One backend each. | That graphics API's SDK. |
| `argonui_demo` | A runnable Win32 application. | Direct3D 11, on Windows. |

The core never links a GPU SDK. That is deliberate: it means the UI compiles on
a build server with no graphics drivers, and it means swapping graphics API is a
link-time decision rather than a recompile of the interface layer.

## Requirements

- A C++17 compiler. MSVC 2019 or newer, GCC 8 or newer, Clang 7 or newer.
- CMake 3.20 or newer.
- A windowing system you already have, or Win32 directly. ArgonUI opens no
  windows of its own.

## Cloning

```bash
git clone https://github.com/JoeyKoch1/ArgonUI.git
cd ArgonUI
```

## Integrating into a CMake project

The supported path is `add_subdirectory`. The `ARGONUI_BUILD_*` options let you
disable backends you do not want to compile.

```cmake
set(ARGONUI_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(ARGONUI_BUILD_VULKAN OFF CACHE BOOL "" FORCE)   # skip if no Vulkan SDK

add_subdirectory(third_party/ArgonUI)

target_link_libraries(my_app PRIVATE argonui_core argonui-d3d11)
```

Add the source tree to your project as a git submodule so you can track a known
revision:

```bash
git submodule add https://github.com/JoeyKoch1/ArgonUI.git third_party/ArgonUI
```

## Integrating without CMake

If your build does not use CMake, compile the sources under `src/` directly and
add `include/` and the repository root to your include path. Both are needed:
the core includes `argonui/...` for its own headers and `backends/common/...`
for the shared shader source.

```
include/
    <your include path>
<ArgonUI root>/
    <your include path>
third_party/stb/
    <your include path>   (stb_truetype.h only)
```

Sources to compile, all of `src/**/*.cpp`. Nothing under `src/` includes a GPU
SDK header, so this works on any platform.

## Linking against a backend

Each backend is its own static library. Link the ones you need:

```cmake
if(WIN32)
    target_link_libraries(my_app PRIVATE argonui-d3d11 d3d11 dxgi d3dcompiler)
endif()
```

On Windows the D3D11 backend additionally needs `d3dcompiler` because it
compiles the shader at runtime. There is no build-time shader step for any
backend, which is what keeps the source tree free of generated files.

## Selecting a backend at runtime

You do not have to hard-code the choice. `SelectBackend()` honours an
environment variable, so the same binary can be pointed at a different API
without a rebuild.

```bash
set ARGONUI_BACKEND=vulkan
set ARGONUI_BACKEND=d3d11
```

See [Choosing a backend](../guides/choosing-a-backend.md) for the full decision guide.

## Next

- [Building the library](building.md) for per-backend build details
- [Your first window](first-window.md) for a complete working application
- [Backend ownership](../concepts/backend-ownership.md) before you write any
  integration code