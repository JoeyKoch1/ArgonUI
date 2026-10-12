// ArgonUI - Core/Log.h
// Minimal logging with an in-memory ring so a runtime overlay can show its own
// console (very handy when the overlay is the only thing you can actually see).
#pragma once

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include "Color.h"
#include "Types.h"

namespace argon::ui {

enum class LogLevel : u8 { Trace = 0, Debug, Info, Warn, Error, Off };

struct LogEntry {
    LogLevel level = LogLevel::Info;
    std::string message;
    std::string tag;
};

/// Process-wide logger. Thread-safe enough for overlay use (mutex guarded).
class Log {
public:
    static Log& Get();

    void SetMinLevel(LogLevel level) { minLevel_ = level; }
    LogLevel MinLevel() const { return minLevel_; }
    void SetMirrorToStdout(bool on) { mirror_ = on; }

    void Write(LogLevel level, const char* tag, const std::string& message);

    /// Snapshot of the ring buffer, oldest first.
    std::vector<LogEntry> Snapshot() const;
    void Clear() const;
    size_t RingCapacity() const { return capacity_; }

private:
    Log() = default;

    LogLevel minLevel_ = LogLevel::Info;
    bool mirror_ = true;
    size_t capacity_ = 512;
    mutable std::mutex mutex_;
    mutable std::vector<LogEntry> ring_;
};

namespace detail {

/// Formats `std::string msg = fmt; return msg;` on the way through so we can
/// keep the call sites terse without dragging <sstream> into every header.
inline void Appendf(std::string& out, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char stackBuf[1024];
    const int needed = vsnprintf(stackBuf, sizeof(stackBuf), fmt, args);
    va_end(args);
    if (needed < 0) return;
    if (static_cast<size_t>(needed) < sizeof(stackBuf)) {
        out.append(stackBuf, static_cast<size_t>(needed));
        return;
    }
    out.resize(static_cast<size_t>(needed) + 1);
    va_start(args, fmt);
    vsnprintf(&out[0], out.size(), fmt, args);
    va_end(args);
    out.resize(static_cast<size_t>(needed));
}

} // namespace detail

} // namespace argon::ui

// Convenience macros. ARGONUI_STRIP_LOGS can remove them entirely in release.
#if defined(ARGONUI_STRIP_LOGS)
#define ARGON_TRACE(tag, ...) (void)0
#define ARGON_DEBUG(tag, ...) (void)0
#define ARGON_INFO(tag, ...) (void)0
#define ARGON_WARN(tag, ...) (void)0
#define ARGON_ERROR(tag, ...) (void)0
#else
#define ARGON_TRACE(tag, ...)                                                     \
    do {                                                                          \
        if (argon::ui::Log::Get().MinLevel() <= argon::ui::LogLevel::Trace) {      \
            std::string msg;                                                      \
            argon::ui::detail::Appendf(msg, __VA_ARGS__);                          \
            argon::ui::Log::Get().Write(argon::ui::LogLevel::Trace, tag, msg);     \
        }                                                                         \
    } while (0)
#define ARGON_DEBUG(tag, ...)                                                     \
    do {                                                                          \
        if (argon::ui::Log::Get().MinLevel() <= argon::ui::LogLevel::Debug) {      \
            std::string msg;                                                      \
            argon::ui::detail::Appendf(msg, __VA_ARGS__);                          \
            argon::ui::Log::Get().Write(argon::ui::LogLevel::Debug, tag, msg);     \
        }                                                                         \
    } while (0)
#define ARGON_INFO(tag, ...)                                                      \
    do {                                                                          \
        if (argon::ui::Log::Get().MinLevel() <= argon::ui::LogLevel::Info) {       \
            std::string msg;                                                      \
            argon::ui::detail::Appendf(msg, __VA_ARGS__);                          \
            argon::ui::Log::Get().Write(argon::ui::LogLevel::Info, tag, msg);      \
        }                                                                         \
    } while (0)
#define ARGON_WARN(tag, ...)                                                      \
    do {                                                                          \
        if (argon::ui::Log::Get().MinLevel() <= argon::ui::LogLevel::Warn) {       \
            std::string msg;                                                      \
            argon::ui::detail::Appendf(msg, __VA_ARGS__);                          \
            argon::ui::Log::Get().Write(argon::ui::LogLevel::Warn, tag, msg);      \
        }                                                                         \
    } while (0)
#define ARGON_ERROR(tag, ...)                                                     \
    do {                                                                          \
        if (argon::ui::Log::Get().MinLevel() <= argon::ui::LogLevel::Error) {      \
            std::string msg;                                                      \
            argon::ui::detail::Appendf(msg, __VA_ARGS__);                          \
            argon::ui::Log::Get().Write(argon::ui::LogLevel::Error, tag, msg);     \
        }                                                                         \
    } while (0)
#endif