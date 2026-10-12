# Backend selection

ArgonUI ships four backends. They are four independent static libraries, so you
can link one, several, or all of them. This page is for working out which.

## At a glance

| | D3D11 | D3D12 | Vulkan | OpenGL 3.3 |
|---|---|---|---|---|
| Setup cost | none | none | needs the SDK | none |
| Own window | yes | yes | yes | host provides the context |
| Adopt a host device | yes | yes | yes | no, contexts are per-thread |
| Adopt a host swapchain | yes | yes | yes | FBO only |
| MSAA | swapchain | not implemented | not implemented | host's choice |
| Shader source | HLSL, runtime | HLSL, runtime | SPIR-V, precompiled | GLSL, runtime |
| Overhead | lowest | lowest | medium | lowest |
| Where it wins | DX11 games | DX12 games | Vulkan games, Linux | portability, tooling |

## The common case

For a D3D11 or D3D12 game, use the matching DirectX backend. It is what the
reference UI was built against, it has the fewest moving parts, and adopting a
host device and swapchain is straightforward.

```cpp
DeviceDesc d;
d.api            = GraphicsApi::D3D11;
d.deviceSource   = DeviceSource::External;
d.externalDevice  = gameDevice;
d.externalContext = gameImmediateContext;

TargetDesc t;
t.targetSource     = TargetSource::ExternalSwapchain;
t.externalSwapchain = gameSwapChain;

backend->Initialize(d);
backend->AdoptTarget(t);
```

## Vulkan

The only backend that needs extra setup, because Vulkan has no standard way to
compile GLSL at runtime. It loads precompiled SPIR-V from:

1. `SetShaderSpv()`, if you embedded the binaries
2. `SetShaderPaths()`, if you know where they are
3. `$ARGONUI_VULKAN_SHADER_DIR`
4. `shaders/` next to the executable
5. the bare filenames in the working directory

If none of those resolve, `Initialize` fails and `LastError()` lists every path
it tried. See [Building the library](../getting-started/building.md#shaders) for
generating the SPIR-V.

Vulkan is also the only backend that targets Linux and macOS out of the box. Set
`ARGON_VK_SURFACE_EXT` at configure time to pick the window system.

Its handling of adopted targets is the most careful of the four. Acquired
images use a `LOAD` loadOp so a frame the host just drew survives, and acquire
has a one second timeout so a stalled compositor drops a frame instead of
hanging the host.

## OpenGL 3.3

Useful for portability, for Linux, and for running the menu in a browser-hosted
canvas or a remote desktop where DirectX is unavailable.

Two constraints:

**The context must already be current.** The backend probes and uses the context
on the calling thread rather than creating one, which suits a host that owns its
window system. `DeviceSource::CreateInternal` can build a 3.3 core context via
WGL if you want it to.

**It never presents.** GL has no swapchain abstraction; the host's window system
owns that. `Present()` always returns false. Call `SwapBuffers` yourself.

If your host's default framebuffer is multisampled, the analytic AA in the
shader double-filters and edges look soft. The backend warns at target creation.

## Runtime selection

```cpp
GraphicsApi api = SelectBackend(GraphicsApi::None);
IArgonBackend* backend = CreateBackend(api);
```

`SelectBackend` takes an explicit preference if you pass one, otherwise reads
`ARGONUI_BACKEND`, otherwise picks the first compiled-in API in the order D3D11,
D3D12, Vulkan, OpenGL, Null.

```bash
set ARGONUI_BACKEND=vulkan      # d3d11 | d3d12 | vulkan | gl | null
```

`CreateBackend` returns null for anything not compiled in. `IsBackendAvailable`
asks without constructing, and `AvailableBackends()` returns the list.

## Null

Accepts every call and renders nothing. Useful for:

- measuring the CPU cost of your widget tree without a GPU
- headless layout regression tests
- exercising code paths that would otherwise need a display

```cpp
auto* backend = CreateBackend(GraphicsApi::Null);
Context ctx;
ctx.Initialize(backend);   // configure.submitAutomatically = false
// ... draw a frame ...
assert(ctx.GetDrawList().Stats().drawCalls < 20);
```

## Multiple backends in one binary

Link all four, select at runtime. There is no cost beyond binary size, because
the core does not know any of them exist.

```cmake
target_link_libraries(my_app PRIVATE argonui_core argonui-d3d11 argonui-vulkan)
```

## Choosing on what you are attaching to

| You are drawing over | Use |
|---|---|
| a D3D11 game | D3D11 |
| a D3D12 game | D3D12 |
| a Vulkan game | Vulkan |
| a game you do not know the API of | query it, then pick |
| nothing, your own window | whatever links easiest on your platform |
| a headless test | Null |

## See also

- [Backend ownership](../concepts/backend-ownership.md)
- [Backend registry reference](../api/registry.md)