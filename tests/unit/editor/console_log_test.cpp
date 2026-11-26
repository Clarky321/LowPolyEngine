// The console's lines (editor/panels/ConsoleLog, M5.7), headless: the inbox
// that receives the engine's log on any thread, and the main thread's lines
// with their limit, counts and level filter.

#include "editor/panels/ConsoleLog.hpp"

#include "engine/core/Log.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using editor::ConsoleLine;
using editor::ConsoleLog;
using editor::LogInbox;
using engine::core::LogLevel;

namespace {

ConsoleLine line(LogLevel level, std::string text)
{
    return ConsoleLine{level, std::move(text)};
}

std::vector<std::string> shownTexts(const ConsoleLog& log)
{
    std::vector<std::string> texts;
    for (const std::size_t index : log.shown()) {
        texts.push_back(log.lines()[index].text);
    }
    return texts;
}

} // namespace

TEST_CASE("The console keeps its lines in order, counted by level", "[editor][console]")
{
    ConsoleLog log;
    CHECK(log.lineLimit() == editor::kConsoleLineLimit);
    CHECK(log.lines().empty());
    const auto start = log.version();
    log.append(
        {line(LogLevel::Info, "a"), line(LogLevel::Warning, "b"), line(LogLevel::Error, "c")});
    log.append({line(LogLevel::Debug, "d"), line(LogLevel::Warning, "e")});
    CHECK(log.version() != start);
    CHECK(shownTexts(log) == std::vector<std::string>{"a", "b", "c", "d", "e"});
    CHECK(log.count(LogLevel::Info) == 1);
    CHECK(log.count(LogLevel::Warning) == 2);
    CHECK(log.count(LogLevel::Error) == 1);
    CHECK(log.count(LogLevel::Debug) == 1);

    // Nothing to add changes nothing.
    const auto version = log.version();
    log.append({});
    CHECK(log.version() == version);

    log.clear();
    CHECK(log.lines().empty());
    CHECK(log.shown().empty());
    CHECK(log.count(LogLevel::Warning) == 0);
    CHECK(log.version() != version);
}

TEST_CASE("The console's level filter shows and hides lines", "[editor][console]")
{
    ConsoleLog log;
    log.append({line(LogLevel::Info, "i1"), line(LogLevel::Warning, "w1"),
                line(LogLevel::Error, "e1"), line(LogLevel::Debug, "d1"),
                line(LogLevel::Info, "i2")});
    for (const LogLevel level :
         {LogLevel::Debug, LogLevel::Info, LogLevel::Warning, LogLevel::Error}) {
        CHECK(log.shows(level));
    }

    auto version = log.version();
    log.show(LogLevel::Info, false);
    CHECK_FALSE(log.shows(LogLevel::Info));
    CHECK(log.version() != version);
    CHECK(shownTexts(log) == std::vector<std::string>{"w1", "e1", "d1"});

    // The same state again changes nothing.
    version = log.version();
    log.show(LogLevel::Info, false);
    CHECK(log.version() == version);

    log.show(LogLevel::Debug, false);
    log.show(LogLevel::Warning, false);
    CHECK(shownTexts(log) == std::vector<std::string>{"e1"});

    // New lines go through the filter as they come; the counts include the
    // hidden ones.
    log.append({line(LogLevel::Error, "e2"), line(LogLevel::Info, "i3")});
    CHECK(shownTexts(log) == std::vector<std::string>{"e1", "e2"});
    CHECK(log.count(LogLevel::Info) == 3);

    log.show(LogLevel::Info, true);
    CHECK(shownTexts(log) == std::vector<std::string>{"i1", "e1", "i2", "e2", "i3"});
}

TEST_CASE("The console drops its oldest lines beyond its limit", "[editor][console]")
{
    ConsoleLog log(3);
    log.append({line(LogLevel::Info, "1"), line(LogLevel::Warning, "2")});
    log.append({line(LogLevel::Info, "3"), line(LogLevel::Error, "4"), line(LogLevel::Info, "5")});
    CHECK(shownTexts(log) == std::vector<std::string>{"3", "4", "5"});
    CHECK(log.droppedLines() == 2);
    CHECK(log.count(LogLevel::Warning) == 0);
    CHECK(log.count(LogLevel::Info) == 2);
    CHECK(log.count(LogLevel::Error) == 1);

    // A filter over the kept lines only.
    log.show(LogLevel::Info, false);
    CHECK(shownTexts(log) == std::vector<std::string>{"4"});

    // A limit of 0 keeps one line.
    ConsoleLog tiny(0);
    CHECK(tiny.lineLimit() == 1);
    tiny.append({line(LogLevel::Info, "x"), line(LogLevel::Info, "y")});
    CHECK(shownTexts(tiny) == std::vector<std::string>{"y"});
}

TEST_CASE("The inbox receives the engine's log while it listens, from any thread",
          "[editor][console]")
{
    LogInbox inbox;
    CHECK_FALSE(inbox.listening());
    engine::core::logInfo("console-test", "before listening");
    CHECK(inbox.take().empty());

    inbox.listen();
    inbox.listen(); // a second time: still one listener
    CHECK(inbox.listening());
    engine::core::logWarning("console-test", "a warning {}", 1);
    std::vector<ConsoleLine> lines = inbox.take();
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].level == LogLevel::Warning);
    CHECK(lines[0].text == "[warning] [console-test] a warning 1");
    CHECK(inbox.take().empty());

    // Lines of other threads, each thread's in order.
    {
        std::vector<std::jthread> threads;
        for (int t = 0; t < 3; ++t) {
            threads.emplace_back([t] {
                for (int i = 0; i < 50; ++i) {
                    engine::core::logInfo("console-test", "thread {} line {}", t, i);
                }
            });
        }
    }
    lines = inbox.take();
    REQUIRE(lines.size() == 150);
    std::vector<int> next(3, 0);
    for (const ConsoleLine& received : lines) {
        // "[info] [console-test] thread T line I"
        const std::string prefix = "[info] [console-test] thread ";
        REQUIRE(received.text.starts_with(prefix));
        const int thread = received.text[prefix.size()] - '0';
        REQUIRE(thread >= 0);
        REQUIRE(thread < 3);
        const std::string expected = prefix + std::to_string(thread) + " line " +
                                     std::to_string(next[static_cast<std::size_t>(thread)]);
        CHECK(received.text == expected);
        ++next[static_cast<std::size_t>(thread)];
    }

    inbox.stopListening();
    CHECK_FALSE(inbox.listening());
    engine::core::logError("console-test", "after");
    CHECK(inbox.take().empty());

    // Lines added directly, as the listener adds them.
    inbox.add(LogLevel::Error, "x", "direct");
    lines = inbox.take();
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].text == "[error] [x] direct");
}

TEST_CASE("An inbox that goes away stops listening", "[editor][console]")
{
    const std::size_t before = engine::core::logListenerCount();
    {
        LogInbox inbox;
        inbox.listen();
        CHECK(engine::core::logListenerCount() == before + 1);
    }
    CHECK(engine::core::logListenerCount() == before);
    // Nothing refers to the inbox any more.
    CHECK_NOTHROW(engine::core::logInfo("console-test", "after the inbox"));
}
