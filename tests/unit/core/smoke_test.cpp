#include "engine/core/Version.hpp"

#include <catch2/catch_test_macros.hpp>

// Proves the whole toolchain works end to end: Conan-provided Catch2,
// the engine library, linking and CTest discovery.

TEST_CASE("toString formats a version as major.minor.patch", "[core][version]")
{
    CHECK(engine::core::toString(engine::core::Version{1, 2, 3}) == "1.2.3");
    CHECK(engine::core::toString(engine::core::Version{}) == "0.0.0");
}

TEST_CASE("engineVersion matches the version declared in CMake", "[core][version][smoke]")
{
    // Keep in sync with project(VERSION ...) in the root CMakeLists.txt.
    constexpr engine::core::Version expected{0, 1, 0};
    CHECK(engine::core::engineVersion() == expected);
}
