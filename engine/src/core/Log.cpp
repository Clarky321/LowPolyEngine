#include "engine/core/Log.hpp"

#include <cstdio>
#include <mutex>

namespace engine::core {

namespace {

#ifdef NDEBUG
constexpr LogLevel kMinimumLevel = LogLevel::Info;
#else
constexpr LogLevel kMinimumLevel = LogLevel::Debug;
#endif

// Keeps lines from different threads from interleaving.
std::mutex& outputMutex()
{
    static std::mutex mutex;
    return mutex;
}

} // namespace

std::string_view toString(LogLevel level) noexcept
{
    switch (level) {
    case LogLevel::Debug:
        return "debug";
    case LogLevel::Info:
        return "info";
    case LogLevel::Warning:
        return "warning";
    case LogLevel::Error:
        return "error";
    }
    return "unknown";
}

std::string formatLogLine(LogLevel level, std::string_view category, std::string_view message)
{
    const std::string_view levelName = toString(level);
    std::string line;
    line.reserve(levelName.size() + category.size() + message.size() + 6);
    line += '[';
    line += levelName;
    line += "] [";
    line += category;
    line += "] ";
    line += message;
    return line;
}

bool isLogLevelEnabled(LogLevel level) noexcept
{
    return static_cast<int>(level) >= static_cast<int>(kMinimumLevel);
}

void writeLog(LogLevel level, std::string_view category, std::string_view message) noexcept
{
    if (!isLogLevelEnabled(level)) {
        return;
    }
    try {
        std::string line = formatLogLine(level, category, message);
        line += '\n';
        const std::lock_guard lock(outputMutex());
        std::fwrite(line.data(), 1, line.size(), stderr);
    } catch (const std::exception&) {
        // Out of memory while building the line: there is nothing useful left to do.
    }
}

} // namespace engine::core
