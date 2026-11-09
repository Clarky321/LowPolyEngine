// Island preview sandbox: the first end-to-end executable (docs/ENGINE_PLAN.md).
// M1 scope: engine loop, input, free-fly camera and a static test scene.
//
// Controls: hold the right mouse button to look around; W/A/S/D move, E/Space
// up, Q/Ctrl down, Shift faster, mouse wheel changes the speed. F12 saves a
// screenshot to screenshots/, Escape quits.

#include "TestScene.hpp"

#include "engine/app/Application.hpp"
#include "engine/camera/Camera.hpp"
#include "engine/camera/FreeFlyController.hpp"
#include "engine/core/Log.hpp"
#include "engine/core/Version.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <optional>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

#ifdef NDEBUG
constexpr bool kDebugBuild = false;
#else
constexpr bool kDebugBuild = true;
#endif

// Placeholder sky tone until the scene has real lighting.
constexpr engine::render::Color kClearColor{0.53F, 0.74F, 0.86F, 1.0F};

// Frames rendered before a --screenshot is taken when --frames is not given.
// Long enough for the frame statistics to fill their first window.
constexpr std::uint64_t kDefaultScreenshotFrames = 120;

// Process exit codes, so smoke tests can tell failures apart.
constexpr int kExitStartupFailed = 1;
constexpr int kExitUsage = 2;
constexpr int kExitGlErrors = 3;
constexpr int kExitScreenshotFailed = 4;

struct Options {
    // Stop after this many frames (smoke tests); empty means run until closed.
    std::optional<std::uint64_t> maxFrames;
    // Relative to the working directory; scripts/windows/run.bat and the CTest
    // tests start the program from the repository root.
    std::filesystem::path assetDirectory = "assets";
    // Save the last frame as PNG and exit.
    std::optional<std::filesystem::path> screenshotPath;
};

std::optional<std::uint64_t> parsePositiveInt(std::string_view text)
{
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value == 0) {
        return std::nullopt;
    }
    return value;
}

std::optional<Options> parseOptions(int argc, char** argv)
{
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--frames" && hasValue) {
            options.maxFrames = parsePositiveInt(argv[++i]);
            if (!options.maxFrames) {
                return std::nullopt;
            }
        } else if (arg == "--assets" && hasValue) {
            options.assetDirectory = argv[++i];
        } else if (arg == "--screenshot" && hasValue) {
            options.screenshotPath = std::filesystem::path(argv[++i]);
        } else {
            return std::nullopt;
        }
    }
    if (options.screenshotPath && !options.maxFrames) {
        options.maxFrames = kDefaultScreenshotFrames;
    }
    return options;
}

// Starting view: front-right of the pyramid, slightly above, looking at its tip,
// so two of its faces and the ground perspective are visible.
engine::camera::Camera initialCamera()
{
    engine::camera::Camera camera;
    camera.position = {11.0F, 7.5F, 22.0F};
    camera.yawRadians = 0.4636F; // atan2(11, 22): towards the origin
    camera.pitchRadians = -0.24F;
    return camera;
}

class IslandPreview final : public engine::app::Application {
public:
    explicit IslandPreview(const Options& options) : options_(options) {}

    std::optional<engine::core::Error> start(engine::app::AppContext& context) override
    {
        for (auto& object : island_preview::buildTestScene()) {
            auto handle = context.renderer().createMesh(object.mesh);
            if (!handle) {
                return handle.error();
            }
            meshes_.push_back({handle.value(), object.position, object.spinRadiansPerSecond});
        }
        engine::core::logInfo("app",
                              "test scene: {} meshes; hold the right mouse button to look "
                              "around, WASD to fly, Escape to quit",
                              meshes_.size());
        return std::nullopt;
    }

    void stop(engine::app::AppContext& context) override
    {
        for (const auto& mesh : meshes_) {
            context.renderer().destroyMesh(mesh.handle);
        }
        meshes_.clear();
        context.window().setRelativeMouseMode(false);
    }

    void update(engine::app::AppContext& context, const engine::core::FrameTime& time) override
    {
        using engine::platform::Key;
        using engine::platform::MouseButton;
        const auto& input = context.input();
        if (input.wasPressed(Key::Escape)) {
            context.requestQuit();
            return;
        }
        if (input.wasPressed(Key::F12)) {
            // Relative to the working directory (the repository root with run.bat);
            // the folder is ignored by Git.
            context.requestScreenshot(
                std::format("screenshots/island-preview-{:06}.png", time.frameIndex));
        }

        // Mouse look while the right button is held; the cursor is hidden and
        // captured meanwhile, so it cannot leave the window mid-turn.
        const bool wantMouseLook = input.isDown(MouseButton::Right);
        if (wantMouseLook != mouseLook_) {
            mouseLook_ = wantMouseLook;
            context.window().setRelativeMouseMode(mouseLook_);
        }
        controller_.update(camera_, engine::camera::readFlyInput(input, mouseLook_),
                           static_cast<float>(time.deltaSeconds));
        sceneSeconds_ = static_cast<float>(time.elapsedSeconds);

        // The last frame of a --screenshot run is saved.
        if (options_.screenshotPath && options_.maxFrames &&
            time.frameIndex + 1 == *options_.maxFrames) {
            context.requestScreenshot(*options_.screenshotPath);
        }
    }

    void render(engine::app::AppContext& context) override
    {
        const auto size = context.framebufferSize();
        auto& renderer = context.renderer();
        renderer.setView({
            .view = engine::camera::viewMatrix(camera_),
            .projection = engine::camera::projectionMatrix(
                camera_, engine::camera::aspectRatio(size.width, size.height)),
        });
        for (const auto& mesh : meshes_) {
            engine::math::Mat4 model = glm::translate(engine::math::Mat4{1.0F}, mesh.position);
            if (mesh.spinRadiansPerSecond != 0.0F) {
                model = glm::rotate(model, mesh.spinRadiansPerSecond * sceneSeconds_,
                                    engine::camera::kWorldUp);
            }
            renderer.drawMesh(mesh.handle, model);
        }
    }

private:
    struct SceneMesh {
        engine::render::MeshHandle handle;
        engine::math::Vec3 position{0.0F};
        float spinRadiansPerSecond = 0.0F;
    };

    Options options_;
    std::vector<SceneMesh> meshes_;
    engine::camera::Camera camera_ = initialCamera();
    engine::camera::FreeFlyController controller_;
    bool mouseLook_ = false;
    float sceneSeconds_ = 0.0F;
};

} // namespace

int main(int argc, char** argv)
{
    const auto options = parseOptions(argc, argv);
    if (!options) {
        std::fprintf(stderr,
                     "usage: island-preview [--frames N] [--assets DIR] [--screenshot FILE.png]\n");
        return kExitUsage;
    }

    const engine::app::ApplicationDesc desc{
        .window = {.title = "LowPolyEngine " +
                            engine::core::toString(engine::core::engineVersion()) +
                            " - Island Preview"},
        .context = {.debug = kDebugBuild},
        .assetDirectory = options->assetDirectory,
        .clearColor = kClearColor,
        .maxFrames = options->maxFrames,
    };

    IslandPreview app(*options);
    const auto result = engine::app::run(app, desc);
    if (!result) {
        engine::core::logError("app", "{}", result.error().message);
        return kExitStartupFailed;
    }
    if (const auto errors = result.value().openGlErrors; errors > 0) {
        engine::core::logError("app", "{} OpenGL error(s) occurred", errors);
        return kExitGlErrors;
    }
    if (result.value().failedScreenshots > 0) {
        return kExitScreenshotFailed;
    }
    return 0;
}
