#include "engine/core/Result.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <utility>
#include <variant>

using engine::core::Error;
using engine::core::Result;

TEST_CASE("Result holds a value", "[core][result]")
{
    const Result<int> result = 42;
    REQUIRE(result.hasValue());
    CHECK(static_cast<bool>(result));
    CHECK(result.value() == 42);
}

TEST_CASE("Result holds an error", "[core][result]")
{
    const Result<int> result = Error{"broken"};
    REQUIRE_FALSE(result.hasValue());
    CHECK_FALSE(static_cast<bool>(result));
    CHECK(result.error().message == "broken");
}

TEST_CASE("Result supports move-only values", "[core][result]")
{
    Result<std::unique_ptr<int>> result = std::make_unique<int>(7);
    REQUIRE(result.hasValue());

    const std::unique_ptr<int> owned = std::move(result).value();
    REQUIRE(owned != nullptr);
    CHECK(*owned == 7);
}

TEST_CASE("Result reports wrong-side access instead of undefined behavior", "[core][result]")
{
    const Result<int> failed = Error{"no value"};
    CHECK_THROWS_AS(failed.value(), std::bad_variant_access);

    const Result<std::string> succeeded = std::string("ok");
    CHECK_THROWS_AS(succeeded.error(), std::bad_variant_access);
}
