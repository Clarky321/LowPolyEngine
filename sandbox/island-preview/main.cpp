// Island preview sandbox: the first end-to-end executable (docs/ENGINE_PLAN.md).
// So far: engine loop, input, free-fly camera (M1) and a static test scene
// drawn with materials, textures and a directional light, including a model
// imported from an OBJ file (M2).
//
// Controls: hold the right mouse button to look around; W/A/S/D move, E/Space
// up, Q/Ctrl down, Shift faster, mouse wheel changes the speed. F1 toggles the
// debug overlay, F12 saves a screenshot to screenshots/, Escape quits.

#include "DebugOverlay.hpp"
#include "TestScene.hpp"

#include "engine/app/Application.hpp"
#include "engine/camera/Camera.hpp"
#include "engine/camera/FreeFlyController.hpp"
#include "engine/core/FileSystem.hpp"
#include "engine/core/Image.hpp"
#include "engine/core/Log.hpp"
#include "engine/core/Version.hpp"
#include "engine/geometry/ObjImport.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
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
    // OBJ file loaded instead of the scene's model file (tests use broken ones).
    std::optional<std::filesystem::path> modelPath;
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
        } else if (arg == "--model" && hasValue) {
            options.modelPath = std::filesystem::path(argv[++i]);
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
        auto& renderer = context.renderer();
        const island_preview::TestScene scene = island_preview::buildTestScene();
        light_ = scene.light;
        for (const auto& mesh : scene.meshes) {
            auto handle = renderer.createMesh(mesh);
            if (!handle) {
                return handle.error();
            }
            meshes_.push_back(handle.value());
        }
        // A texture that cannot be loaded stays a null handle: the materials
        // that use it are drawn without it.
        for (const auto& texture : scene.textures) {
            textures_.push_back(loadTexture(
                renderer, options_.assetDirectory / "textures" / texture.file, texture.desc));
        }
        for (const auto& object : scene.objects) {
            engine::render::MaterialDesc desc = object.material;
            if (object.texture) {
                desc.baseColorTexture = textures_.at(*object.texture);
            }
            auto material = renderer.createMaterial(desc);
            if (!material) {
                return material.error();
            }
            objects_.push_back({meshes_.at(object.mesh), material.value(), object.position,
                                object.spinRadiansPerSecond});
        }
        // A model that cannot be loaded is left out; the rest of the scene runs.
        for (const auto& model : scene.models) {
            const auto path =
                options_.modelPath.value_or(options_.assetDirectory / "models" / model.file);
            const engine::render::MeshHandle mesh = loadModel(renderer, path);
            if (mesh.isNull()) {
                continue;
            }
            meshes_.push_back(mesh);
            engine::render::MaterialDesc desc = model.material;
            if (model.texture) {
                desc.baseColorTexture = textures_.at(*model.texture);
            }
            auto material = renderer.createMaterial(desc);
            if (!material) {
                return material.error();
            }
            objects_.push_back({mesh, material.value(), model.position, 0.0F});
        }
        // Rebuilt every frame; starts empty.
        auto overlay = context.renderer().createMesh(engine::geometry::MeshData{});
        if (!overlay) {
            return overlay.error();
        }
        overlay_ = overlay.value();
        const auto loadedTextures = std::ranges::count_if(
            textures_, [](engine::render::TextureHandle texture) { return !texture.isNull(); });
        engine::core::logInfo("app",
                              "test scene: {} objects, {} meshes, {} textures; hold the right "
                              "mouse button to look around, WASD to fly, Escape to quit",
                              objects_.size(), meshes_.size(), loadedTextures);
        return std::nullopt;
    }

    void stop(engine::app::AppContext& context) override
    {
        for (const auto& object : objects_) {
            context.renderer().destroyMaterial(object.material);
        }
        objects_.clear();
        for (const auto mesh : meshes_) {
            context.renderer().destroyMesh(mesh);
        }
        meshes_.clear();
        for (const auto texture : textures_) {
            context.renderer().destroyTexture(texture);
        }
        textures_.clear();
        context.renderer().destroyMesh(overlay_);
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
        if (input.wasPressed(Key::F1)) {
            showOverlay_ = !showOverlay_;
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
        renderer.setLight(light_);
        for (const auto& object : objects_) {
            engine::math::Mat4 model = glm::translate(engine::math::Mat4{1.0F}, object.position);
            if (object.spinRadiansPerSecond != 0.0F) {
                model = glm::rotate(model, object.spinRadiansPerSecond * sceneSeconds_,
                                    engine::camera::kWorldUp);
            }
            renderer.drawMesh(object.mesh, object.material, model);
        }

        if (showOverlay_) {
            renderOverlay(context);
        }
    }

private:
    // Loads a PNG as a texture. Failures are logged; the result is then null.
    static engine::render::TextureHandle loadTexture(engine::render::Renderer& renderer,
                                                     const std::filesystem::path& path,
                                                     const engine::render::TextureDesc& desc)
    {
        auto image = engine::core::readPng(path);
        if (!image) {
            engine::core::logError("app", "{}; it is not used", image.error().message);
            return {};
        }
        auto texture = renderer.createTexture(image.value(), desc);
        if (!texture) {
            engine::core::logError("app", "texture '{}': {}; it is not used",
                                   engine::core::toUtf8(path), texture.error().message);
            return {};
        }
        return texture.value();
    }

    // Loads an OBJ model as a mesh. Failures are logged; the result is then null.
    static engine::render::MeshHandle loadModel(engine::render::Renderer& renderer,
                                                const std::filesystem::path& path)
    {
        auto model = engine::geometry::readObj(path);
        if (!model) {
            engine::core::logError("app", "{}; the model is left out", model.error().message);
            return {};
        }
        auto mesh = renderer.createMesh(model.value().mesh);
        if (!mesh) {
            engine::core::logError("app", "model '{}': {}; the model is left out",
                                   engine::core::toUtf8(path), mesh.error().message);
            return {};
        }
        engine::core::logInfo("app", "model '{}': {} triangles", engine::core::toUtf8(path),
                              model.value().mesh.triangleCount());
        return mesh.value();
    }

    // Screen-space text on top of the scene: no depth test, alpha blending.
    void renderOverlay(engine::app::AppContext& context)
    {
        auto& renderer = context.renderer();
        const auto size = context.framebufferSize();
        island_preview::buildOverlay(overlayText_, {
                                                       .frame = context.frameStats(),
                                                       .renderer = renderer.info(),
                                                       .renderStats = renderer.lastFrameStats(),
                                                       .framebuffer = size,
                                                       .camera = camera_,
                                                       .moveSpeed = controller_.moveSpeed(),
                                                   });
        if (auto error = renderer.updateMesh(overlay_, overlayText_.mesh())) {
            // Not fatal: the scene still renders. Log once, then hide the overlay.
            engine::core::logError("app", "debug overlay disabled: {}", error->message);
            showOverlay_ = false;
            return;
        }
        renderer.setView({
            .projection = engine::debug::overlayProjection(size.width, size.height),
            .depth = engine::render::DepthMode::Off,
            .blend = engine::render::BlendMode::Alpha,
        });
        renderer.drawMesh(overlay_);
    }

    struct SceneObject {
        engine::render::MeshHandle mesh;
        engine::render::MaterialHandle material;
        engine::math::Vec3 position{0.0F};
        float spinRadiansPerSecond = 0.0F;
    };

    Options options_;
    // Shared by several objects; destroyed after them.
    std::vector<engine::render::MeshHandle> meshes_;
    // Used by the objects' materials; destroyed after them.
    std::vector<engine::render::TextureHandle> textures_;
    std::vector<SceneObject> objects_;
    engine::render::LightDesc light_;
    engine::camera::Camera camera_ = initialCamera();
    engine::camera::FreeFlyController controller_;
    bool mouseLook_ = false;
    float sceneSeconds_ = 0.0F;
    engine::render::MeshHandle overlay_;
    engine::debug::TextMeshBuilder overlayText_;
    bool showOverlay_ = true;
};

} // namespace

int main(int argc, char** argv)
{
    const auto options = parseOptions(argc, argv);
    if (!options) {
        std::fprintf(stderr,
                     "usage: island-preview [--frames N] [--assets DIR] [--screenshot FILE.png] "
                     "[--model FILE.obj]\n");
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
