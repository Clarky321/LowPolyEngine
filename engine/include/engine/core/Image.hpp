#pragma once

#include "engine/core/Result.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace engine::core {

// 8-bit RGB image in memory: 3 bytes per pixel, rows from top to bottom, no padding.
struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
};

// Largest accepted width/height; far above any display, rejects garbage sizes.
inline constexpr int kMaxImageExtent = 16384;

// Checks the size limits and that pixels holds exactly width * height * 3 bytes.
[[nodiscard]] std::optional<Error> validate(const Image& image);

// Encodes the image as a PNG file in memory.
[[nodiscard]] Result<std::vector<std::uint8_t>> encodePng(const Image& image);

// Encodes and writes a PNG file, creating missing parent folders.
[[nodiscard]] std::optional<Error> writePng(const std::filesystem::path& path, const Image& image);

} // namespace engine::core
