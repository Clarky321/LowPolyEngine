#include "engine/core/Image.hpp"

#include "engine/core/FileSystem.hpp"

#include <cstddef>
#include <exception>
#include <string>
#include <utility>

// File output goes through writeBinaryFile (Unicode paths, error reporting),
// so stb's own stdio code is not compiled.
#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace engine::core {

namespace {

constexpr int kChannels = 3;

struct PngOutput {
    std::vector<std::uint8_t> bytes;
    bool failed = false;
};

// stb calls this with consecutive chunks of the encoded file. It is a C
// callback, so it must not let an exception escape.
void appendChunk(void* context, void* data, int size)
{
    auto* output = static_cast<PngOutput*>(context);
    if (output->failed || size <= 0) {
        return;
    }
    const auto* begin = static_cast<const std::uint8_t*>(data);
    try {
        output->bytes.insert(output->bytes.end(), begin, begin + size);
    } catch (const std::exception&) {
        output->failed = true;
    }
}

} // namespace

std::optional<Error> validate(const Image& image)
{
    if (image.width <= 0 || image.height <= 0 || image.width > kMaxImageExtent ||
        image.height > kMaxImageExtent) {
        return Error{"image size must be in 1.." + std::to_string(kMaxImageExtent) + ", got " +
                     std::to_string(image.width) + "x" + std::to_string(image.height)};
    }
    const auto expected =
        static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * kChannels;
    if (image.pixels.size() != expected) {
        return Error{"image has " + std::to_string(image.pixels.size()) + " bytes, expected " +
                     std::to_string(expected) + " for " + std::to_string(image.width) + "x" +
                     std::to_string(image.height) + " RGB"};
    }
    return std::nullopt;
}

Result<std::vector<std::uint8_t>> encodePng(const Image& image)
{
    if (auto error = validate(image)) {
        return std::move(*error);
    }
    PngOutput output;
    const int strideBytes = image.width * kChannels;
    const int ok = stbi_write_png_to_func(&appendChunk, &output, image.width, image.height,
                                          kChannels, image.pixels.data(), strideBytes);
    if (ok == 0 || output.failed) {
        return Error{"PNG encoding failed"};
    }
    return std::move(output.bytes);
}

std::optional<Error> writePng(const std::filesystem::path& path, const Image& image)
{
    auto png = encodePng(image);
    if (!png) {
        return Error{"cannot write '" + toUtf8(path) + "': " + png.error().message};
    }
    return writeBinaryFile(path, png.value());
}

} // namespace engine::core
