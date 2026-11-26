// Log listeners (M5.7, ADR 0030): what a listener receives, in which order and
// on which thread, and what happens when listeners are added or removed while
// lines are written, also from inside a listener. The lines still go to
// stderr, as every log line does. Catch's assertions are not thread-safe, so
// the threads only record, and the test thread checks.

#include "engine/core/Log.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <charconv>
#include <cstddef>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using engine::core::addLogListener;
using engine::core::isLogLevelEnabled;
using engine::core::LogLevel;
using engine::core::logListenerCount;
using engine::core::LogRecord;
using engine::core::LogSubscription;

namespace {

// The lines this test file writes; anything else is ignored.
constexpr std::string_view kCategory = "listener-test";

struct Received {
    LogLevel level = LogLevel::Info;
    std::string category;
    std::string message;
};

// Records the test's lines; safe from any thread.
class Recorder {
public:
    void operator()(const LogRecord& record)
    {
        if (record.category != kCategory) {
            return;
        }
        const std::scoped_lock lock(mutex_);
        lines_.push_back({record.level, std::string(record.category), std::string(record.message)});
    }

    [[nodiscard]] std::vector<Received> lines() const
    {
        const std::scoped_lock lock(mutex_);
        return lines_;
    }

    [[nodiscard]] std::vector<std::string> messages() const
    {
        std::vector<std::string> texts;
        for (const Received& line : lines()) {
            texts.push_back(line.message);
        }
        return texts;
    }

private:
    mutable std::mutex mutex_;
    std::vector<Received> lines_;
};

LogSubscription listen(Recorder& recorder)
{
    return addLogListener([&recorder](const LogRecord& record) { recorder(record); });
}

// "<thread> <sequence>" as the threaded tests write it.
std::pair<int, int> parsePair(std::string_view text)
{
    const auto space = text.find(' ');
    if (space == std::string_view::npos) {
        return {-1, -1};
    }
    int first = -1;
    int second = -1;
    std::from_chars(text.data(), text.data() + space, first);
    std::from_chars(text.data() + space + 1, text.data() + text.size(), second);
    return {first, second};
}

} // namespace

TEST_CASE("A log listener receives every enabled line once, in order, as it was logged",
          "[core][log]")
{
    const std::size_t before = logListenerCount();
    Recorder recorder;
    {
        const LogSubscription subscription = listen(recorder);
        CHECK(subscription.active());
        CHECK(logListenerCount() == before + 1);

        engine::core::logInfo(kCategory, "first {}", 1);
        engine::core::logDebug(kCategory, "debug {}", 2);
        engine::core::logWarning(kCategory, "warning {:.1f}", 3.0);
        engine::core::logError(kCategory, "error");
        engine::core::writeLog(LogLevel::Info, kCategory, "written directly");
    }
    // Removed with the subscription: nothing after it.
    CHECK(logListenerCount() == before);
    engine::core::logInfo(kCategory, "after the subscription");

    std::vector<Received> expected{{LogLevel::Info, std::string(kCategory), "first 1"}};
    // Debug lines exist in debug builds only, for the listeners as for stderr.
    if (isLogLevelEnabled(LogLevel::Debug)) {
        expected.push_back({LogLevel::Debug, std::string(kCategory), "debug 2"});
    }
    expected.push_back({LogLevel::Warning, std::string(kCategory), "warning 3.0"});
    expected.push_back({LogLevel::Error, std::string(kCategory), "error"});
    expected.push_back({LogLevel::Info, std::string(kCategory), "written directly"});
    const std::vector<Received> lines = recorder.lines();
    REQUIRE(lines.size() == expected.size());
    for (std::size_t i = 0; i < lines.size(); ++i) {
        INFO("line " << i);
        CHECK(lines[i].level == expected[i].level);
        CHECK(lines[i].category == expected[i].category);
        CHECK(lines[i].message == expected[i].message);
    }
}

TEST_CASE("Listeners are called in the order they were added; removed ones are not", "[core][log]")
{
    std::vector<std::string> calls;
    const auto tagged = [&calls](std::string tag) {
        return addLogListener([&calls, tag = std::move(tag)](const LogRecord& record) {
            if (record.category == kCategory) {
                calls.push_back(tag + ":" + std::string(record.message));
            }
        });
    };
    LogSubscription first = tagged("a");
    LogSubscription second = tagged("b");
    LogSubscription third = tagged("c");
    engine::core::logInfo(kCategory, "one");
    CHECK(calls == std::vector<std::string>{"a:one", "b:one", "c:one"});

    calls.clear();
    second.reset();
    CHECK_FALSE(second.active());
    second.reset(); // twice: nothing happens
    engine::core::logInfo(kCategory, "two");
    CHECK(calls == std::vector<std::string>{"a:two", "c:two"});

    SECTION("A moved subscription keeps its listener; the moved-from one removes nothing")
    {
        LogSubscription moved = std::move(first);
        CHECK(moved.active());
        CHECK_FALSE(first.active()); // NOLINT(bugprone-use-after-move): checked on purpose
        first.reset();
        calls.clear();
        engine::core::logInfo(kCategory, "three");
        CHECK(calls == std::vector<std::string>{"a:three", "c:three"});

        // Assigning over a subscription removes the listener it had.
        moved = std::move(third);
        calls.clear();
        engine::core::logInfo(kCategory, "four");
        CHECK(calls == std::vector<std::string>{"c:four"});
    }

    SECTION("An empty function adds no listener")
    {
        const std::size_t before = logListenerCount();
        LogSubscription empty = addLogListener(engine::core::LogListener{});
        CHECK_FALSE(empty.active());
        CHECK(logListenerCount() == before);
    }
}

TEST_CASE("Inside a listener: logging, throwing, removing itself and adding another", "[core][log]")
{
    std::vector<std::string> calls;
    const auto record = [&calls](std::string_view tag, const LogRecord& line) {
        if (line.category == kCategory) {
            calls.push_back(std::string(tag) + ":" + std::string(line.message));
        }
    };

    SECTION("A line logged by a listener goes to stderr only: no listener receives it")
    {
        const LogSubscription logging = addLogListener([&record](const LogRecord& line) {
            record("logging", line);
            if (line.category == kCategory) {
                engine::core::logWarning(kCategory, "from inside: {}", line.message);
            }
        });
        const LogSubscription watching =
            addLogListener([&record](const LogRecord& line) { record("watching", line); });
        engine::core::logInfo(kCategory, "outer");
        CHECK(calls == std::vector<std::string>{"logging:outer", "watching:outer"});
    }

    SECTION("A listener that throws loses its line; the others and the caller go on")
    {
        const LogSubscription throwing = addLogListener([&record](const LogRecord& line) {
            record("throwing", line);
            if (line.category == kCategory) {
                throw std::runtime_error("listener failed");
            }
        });
        const LogSubscription after =
            addLogListener([&record](const LogRecord& line) { record("after", line); });
        CHECK_NOTHROW(engine::core::logInfo(kCategory, "x"));
        CHECK_NOTHROW(engine::core::logInfo(kCategory, "y"));
        CHECK(calls == std::vector<std::string>{"throwing:x", "after:x", "throwing:y", "after:y"});
    }

    SECTION("A listener removing itself is not called again, not even for the rest of the line")
    {
        const std::size_t before = logListenerCount();
        // The listeners of this section refer to each other's subscriptions.
        LogSubscription second;
        LogSubscription first = addLogListener([&](const LogRecord& line) {
            record("first", line);
            if (line.category == kCategory) {
                // Its own, and the next listener's: both stop at once.
                first.reset();
                second.reset();
                CHECK(logListenerCount() == before + 1);
            }
        });
        second = addLogListener([&record](const LogRecord& line) { record("second", line); });
        const LogSubscription third =
            addLogListener([&record](const LogRecord& line) { record("third", line); });
        engine::core::logInfo(kCategory, "once");
        engine::core::logInfo(kCategory, "again");
        CHECK_FALSE(first.active());
        CHECK_FALSE(second.active());
        CHECK(calls == std::vector<std::string>{"first:once", "third:once", "third:again"});
        CHECK(logListenerCount() == before + 1);
    }

    SECTION("A listener added from inside a listener receives the lines after the current one")
    {
        LogSubscription added;
        const LogSubscription adding = addLogListener([&](const LogRecord& line) {
            record("adding", line);
            if (line.category == kCategory && !added.active()) {
                added =
                    addLogListener([&record](const LogRecord& inner) { record("added", inner); });
            }
        });
        engine::core::logInfo(kCategory, "one");
        engine::core::logInfo(kCategory, "two");
        CHECK(calls == std::vector<std::string>{"adding:one", "adding:two", "added:two"});
    }
}

TEST_CASE("Lines from several threads reach a listener whole, each thread's in its order",
          "[core][log]")
{
    constexpr int kThreads = 4;
    constexpr int kLines = 150;
    Recorder recorder;
    const LogSubscription subscription = listen(recorder);
    {
        std::vector<std::jthread> threads;
        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([t] {
                for (int i = 0; i < kLines; ++i) {
                    engine::core::logInfo(kCategory, "{} {}", t, i);
                }
            });
        }
    }
    const std::vector<std::string> messages = recorder.messages();
    REQUIRE(messages.size() == static_cast<std::size_t>(kThreads * kLines));
    std::vector<int> next(kThreads, 0);
    bool ordered = true;
    for (const std::string& message : messages) {
        const auto [thread, sequence] = parsePair(message);
        REQUIRE(thread >= 0);
        REQUIRE(thread < kThreads);
        ordered = ordered && sequence == next[static_cast<std::size_t>(thread)];
        next[static_cast<std::size_t>(thread)] = sequence + 1;
    }
    CHECK(ordered);
    CHECK(next == std::vector<int>(kThreads, kLines));
}

TEST_CASE("A removal waits for a call on another thread; no call comes after it", "[core][log]")
{
    // A thread logs without pause while the test thread adds and removes
    // listeners. Each listener writes into state that is destroyed right after
    // its removal: a late call would be counted (and would touch freed memory).
    std::atomic<bool> stop{false};
    std::atomic<int> logged{0};
    std::jthread writer([&stop, &logged] {
        while (!stop.load()) {
            engine::core::logInfo(kCategory, "busy {}", logged.load());
            logged.fetch_add(1);
        }
    });

    std::atomic<int> lateCalls{0};
    int listenersCalled = 0;
    for (int round = 0; round < 60; ++round) {
        struct State {
            std::atomic<int> calls{0};
            std::atomic<bool> removed{false};
        };
        auto state = std::make_unique<State>();
        State* raw = state.get();
        LogSubscription subscription = addLogListener([raw, &lateCalls](const LogRecord& line) {
            if (line.category != kCategory) {
                return;
            }
            if (raw->removed.load()) {
                lateCalls.fetch_add(1);
            }
            raw->calls.fetch_add(1);
        });
        // Until the writer has gone through the listener at least once.
        const int start = logged.load();
        while (raw->calls.load() == 0 && logged.load() < start + 10000) {
            std::this_thread::yield();
        }
        listenersCalled += raw->calls.load() > 0 ? 1 : 0;
        subscription.reset();
        raw->removed.store(true);
        const int callsAtRemoval = raw->calls.load();
        // Let the writer write a few more lines, then check and free the state.
        const int after = logged.load();
        while (logged.load() < after + 5) {
            std::this_thread::yield();
        }
        CHECK(raw->calls.load() == callsAtRemoval);
        state.reset();
    }
    stop.store(true);
    writer.join();
    CHECK(lateCalls.load() == 0);
    CHECK(listenersCalled == 60);
}
