#include "engine/core/Log.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <mutex>
#include <vector>

namespace engine::core {

namespace {

#ifdef NDEBUG
constexpr LogLevel kMinimumLevel = LogLevel::Info;
#else
constexpr LogLevel kMinimumLevel = LogLevel::Debug;
#endif

struct ListenerEntry {
    std::uint64_t id = 0;
    LogListener listener;
    // Removed from inside a listener: no longer called, erased after the line.
    bool removed = false;
};

using ListenerList = std::vector<std::unique_ptr<ListenerEntry>>;

// The log's lock and its listeners (ADR 0030). The lock keeps lines from
// different threads from interleaving and gives the listeners one line at a
// time. Constructed on first use; subscriptions must not outlive it (they are
// owned by objects with ordinary lifetimes, not by other statics).
struct LogState {
    std::mutex mutex;
    // Each entry on the heap: it stays where it is while a listener adds
    // another (the vector may then move its pointers).
    ListenerList listeners;
    std::uint64_t nextId = 1;
    bool removedPending = false;
};

LogState& logState()
{
    static LogState state;
    return state;
}

// True while this thread gives a line to the listeners, and therefore holds
// the log's lock: the lock is not taken again on this thread.
thread_local bool tInListener = false;

void writeToStderr(const std::string& line) noexcept
{
    std::fwrite(line.data(), 1, line.size(), stderr);
}

// Calls the listeners present when the line came, in the order they were
// added; the lock is held.
void dispatch(LogState& state, const LogRecord& record) noexcept
{
    tInListener = true;
    const std::size_t count = state.listeners.size();
    for (std::size_t i = 0; i < count; ++i) {
        ListenerEntry& entry = *state.listeners[i];
        if (entry.removed) {
            continue;
        }
        try {
            entry.listener(record);
        } catch (...) {
            // A listener's failure must not reach the code that logged.
        }
    }
    tInListener = false;
}

// Moves the entries removed from inside listeners out of the list (the lock is
// held), so that they are destroyed after it is released: what a listener
// holds may log when it goes.
void takeRemoved(LogState& state, ListenerList& removed)
{
    if (!state.removedPending) {
        return;
    }
    for (auto& entry : state.listeners) {
        if (entry->removed) {
            removed.push_back(std::move(entry));
        }
    }
    std::erase(state.listeners, nullptr);
    state.removedPending = false;
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
        if (tInListener) {
            // Logged by a listener: this thread holds the lock already, and the
            // line goes to stderr only, so that listeners never recurse.
            writeToStderr(line);
            return;
        }
        LogState& state = logState();
        // Declared before the lock: destroyed after it is released.
        ListenerList removed;
        const std::lock_guard lock(state.mutex);
        writeToStderr(line);
        if (!state.listeners.empty()) {
            dispatch(state, LogRecord{level, category, message});
            takeRemoved(state, removed);
        }
    } catch (const std::exception&) {
        // Out of memory while building the line: there is nothing useful left to do.
    }
}

LogSubscription::LogSubscription(LogSubscription&& other) noexcept
    : id_(std::exchange(other.id_, 0))
{
}

LogSubscription& LogSubscription::operator=(LogSubscription&& other) noexcept
{
    if (this != &other) {
        reset();
        id_ = std::exchange(other.id_, 0);
    }
    return *this;
}

void LogSubscription::reset() noexcept
{
    if (id_ == 0) {
        return;
    }
    const std::uint64_t id = std::exchange(id_, 0);
    LogState& state = logState();
    if (tInListener) {
        // Inside a listener on this thread, which holds the lock: marked now,
        // erased when the line has reached every listener.
        for (const auto& entry : state.listeners) {
            if (entry && entry->id == id) {
                entry->removed = true;
                state.removedPending = true;
            }
        }
        return;
    }
    try {
        // Declared before the lock: the listener is destroyed after it is released.
        std::unique_ptr<ListenerEntry> gone;
        const std::lock_guard lock(state.mutex);
        const auto found = std::ranges::find_if(
            state.listeners, [id](const auto& entry) { return entry && entry->id == id; });
        if (found != state.listeners.end()) {
            gone = std::move(*found);
            state.listeners.erase(found);
        }
    } catch (const std::exception&) {
        // Only the lock can fail (std::system_error); the listener then stays.
    }
}

LogSubscription addLogListener(LogListener listener)
{
    if (!listener) {
        return {};
    }
    auto entry = std::make_unique<ListenerEntry>();
    entry->listener = std::move(listener);
    LogState& state = logState();
    // Inside a listener this thread holds the lock already.
    std::unique_lock lock(state.mutex, std::defer_lock);
    if (!tInListener) {
        lock.lock();
    }
    entry->id = state.nextId++;
    const std::uint64_t id = entry->id;
    state.listeners.push_back(std::move(entry));
    return LogSubscription{id};
}

std::size_t logListenerCount() noexcept
{
    LogState& state = logState();
    const auto count = [&state] {
        return static_cast<std::size_t>(std::ranges::count_if(
            state.listeners, [](const auto& entry) { return entry && !entry->removed; }));
    };
    if (tInListener) {
        return count();
    }
    try {
        const std::lock_guard lock(state.mutex);
        return count();
    } catch (const std::exception&) {
        return 0;
    }
}

} // namespace engine::core
