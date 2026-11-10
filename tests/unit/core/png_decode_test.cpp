#include "engine/core/FileSystem.hpp"
#include "engine/core/Image.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <system_error>
#include <vector>

using engine::core::decodePng;
using engine::core::encodePng;
using engine::core::Image;
using engine::core::kMaxImageExtent;
using engine::core::kMaxPngFileBytes;
using engine::core::kMaxPngPixels;
using engine::core::PixelFormat;
using engine::core::readPng;
using engine::core::validate;
using engine::core::writePng;

namespace {

using Bytes = std::vector<std::uint8_t>;
using Rgba = std::array<std::uint8_t, 4>;

// --- Minimal PNG writer for test fixtures --------------------------------------
// Builds the files stb_image_write cannot write (gray, palette, 16 bits per
// channel) and broken ones. Every scanline uses filter 0, and the image data
// is one zlib stream of stored (uncompressed) deflate blocks.

std::uint32_t crc32(std::span<const std::uint8_t> data)
{
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const std::uint8_t byte : data) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1U) != 0 ? (crc >> 1U) ^ 0xEDB88320U : crc >> 1U;
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

std::uint32_t adler32(std::span<const std::uint8_t> data)
{
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (const std::uint8_t byte : data) {
        a = (a + byte) % 65521U;
        b = (b + a) % 65521U;
    }
    return (b << 16U) | a;
}

void appendBigEndian32(Bytes& out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>(value >> 24U));
    out.push_back(static_cast<std::uint8_t>(value >> 16U));
    out.push_back(static_cast<std::uint8_t>(value >> 8U));
    out.push_back(static_cast<std::uint8_t>(value));
}

void appendChunk(Bytes& png, const std::string& type, const Bytes& data)
{
    appendBigEndian32(png, static_cast<std::uint32_t>(data.size()));
    Bytes typed;
    for (const char c : type) {
        typed.push_back(static_cast<std::uint8_t>(c));
    }
    typed.insert(typed.end(), data.begin(), data.end());
    png.insert(png.end(), typed.begin(), typed.end());
    appendBigEndian32(png, crc32(typed));
}

Bytes zlibStored(const Bytes& raw)
{
    Bytes out{0x78, 0x01};
    std::size_t offset = 0;
    do {
        const std::size_t length = std::min<std::size_t>(raw.size() - offset, 65535);
        const bool last = offset + length == raw.size();
        out.push_back(static_cast<std::uint8_t>(last ? 1U : 0U));
        out.push_back(static_cast<std::uint8_t>(length & 0xFFU));
        out.push_back(static_cast<std::uint8_t>(length >> 8U));
        out.push_back(static_cast<std::uint8_t>(~length & 0xFFU));
        out.push_back(static_cast<std::uint8_t>((~length >> 8U) & 0xFFU));
        const auto begin = raw.begin() + static_cast<std::ptrdiff_t>(offset);
        out.insert(out.end(), begin, begin + static_cast<std::ptrdiff_t>(length));
        offset += length;
    } while (offset < raw.size());
    appendBigEndian32(out, adler32(raw));
    return out;
}

// A test PNG from its header fields and image rows (without filter bytes).
// colorType: 0 gray, 2 RGB, 3 palette, 4 gray + alpha, 6 RGBA. palette holds
// the PLTE contents (RGB triples) of palette images, transparency the
// optional tRNS contents. A function rather than an aggregate, so leaving out
// the optional chunks needs no designated-initializer gaps (GCC warns about
// those with -Wmissing-field-initializers).
Bytes buildPng(std::uint32_t width, std::uint32_t height, std::uint8_t bitDepth,
               std::uint8_t colorType, const std::vector<Bytes>& rows, const Bytes& palette = {},
               const Bytes& transparency = {})
{
    Bytes png{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    Bytes header;
    appendBigEndian32(header, width);
    appendBigEndian32(header, height);
    // Bit depth, color type, compression, filter method, no interlacing.
    header.insert(header.end(), {bitDepth, colorType, 0, 0, 0});
    appendChunk(png, "IHDR", header);
    if (!palette.empty()) {
        appendChunk(png, "PLTE", palette);
    }
    if (!transparency.empty()) {
        appendChunk(png, "tRNS", transparency);
    }
    Bytes raw;
    for (const Bytes& row : rows) {
        raw.push_back(0); // filter type 0 (none)
        raw.insert(raw.end(), row.begin(), row.end());
    }
    appendChunk(png, "IDAT", zlibStored(raw));
    appendChunk(png, "IEND", {});
    return png;
}

// --- Helpers -------------------------------------------------------------------

Image decodeOrFail(const Bytes& png)
{
    auto image = decodePng(png);
    if (!image) {
        FAIL("decodePng failed: " << image.error().message);
    }
    REQUIRE_FALSE(validate(image.value()).has_value());
    REQUIRE(image.value().format == PixelFormat::Rgba8);
    return std::move(image).value();
}

Rgba pixelAt(const Image& image, int x, int y)
{
    const auto offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
                         static_cast<std::size_t>(x)) *
                        4;
    return {image.pixels[offset], image.pixels[offset + 1], image.pixels[offset + 2],
            image.pixels[offset + 3]};
}

bool failsWith(const engine::core::Result<Image>& result, const std::string& text)
{
    return !result.hasValue() && result.error().message.find(text) != std::string::npos;
}

// A small, compressed RGBA file with distinct pixels, written by encodePng.
Bytes patternPng()
{
    Image image{6, 5, {}, PixelFormat::Rgba8};
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            image.pixels.insert(image.pixels.end(), {static_cast<std::uint8_t>(x * 40),
                                                     static_cast<std::uint8_t>(y * 50),
                                                     static_cast<std::uint8_t>(200 - x * y),
                                                     static_cast<std::uint8_t>(255 - y * 30)});
        }
    }
    auto png = encodePng(image);
    REQUIRE(png.hasValue());
    return std::move(png).value();
}

// Removes a folder tree created by a test at scope exit.
class TempFolder {
public:
    explicit TempFolder(const std::filesystem::path& name)
        : path_(std::filesystem::temp_directory_path() / name)
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
        std::filesystem::create_directories(path_, ignored);
    }
    ~TempFolder()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

} // namespace

// --- Supported inputs ----------------------------------------------------------

TEST_CASE("decodePng reads RGB and RGBA files written by encodePng", "[core][image][png]")
{
    SECTION("RGB gets alpha 255")
    {
        const Image rgb{3, 2, {10, 20, 30, 40, 50, 60, 70, 80, 90, 1, 2, 3, 4, 5, 6, 7, 8, 9}};
        auto png = encodePng(rgb);
        REQUIRE(png.hasValue());
        const Image image = decodeOrFail(png.value());
        CHECK(image.width == 3);
        CHECK(image.height == 2);
        CHECK(pixelAt(image, 0, 0) == Rgba{10, 20, 30, 255});
        CHECK(pixelAt(image, 2, 0) == Rgba{70, 80, 90, 255});
        CHECK(pixelAt(image, 0, 1) == Rgba{1, 2, 3, 255});
        CHECK(pixelAt(image, 2, 1) == Rgba{7, 8, 9, 255});
    }
    SECTION("RGBA is kept exactly")
    {
        const Image rgba{
            2, 2, {255, 0, 0, 0, 0, 255, 0, 64, 0, 0, 255, 128, 9, 9, 9, 255}, PixelFormat::Rgba8};
        auto png = encodePng(rgba);
        REQUIRE(png.hasValue());
        const Image image = decodeOrFail(png.value());
        CHECK(image.pixels == rgba.pixels);
    }
}

TEST_CASE("decodePng keeps the top row first", "[core][image][png]")
{
    const Bytes png = buildPng(1, 2, 8, 6, {{255, 0, 0, 255}, {0, 0, 255, 255}});
    const Image image = decodeOrFail(png);
    CHECK(pixelAt(image, 0, 0) == Rgba{255, 0, 0, 255});
    CHECK(pixelAt(image, 0, 1) == Rgba{0, 0, 255, 255});
}

TEST_CASE("decodePng converts every PNG color type to RGBA8", "[core][image][png]")
{
    SECTION("gray, 8 bits")
    {
        const Image image = decodeOrFail(buildPng(3, 1, 8, 0, {{0, 128, 255}}));
        CHECK(pixelAt(image, 0, 0) == Rgba{0, 0, 0, 255});
        CHECK(pixelAt(image, 1, 0) == Rgba{128, 128, 128, 255});
        CHECK(pixelAt(image, 2, 0) == Rgba{255, 255, 255, 255});
    }
    SECTION("gray, 1 bit")
    {
        const Image image = decodeOrFail(buildPng(8, 1, 1, 0, {{0b10110000}}));
        const std::array<std::uint8_t, 8> expected{255, 0, 255, 255, 0, 0, 0, 0};
        for (int x = 0; x < 8; ++x) {
            INFO("pixel " << x);
            const std::uint8_t g = expected[static_cast<std::size_t>(x)];
            CHECK(pixelAt(image, x, 0) == Rgba{g, g, g, 255});
        }
    }
    SECTION("gray with alpha")
    {
        const Image image = decodeOrFail(buildPng(2, 1, 8, 4, {{10, 20, 30, 40}}));
        CHECK(pixelAt(image, 0, 0) == Rgba{10, 10, 10, 20});
        CHECK(pixelAt(image, 1, 0) == Rgba{30, 30, 30, 40});
    }
    SECTION("palette with transparency")
    {
        const Image image = decodeOrFail(
            buildPng(3, 1, 8, 3, {{0, 1, 2}}, {255, 0, 0, 0, 255, 0, 0, 0, 255}, {0, 128}));
        CHECK(pixelAt(image, 0, 0) == Rgba{255, 0, 0, 0});
        CHECK(pixelAt(image, 1, 0) == Rgba{0, 255, 0, 128});
        // Entries without a tRNS value are opaque.
        CHECK(pixelAt(image, 2, 0) == Rgba{0, 0, 255, 255});
    }
    SECTION("palette without transparency")
    {
        const Image image = decodeOrFail(buildPng(2, 1, 8, 3, {{1, 0}}, {10, 20, 30, 40, 50, 60}));
        CHECK(pixelAt(image, 0, 0) == Rgba{40, 50, 60, 255});
        CHECK(pixelAt(image, 1, 0) == Rgba{10, 20, 30, 255});
    }
    SECTION("RGB, 16 bits: the high byte is kept")
    {
        const Image image =
            decodeOrFail(buildPng(1, 1, 16, 2, {{0x12, 0xAB, 0x34, 0xCD, 0xFF, 0x00}}));
        CHECK(pixelAt(image, 0, 0) == Rgba{0x12, 0x34, 0xFF, 255});
    }
    SECTION("RGBA, 16 bits")
    {
        const Image image =
            decodeOrFail(buildPng(1, 1, 16, 6, {{0x80, 0x01, 0x40, 0x02, 0x20, 0x03, 0x10, 0x04}}));
        CHECK(pixelAt(image, 0, 0) == Rgba{0x80, 0x40, 0x20, 0x10});
    }
}

// --- Rejected inputs -----------------------------------------------------------

TEST_CASE("decodePng rejects data that is not a PNG", "[core][image][png]")
{
    CHECK(failsWith(decodePng(Bytes{}), "PNG data is empty"));
    CHECK(failsWith(decodePng(Bytes{'G', 'I', 'F', '8', '9', 'a', 1, 0, 1, 0, 0, 0, 0}),
                    "not a PNG file"));
    CHECK(failsWith(decodePng(Bytes{0xFF, 0xD8, 0xFF, 0xE0, 0, 16, 'J', 'F', 'I', 'F'}),
                    "not a PNG file"));
    // The first 7 bytes of the signature only.
    CHECK(failsWith(decodePng(Bytes{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A}), "not a PNG file"));
}

TEST_CASE("decodePng rejects truncated files", "[core][image][png]")
{
    const Bytes png = patternPng();
    const auto cut = [&png](std::size_t length) { return decodePng(std::span(png).first(length)); };
    CHECK(failsWith(cut(8), "corrupt PNG header"));  // signature only
    CHECK(failsWith(cut(20), "corrupt PNG header")); // inside IHDR
    // Inside the image data: the header is fine, decoding fails.
    CHECK(failsWith(cut(png.size() - 24), "corrupt PNG data"));
}

TEST_CASE("decodePng checks the size in the header before decoding", "[core][image][png]")
{
    // Each file holds a single tiny row: only the header claims the size.
    const auto withSize = [](std::uint32_t width, std::uint32_t height) {
        return buildPng(width, height, 8, 0, {{0}});
    };
    CHECK(failsWith(decodePng(withSize(0, 4)), "corrupt PNG header"));
    CHECK(failsWith(decodePng(withSize(4, 0)), "corrupt PNG header"));
    CHECK(failsWith(decodePng(withSize(kMaxImageExtent + 1, 1)),
                    "PNG size 16385x1 is outside 1..16384 per side"));
    CHECK(failsWith(decodePng(withSize(1, kMaxImageExtent + 1)),
                    "PNG size 1x16385 is outside 1..16384 per side"));
    // Within the per-side limit, but too many pixels in total.
    CHECK(failsWith(decodePng(withSize(kMaxImageExtent, kMaxImageExtent)),
                    "has 268435456 pixels; at most 67108864 are accepted"));
    CHECK(failsWith(decodePng(withSize(8192, 8193)), "at most 67108864 are accepted"));
    CHECK(kMaxPngPixels == 8192U * 8192U);
}

TEST_CASE("decodePng accepts the largest width", "[core][image][png]")
{
    // 16384 x 1 gray: a 16 KiB row, cheap to decode.
    const Bytes row(static_cast<std::size_t>(kMaxImageExtent), 77);
    const Image image = decodeOrFail(buildPng(kMaxImageExtent, 1, 8, 0, {row}));
    CHECK(image.width == kMaxImageExtent);
    CHECK(pixelAt(image, kMaxImageExtent - 1, 0) == Rgba{77, 77, 77, 255});
}

TEST_CASE("decodePng survives corrupted and truncated data", "[core][image][png]")
{
    // Every result is either an error or a valid image within the limits;
    // nothing may crash, hang or allocate without bound.
    const auto checkOutcome = [](const engine::core::Result<Image>& result) {
        if (result.hasValue()) {
            const Image& image = result.value();
            CHECK_FALSE(validate(image).has_value());
            CHECK(image.format == PixelFormat::Rgba8);
            CHECK(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) <=
                  kMaxPngPixels);
        } else {
            CHECK_FALSE(result.error().message.empty());
        }
    };
    const std::array<Bytes, 2> inputs{
        patternPng(),
        buildPng(3, 2, 8, 3, {{0, 1, 2}, {2, 1, 0}}, {255, 0, 0, 0, 255, 0, 0, 0, 255}, {0, 128}),
    };
    for (const Bytes& png : inputs) {
        for (std::size_t i = 0; i < png.size(); ++i) {
            INFO("byte " << i << " of " << png.size());
            Bytes flipped = png;
            flipped[i] = static_cast<std::uint8_t>(flipped[i] ^ 0xFFU);
            checkOutcome(decodePng(flipped));
            checkOutcome(decodePng(std::span(png).first(i)));
        }
    }
}

// --- Files ---------------------------------------------------------------------

TEST_CASE("readPng reads a file written by writePng, also with non-ASCII names",
          "[core][image][png]")
{
    const TempFolder folder(u8"lpe_png_read_текстуры");
    const auto path = folder.path() / u8"камень.png";
    const Image rgb{2, 1, {1, 2, 3, 4, 5, 6}};
    REQUIRE_FALSE(writePng(path, rgb).has_value());

    const auto image = readPng(path);
    REQUIRE(image.hasValue());
    CHECK(image.value().format == PixelFormat::Rgba8);
    CHECK(image.value().pixels == Bytes{1, 2, 3, 255, 4, 5, 6, 255});
}

TEST_CASE("readPng names the file in its errors", "[core][image][png]")
{
    const TempFolder folder("lpe_png_read_errors");

    const auto missing = readPng(folder.path() / "missing.png");
    CHECK(failsWith(missing, "missing.png"));

    CHECK_FALSE(readPng(folder.path()).hasValue()); // a folder, not a file

    const auto textPath = folder.path() / "notes.png";
    REQUIRE_FALSE(
        engine::core::writeBinaryFile(textPath, Bytes{'h', 'e', 'l', 'l', 'o'}).has_value());
    const auto text = readPng(textPath);
    CHECK(failsWith(text, "notes.png"));
    CHECK(failsWith(text, "not a PNG file"));
}

TEST_CASE("readPng rejects a file above the size limit before reading it", "[core][image][png]")
{
    const TempFolder folder("lpe_png_read_large");
    const auto path = folder.path() / "huge.png";
    REQUIRE_FALSE(engine::core::writeBinaryFile(path, Bytes{0}).has_value());
    // Only the file size is set (no data is written), so this stays cheap.
    std::error_code error;
    std::filesystem::resize_file(path, kMaxPngFileBytes + 1, error);
    REQUIRE_FALSE(error);

    const auto image = readPng(path);
    CHECK(failsWith(image, "huge.png"));
    CHECK(failsWith(image, "at most 268435456 are accepted"));
}
