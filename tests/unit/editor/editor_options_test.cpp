// lpe-editor's command line (M5.2): --frames, --screenshot, --size, and every
// way they can be wrong.

#include "application/EditorOptions.hpp"

#include <catch2/catch_test_macros.hpp>

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace {

engine::core::Result<editor::EditorOptions> parse(std::initializer_list<std::string_view> list)
{
    const std::vector<std::string_view> arguments(list);
    return editor::parseEditorOptions(arguments);
}

bool fails(std::initializer_list<std::string_view> list, std::string_view mentioning)
{
    const auto result = parse(list);
    return !result.hasValue() && result.error().message.find(mentioning) != std::string::npos;
}

} // namespace

TEST_CASE("Without arguments the editor runs until closed in a 1280x760 window",
          "[editor][options]")
{
    const auto options = parse({});
    REQUIRE(options.hasValue());
    CHECK_FALSE(options.value().maxFrames.has_value());
    CHECK_FALSE(options.value().screenshotPath.has_value());
    CHECK(options.value().width == 1280);
    CHECK(options.value().height == 760);
}

TEST_CASE("The editor reads its options --frames, --screenshot and --size", "[editor][options]")
{
    const auto options =
        parse({"--frames", "60", "--screenshot", "out/editor.png", "--size", "1024x640"});
    REQUIRE(options.hasValue());
    CHECK(options.value().maxFrames == 60U);
    CHECK(options.value().screenshotPath == std::filesystem::path("out/editor.png"));
    CHECK(options.value().width == 1024);
    CHECK(options.value().height == 640);

    // A screenshot alone runs a few frames first, then exits.
    const auto screenshot = parse({"--screenshot", "shot.png"});
    REQUIRE(screenshot.hasValue());
    CHECK(screenshot.value().maxFrames == editor::kDefaultScreenshotFrames);

    // The largest window size the platform accepts, and the smallest.
    const auto largest = parse({"--size", "16384x16384"});
    REQUIRE(largest.hasValue());
    CHECK(largest.value().width == 16384);
    const auto smallest = parse({"--size", "1x1"});
    REQUIRE(smallest.hasValue());
    CHECK(smallest.value().height == 1);
}

TEST_CASE("Unknown arguments and missing values are refused with their name", "[editor][options]")
{
    CHECK(fails({"--fullscreen"}, "unknown argument '--fullscreen'"));
    CHECK(fails({"frames", "60"}, "unknown argument 'frames'"));
    CHECK(fails({"--frames"}, "--frames needs a value"));
    CHECK(fails({"--size"}, "--size needs a value"));
    CHECK(fails({"--screenshot"}, "--screenshot needs a value"));
    CHECK(fails({"--screenshot", ""}, "--screenshot"));
}

TEST_CASE("Invalid frame counts and sizes are refused", "[editor][options]")
{
    CHECK(fails({"--frames", "0"}, "--frames 0: expected a number of frames of at least 1"));
    CHECK(fails({"--frames", "-5"}, "--frames -5"));
    CHECK(fails({"--frames", "ten"}, "--frames ten"));
    CHECK(fails({"--frames", "60fps"}, "--frames 60fps"));
    CHECK(fails({"--size", "0x10"}, "--size 0x10: expected WIDTHxHEIGHT, each from 1 to 16384"));
    CHECK(fails({"--size", "100x0"}, "--size 100x0"));
    CHECK(fails({"--size", "1280"}, "--size 1280"));
    CHECK(fails({"--size", "1280x"}, "--size 1280x"));
    CHECK(fails({"--size", "x760"}, "--size x760"));
    CHECK(fails({"--size", "1280X760"}, "--size 1280X760"));
    CHECK(fails({"--size", "-1280x760"}, "--size -1280x760"));
    CHECK(fails({"--size", "16385x100"}, "--size 16385x100"));
    CHECK(fails({"--size", "1280x760x2"}, "--size 1280x760x2"));
    CHECK(fails({"--size", "99999999999x1"}, "--size 99999999999x1"));
}
