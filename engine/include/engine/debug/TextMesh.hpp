#pragma once

#include "engine/core/Math.hpp"
#include "engine/geometry/MeshData.hpp"

#include <optional>
#include <string_view>

namespace engine::debug {

// Debug text without textures: a built-in 5x7 pixel font turned into colored
// quads (MeshData), drawn like any other mesh with overlayProjection().
// Meant for on-screen diagnostics, not for game UI.
//
// Covers A-Z (lowercase is drawn as uppercase), 0-9, space and
// . , : ; - + / ( ) % = _ ' " ! ? [ ] < > # * |
// Any other character is drawn as a hollow box.

inline constexpr int kGlyphWidth = 5;
inline constexpr int kGlyphHeight = 7;
// Horizontal distance between characters, including one empty column.
inline constexpr int kGlyphAdvance = 6;
// Vertical distance between lines, including two empty rows.
inline constexpr int kLineAdvance = 9;

[[nodiscard]] bool hasGlyph(char c) noexcept;

// Width of the widest line and height of all lines, in pixels at `scale`
// (font pixels per screen pixel). '\n' starts a new line. Empty text is 0 x 0.
[[nodiscard]] math::Vec2 measureText(std::string_view text, float scale) noexcept;

struct TextStyle {
    // Screen pixels per font pixel.
    float scale = 2.0F;
    math::Vec4 color{1.0F};
    // Drawn one font pixel down-right behind the text, for contrast on any background.
    std::optional<math::Vec4> shadowColor = math::Vec4{0.0F, 0.0F, 0.0F, 0.6F};
};

// Collects text and rectangles into one mesh in overlay pixel coordinates:
// origin at the top-left corner of the framebuffer, y pointing down.
class TextMeshBuilder {
public:
    void clear() noexcept;

    void addText(std::string_view text, math::Vec2 topLeft, const TextStyle& style);
    void addRect(math::Vec2 topLeft, math::Vec2 size, const math::Vec4& color);

    [[nodiscard]] const geometry::MeshData& mesh() const noexcept { return mesh_; }

private:
    void addQuad(float left, float top, float right, float bottom, const math::Vec4& color);
    void addGlyphs(std::string_view text, math::Vec2 topLeft, float scale, const math::Vec4& color);

    geometry::MeshData mesh_;
};

// Orthographic projection for overlay meshes: maps pixel (0, 0) to the
// top-left corner and (width, height) to the bottom-right corner. Front faces
// built by TextMeshBuilder stay counter-clockwise after the y flip.
[[nodiscard]] math::Mat4 overlayProjection(int width, int height);

} // namespace engine::debug
