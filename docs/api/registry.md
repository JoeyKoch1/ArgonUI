# Backend registry

The registry is how an application picks a backend at runtime instead of
hardcoding one at link time. Each backend lives in its own static library
target; CMake decides which ones exist, and this page's functions expose the
ones that were compiled in. Applications can select by enum, by name, or by the
`ARGONUI_BACKEND` environment variable without touching their link
configuration.

This page documents [`include/argonui/Backend/Registry.h`](../../include/argonui/Backend/Registry.h).

## Selecting a backend

`SelectBackend` resolves a choice from three sources, in order: an explicit
`preferred` argument (when it is not `GraphicsApi::None`), the
`ARGONUI_BACKEND` environment variable, then the first entry in
`AvailableBackends()`.

```cpp
GraphicsApi api = SelectBackend();          // env var, then first available
GraphicsApi d3d = SelectBackend(GraphicsApi::D3D11); // explicit, if compiled in
```

Setting `ARGONUI_BACKEND=vulkan` forces Vulkan at runtime without recompiling.
If the named API was not compiled in, or the name does not parse, selection falls
through to the next source rather than failing.

**Accepted `ARGONUI_BACKEND` names**, matched case-insensitively by
`ParseGraphicsApi`:

| Name | Result |
|---|---|
| `d3d11`, `dx11` | `GraphicsApi::D3D11` |
| `d3d12`, `dx12` | `GraphicsApi::D3D12` |
| `vulkan`, `vk` | `GraphicsApi::Vulkan` |
| `gl`, `opengl`, `glfw` | `GraphicsApi::OpenGL` |
| `null`, `headless` | `GraphicsApi::Null` |
| anything else, or empty | `GraphicsApi::None` |

{% hint style="info" %}
`GraphicsApi::None` is the "not recognized" result, not an error. `SelectBackend`
treats a `None` from the environment variable as "ignore and keep looking".
{% endhint %}

## API

### `CreateBackend`

```cpp
IArgonBackend* CreateBackend(GraphicsApi api);
```

Creates a backend instance for `api`.

**Returns:** a new `IArgonBackend*`, or `nullptr` when that backend was not
compiled in. The caller owns the result and must `delete` it or pass it to
`Context::Shutdown()`.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `api` | `GraphicsApi` | | Which backend to instantiate. |

{% hint style="warning" %}
The returned pointer is heap allocated. Deleting it is the caller's job, and
leaking it keeps the GPU device alive. `CreateNullBackend` always succeeds, so a
null return really does mean the API was not compiled in.
{% endhint %}

**Example**

```cpp
IArgonBackend* backend = CreateBackend(SelectBackend());
if (backend == nullptr) {
    // No GPU backend in this build.
    return;
}
// ... use backend, then delete backend.
```

### `IsBackendAvailable`

```cpp
bool IsBackendAvailable(GraphicsApi api);
```

Reports whether a backend for `api` was compiled into this binary.

**Returns:** true if `CreateBackend(api)` would succeed.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `api` | `GraphicsApi` | | The API to test. |

`GraphicsApi::Null` is always available. `GraphicsApi::None` is never available.

### `AvailableBackends`

```cpp
std::vector<GraphicsApi> AvailableBackends();
```

Every API compiled into this binary, in preference order for overlays.

**Returns:** a vector of available APIs. DirectX backends come first because
they dominate in-game use; OpenGL is the portable fallback. `GraphicsApi::Null`
is always the last entry.

### `GraphicsApiName`

```cpp
const char* GraphicsApiName(GraphicsApi api);
```

Display name for an API.

**Returns:** a static string such as `"D3D11"`, `"Vulkan"`, `"OpenGL"`,
`"Null"`, or `"None"` for `GraphicsApi::None`. The pointer is to string
literals and stays valid forever.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `api` | `GraphicsApi` | | The API to name. |

### `ParseGraphicsApi`

```cpp
GraphicsApi ParseGraphicsApi(const char* name);
```

Parses a case-insensitive name into a `GraphicsApi`.

**Returns:** the matching enum, or `GraphicsApi::None` if `name` is null, empty,
or unrecognized.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `name` | `const char*` | | The string to parse. |

### `SelectBackend`

```cpp
GraphicsApi SelectBackend(GraphicsApi preferred = GraphicsApi::None);
```

Resolves a backend choice from, in order: `preferred` if non-None, the
`ARGONUI_BACKEND` environment variable, then the first available backend.

**Returns:** the chosen API, or `GraphicsApi::None` if nothing is available.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `preferred` | `GraphicsApi` | `GraphicsApi::None` | An explicit first choice. Honored only if non-None and actually compiled in. |

{% hint style="info" %}
An explicit `preferred` that was not compiled in does not stop the search; it
falls through to the environment variable and then to the first available
backend. Pass `GraphicsApi::None` to opt out of the explicit preference entirely.
{% endhint %}

## See also

- [Backend interface](backend.md) - the `IArgonBackend` a registry creates.
- [Choosing a backend](../guides/choosing-a-backend.md) - picking one for a project.