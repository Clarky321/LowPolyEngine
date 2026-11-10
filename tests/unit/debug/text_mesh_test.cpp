#include "engine/debug/TextMesh.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <optional>
#include <string_view>

using Catch::Approx;
using engine::debug::hasGlyph;
using engine::debug::measureText;
using engine::debug::overlayProjection;
using engine::debug::TextMeshBuilder;
using engine::debug::TextStyle;
using engine::geometry::validate;
using engine::math::Vec2;
using engine::math::Vec3;
using engine::math::Vec4;

namespace {

TextStyle plain(float scale = 1.0F)
{
    return TextStyle{.scale = scale, .color = Vec4{1.0F}, .shadowColor = std::nullopt};
}

} // namespace

TEST_CASE("The font covers letters, digits and the documented punctuation", "[debug][text]")
{
    for (char c = 'A'; c <= 'Z'; ++c) {
        CHECK(hasGlyph(c));
        CHECK(hasGlyph(static_cast<char>(c - 'A' + 'a'))); // lowercase uses uppercase
    }
    for (char c = '0'; c <= '9'; ++c) {
        CHECK(hasGlyph(c));
    }
    for (const char c : std::string_view(" .,:;-+/()%=_'\"!?[]<>#*|")) {
        INFO("character '" << c << "'");
        CHECK(hasGlyph(c));
    }
    CHECK_FALSE(hasGlyph('@'));
    CHECK_FALSE(hasGlyph('\t'));
}

TEST_CASE("measureText counts glyph advances and lines", "[debug][text]")
{
    // 2 glyphs: 5 + 1 + 5 pixels wide, 7 high.
    CHECK(measureText("AB", 1.0F) == Vec2{11.0F, 7.0F});
    CHECK(measureText("AB", 2.0F) == Vec2{22.0F, 14.0F});
    // Widest line wins; each further line adds the line advance.
    CHECK(measureText("A\nBCD", 1.0F) == Vec2{17.0F, 16.0F});
    CHECK(measureText("", 2.0F) == Vec2{0.0F});
    CHECK(measureText("AB", 0.0F) == Vec2{0.0F});
}

TEST_CASE("Text becomes valid quads, one per run of lit pixels", "[debug][text]")
{
    TextMeshBuilder builder;
    builder.addText("I", {0.0F, 0.0F}, plain());
    REQUIRE_FALSE(validate(builder.mesh()).has_value());
    // 'I' has one run in each of its 7 rows.
    CHECK(builder.mesh().triangleCount() == 7 * 2);

    builder.clear();
    CHECK(builder.mesh().empty());
    builder.addText("   ", {0.0F, 0.0F}, plain());
    CHECK(builder.mesh().empty());
}

TEST_CASE("A shadow doubles the text geometry", "[debug][text]")
{
    TextMeshBuilder withShadow;
    withShadow.addText("FPS 60", {0.0F, 0.0F}, TextStyle{});
    TextMeshBuilder without;
    without.addText("FPS 60", {0.0F, 0.0F}, plain(2.0F));
    CHECK(withShadow.mesh().triangleCount() == 2 * without.mesh().triangleCount());
}

TEST_CASE("Characters without a glyph are drawn as a box", "[debug][text]")
{
    TextMeshBuilder builder;
    builder.addText("@", {0.0F, 0.0F}, plain());
    CHECK_FALSE(builder.mesh().empty());
}

TEST_CASE("Text geometry stays inside its measured box", "[debug][text]")
{
    const std::string_view text = "RENDERER: OPENGL 4.5\nCAMERA X -12.5";
    const Vec2 origin{40.0F, 30.0F};
    TextMeshBuilder builder;
    builder.addText(text, origin, plain(3.0F));
    const Vec2 size = measureText(text, 3.0F);
    for (const Vec3& p : builder.mesh().positions) {
        CHECK(p.x >= origin.x);
        CHECK(p.y >= origin.y);
        CHECK(p.x <= origin.x + size.x);
        CHECK(p.y <= origin.y + size.y);
    }
}

TEST_CASE("Overlay quads are front-facing after the overlay projection", "[debug][text]")
{
    TextMeshBuilder builder;
    builder.addText("LOW POLY 0123", {10.0F, 10.0F}, TextStyle{});
    builder.addRect({0.0F, 0.0F}, {200.0F, 50.0F}, Vec4{0.0F, 0.0F, 0.0F, 0.5F});
    const auto projection = overlayProjection(640, 360);
    const auto& mesh = builder.mesh();
    REQUIRE(mesh.triangleCount() > 0);
    for (std::size_t t = 0; t < mesh.triangleCount(); ++t) {
        Vec2 ndc[3];
        for (std::size_t k = 0; k < 3; ++k) {
            const Vec4 clip = projection * Vec4(mesh.positions[mesh.indices[t * 3 + k]], 1.0F);
            ndc[k] = Vec2(clip) / clip.w;
        }
        const Vec2 e1 = ndc[1] - ndc[0];
        const Vec2 e2 = ndc[2] - ndc[0];
        INFO("triangle " << t);
        CHECK(e1.x * e2.y - e1.y * e2.x > 0.0F); // counter-clockwise
    }
}

TEST_CASE("overlayProjection maps pixels with the origin at the top left", "[debug][text]")
{
    const auto projection = overlayProjection(800, 600);
    const Vec4 topLeft = projection * Vec4(0.0F, 0.0F, 0.0F, 1.0F);
    const Vec4 bottomRight = projection * Vec4(800.0F, 600.0F, 0.0F, 1.0F);
    CHECK(topLeft.x == Approx(-1.0F));
    CHECK(topLeft.y == Approx(1.0F));
    CHECK(bottomRight.x == Approx(1.0F));
    CHECK(bottomRight.y == Approx(-1.0F));

    // A zero-sized framebuffer still gives a finite matrix.
    const auto degenerate = overlayProjection(0, 0);
    CHECK(std::isfinite(degenerate[0][0]));
    CHECK(std::isfinite(degenerate[1][1]));
}

TEST_CASE("Rectangles with no area are skipped", "[debug][text]")
{
    TextMeshBuilder builder;
    builder.addRect({0.0F, 0.0F}, {0.0F, 10.0F}, Vec4{1.0F});
    builder.addRect({0.0F, 0.0F}, {10.0F, -1.0F}, Vec4{1.0F});
    CHECK(builder.mesh().empty());
}
