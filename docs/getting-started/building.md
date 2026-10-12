# Building the library

## Configure

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Binaries land in `build/bin`.

## Options

| Option | Default | Effect |
|---|---|---|
| `ARGONUI_BUILD_D3D11` | ON | Build the Direct3D 11 backend. |
| `ARGONUI_BUILD_D3D12` | ON | Build the Direct3D 12 backend. |
| `ARGONUI_BUILD_VULKAN` | ON | Build the Vulkan backend. Needs the SDK. |
| `ARGONUI_BUILD_OPENGL` | ON | Build the OpenGL 3.3 backend. |
| `ARGONUI_BUILD_DEMO` | ON | Build the demo. Windows only. |
| `ARGONUI_WARNINGS_AS_ERRORS` | OFF | `-Werror` or `/WX`. |
| `ARGON_VK_SURFACE_EXT` | `win32` | `xcb`, `xlib` or `wayland`. |
| `ARGONUI_SHARED` | OFF | Reserved. |

Turning a backend off removes it from `AvailableBackends()` and from
`SelectBackend()`'s preference order, but nothing else changes. The core does
not know which backends exist.

## Turning warnings into errors

```bash
cmake -B build -DARGONUI_WARNINGS_AS_ERRORS=ON
```

The core is written to compile clean at `/W4` and at `-Wall -Wextra -Wpedantic`.
Backends are looser where a platform header forces it. If you hit a warning in
the core during ordinary use, that is a bug worth reporting.

## Platform specifics

### Windows

No SDK discovery is needed for D3D11 or D3D12, CMake finds the system SDK. The
Vulkan backend needs the Vulkan SDK; without it the target builds but does not
link and CMake prints a warning.

### Linux

D3D backends are still built but will not link without the DirectX headers.
Turn them off:

```bash
cmake -B build \
  -DARGONUI_BUILD_D3D11=OFF \
  -DARGONUI_BUILD_D3D12=OFF \
  -DARGON_VK_SURFACE_EXT=xcb
```

The demo is Windows-only and gated on `WIN32`.

## Shaders

There is no build-time shader compilation for any backend. Each one compiles or
loads at runtime:

| Backend | Source | How |
|---|---|---|
| D3D11, D3D12 | HLSL string in `backends/common/ArgonHLSL.h` | `D3DCompile` at startup |
| OpenGL | GLSL string in `backends/opengl/GLBackend.cpp` | `glCreateShader` at startup |
| Vulkan | GLSL string in `backends/vulkan/VulkanBackend.cpp` | needs precompiled SPIR-V |

The Vulkan case is the exception because Vulkan requires SPIR-V and there is no
standard way to get it at runtime without linking a compiler. The backend
searches, in order:

1. SPIR-V supplied through `VulkanBackend::SetShaderSpv()`
2. Paths set with `VulkanBackend::SetShaderPaths()`
3. `$ARGONUI_VULKAN_SHADER_DIR/argonui.vert.spv` and `.frag.spv`
4. `shaders/` next to the executable
5. The bare filenames in the working directory

To generate those files from the GLSL in the backend source, extract the two
string literals and run `glslc`:

```bash
glslc -O -fshader-stage=vertex   argon.vert.glsl -o argonui.vert.spv
glslc -O -fshader-stage=fragment argon.frag.glsl -o argonui.frag.spv
```

`.gitignore` excludes `*.spv`, so if you check the results in for a platform
that cannot run `glslc`, use `git add -f`.

`shaders/argonui.hlsl` is the reference shader all four are ports of. If you
change one, change all four, or the backends render differently.

## Verifying the build

There is no test suite yet. Two cheap smoke checks once you have a binary:

- Run the demo. `Ctrl+?` is not wired up; the footer has a metrics toggle that
  prints draw calls, quad counts and upload size in the corner.
- Drive a `Context` with `GraphicsApi::Null`. It accepts everything and renders
  nothing, which lets you exercise layout and widget logic without a GPU. Useful
  for a headless layout regression test.

## Next

- [Your first window](first-window.md)
- [Choosing a backend](../guides/choosing-a-backend.md)