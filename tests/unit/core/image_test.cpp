#include "engine/core/FileSystem.hpp"
#include "engine/core/Image.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using engine::core::encodePng;
using engine::core::Image;
using engine::core::validate;
using engine::core::writePng;

namespace {

Image solidImage(int width, int height, std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    Image image{width, height, {}};
    for (int i = 0; i < width * height; ++i) {
        image.pixels.insert(image.pixels.end(), {r, g, b});
    }
    return image;
}

std::uint32_t readBigEndian32(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    return (std::uint32_t{bytes[offset]} << 24U) | (std::uint32_t{bytes[offset + 1]} << 16U) |
           (std::uint32_t{bytes[offset + 2]} << 8U) | std::uint32_t{bytes[offset + 3]};
}

// Removes a folder tree created by a test at scope exit.
class TempFolder {
public:
    explicit TempFolder(const std::filesystem::path& name)
        : path_(std::filesystem::temp_directory_path() / name)
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
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

constexpr std::array<std::uint8_t, 8> kPngSignature{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

} // namespace

TEST_CASE("Image validation accepts consistent RGB images", "[core][image]")
{
    CHECK_FALSE(validate(solidImage(1, 1, 0, 0, 0)).has_value());
    CHECK_FALSE(validate(solidImage(7, 3, 10, 20, 30)).has_value());
}

TEST_CASE("Image validation rejects bad sizes and pixel counts", "[core][image]")
{
    CHECK(validate(Image{}).has_value());
    CHECK(validate(Image{0, 4, {}}).has_value());
    CHECK(validate(Image{-2, 4, {}}).has_value());
    CHECK(validate(Image{engine::core::kMaxImageExtent + 1, 1, {}}).has_value());

    Image tooShort = solidImage(4, 4, 1, 2, 3);
    tooShort.pixels.pop_back();
    const auto error = validate(tooShort);
    REQUIRE(error.has_value());
    CHECK(error->message.find("expected 48") != std::string::npos);
}

TEST_CASE("encodePng writes a PNG signature and the image size", "[core][image]")
{
    const auto png = encodePng(solidImage(5, 3, 200, 100, 50));
    REQUIRE(png.hasValue());
    const auto& bytes = png.value();
    REQUIRE(bytes.size() > 33);
    CHECK(std::equal(kPngSignature.begin(), kPngSignature.end(), bytes.begin()));
    // The first chunk is IHDR: length, type, then width and height (big endian).
    CHECK(std::string(bytes.begin() + 12, bytes.begin() + 16) == "IHDR");
    CHECK(readBigEndian32(bytes, 16) == 5);
    CHECK(readBigEndian32(bytes, 20) == 3);
}

TEST_CASE("Images are RGB by default and may be RGBA", "[core][image]")
{
    using engine::core::channelCount;
    using engine::core::PixelFormat;
    CHECK(channelCount(PixelFormat::Rgb8) == 3);
    CHECK(channelCount(PixelFormat::Rgba8) == 4);
    CHECK(Image{}.format == PixelFormat::Rgb8);

    const Image rgba{2, 2, std::vector<std::uint8_t>(16, 7), PixelFormat::Rgba8};
    CHECK_FALSE(validate(rgba).has_value());
    // The same bytes as RGB are too many.
    CHECK(validate(Image{2, 2, rgba.pixels}).has_value());

    Image tooShort = rgba;
    tooShort.pixels.resize(12);
    const auto error = validate(tooShort);
    REQUIRE(error.has_value());
    CHECK(error->message.find("expected 16 for 2x2 RGBA") != std::string::npos);
}

TEST_CASE("encodePng writes RGB and RGBA color types", "[core][image]")
{
    // IHDR color type (byte 25 of the file): 2 = RGB, 6 = RGBA.
    const auto rgb = encodePng(solidImage(2, 2, 1, 2, 3));
    REQUIRE(rgb.hasValue());
    CHECK(rgb.value()[25] == 2);

    const auto rgba = encodePng(
        Image{2, 2, std::vector<std::uint8_t>(16, 200), engine::core::PixelFormat::Rgba8});
    REQUIRE(rgba.hasValue());
    CHECK(rgba.value()[25] == 6);
}

TEST_CASE("encodePng compresses flat images", "[core][image]")
{
    // Screenshots of flat-shaded scenes must stay small enough for the repository.
    const auto png = encodePng(solidImage(256, 256, 90, 160, 220));
    REQUIRE(png.hasValue());
    CHECK(png.value().size() < 256 * 256 * 3 / 20);
}

TEST_CASE("encodePng rejects an invalid image", "[core][image]")
{
    const auto png = encodePng(Image{2, 2, {1, 2, 3}});
    REQUIRE_FALSE(png.hasValue());
    CHECK(png.error().message.find("expected 12") != std::string::npos);
}

TEST_CASE("writePng creates parent folders, also with non-ASCII names", "[core][image]")
{
    const TempFolder folder(u8"lpe_image_test_снимки");
    const auto path = folder.path() / "nested" / u8"кадр.png";
    REQUIRE_FALSE(writePng(path, solidImage(4, 2, 255, 0, 0)).has_value());
    REQUIRE(std::filesystem::is_regular_file(path));

    const auto contents = engine::core::readTextFile(path);
    REQUIRE(contents.hasValue());
    CHECK(contents.value().compare(0, 4, "\x89PNG") == 0);
}

TEST_CASE("writePng reports an invalid image with the target path", "[core][image]")
{
    const TempFolder folder("lpe_image_test_invalid");
    const auto error = writePng(folder.path() / "broken.png", Image{});
    REQUIRE(error.has_value());
    CHECK(error->message.find("broken.png") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(folder.path() / "broken.png"));
}

TEST_CASE("writeBinaryFile replaces existing contents", "[core][filesystem]")
{
    const TempFolder folder("lpe_write_binary_test");
    const auto path = folder.path() / "data.bin";
    const std::vector<std::uint8_t> first{1, 2, 3, 4, 5};
    const std::vector<std::uint8_t> second{9, 8};
    REQUIRE_FALSE(engine::core::writeBinaryFile(path, first).has_value());
    REQUIRE_FALSE(engine::core::writeBinaryFile(path, second).has_value());

    const auto contents = engine::core::readTextFile(path);
    REQUIRE(contents.hasValue());
    CHECK(contents.value() == std::string("\x09\x08"));
}

TEST_CASE("writeBinaryFile reports a path that cannot be created", "[core][filesystem]")
{
    const TempFolder folder("lpe_write_binary_blocked");
    // A regular file where a folder is needed blocks create_directories.
    const auto blocker = folder.path() / "blocker";
    REQUIRE_FALSE(engine::core::writeBinaryFile(blocker, std::vector<std::uint8_t>{1}).has_value());

    const auto error =
        engine::core::writeBinaryFile(blocker / "inside.bin", std::vector<std::uint8_t>{1});
    REQUIRE(error.has_value());
    CHECK(error->message.find("blocker") != std::string::npos);
}
