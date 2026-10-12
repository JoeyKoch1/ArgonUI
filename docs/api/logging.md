# Logging

ArgonUI's logger is a singleton that writes to stderr and keeps the last few
hundred lines in memory so a runtime overlay can display its own console. Read
this page if you are instrumenting a widget, or adding a console panel to a
debug overlay.

Source: [`include/argonui/Core/Log.h`](../../include/argonui/Core/Log.h),
implementation in [`src/Core/Log.cpp`](../../src/Core/Log.cpp).

## `LogLevel`

```cpp
enum class LogLevel : u8 { Trace = 0, Debug, Info, Warn, Error, Off };
```

Severity, ordered from most to least verbose. The underlying values are
sequential, so the numeric comparison in the filtering macros is meaningful.

| Enumerator | Value | Meaning |
|---|---|---|
| `Trace` | 0 | Per-frame detail: layout positions, batch merges. |
| `Debug` | 1 | Developer diagnostics that are off in a shipped build. |
| `Info` | 2 | Lifecycle events. The default threshold. |
| `Warn` | 3 | Recoverable problems the user might see. |
| `Error` | 4 | Failures. |
| `Off` | 5 | Nothing is written at any level. |

The values are unlisted past `Trace`, `Debug` and `Info`; read the name rather
than hardcoding a number.

## `LogEntry`

```cpp
struct LogEntry {
    LogLevel level = LogLevel::Info;
    std::string message;
    std::string tag;
};
```

One line in the ring.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `level` | `LogLevel` | `LogLevel::Info` | Severity the line was written at. |
| `message` | `std::string` | empty | The formatted text. |
| `tag` | `std::string` | empty | Caller-supplied category, or empty if a null tag was passed. |

## `Log`

```cpp
class Log {
public:
    static Log& Get();

    // ... public methods omitted, see below ...

private:
    Log() = default;

    LogLevel minLevel_ = LogLevel::Info;
    bool mirror_ = true;
    size_t capacity_ = 512;
    mutable std::mutex mutex_;
    mutable std::vector<LogEntry> ring_;
};
```

A process-wide singleton. The constructor is private, so there is exactly one
logger per process and no teardown ordering to get wrong. It has no destructor
logic and no other state.

Every method that touches the ring takes a `std::mutex`, so an overlay can read
the log from a render thread while widgets write to it from the thread driving
the UI.

### `Log::Get`

```cpp
static Log& Get();
```

**Returns:** a reference to the process-wide logger.

A function-local static, constructed on first call. Cheap enough to call from
every macro expansion.

### `Log::SetMinLevel`

```cpp
void SetMinLevel(LogLevel level) { minLevel_ = level; }
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `level` | `LogLevel` | | Lines below this are discarded. |

**Returns:** nothing, it changes the threshold.

Set this before the first frame. `LogLevel::Off` silences everything including
the mirror to stderr.

### `Log::MinLevel`

```cpp
LogLevel MinLevel() const { return minLevel_; }
```

**Returns:** the current threshold.

The macros call this on every use, so the comparison at the call site is what
keeps a disabled log from formatting its arguments at all. Raising the threshold
after startup costs a few hundred cycles per suppressed call site.

### `Log::SetMirrorToStdout`

```cpp
void SetMirrorToStdout(bool on) { mirror_ = on; }
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `on` | `bool` | | Whether to also print each line. |

**Returns:** nothing, it toggles the stderr mirror.

Despite the name this writes to `stderr`, formatted as
`[ArgonUI][LEVEL][tag] message`. It defaults to on. Ring entries are unaffected
either way.

### `Log::Write`

```cpp
void Write(LogLevel level, const char* tag, const std::string& message);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `level` | `LogLevel` | | Severity to record. |
| `tag` | `const char*` | | Category. A null pointer becomes an empty tag. |
| `message` | `const std::string&` | | Text to record. |

**Returns:** nothing, it appends to the ring and possibly prints.

Discarded before any allocation when `level` is below the threshold or the
threshold is `Off`. When the ring is at capacity the oldest entry is erased and
this one appended, so the ring is bounded at
[`Log::RingCapacity`](#logringcapacity) and never grows.

Prefer the macros over calling this directly: they check the threshold before
building the `std::string`, so a suppressed call site costs a comparison instead
of a heap allocation.

### `Log::Snapshot`

```cpp
std::vector<LogEntry> Snapshot() const;
```

**Returns:** a copy of the ring contents, oldest first.

Copies under the lock, so it is consistent but not free. Calling it every frame
for an on-screen console is fine at the default capacity; calling it in a loop
over widgets is not.

### `Log::Clear`

```cpp
void Clear() const;
```

**Returns:** nothing, it empties the ring.

`const` because clearing is a mutation of shared state the singleton holds for
everyone, not of the object. Does not reset the threshold or the mirror flag.

### `Log::RingCapacity`

```cpp
size_t RingCapacity() const { return capacity_; }
```

**Returns:** the maximum number of entries the ring holds, 512.

Fixed at construction. The ring is not a deque of segments, so it never grows to
absorb a burst; lines past this limit are discarded oldest-first.

## Formatting helper

### `argon::ui::detail::Appendf`

```cpp
inline void Appendf(std::string& out, const char* fmt, ...) {
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `out` | `std::string&` | | Appended to in place. |
| `fmt` | `const char*` | | `printf`-style format string. |

**Returns:** nothing, it appends to `out`.

Formats into a 1024-byte stack buffer and falls back to a heap resize only when
the result does not fit. This lives in a header and pulls in `<cstdarg>` rather
than `<sstream>`, so a call site stays one line and the include cost stays with
the users of the macros.

It is in the `detail` namespace and is not part of the supported surface. Call
it only if you are writing your own logging macro.

## Macros

`printf`-style convenience wrappers. The first argument is the tag, the rest is
the format string and its arguments.

| Macro | Level |
|---|---|
| `ARGON_TRACE(tag, ...)` | `LogLevel::Trace` |
| `ARGON_DEBUG(tag, ...)` | `LogLevel::Debug` |
| `ARGON_INFO(tag, ...)` | `LogLevel::Info` |
| `ARGON_WARN(tag, ...)` | `LogLevel::Warn` |
| `ARGON_ERROR(tag, ...)` | `LogLevel::Error` |

Each expands to a `do { ... } while (0)` block, so it is safe after an
unbraced `if`. Each begins by testing `Log::Get().MinLevel()` against its own
level and formats only if that passes, which means the format arguments are not
evaluated when the level is suppressed. They are still *compiled*, so every
argument passed must name a valid object.

{% hint style="warning" %}
Defining `ARGONUI_STRIP_LOGS` at compile time replaces all five macros with
`(void)0`. The format string and every argument vanish from the binary: no
comparison, no `std::string`, no call to `Log::Write`. Use it for shipping
builds that must not carry log text.
{% endhint %}

{% hint style="warning" %}
Because the arguments are discarded along with the format string, a macro
argument that computes something has no effect under `ARGONUI_STRIP_LOGS`. If a
log statement is load-bearing for behaviour, move the computation out and pass
the result in.
{% endhint %}

**Example**

```cpp
ARGON_INFO("panel", "BeginPanel('%s') bounds=(%.1f, %.1f %.1fx%.1f)",
           title, r.min.x, r.min.y, r.Width(), r.Height());
```

## See also

- [Draw list](draw-list.md) - batches are where tracing pays off.
- [Backend interface](backend.md) - where a submitted draw call actually goes.
- [Core types](core-types.md) - `Color`, which the tag-free header pull-in brings along.