#pragma once

#include "engine/core/Result.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace engine::core {

// Layout of one pixel: 8 bits per channel, in this channel order.
enum class PixelFormat {
    Rgb8,
    Rgba8,
};

[[nodiscard]] constexpr int channelCount(PixelFormat format) noexcept
{
    return format == PixelFormat::Rgba8 ? 4 : 3;
}

// 8-bit image in memory: channelCount(format) bytes per pixel, rows from top
// to bottom, no padding. `format` comes last, so Image{width, height, pixels}
// is an RGB image (frame captures).
struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
    PixelFormat format = PixelFormat::Rgb8;
};

// Largest accepted width/height; far above any display, rejects garbage sizes.
inline constexpr int kMaxImageExtent = 16384;

// Limits for decoding PNG files, which may come from outside the project
// (ADR 0008). The pixel limit keeps a small, highly compressed file from
// making the decoder allocate gigabytes: 64 Mi pixels are 256 MiB as RGBA.
inline constexpr std::size_t kMaxPngFileBytes = std::size_t{256} * 1024 * 1024;
inline constexpr std::size_t kMaxPngPixels = std::size_t{64} * 1024 * 1024;

// Checks the size limits and that pixels holds exactly
// width * height * channelCount(format) bytes.
[[nodiscard]] std::optional<Error> validate(const Image& image);

// Encodes the image as a PNG file in memory (RGB or RGBA, 8 bits per channel).
[[nodiscard]] Result<std::vector<std::uint8_t>> encodePng(const Image& image);

// Encodes and writes a PNG file, creating missing parent folders.
[[nodiscard]] std::optional<Error> writePng(const std::filesystem::path& path, const Image& image);

// Decodes a PNG file held in memory into an RGBA8 image, top row first. Every
// PNG color type and bit depth is converted: gray and palette colors are
// expanded, missing alpha becomes 255, 16-bit channels keep their high byte.
// The input is treated as untrusted: data larger than kMaxPngFileBytes, a
// wrong signature, and sizes outside 1..kMaxImageExtent per side or above
// kMaxPngPixels in total (read from the header, before decoding) are
// rejected, as is any corrupt data.
[[nodiscard]] Result<Image> decodePng(std::span<const std::uint8_t> bytes);

// Reads and decodes a PNG file (see decodePng). A file larger than
// kMaxPngFileBytes is rejected before it is read. Errors name the file.
[[nodiscard]] Result<Image> readPng(const std::filesystem::path& path);

} // namespace engine::core
