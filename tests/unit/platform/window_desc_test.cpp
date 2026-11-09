#include "engine/platform/Window.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using engine::platform::kMaxWindowExtent;
using engine::platform::validate;
using engine::platform::WindowDesc;

TEST_CASE("Default window description is valid", "[platform][window]")
{
    CHECK_FALSE(validate(WindowDesc{}).has_value());
}

TEST_CASE("Window sizes at the accepted limits are valid", "[platform][window]")
{
    CHECK_FALSE(validate(WindowDesc{.width = 1, .height = 1}).has_value());
    CHECK_FALSE(
        validate(WindowDesc{.width = kMaxWindowExtent, .height = kMaxWindowExtent}).has_value());
}

TEST_CASE("Non-positive window sizes are rejected", "[platform][window]")
{
    const auto zeroWidth = validate(WindowDesc{.width = 0});
    REQUIRE(zeroWidth.has_value());
    CHECK(zeroWidth->message.find("width") != std::string::npos);

    const auto negativeHeight = validate(WindowDesc{.height = -5});
    REQUIRE(negativeHeight.has_value());
    CHECK(negativeHeight->message.find("height") != std::string::npos);
    CHECK(negativeHeight->message.find("-5") != std::string::npos);
}

TEST_CASE("Window sizes above the limit are rejected", "[platform][window]")
{
    CHECK(validate(WindowDesc{.width = kMaxWindowExtent + 1}).has_value());
    CHECK(validate(WindowDesc{.height = kMaxWindowExtent + 1}).has_value());
}
