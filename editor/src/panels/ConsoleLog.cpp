#include "editor/panels/ConsoleLog.hpp"

#include <algorithm>
#include <utility>

namespace editor {

using engine::core::LogLevel;

namespace {

std::size_t levelIndex(LogLevel level) noexcept
{
    switch (level) {
    case LogLevel::Debug:
        return 0;
    case LogLevel::Info:
        return 1;
    case LogLevel::Warning:
        return 2;
    case LogLevel::Error:
        return 3;
    }
    return 1;
}

} // namespace

void LogInbox::listen()
{
    if (subscription_.active()) {
        return;
    }
    subscription_ = engine::core::addLogListener([this](const engine::core::LogRecord& record) {
        add(record.level, record.category, record.message);
    });
}

void LogInbox::add(LogLevel level, std::string_view category, std::string_view message)
{
    ConsoleLine line{level, engine::core::formatLogLine(level, category, message)};
    const std::scoped_lock lock(mutex_);
    pending_.push_back(std::move(line));
}

std::vector<ConsoleLine> LogInbox::take()
{
    std::vector<ConsoleLine> lines;
    const std::scoped_lock lock(mutex_);
    lines.swap(pending_);
    return lines;
}

ConsoleLog::ConsoleLog(std::size_t lineLimit) : limit_(std::max<std::size_t>(lineLimit, 1)) {}

void ConsoleLog::append(std::vector<ConsoleLine> lines)
{
    if (lines.empty()) {
        return;
    }
    for (ConsoleLine& line : lines) {
        ++counts_[levelIndex(line.level)];
        lines_.push_back(std::move(line));
    }
    while (lines_.size() > limit_) {
        --counts_[levelIndex(lines_.front().level)];
        lines_.pop_front();
        ++dropped_;
    }
    refilter();
}

void ConsoleLog::clear() noexcept
{
    lines_.clear();
    counts_.fill(0);
    dropped_ = 0;
    shown_.clear();
    ++version_;
}

std::size_t ConsoleLog::count(LogLevel level) const noexcept
{
    return counts_[levelIndex(level)];
}

void ConsoleLog::show(LogLevel level, bool shown) noexcept
{
    bool& current = shownLevels_[levelIndex(level)];
    if (current == shown) {
        return;
    }
    current = shown;
    try {
        refilter();
    } catch (...) {
        // Out of memory while listing the lines: none is shown until the next change.
        shown_.clear();
        ++version_;
    }
}

bool ConsoleLog::shows(LogLevel level) const noexcept
{
    return shownLevels_[levelIndex(level)];
}

void ConsoleLog::refilter()
{
    shown_.clear();
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        if (shownLevels_[levelIndex(lines_[i].level)]) {
            shown_.push_back(i);
        }
    }
    ++version_;
}

} // namespace editor
