#pragma once

#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <functional>
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

// Writes one line to stderr, then gives it to the log listeners (below). Safe
// to call from several threads; never throws.
void writeLog(LogLevel level, std::string_view category, std::string_view message) noexcept;

// One line as a log listener receives it (M5.7, ADR 0030). The views are valid
// during the call only.
struct LogRecord {
    LogLevel level = LogLevel::Info;
    std::string_view category;
    std::string_view message;
};

using LogListener = std::function<void(const LogRecord&)>;

// A listener's registration: the listener is removed by reset() or the
// destructor. Move-only; an empty subscription removes nothing.
class LogSubscription {
public:
    LogSubscription() noexcept = default;
    LogSubscription(LogSubscription&& other) noexcept;
    LogSubscription& operator=(LogSubscription&& other) noexcept;
    LogSubscription(const LogSubscription&) = delete;
    LogSubscription& operator=(const LogSubscription&) = delete;
    ~LogSubscription() { reset(); }

    // Removes the listener. When it returns, the listener is not called again:
    // a call in progress on another thread is waited for. From inside a
    // listener (any listener, on the logging thread) the removal takes effect
    // at once, and the running call ends normally.
    void reset() noexcept;

    [[nodiscard]] bool active() const noexcept { return id_ != 0; }

private:
    friend LogSubscription addLogListener(LogListener listener);
    explicit LogSubscription(std::uint64_t id) noexcept : id_(id) {}

    std::uint64_t id_ = 0;
};

// Adds a listener for the log (ADR 0030). The rules:
//   - it receives every line written from now on (the levels isLogLevelEnabled
//     lets through), after the line went to stderr, in the order the lines
//     were written;
//   - it runs on the thread that writes the line, while the log holds its
//     lock: one line at a time, given to the listeners in the order they were
//     added. It must be quick and must not wait for another thread that logs;
//   - a line it logs itself goes to stderr only, not to the listeners;
//   - a listener added from inside a listener receives the lines after the
//     current one; an exception from a listener is caught and dropped.
// An empty function adds nothing and gives an empty subscription.
[[nodiscard]] LogSubscription addLogListener(LogListener listener);

// The number of listeners added and not yet removed.
[[nodiscard]] std::size_t logListenerCount() noexcept;

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
