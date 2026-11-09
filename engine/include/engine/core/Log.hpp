#pragma once

#include <exception>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace engine::core {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error,
};

[[nodiscard]] std::string_view toString(LogLevel level) noexcept;

// One log line without the newline: "[level] [category] message".
[[nodiscard]] std::string formatLogLine(LogLevel level, std::string_view category,
                                        std::string_view message);

// Debug messages are written in debug builds only; all other levels always.
[[nodiscard]] bool isLogLevelEnabled(LogLevel level) noexcept;

// Writes one line to stderr. Safe to call from several threads; never throws.
void writeLog(LogLevel level, std::string_view category, std::string_view message) noexcept;

// Formats with std::format (checked at compile time) and writes the line.
// `category` names the subsystem, e.g. "render" or "app".
template <typename... Args>
void log(LogLevel level, std::string_view category, std::format_string<Args...> format,
         Args&&... args) noexcept
{
    if (!isLogLevelEnabled(level)) {
        return;
    }
    try {
        writeLog(level, category, std::format(format, std::forward<Args>(args)...));
    } catch (const std::exception&) {
        // Formatting can only fail on allocation; logging must not take the caller down.
        writeLog(level, category, "(log message could not be formatted)");
    }
}

template <typename... Args>
void logDebug(std::string_view category, std::format_string<Args...> format,
              Args&&... args) noexcept
{
    log(LogLevel::Debug, category, format, std::forward<Args>(args)...);
}

template <typename... Args>
void logInfo(std::string_view category, std::format_string<Args...> format, Args&&... args) noexcept
{
    log(LogLevel::Info, category, format, std::forward<Args>(args)...);
}

template <typename... Args>
void logWarning(std::string_view category, std::format_string<Args...> format,
                Args&&... args) noexcept
{
    log(LogLevel::Warning, category, format, std::forward<Args>(args)...);
}

template <typename... Args>
void logError(std::string_view category, std::format_string<Args...> format,
              Args&&... args) noexcept
{
    log(LogLevel::Error, category, format, std::forward<Args>(args)...);
}

} // namespace engine::core
