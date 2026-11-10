// Material descriptions are checked without a GPU (engine/src/render/Material.cpp).

#include "engine/render/Renderer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <optional>
#include <string>

using engine::render::Color;
using engine::render::MaterialDesc;
using engine::render::validate;

namespace {

bool mentions(const std::optional<engine::core::Error>& error, const std::string& text)
{
    return error.has_value() && error->message.find(text) != std::string::npos;
}

MaterialDesc withColor(float r, float g, float b, float a)
{
    return MaterialDesc{.baseColor = Color{r, g, b, a}};
}

} // namespace

TEST_CASE("The default material is white and valid", "[render][material]")
{
    const MaterialDesc desc;
    CHECK(desc.baseColor.r == 1.0F);
    CHECK(desc.baseColor.g == 1.0F);
    CHECK(desc.baseColor.b == 1.0F);
    CHECK(desc.baseColor.a == 1.0F);
    CHECK_FALSE(validate(desc).has_value());
}

TEST_CASE("Base color components in [0, 1] are valid", "[render][material]")
{
    CHECK_FALSE(validate(withColor(0.0F, 0.0F, 0.0F, 0.0F)).has_value());
    CHECK_FALSE(validate(withColor(0.25F, 0.5F, 0.75F, 1.0F)).has_value());
    CHECK_FALSE(validate(withColor(1.0F, 1.0F, 1.0F, 0.5F)).has_value());
}

TEST_CASE("Base color components outside [0, 1] or not finite are rejected", "[render][material]")
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    // The message names the component and its value.
    CHECK(mentions(validate(withColor(-0.1F, 0.0F, 0.0F, 1.0F)), "base color red is -0.1"));
    CHECK(mentions(validate(withColor(0.0F, 1.5F, 0.0F, 1.0F)), "base color green is 1.5"));
    CHECK(mentions(validate(withColor(0.0F, 0.0F, nan, 1.0F)), "base color blue is nan"));
    CHECK(mentions(validate(withColor(0.0F, 0.0F, 0.0F, infinity)), "base color alpha is inf"));
    CHECK(mentions(validate(withColor(2.0F, 0.0F, 0.0F, 1.0F)), "must be finite and in [0, 1]"));
    // The first offending component is reported.
    CHECK(mentions(validate(withColor(2.0F, 3.0F, 0.0F, 1.0F)), "red"));
}
