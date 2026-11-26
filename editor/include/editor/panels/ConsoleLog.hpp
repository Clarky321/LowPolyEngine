#pragma once

// The console's lines (M5.7), UI-free. Two parts, for two threads:
//   - LogInbox receives the engine's log through a log listener (ADR 0030):
//     its listener runs on whichever thread logs, so it only appends the line
//     under its own lock;
//   - ConsoleLog belongs to the editor's main thread: once per frame it takes
//     what the inbox received, keeps the newest lines up to its limit, counts
//     them by level and lists those the level filter shows.

#include "engine/core/Log.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace editor {

// Lines the console keeps; the oldest go first.
inline constexpr std::size_t kConsoleLineLimit = 10000;

struct ConsoleLine {
    engine::core::LogLevel level = engine::core::LogLevel::Info;
    // As the terminal shows it: "[level] [category] message".
    std::string text;
};

// Lines from any thread, waiting for the console.
class LogInbox {
public:
    LogInbox() = default;
    // The listener refers to the inbox: it neither moves nor copies.
    LogInbox(const LogInbox&) = delete;
    LogInbox& operator=(const LogInbox&) = delete;
    LogInbox(LogInbox&&) = delete;
    LogInbox& operator=(LogInbox&&) = delete;
    ~LogInbox() = default;

    // Receives the engine's log from now on, until stopListening() or the
    // destructor. Listening again changes nothing.
    void listen();
    void stopListening() noexcept { subscription_.reset(); }
    [[nodiscard]] bool listening() const noexcept { return subscription_.active(); }

    // Appends a line; safe from any thread.
    void add(engine::core::LogLevel level, std::string_view category, std::string_view message);
    // The lines received since the previous call, in the order they came.
    [[nodiscard]] std::vector<ConsoleLine> take();

private:
    std::mutex mutex_;
    std::vector<ConsoleLine> pending_;
    // Last: removed (and waited for) before the rest is destroyed.
    engine::core::LogSubscription subscription_;
};

// The console's lines and its level filter, for one thread.
class ConsoleLog {
public:
    // A limit of 0 is taken as 1.
    explicit ConsoleLog(std::size_t lineLimit = kConsoleLineLimit);

    // Adds lines at the end, dropping the oldest beyond the limit.
    void append(std::vector<ConsoleLine> lines);
    // Every line is gone; the filter stays.
    void clear() noexcept;

    [[nodiscard]] const std::deque<ConsoleLine>& lines() const noexcept { return lines_; }
    [[nodiscard]] std::size_t lineLimit() const noexcept { return limit_; }
    // Lines dropped by the limit since the start (or the last clear()).
    [[nodiscard]] std::uint64_t droppedLines() const noexcept { return dropped_; }
    // How many of the kept lines have the level.
    [[nodiscard]] std::size_t count(engine::core::LogLevel level) const noexcept;

    // Every level is shown at first.
    void show(engine::core::LogLevel level, bool shown) noexcept;
    [[nodiscard]] bool shows(engine::core::LogLevel level) const noexcept;

    // The kept lines the filter shows, oldest first (indices into lines()).
    [[nodiscard]] const std::vector<std::size_t>& shown() const noexcept { return shown_; }

    // A number that changes whenever lines() or shown() change.
    [[nodiscard]] std::uint64_t version() const noexcept { return version_; }

private:
    void refilter();

    std::size_t limit_;
    std::deque<ConsoleLine> lines_;
    std::uint64_t dropped_ = 0;
    std::array<std::size_t, 4> counts_{};
    std::array<bool, 4> shownLevels_{true, true, true, true};
    std::vector<std::size_t> shown_;
    std::uint64_t version_ = 0;
};

} // namespace editor
