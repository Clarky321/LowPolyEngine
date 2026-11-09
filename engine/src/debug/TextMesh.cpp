#include "engine/debug/TextMesh.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace engine::debug {

namespace {

// One glyph: 7 rows, top first; bit 4 is the leftmost of 5 columns.
struct Glyph {
    char character;
    std::array<std::uint8_t, kGlyphHeight> rows;
};

// One glyph per line keeps the table readable.
// clang-format off
constexpr std::array kGlyphs{
    Glyph{' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    Glyph{'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    Glyph{'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    Glyph{'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    Glyph{'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
    Glyph{'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    Glyph{'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    Glyph{'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}},
    Glyph{'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    Glyph{'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    Glyph{'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},
    Glyph{'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    Glyph{'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    Glyph{'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    Glyph{'N', {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}},
    Glyph{'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    Glyph{'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    Glyph{'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
    Glyph{'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    Glyph{'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    Glyph{'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    Glyph{'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    Glyph{'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    Glyph{'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}},
    Glyph{'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    Glyph{'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
    Glyph{'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    Glyph{'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    Glyph{'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    Glyph{'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    Glyph{'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
    Glyph{'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    Glyph{'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    Glyph{'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    Glyph{'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    Glyph{'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    Glyph{'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    Glyph{'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
    Glyph{',', {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08}},
    Glyph{':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}},
    Glyph{';', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x04, 0x08}},
    Glyph{'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    Glyph{'+', {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}},
    Glyph{'/', {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10}},
    Glyph{'(', {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}},
    Glyph{')', {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}},
    Glyph{'%', {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03}},
    Glyph{'=', {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}},
    Glyph{'_', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}},
    Glyph{'\'', {0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00}},
    Glyph{'"', {0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00}},
    Glyph{'!', {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}},
    Glyph{'?', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}},
    Glyph{'[', {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E}},
    Glyph{']', {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E}},
    Glyph{'<', {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}},
    Glyph{'>', {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}},
    Glyph{'#', {0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A}},
    Glyph{'*', {0x00, 0x04, 0x15, 0x0E, 0x15, 0x04, 0x00}},
    Glyph{'|', {0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
};
// clang-format on

// Drawn for characters the font does not cover.
constexpr std::array<std::uint8_t, kGlyphHeight> kMissingGlyph{0x1F, 0x11, 0x11, 0x11,
                                                               0x11, 0x11, 0x1F};

constexpr bool rowsFitWidth()
{
    for (const Glyph& glyph : kGlyphs) {
        for (const std::uint8_t row : glyph.rows) {
            if (row >= (1U << kGlyphWidth)) {
                return false;
            }
        }
    }
    return true;
}
static_assert(rowsFitWidth(), "glyph rows must use only the low kGlyphWidth bits");

char toFontCase(char c) noexcept
{
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

const Glyph* findGlyph(char c) noexcept
{
    const char key = toFontCase(c);
    const auto it = std::find_if(kGlyphs.begin(), kGlyphs.end(),
                                 [key](const Glyph& glyph) { return glyph.character == key; });
    return it != kGlyphs.end() ? &*it : nullptr;
}

const std::array<std::uint8_t, kGlyphHeight>& glyphRows(char c) noexcept
{
    const Glyph* glyph = findGlyph(c);
    return glyph != nullptr ? glyph->rows : kMissingGlyph;
}

bool isUsableScale(float scale) noexcept
{
    return std::isfinite(scale) && scale > 0.0F;
}

} // namespace

bool hasGlyph(char c) noexcept
{
    return findGlyph(c) != nullptr;
}

math::Vec2 measureText(std::string_view text, float scale) noexcept
{
    if (text.empty() || !isUsableScale(scale)) {
        return math::Vec2{0.0F};
    }
    std::size_t lines = 1;
    std::size_t lineLength = 0;
    std::size_t longest = 0;
    for (const char c : text) {
        if (c == '\n') {
            ++lines;
            lineLength = 0;
        } else {
            longest = std::max(longest, ++lineLength);
        }
    }
    // The last character of a line and the last line have no trailing spacing.
    const float width =
        longest == 0 ? 0.0F
                     : static_cast<float>(longest * kGlyphAdvance - (kGlyphAdvance - kGlyphWidth));
    const float height = static_cast<float>((lines - 1) * kLineAdvance + kGlyphHeight);
    return math::Vec2{width, height} * scale;
}

void TextMeshBuilder::clear() noexcept
{
    mesh_.positions.clear();
    mesh_.colors.clear();
    mesh_.indices.clear();
}

void TextMeshBuilder::addText(std::string_view text, math::Vec2 topLeft, const TextStyle& style)
{
    if (!isUsableScale(style.scale)) {
        return;
    }
    if (style.shadowColor) {
        addGlyphs(text, topLeft + math::Vec2{style.scale}, style.scale, *style.shadowColor);
    }
    addGlyphs(text, topLeft, style.scale, style.color);
}

void TextMeshBuilder::addRect(math::Vec2 topLeft, math::Vec2 size, const math::Vec4& color)
{
    if (!(size.x > 0.0F) || !(size.y > 0.0F)) {
        return;
    }
    addQuad(topLeft.x, topLeft.y, topLeft.x + size.x, topLeft.y + size.y, color);
}

void TextMeshBuilder::addGlyphs(std::string_view text, math::Vec2 topLeft, float scale,
                                const math::Vec4& color)
{
    float penX = topLeft.x;
    float penY = topLeft.y;
    for (const char c : text) {
        if (c == '\n') {
            penX = topLeft.x;
            penY += static_cast<float>(kLineAdvance) * scale;
            continue;
        }
        const auto& rows = glyphRows(c);
        for (int row = 0; row < kGlyphHeight; ++row) {
            const unsigned bits = rows[static_cast<std::size_t>(row)];
            const float top = penY + static_cast<float>(row) * scale;
            // One quad per horizontal run of lit pixels keeps the mesh small.
            int column = 0;
            while (column < kGlyphWidth) {
                const auto lit = [bits](int col) {
                    return ((bits >> static_cast<unsigned>(kGlyphWidth - 1 - col)) & 1U) != 0;
                };
                if (!lit(column)) {
                    ++column;
                    continue;
                }
                const int runStart = column;
                while (column < kGlyphWidth && lit(column)) {
                    ++column;
                }
                addQuad(penX + static_cast<float>(runStart) * scale, top,
                        penX + static_cast<float>(column) * scale, top + scale, color);
            }
        }
        penX += static_cast<float>(kGlyphAdvance) * scale;
    }
}

void TextMeshBuilder::addQuad(float left, float top, float right, float bottom,
                              const math::Vec4& color)
{
    // Top-left, bottom-left, bottom-right, top-right: counter-clockwise on
    // screen once overlayProjection flips y.
    const auto first = static_cast<std::uint32_t>(mesh_.positions.size());
    mesh_.positions.insert(mesh_.positions.end(),
                           {math::Vec3{left, top, 0.0F}, math::Vec3{left, bottom, 0.0F},
                            math::Vec3{right, bottom, 0.0F}, math::Vec3{right, top, 0.0F}});
    mesh_.colors.insert(mesh_.colors.end(), 4, color);
    mesh_.indices.insert(mesh_.indices.end(),
                         {first, first + 1, first + 2, first, first + 2, first + 3});
}

math::Mat4 overlayProjection(int width, int height)
{
    const float w = width > 0 ? static_cast<float>(width) : 1.0F;
    const float h = height > 0 ? static_cast<float>(height) : 1.0F;
    return glm::ortho(0.0F, w, h, 0.0F, -1.0F, 1.0F);
}

} // namespace engine::debug
