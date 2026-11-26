#pragma once

// Pixel helpers for the editor's GPU tests (M5.2, M5.3, M5.7): colours of a
// frame read back as PNG, and the shell's areas in it.

#include "application/EditorShell.hpp"

#include "engine/core/Image.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <map>
#include <string_view>
#include <tuple>
#include <vector>

namespace lpe_gpu_test {

struct Rgb {
    int r = 0;
    int g = 0;
    int b = 0;
};

inline Rgb pixelAt(const engine::core::Image& image, int x, int y)
{
    const int channels = engine::core::channelCount(image.format);
    const std::size_t at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
                            static_cast<std::size_t>(x)) *
                           static_cast<std::size_t>(channels);
    return {image.pixels[at], image.pixels[at + 1], image.pixels[at + 2]};
}

// The largest difference of one channel.
inline int difference(const Rgb& a, const Rgb& b)
{
    return std::max({std::abs(a.r - b.r), std::abs(a.g - b.g), std::abs(a.b - b.b)});
}

inline bool near(const Rgb& a, const Rgb& b, int tolerance)
{
    return difference(a, b) <= tolerance;
}

inline int luminance(const Rgb& c)
{
    return (c.r * 3 + c.g * 6 + c.b) / 10;
}

inline const editor::ShellArea& area(const std::vector<editor::ShellArea>& areas,
                                     std::string_view name)
{
    const auto found = std::find_if(areas.begin(), areas.end(),
                                    [name](const editor::ShellArea& a) { return a.name == name; });
    REQUIRE(found != areas.end());
    return *found;
}

inline Rgb centerOf(const engine::core::Image& image, const editor::ShellArea& a)
{
    return pixelAt(image, a.x + a.width / 2, a.y + a.height / 2);
}

// The colour most pixels of an area have: a panel's background, whatever text
// or widgets lie on it.
inline Rgb dominantColor(const engine::core::Image& image, const editor::ShellArea& a)
{
    std::map<std::tuple<int, int, int>, int> counts;
    for (int y = a.y; y < a.y + a.height; ++y) {
        for (int x = a.x; x < a.x + a.width; ++x) {
            const Rgb c = pixelAt(image, x, y);
            ++counts[{c.r, c.g, c.b}];
        }
    }
    const auto most =
        std::max_element(counts.begin(), counts.end(),
                         [](const auto& l, const auto& r) { return l.second < r.second; });
    REQUIRE(most != counts.end());
    return {std::get<0>(most->first), std::get<1>(most->first), std::get<2>(most->first)};
}

// The pixels of a rectangle brighter than `threshold`: light text on the dark
// theme.
inline int brightPixels(const engine::core::Image& image, int x0, int y0, int width, int height,
                        int threshold)
{
    int count = 0;
    for (int y = std::max(y0, 0); y < std::min(y0 + height, image.height); ++y) {
        for (int x = std::max(x0, 0); x < std::min(x0 + width, image.width); ++x) {
            count += luminance(pixelAt(image, x, y)) > threshold ? 1 : 0;
        }
    }
    return count;
}

} // namespace lpe_gpu_test
