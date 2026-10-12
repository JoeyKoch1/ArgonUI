#include "argonui/Core/Log.h"

#include <cstdio>

namespace argon::ui {

namespace {
const char* LevelTag(LogLevel l) {
    switch (l) {
    case LogLevel::Trace: return "TRACE";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info:  return "INFO ";
    case LogLevel::Warn:  return "WARN ";
    case LogLevel::Error: return "ERROR";
    default:              return "?????";
    }
}
} // namespace

Log& Log::Get() {
    static Log instance;
    return instance;
}

void Log::Write(LogLevel level, const char* tag, const std::string& message) {
    if (level < minLevel_ || minLevel_ == LogLevel::Off) return;

    LogEntry entry;
    entry.level = level;
    entry.message = message;
    entry.tag = tag ? tag : "";

    if (mirror_) {
        std::fprintf(stderr, "[ArgonUI][%s][%s] %s\n", LevelTag(level), entry.tag.c_str(),
                     entry.message.c_str());
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (ring_.size() >= capacity_) {
        ring_.erase(ring_.begin());
    }
    ring_.push_back(std::move(entry));
}

std::vector<LogEntry> Log::Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ring_;
}

void Log::Clear() const {
    std::lock_guard<std::mutex> lock(mutex_);
    ring_.clear();
}

} // namespace argon::ui