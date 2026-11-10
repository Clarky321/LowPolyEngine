// GPU tier: the application loop with a real window and OpenGL 4.5 context.
// Runs from the repository root (tests/rendering/CMakeLists.txt) so assets/ is found.

#include "engine/app/Application.hpp"
#include "engine/core/FileSystem.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

using engine::app::AppContext;
using engine::app::ApplicationDesc;

ApplicationDesc smallWindow(std::uint64_t frames)
{
    return ApplicationDesc{
        .window = {.title = "lpe gpu test", .width = 320, .height = 200},
        .context = {.debug = true},
        .clearColor = {0.25F, 0.5F, 0.75F, 1.0F},
        .maxFrames = frames,
    };
}

// Records the callback order: s = start, u = update, r = render, x = stop.
class RecordingApp : public engine::app::Application {
public:
    std::optional<engine::core::Error> start(AppContext& context) override
    {
        calls += 's';
        startSize = context.framebufferSize();
        return std::nullopt;
    }
    void update(AppContext& /*context*/, const engine::core::FrameTime& time) override
    {
        calls += 'u';
        frameIndices.push_back(time.frameIndex);
    }
    void render(AppContext& /*context*/) override { calls += 'r'; }
    void stop(AppContext& /*context*/) override { calls += 'x'; }

    std::string calls;
    std::vector<std::uint64_t> frameIndices;
    engine::platform::PixelSize startSize;
};

class QuitOnSecondUpdate final : public RecordingApp {
public:
    void update(AppContext& context, const engine::core::FrameTime& time) override
    {
        RecordingApp::update(context, time);
        if (time.frameIndex == 1) {
            context.requestQuit();
        }
    }
};

class FailingStart final : public RecordingApp {
public:
    std::optional<engine::core::Error> start(AppContext& /*context*/) override
    {
        calls += 's';
        return engine::core::Error{"scene could not be built"};
    }
};

class ScreenshotOnFirstFrame final : public RecordingApp {
public:
    explicit ScreenshotOnFirstFrame(std::filesystem::path path) : path_(std::move(path)) {}
    void update(AppContext& context, const engine::core::FrameTime& time) override
    {
        RecordingApp::update(context, time);
        if (time.frameIndex == 0) {
            context.requestScreenshot(path_);
        }
    }

private:
    std::filesystem::path path_;
};

} // namespace

TEST_CASE("run() calls start, then update and render per frame, then stop", "[app][gpu]")
{
    RecordingApp app;
    const auto result = engine::app::run(app, smallWindow(3));
    REQUIRE(result.hasValue());
    CHECK(app.calls == "surururx");
    CHECK(app.frameIndices == std::vector<std::uint64_t>{0, 1, 2});
    CHECK(result.value().renderedFrames == 3);
    CHECK(result.value().openGlErrors == 0);
    CHECK(app.startSize.width > 0);
    CHECK(app.startSize.height > 0);
}

TEST_CASE("requestQuit() ends the loop before that frame is rendered", "[app][gpu]")
{
    QuitOnSecondUpdate app;
    const auto result = engine::app::run(app, smallWindow(100));
    REQUIRE(result.hasValue());
    CHECK(app.calls == "surux");
    CHECK(result.value().renderedFrames == 1);
}

TEST_CASE("An error from start() ends the run without frames or stop()", "[app][gpu]")
{
    FailingStart app;
    const auto result = engine::app::run(app, smallWindow(3));
    REQUIRE_FALSE(result.hasValue());
    CHECK(result.error().message.find("scene could not be built") != std::string::npos);
    CHECK(app.calls == "s");
}

TEST_CASE("A missing shader folder fails startup with the file name", "[app][gpu]")
{
    RecordingApp app;
    auto desc = smallWindow(1);
    desc.assetDirectory = "does-not-exist";
    const auto result = engine::app::run(app, desc);
    REQUIRE_FALSE(result.hasValue());
    CHECK(result.error().message.find("renderer") != std::string::npos);
    CHECK(result.error().message.find("does-not-exist") != std::string::npos);
    CHECK(app.calls.empty());
}

TEST_CASE("A screenshot request saves the frame as a PNG file", "[app][gpu][capture]")
{
    const auto path = std::filesystem::temp_directory_path() / "lpe_gpu_test" / "frame.png";
    std::error_code ignored;
    std::filesystem::remove(path, ignored);

    ScreenshotOnFirstFrame app(path);
    const auto result = engine::app::run(app, smallWindow(2));
    REQUIRE(result.hasValue());
    CHECK(result.value().failedScreenshots == 0);
    REQUIRE(std::filesystem::is_regular_file(path));
    const auto contents = engine::core::readTextFile(path);
    REQUIRE(contents.hasValue());
    CHECK(contents.value().compare(0, 4, "\x89PNG") == 0);
    std::filesystem::remove(path, ignored);
}
