# Contributing

## Build it before you send it

ArgonUI has not been compiled yet, so the first person to build it will hit
rough edges. Run a Release build and fix what falls out before opening a pull
request.

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Turn warnings into errors while you work:

```
cmake -B build -DARGONUI_WARNINGS_AS_ERRORS=ON
```

The core is warning-clean at `/W4` and `-Wall -Wextra -Wpedantic`. Keep it that
way. Backends may be a little looser where a platform header forces it.

## Code style

- 4-space indent, no tabs.
- `u32`, `i32`, `f32`, `f64`, `u8` and friends from `argonui/Core/Types.h`.
  Never bare `unsigned`, `int`, `float`, `unsigned char`.
- Member variables carry no `m_` prefix. Trailing member initialisers.
- 100 column soft limit.
- Braces on their own line for functions and namespaces.
- Prefer `Min`, `Max`, `Clamp`, `Clamp01`, `Lerp`, `Damp` from `Types.h` over
  open-coding the comparison.

## Comment style

Comments should explain why, not what. A comment restating the line below it is
noise and will be deleted in review.

- Keep them in English and ASCII. No em dashes, no smart quotes, no box drawing
  characters. Use `-` or restructure the sentence.
- Avoid constructions that read like filler: "Note that", "this is what makes",
  "simply", "just". They add length without adding information.
- When something is surprising, say what surprises you and why it is that way.
  "ResizeBuffers fails if any back-buffer reference is still alive, so release
  the RTV first" earns its place. "Release the RTV" does not.
- Prefer several short comments over one long one.

## The vertex format is a contract

`argon::ui::Vertex` is 64 bytes and its field offsets are baked into four
different places: the D3D11 input layout, the D3D12 input layout, the Vulkan
`VkVertexInputAttributeDescription` array and the OpenGL VAO. All four also have
to agree with the GLSL/HLSL attribute declarations in their own source.

If you change the vertex, change all of them in the same commit and update the
`static_assert` block in `Draw/Vertex.h`. A mismatch here renders a UI that looks
plausible and is subtly wrong everywhere.

## Backends

A backend is an independent static library and must not leak into the core.
Nothing under `include/argonui/` may include a GPU SDK header.

Backends must satisfy the same contract as the D3D11 implementation, which is
the reference:

- `Present()` returns `false` for adopted targets. The host presents.
- `BeginFrame()` returns `false` when there is no device, no render target, the
  frame is already open, or the frame is occluded.
- `LastError()` is non-empty only when something is actually wrong.
- Adopted objects are never released in `Shutdown()`.
- Output is premultiplied alpha with blend `(ONE, ONE_MINUS_SRC_ALPHA)`.

## Commit messages

Imperative mood, one subject line under 72 characters, blank line, then the
detail. Say what changed and why, not what the diff already shows.

```
Add frame-rate independent animation curves

Widgets were lerping against a fixed 0.1 step, which made transitions
run at different speeds on a 144 Hz display and a 60 Hz one. Replaced
with exponential damping seeded per widget.
```

## Reporting bugs

A repro is worth a hundred lines of description. Useful to include: ArgonUI
version, backend and version string from `IArgonBackend::Info()`, GPU, display
DPI, and whether a debug build of the backend reports anything.