#include "engine/core/Image.hpp"

#include "engine/core/FileSystem.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <memory>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

// Declarations only. The implementations are compiled in third_party/stb_image/
// and third_party/stb_image_write/ without project warning flags (ADR 0006,
// ADR 0008). Files are read and written by project code (Unicode paths, error
// reporting), so stb is built without its stdio code.
#include <stb_image.h>
#include <stb_image_write.h>

namespace engine::core {

namespace {

// stb works on unsigned char buffers; std::uint8_t is the same type here.
static_assert(std::is_same_v<std::uint8_t, stbi_uc>);

constexpr std::array<std::uint8_t, 8> kPngSignature{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

const char* formatName(PixelFormat format) noexcept
{
    return format == PixelFormat::Rgba8 ? "RGBA" : "RGB";
}

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

// Frees pixels returned by stbi_load_from_memory.
struct StbPixelsDeleter {
    void operator()(stbi_uc* pixels) const noexcept { stbi_image_free(pixels); }
};

// stb keeps the reason of its last failure per thread.
std::string stbFailureReason()
{
    const char* reason = stbi_failure_reason();
    return reason != nullptr ? reason : "unknown error";
}

std::string sizeText(int width, int height)
{
    return std::to_string(width) + "x" + std::to_string(height);
}

} // namespace

std::optional<Error> validate(const Image& image)
{
    if (image.width <= 0 || image.height <= 0 || image.width > kMaxImageExtent ||
        image.height > kMaxImageExtent) {
        return Error{"image size must be in 1.." + std::to_string(kMaxImageExtent) + ", got " +
                     sizeText(image.width, image.height)};
    }
    const auto expected = static_cast<std::size_t>(image.width) *
                          static_cast<std::size_t>(image.height) *
                          static_cast<std::size_t>(channelCount(image.format));
    if (image.pixels.size() != expected) {
        return Error{"image has " + std::to_string(image.pixels.size()) + " bytes, expected " +
                     std::to_string(expected) + " for " + sizeText(image.width, image.height) +
                     " " + formatName(image.format)};
    }
    return std::nullopt;
}

Result<std::vector<std::uint8_t>> encodePng(const Image& image)
{
    if (auto error = validate(image)) {
        return std::move(*error);
    }
    PngOutput output;
    const int channels = channelCount(image.format);
    const int strideBytes = image.width * channels;
    const int ok = stbi_write_png_to_func(&appendChunk, &output, image.width, image.height,
                                          channels, image.pixels.data(), strideBytes);
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

Result<Image> decodePng(std::span<const std::uint8_t> bytes)
{
    if (bytes.empty()) {
        return Error{"PNG data is empty"};
    }
    if (bytes.size() > kMaxPngFileBytes) {
        return Error{"PNG data has " + std::to_string(bytes.size()) + " bytes; at most " +
                     std::to_string(kMaxPngFileBytes) + " are accepted"};
    }
    if (bytes.size() < kPngSignature.size() ||
        !std::equal(kPngSignature.begin(), kPngSignature.end(), bytes.begin())) {
        return Error{"not a PNG file (the signature does not match)"};
    }
    // Fits: kMaxPngFileBytes is far below INT_MAX.
    const auto length = static_cast<int>(bytes.size());

    // Size limits come from the header, before anything is decoded or allocated.
    int width = 0;
    int height = 0;
    int channelsInFile = 0;
    if (stbi_info_from_memory(bytes.data(), length, &width, &height, &channelsInFile) == 0) {
        return Error{"corrupt PNG header (" + stbFailureReason() + ")"};
    }
    if (width <= 0 || height <= 0 || width > kMaxImageExtent || height > kMaxImageExtent) {
        return Error{"PNG size " + sizeText(width, height) + " is outside 1.." +
                     std::to_string(kMaxImageExtent) + " per side"};
    }
    const std::size_t pixelCount =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (pixelCount > kMaxPngPixels) {
        return Error{"PNG size " + sizeText(width, height) + " has " + std::to_string(pixelCount) +
                     " pixels; at most " + std::to_string(kMaxPngPixels) + " are accepted"};
    }

    constexpr int kRgba = 4;
    int decodedWidth = 0;
    int decodedHeight = 0;
    const std::unique_ptr<stbi_uc, StbPixelsDeleter> pixels{stbi_load_from_memory(
        bytes.data(), length, &decodedWidth, &decodedHeight, &channelsInFile, kRgba)};
    if (!pixels) {
        return Error{"corrupt PNG data (" + stbFailureReason() + ")"};
    }
    if (decodedWidth != width || decodedHeight != height) {
        return Error{"PNG size changed while decoding: header " + sizeText(width, height) +
                     ", data " + sizeText(decodedWidth, decodedHeight)};
    }

    Image image;
    image.width = width;
    image.height = height;
    image.format = PixelFormat::Rgba8;
    try {
        image.pixels.assign(pixels.get(),
                            pixels.get() + pixelCount * static_cast<std::size_t>(kRgba));
    } catch (const std::exception&) {
        // bad_alloc must not escape: callers expect a Result.
        return Error{"not enough memory for a " + sizeText(width, height) + " image"};
    }
    return image;
}

Result<Image> readPng(const std::filesystem::path& path)
{
    const auto failure = [&path](const std::string& reason) {
        return Error{"cannot read PNG '" + toUtf8(path) + "': " + reason};
    };
    // Check the size before reading: the whole file is loaded into memory.
    std::error_code error;
    const auto fileSize = std::filesystem::file_size(path, error);
    if (!error && fileSize > kMaxPngFileBytes) {
        return failure("the file has " + std::to_string(fileSize) + " bytes; at most " +
                       std::to_string(kMaxPngFileBytes) + " are accepted");
    }
    // A missing file or a folder is reported by readTextFile, with the path.
    auto contents = readTextFile(path);
    if (!contents) {
        return contents.error();
    }
    const std::string& data = contents.value();
    // char and std::uint8_t have the same size and representation.
    auto image =
        decodePng(std::span(reinterpret_cast<const std::uint8_t*>(data.data()), data.size()));
    if (!image) {
        return failure(image.error().message);
    }
    return image;
}

} // namespace engine::core
