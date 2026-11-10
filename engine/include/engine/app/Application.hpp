#pragma once

#include "engine/core/FrameClock.hpp"
#include "engine/core/Result.hpp"
#include "engine/platform/GlContext.hpp"
#include "engine/platform/Input.hpp"
#include "engine/platform/Window.hpp"
#include "engine/render/Renderer.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>

namespace engine::app {

struct ApplicationDesc {
    // The window is always created for OpenGL; WindowDesc::openGl is ignored.
    platform::WindowDesc window{};
    platform::GlContextDesc context{};
    // Runtime assets; the renderer reads its shaders from <assetDirectory>/shaders.
    std::filesystem::path assetDirectory = "assets";
    render::Color clearColor{};
    // Stop after this many rendered frames (smoke tests, screenshots). Empty: run until quit.
    std::optional<std::uint64_t> maxFrames;
    double maxFrameDeltaSeconds = core::kDefaultMaxFrameDeltaSeconds;
};

// Engine services an Application uses while it runs. Valid only inside the
// Application callbacks; implemented by the main loop.
class AppContext {
public:
    [[nodiscard]] virtual platform::Window& window() noexcept = 0;
    [[nodiscard]] virtual render::Renderer& renderer() noexcept = 0;
    // Keyboard and mouse state of the current frame.
    [[nodiscard]] virtual const platform::InputState& input() const noexcept = 0;

    // Size of the frame being rendered, in pixels.
    [[nodiscard]] virtual platform::PixelSize framebufferSize() const noexcept = 0;
    // Frame rate and frame time, averaged over the last completed window.
    [[nodiscard]] virtual const core::FrameStatsSnapshot& frameStats() const noexcept = 0;

    // Ends the loop after the current frame's update; no further frame is rendered.
    virtual void requestQuit() noexcept = 0;
    // Saves the frame rendered in this iteration as a PNG once it is complete.
    // A later request in the same frame replaces an earlier one.
    virtual void requestScreenshot(std::filesystem::path path) = 0;

protected:
    AppContext() = default;
    ~AppContext() = default;
    AppContext(const AppContext&) = default;
    AppContext& operator=(const AppContext&) = default;
};

// What a program built on the engine implements. Callbacks run on the main
// thread, which owns the OpenGL context.
class Application {
public:
    virtual ~Application() = default;

    // Once, after the window, OpenGL context and renderer exist. An error ends
    // the run before the first frame; stop() is not called then.
    [[nodiscard]] virtual std::optional<core::Error> start(AppContext& context) = 0;

    // Once per frame, after the OS events of the frame were processed.
    virtual void update(AppContext& context, const core::FrameTime& time) = 0;

    // Once per frame between Renderer::beginFrame and Renderer::endFrame.
    // Skipped while the window is minimized.
    virtual void render(AppContext& context) = 0;

    // Once after the last frame if start() succeeded, while the renderer still
    // exists, so GPU resources can be released here.
    virtual void stop(AppContext& /*context*/) {}

protected:
    Application() = default;
    Application(const Application&) = default;
    Application& operator=(const Application&) = default;
};

struct RunSummary {
    std::uint64_t renderedFrames = 0;
    std::uint32_t openGlErrors = 0;
    std::uint32_t failedScreenshots = 0;
};

// Creates the platform, window, OpenGL context and renderer, runs the frame
// loop until the window is closed, requestQuit() is called or maxFrames is
// reached, then destroys everything in reverse order. Returns an error if
// startup fails; problems during frames are counted in the summary.
[[nodiscard]] core::Result<RunSummary> run(Application& application, const ApplicationDesc& desc);

} // namespace engine::app
