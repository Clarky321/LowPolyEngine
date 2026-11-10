#include "engine/core/Log.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using engine::core::formatLogLine;
using engine::core::isLogLevelEnabled;
using engine::core::LogLevel;

TEST_CASE("Log lines name the level and the category", "[core][log]")
{
    CHECK(formatLogLine(LogLevel::Info, "render", "context created") ==
          "[info] [render] context created");
    CHECK(formatLogLine(LogLevel::Warning, "app", "vsync off") == "[warning] [app] vsync off");
    CHECK(formatLogLine(LogLevel::Error, "core", "") == "[error] [core] ");
    CHECK(formatLogLine(LogLevel::Debug, "input", "x") == "[debug] [input] x");
}

TEST_CASE("Multi-line messages are kept intact", "[core][log]")
{
    // Shader info logs span several lines and must not be cut.
    const std::string line = formatLogLine(LogLevel::Error, "render", "failed:\n0(9): error");
    CHECK(line == "[error] [render] failed:\n0(9): error");
}

TEST_CASE("Info, warning and error messages are always enabled", "[core][log]")
{
    CHECK(isLogLevelEnabled(LogLevel::Info));
    CHECK(isLogLevelEnabled(LogLevel::Warning));
    CHECK(isLogLevelEnabled(LogLevel::Error));
#ifdef NDEBUG
    CHECK_FALSE(isLogLevelEnabled(LogLevel::Debug));
#else
    CHECK(isLogLevelEnabled(LogLevel::Debug));
#endif
}

TEST_CASE("Logging with format arguments does not throw", "[core][log]")
{
    // Output goes to stderr; this checks the call path compiles and is safe.
    CHECK_NOTHROW(engine::core::logInfo("test", "value {} of {}", 1, 2));
    CHECK_NOTHROW(engine::core::logDebug("test", "debug {:.2f}", 0.5));
}
