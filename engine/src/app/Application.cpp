#include "engine/app/Application.hpp"

#include "engine/core/FileSystem.hpp"
#include "engine/core/Image.hpp"
#include "engine/core/Log.hpp"
#include "engine/platform/Platform.hpp"

#include <chrono>
#include <string>
#include <thread>
#include <utility>

namespace engine::app {

namespace {

// A minimized window is not drawn; the pause keeps the loop from spinning,
// because swapBuffers no longer waits for the display then.
constexpr std::chrono::milliseconds kMinimizedPause{16};

// The loop's implementation of AppContext. Lives on run()'s stack.
class LoopContext final : public AppContext {
public:
    LoopContext(platform::Window& window, render::Renderer& renderer)
        : window_(window), renderer_(renderer)
    {
    }

    platform::Window& window() noexcept override { return window_; }
    render::Renderer& renderer() noexcept override { return renderer_; }
    platform::PixelSize framebufferSize() const noexcept override { return framebufferSize_; }
    const core::FrameStatsSnapshot& frameStats() const noexcept override { return frameStats_; }

    void requestQuit() noexcept override { quitRequested_ = true; }
    void requestScreenshot(std::filesystem::path path) override
    {
        screenshotPath_ = std::move(path);
    }

    [[nodiscard]] bool quitRequested() const noexcept { return quitRequested_; }
    void setFramebufferSize(platform::PixelSize size) noexcept { framebufferSize_ = size; }
    void setFrameStats(const core::FrameStatsSnapshot& stats) noexcept { frameStats_ = stats; }
    [[nodiscard]] std::optional<std::filesystem::path> takeScreenshotRequest() noexcept
    {
        return std::exchange(screenshotPath_, std::nullopt);
    }

private:
    platform::Window& window_;
    render::Renderer& renderer_;
    platform::PixelSize framebufferSize_;
    core::FrameStatsSnapshot frameStats_;
    std::optional<std::filesystem::path> screenshotPath_;
    bool quitRequested_ = false;
};

// Reads back the finished frame (before it is presented) and writes it as PNG.
bool saveScreenshot(render::Renderer& renderer, const std::filesystem::path& path)
{
    auto image = renderer.captureFrame();
    if (!image) {
        core::logError("app", "screenshot failed: {}", image.error().message);
        return false;
    }
    if (auto error = core::writePng(path, image.value())) {
        core::logError("app", "screenshot failed: {}", error->message);
        return false;
    }
    core::logInfo("app", "saved screenshot '{}' ({}x{})", core::toUtf8(path), image.value().width,
                  image.value().height);
    return true;
}

core::Error startupError(const char* step, const core::Error& error)
{
    return core::Error{std::string(step) + ": " + error.message};
}

} // namespace

core::Result<RunSummary> run(Application& application, const ApplicationDesc& desc)
{
    // Declaration order is the required lifetime order: each object is destroyed
    // before the one it depends on (platform > window > context > renderer).
    auto platform = platform::Platform::initialize();
    if (!platform) {
        return startupError("platform", platform.error());
    }

    platform::WindowDesc windowDesc = desc.window;
    windowDesc.openGl = true;
    auto window = platform::Window::create(platform.value(), windowDesc);
    if (!window) {
        return startupError("window", window.error());
    }

    auto context = platform::GlContext::create(window.value(), desc.context);
    if (!context) {
        return startupError("OpenGL context", context.error());
    }
    if (desc.context.vsync && !context.value().vsyncEnabled()) {
        core::logWarning("app", "vsync is not available; the frame rate is not limited");
    }

    auto renderer = render::Renderer::create({
        .glProcLoader = context.value().procLoader(),
        .shaderDirectory = desc.assetDirectory / "shaders",
        .debugOutput = desc.context.debug,
    });
    if (!renderer) {
        return startupError("renderer", renderer.error());
    }

    LoopContext loop(window.value(), renderer.value());
    loop.setFramebufferSize(window.value().pixelSize());
    if (auto error = application.start(loop)) {
        return startupError("application start", *error);
    }

    core::FrameClock clock(desc.maxFrameDeltaSeconds);
    core::FrameStats stats;
    RunSummary summary;
    platform::PixelSize lastSize = loop.framebufferSize();

    while (!desc.maxFrames || summary.renderedFrames < *desc.maxFrames) {
        if (platform.value().pollEvents().quitRequested) {
            break;
        }

        const core::FrameTime time = clock.tick(std::chrono::steady_clock::now());
        if (stats.addFrame(time.rawDeltaSeconds)) {
            loop.setFrameStats(stats.latest());
        }

        const platform::PixelSize size = window.value().pixelSize();
        if (size.width != lastSize.width || size.height != lastSize.height) {
            core::logDebug("app", "framebuffer resized to {}x{}", size.width, size.height);
            lastSize = size;
        }
        loop.setFramebufferSize(size);

        application.update(loop, time);
        if (loop.quitRequested()) {
            break;
        }

        if (window.value().isMinimized() || size.width <= 0 || size.height <= 0) {
            std::this_thread::sleep_for(kMinimizedPause);
            continue;
        }

        renderer.value().beginFrame(size.width, size.height, desc.clearColor);
        application.render(loop);
        renderer.value().endFrame();

        if (const auto screenshot = loop.takeScreenshotRequest()) {
            if (!saveScreenshot(renderer.value(), *screenshot)) {
                ++summary.failedScreenshots;
            }
        }

        // With vsync this waits for the display refresh and paces the loop.
        context.value().swapBuffers();
        ++summary.renderedFrames;
    }

    application.stop(loop);
    summary.openGlErrors = renderer.value().errorCount();
    return summary;
}

} // namespace engine::app
