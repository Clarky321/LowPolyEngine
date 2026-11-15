#include "engine/app/Application.hpp"

#include "../ui/RavenUi.hpp"

#include "engine/core/FileSystem.hpp"
#include "engine/core/Image.hpp"
#include "engine/core/Log.hpp"
#include "engine/platform/Platform.hpp"

#include <chrono>
#include <memory>
#include <optional>
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
    LoopContext(platform::Window& window, render::Renderer& renderer,
                const platform::InputState& input, debug::DebugUi* debugUi, ui::Ui* ui)
        : window_(window), renderer_(renderer), input_(input), debugUi_(debugUi), ui_(ui)
    {
    }

    platform::Window& window() noexcept override { return window_; }
    render::Renderer& renderer() noexcept override { return renderer_; }
    const platform::InputState& input() const noexcept override { return input_; }
    platform::PixelSize framebufferSize() const noexcept override { return framebufferSize_; }
    const core::FrameStatsSnapshot& frameStats() const noexcept override { return frameStats_; }
    debug::DebugUi* debugUi() noexcept override { return debugUi_; }
    ui::Ui* ui() noexcept override { return ui_; }

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
    const platform::InputState& input_;
    debug::DebugUi* debugUi_ = nullptr;
    ui::Ui* ui_ = nullptr;
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
    // One UI stack per application (ADR 0019 decision 6, ADR 0027).
    if (desc.ui && desc.debugUi) {
        return core::Error{
            "application: the Raven UI (ui) and the debug UI (debugUi) cannot be combined"};
    }

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
    const auto& info = renderer.value().info();
    core::logInfo("render", "{} on {} ({}), driver {}", info.api, info.device, info.vendor,
                  info.driverVersion);

    // After the renderer, so it is destroyed first; it needs the OpenGL context.
    std::optional<debug::DebugUi> debugUi;
    if (desc.debugUi) {
        auto ui = debug::DebugUi::create(window.value(), context.value());
        if (!ui) {
            return ui.error();
        }
        debugUi.emplace(std::move(ui).value());
    }

    // After the renderer as well: Raven draws with it and ends (Rshutdown,
    // its OpenGL objects released) before it.
    std::unique_ptr<ui::RavenUi> ravenUi;
    if (desc.ui) {
        auto ui = ui::RavenUi::create(window.value(), *desc.ui);
        if (!ui) {
            return ui.error();
        }
        ravenUi = std::move(ui).value();
    }

    platform::InputState input;
    LoopContext loop(window.value(), renderer.value(), input, debugUi ? &*debugUi : nullptr,
                     ravenUi.get());
    loop.setFramebufferSize(window.value().pixelSize());
    if (auto error = application.start(loop)) {
        return startupError("application start", *error);
    }

    core::FrameClock clock(desc.maxFrameDeltaSeconds);
    core::FrameStats stats;
    RunSummary summary;
    platform::PixelSize lastSize = loop.framebufferSize();

    while (!desc.maxFrames || summary.renderedFrames < *desc.maxFrames) {
        if (platform.value().pollEvents(input).quitRequested) {
            break;
        }
        // Raven closes its window inside an accepted close request (during the
        // events); its handles are invalid from then on.
        if (ravenUi && !ravenUi->isOpen()) {
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

        if (debugUi) {
            debugUi->beginFrame();
        }
        application.update(loop, time);
        if (loop.quitRequested()) {
            break;
        }

        if (window.value().isMinimized() || size.width <= 0 || size.height <= 0) {
            if (debugUi) {
                debugUi->endFrame();
            }
            std::this_thread::sleep_for(kMinimizedPause);
            continue;
        }

        renderer.value().beginFrame(size.width, size.height, desc.clearColor);
        application.render(loop);
        if (ravenUi) {
            // Raven's frame covers the window (its viewports call back into
            // the renderer); on the main thread, which owns the context.
            ravenUi->frame();
        }
        if (debugUi) {
            debugUi->render();
        }
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
    if (ravenUi) {
        // While the renderer and the context still exist.
        summary.ui = ravenUi->shutdown();
        ravenUi.reset();
    }
    summary.openGlErrors = renderer.value().errorCount() + summary.ui.glErrors;
    return summary;
}

} // namespace engine::app
