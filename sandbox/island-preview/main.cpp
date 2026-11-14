// Island preview sandbox: the first end-to-end executable (docs/ENGINE_PLAN.md).
// By default it generates a floating archipelago from a seed (M3) and shows it
// under a stylized sky, with low-poly clouds drifting between the islands and
// the islands' local water drawn translucent after them; --test-scene shows
// the static test scene of M1/M2 instead (materials, textures, a directional
// light, an imported OBJ model).
//
// Controls: hold the right mouse button to look around; W/A/S/D move, E/Space
// up, Q/Ctrl down, Shift faster, mouse wheel changes the speed. F1 toggles the
// debug overlay, F12 saves a screenshot to screenshots/, Escape quits.

#include "DebugOverlay.hpp"
#include "Sky.hpp"
#include "TestScene.hpp"

#include "engine/app/Application.hpp"
#include "engine/camera/Camera.hpp"
#include "engine/camera/FreeFlyController.hpp"
#include "engine/core/FileSystem.hpp"
#include "engine/core/Image.hpp"
#include "engine/core/Log.hpp"
#include "engine/core/Version.hpp"
#include "engine/geometry/ObjImport.hpp"
#include "engine/worldgen/Archipelago.hpp"
#include "engine/worldgen/Clouds.hpp"
#include "engine/worldgen/WorldGenSettings.hpp"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

#ifdef NDEBUG
constexpr bool kDebugBuild = false;
#else
constexpr bool kDebugBuild = true;
#endif

// Behind the sky dome (archipelago) or the test scene's horizon.
constexpr engine::render::Color kClearColor{0.53F, 0.74F, 0.86F, 1.0F};

// A drifting cloud smaller than this share of its size is not drawn.
constexpr float kMinCloudScale = 1.0e-3F;

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
    // Show the M1/M2 test scene instead of the generated archipelago.
    bool testScene = false;
    // Test scene only: OBJ file loaded instead of the scene's model file
    // (tests use broken ones).
    std::optional<std::filesystem::path> modelPath;
    // Archipelago only: the world seed instead of the default settings' one.
    std::optional<std::uint64_t> seed;
    // Archipelago only: start with a close view of this island (layout order)
    // instead of the overview.
    std::optional<std::uint64_t> island;
};

std::optional<std::uint64_t> parseUnsigned(std::string_view text)
{
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::uint64_t> parsePositiveInt(std::string_view text)
{
    const auto value = parseUnsigned(text);
    if (!value || *value == 0) {
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
        } else if (arg == "--seed" && hasValue) {
            options.seed = parseUnsigned(argv[++i]);
            if (!options.seed) {
                return std::nullopt;
            }
        } else if (arg == "--island" && hasValue) {
            options.island = parseUnsigned(argv[++i]);
            if (!options.island) {
                return std::nullopt;
            }
        } else if (arg == "--test-scene") {
            options.testScene = true;
        } else {
            return std::nullopt;
        }
    }
    // Each option belongs to one of the two scenes.
    if ((options.modelPath && !options.testScene) ||
        ((options.seed || options.island) && options.testScene)) {
        return std::nullopt;
    }
    if (options.screenshotPath && !options.maxFrames) {
        options.maxFrames = kDefaultScreenshotFrames;
    }
    return options;
}

// Test scene: front-right of the pyramid, slightly above, looking at its tip,
// so two of its faces and the ground perspective are visible.
engine::camera::Camera testSceneCamera()
{
    engine::camera::Camera camera;
    camera.position = {11.0F, 7.5F, 22.0F};
    camera.yawRadians = 0.4636F; // atan2(11, 22): towards the origin
    camera.pitchRadians = -0.24F;
    return camera;
}

// A camera at `position` looking at `target`.
engine::camera::Camera cameraLookingAt(const engine::math::Vec3& position,
                                       const engine::math::Vec3& target)
{
    const engine::math::Vec3 direction = glm::normalize(target - position);
    engine::camera::Camera camera;
    camera.position = position;
    // forwardDirection() is (-sin(yaw) cos(pitch), sin(pitch), -cos(yaw) cos(pitch)).
    camera.yawRadians = std::atan2(-direction.x, -direction.z);
    camera.pitchRadians = std::asin(std::clamp(direction.y, -1.0F, 1.0F));
    return camera;
}

// Overview of the archipelago: from above and to the side of the islands'
// centre, far enough to see all of them, looking at the centre.
engine::camera::Camera archipelagoCamera(const engine::worldgen::Archipelago& archipelago)
{
    engine::math::Vec3 centre{0.0F};
    for (const auto& island : archipelago.islands) {
        centre += island.shape.island.anchor;
    }
    centre /= static_cast<float>(std::max<std::size_t>(archipelago.islands.size(), 1U));
    float reach = 0.0F;
    for (const auto& island : archipelago.islands) {
        const engine::math::Vec3 offset = island.shape.island.anchor - centre;
        reach = std::max(reach, std::sqrt(offset.x * offset.x + offset.z * offset.z) +
                                    island.shape.island.radius);
    }
    const engine::math::Vec3 direction = glm::normalize(engine::math::Vec3{0.55F, 0.45F, 0.72F});
    const engine::math::Vec3 target = centre - engine::math::Vec3{0.0F, 0.25F * reach, 0.0F};
    return cameraLookingAt(centre + direction * (1.3F * reach + 60.0F), target);
}

// Close view of one island from above and to the side, at a distance that fits
// it, looking a little below its rim so the underside shows too. An island
// with a waterfall is seen from 65 degrees beside the fall, which then hangs
// against the sky at the island's right side.
engine::camera::Camera islandCamera(const engine::worldgen::GeneratedIsland& island)
{
    const engine::worldgen::PlacedIsland& placed = island.shape.island;
    engine::math::Vec3 around{0.55F, 0.0F, 0.72F};
    if (!island.water.waterfalls.empty()) {
        const engine::math::Vec2 out = island.water.waterfalls.front().outward;
        constexpr float kCos = 0.4226F; // cos 65 degrees
        constexpr float kSin = 0.9063F;
        around = {out.x * kCos - out.y * kSin, 0.0F, out.x * kSin + out.y * kCos};
    }
    const engine::math::Vec3 direction =
        glm::normalize(glm::normalize(around) * 0.85F + engine::math::Vec3{0.0F, 0.38F, 0.0F});
    const engine::math::Vec3 target =
        placed.anchor - engine::math::Vec3{0.0F, 0.45F * placed.undersideDepth, 0.0F};
    return cameraLookingAt(placed.anchor + direction * (2.1F * placed.radius), target);
}

// Test scene: sunlight chosen in M2.8 to match the earlier baked brightness.
// Archipelago: a warm sun from above and to the side, and a cool ambient, so
// tops are bright, cliffs shaded and undersides cool and darker (ADR 0011).
engine::render::LightDesc archipelagoSunlight()
{
    return engine::render::LightDesc{.direction = {-0.45F, -0.78F, -0.43F},
                                     .color = engine::math::Vec3{0.5F, 0.465F, 0.41F},
                                     .ambient = engine::math::Vec3{0.5F, 0.53F, 0.62F}};
}

// Camera at the origin, only its rotation: the sky stays at infinity.
engine::math::Mat4 rotationOnly(const engine::math::Mat4& view)
{
    engine::math::Mat4 rotation = view;
    rotation[3] = engine::math::Vec4{0.0F, 0.0F, 0.0F, 1.0F};
    return rotation;
}

class IslandPreview final : public engine::app::Application {
public:
    explicit IslandPreview(const Options& options) : options_(options) {}

    std::optional<engine::core::Error> start(engine::app::AppContext& context) override
    {
        auto error = options_.testScene ? startTestScene(context) : startArchipelago(context);
        if (error) {
            return error;
        }
        // Rebuilt every frame; starts empty.
        auto overlay = context.renderer().createMesh(engine::geometry::MeshData{});
        if (!overlay) {
            return overlay.error();
        }
        overlay_ = overlay.value();
        return std::nullopt;
    }

    void stop(engine::app::AppContext& context) override
    {
        auto& renderer = context.renderer();
        // Objects may share a material; destroying it again is ignored.
        for (const auto& object : objects_) {
            renderer.destroyMaterial(object.material);
        }
        objects_.clear();
        water_.clear();
        clouds_.clear();
        renderer.destroyMaterial(waterMaterial_);
        for (const auto mesh : meshes_) {
            renderer.destroyMesh(mesh);
        }
        meshes_.clear();
        for (const auto texture : textures_) {
            renderer.destroyTexture(texture);
        }
        textures_.clear();
        renderer.destroyMesh(sky_);
        renderer.destroyMesh(overlay_);
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
        const engine::math::Mat4 view = engine::camera::viewMatrix(camera_);
        const engine::math::Mat4 projection = engine::camera::projectionMatrix(
            camera_, engine::camera::aspectRatio(size.width, size.height));

        // The sky first, behind everything: no depth test, no depth writes.
        if (!sky_.isNull()) {
            renderer.setView({
                .view = rotationOnly(view),
                .projection = projection,
                .depth = engine::render::DepthMode::Off,
            });
            renderer.drawMesh(sky_);
        }

        renderer.setView({.view = view, .projection = projection});
        renderer.setLight(light_);
        for (const auto& object : objects_) {
            engine::math::Mat4 model = glm::translate(engine::math::Mat4{1.0F}, object.position);
            if (object.spinRadiansPerSecond != 0.0F) {
                model = glm::rotate(model, object.spinRadiansPerSecond * sceneSeconds_,
                                    engine::camera::kWorldUp);
            }
            renderer.drawMesh(object.mesh, object.material, model);
        }
        // Clouds where the wind has moved them; a cloud shrinks away at the
        // edge of the world before it wraps around.
        for (const auto& cloud : clouds_) {
            const engine::worldgen::CloudDrift drift = engine::worldgen::cloudDrift(
                cloud.cluster, settings_, static_cast<double>(sceneSeconds_));
            if (drift.scale < kMinCloudScale) {
                continue;
            }
            const engine::math::Mat4 model =
                glm::scale(glm::translate(engine::math::Mat4{1.0F}, drift.position),
                           engine::math::Vec3{drift.scale});
            renderer.drawMesh(cloud.mesh, cloudMaterial_, model);
        }

        // Water after all opaque geometry, blended, roughly back to front: the
        // islands' water meshes by distance from the camera.
        if (!water_.empty()) {
            renderer.setView({
                .view = view,
                .projection = projection,
                .blend = engine::render::BlendMode::Alpha,
            });
            const auto distance = [&](const WaterObject& water) {
                const engine::math::Vec3 offset = water.position - camera_.position;
                return glm::dot(offset, offset);
            };
            std::sort(water_.begin(), water_.end(),
                      [&](const WaterObject& a, const WaterObject& b) {
                          return distance(a) > distance(b);
                      });
            for (const auto& water : water_) {
                renderer.drawMesh(water.mesh, waterMaterial_,
                                  glm::translate(engine::math::Mat4{1.0F}, water.position));
            }
        }

        if (showOverlay_) {
            renderOverlay(context);
        }
    }

private:
    std::optional<engine::core::Error> startArchipelago(engine::app::AppContext& context)
    {
        auto& renderer = context.renderer();
        engine::worldgen::WorldGenSettings& settings = settings_;
        if (options_.seed) {
            settings.seed = *options_.seed;
        }

        const auto started = std::chrono::steady_clock::now();
        auto generated = engine::worldgen::generateArchipelago(settings);
        const double milliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
                .count();
        if (!generated) {
            return engine::core::Error{
                std::format("world generation failed: {}", generated.error().message)};
        }
        const engine::worldgen::Archipelago& archipelago = generated.value();
        if (!archipelago.layout.warning.empty()) {
            engine::core::logWarning("app", "archipelago: {}", archipelago.layout.warning);
        }
        if (!archipelago.clouds.warning.empty()) {
            engine::core::logWarning("app", "clouds: {}", archipelago.clouds.warning);
        }

        // Island colours are vertex colours (palettes); one white lit material
        // lets the sun shade them.
        auto material = renderer.createMaterial(
            {.baseColor = {1.0F, 1.0F, 1.0F, 1.0F}, .shading = engine::render::Shading::Lit});
        if (!material) {
            return material.error();
        }
        for (const auto& island : archipelago.islands) {
            auto mesh = renderer.createMesh(island.mesh);
            if (!mesh) {
                return mesh.error();
            }
            meshes_.push_back(mesh.value());
            objects_.push_back({mesh.value(), material.value(), island.shape.island.anchor, 0.0F});
            // Translucent through its vertex colours' alpha.
            if (!island.water.mesh.positions.empty()) {
                auto water = renderer.createMesh(island.water.mesh);
                if (!water) {
                    return water.error();
                }
                meshes_.push_back(water.value());
                water_.push_back({water.value(), island.shape.island.anchor});
            }
        }
        // Water shows its own colours, unlit: it reads as light-filled against
        // the shaded rock, and a waterfall stays bright on any side.
        auto waterMaterial = renderer.createMaterial(
            {.baseColor = {1.0F, 1.0F, 1.0F, 1.0F}, .shading = engine::render::Shading::Unlit});
        if (!waterMaterial) {
            return waterMaterial.error();
        }
        waterMaterial_ = waterMaterial.value();
        // Clouds are opaque and, like water, show their own colours unlit:
        // white tops and lavender undersides, light on every side.
        cloudMaterial_ = waterMaterial_;
        for (const auto& cluster : archipelago.clouds.clusters) {
            auto mesh = renderer.createMesh(cluster.mesh);
            if (!mesh) {
                return mesh.error();
            }
            meshes_.push_back(mesh.value());
            CloudObject cloud{mesh.value(), cluster};
            cloud.cluster.mesh = {};
            clouds_.push_back(std::move(cloud));
        }
        auto sky =
            renderer.createMesh(island_preview::buildSkyDome(island_preview::floatingWorldSky()));
        if (!sky) {
            return sky.error();
        }
        sky_ = sky.value();

        light_ = archipelagoSunlight();
        if (options_.island) {
            if (*options_.island >= archipelago.islands.size()) {
                return engine::core::Error{
                    std::format("--island {}: the archipelago has {} islands, numbered from 0",
                                *options_.island, archipelago.islands.size())};
            }
            camera_ = islandCamera(archipelago.islands[*options_.island]);
        } else {
            camera_ = archipelagoCamera(archipelago);
        }
        controller_ = engine::camera::FreeFlyController{{.moveSpeed = 30.0F}};
        subtitle_ = "M3  FLOATING ARCHIPELAGO";
        const engine::worldgen::WaterCounts water = archipelago.waterCounts();
        sceneLines_ = {
            std::format("SEED: {}   ISLANDS: {} OF {}   TRIANGLES: {}   GEN: {:.0f} MS",
                        settings.seed, archipelago.islands.size(),
                        archipelago.layout.requestedCount, archipelago.triangleCount(),
                        milliseconds),
            std::format("WATER: {} LAKES   {} STREAMS   {} WATERFALLS   CLOUDS: {}", water.lakes,
                        water.streams, water.waterfalls, archipelago.clouds.clusters.size()),
        };
        engine::core::logInfo("app",
                              "archipelago: seed {}, {} of {} islands, {} triangles, {} lakes, "
                              "{} streams, {} waterfalls, {} clouds, generated in {:.1f} ms",
                              settings.seed, archipelago.islands.size(),
                              archipelago.layout.requestedCount, archipelago.triangleCount(),
                              water.lakes, water.streams, water.waterfalls,
                              archipelago.clouds.clusters.size(), milliseconds);
        engine::core::logInfo("app", "hold the right mouse button to look around, WASD to fly, "
                                     "the mouse wheel to change speed, Escape to quit");
        return std::nullopt;
    }

    std::optional<engine::core::Error> startTestScene(engine::app::AppContext& context)
    {
        auto& renderer = context.renderer();
        const island_preview::TestScene scene = island_preview::buildTestScene();
        light_ = scene.light;
        camera_ = testSceneCamera();
        subtitle_ = "TEST SCENE  (M2 RENDERER / MATERIALS)";
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
        const auto loadedTextures = std::ranges::count_if(
            textures_, [](engine::render::TextureHandle texture) { return !texture.isNull(); });
        engine::core::logInfo("app",
                              "test scene: {} objects, {} meshes, {} textures; hold the right "
                              "mouse button to look around, WASD to fly, Escape to quit",
                              objects_.size(), meshes_.size(), loadedTextures);
        return std::nullopt;
    }

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
                                                       .subtitle = subtitle_,
                                                       .scene = sceneLines_,
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
    // The test scene's objects have a material each; the archipelago's islands
    // share one.
    std::vector<SceneObject> objects_;
    // The islands' water, drawn after the opaque objects. Its meshes are in
    // meshes_.
    struct WaterObject {
        engine::render::MeshHandle mesh;
        engine::math::Vec3 position{0.0F};
    };
    std::vector<WaterObject> water_;
    engine::render::MaterialHandle waterMaterial_;
    // The cloud clusters (without their meshes, which are uploaded and kept in
    // meshes_), drawn where the wind has moved them; their material is the
    // water's one.
    struct CloudObject {
        engine::render::MeshHandle mesh;
        engine::worldgen::CloudCluster cluster;
    };
    std::vector<CloudObject> clouds_;
    engine::render::MaterialHandle cloudMaterial_;
    // The archipelago's settings: the clouds' wind and the world bounds.
    engine::worldgen::WorldGenSettings settings_;
    engine::render::MeshHandle sky_;
    engine::render::LightDesc light_;
    engine::camera::Camera camera_;
    engine::camera::FreeFlyController controller_;
    bool mouseLook_ = false;
    float sceneSeconds_ = 0.0F;
    std::string subtitle_;
    std::vector<std::string> sceneLines_;
    engine::render::MeshHandle overlay_;
    engine::debug::TextMeshBuilder overlayText_;
    bool showOverlay_ = true;
};

} // namespace

int main(int argc, char** argv)
{
    const auto options = parseOptions(argc, argv);
    if (!options) {
        std::fprintf(stderr, "usage: island-preview [--frames N] [--assets DIR] "
                             "[--screenshot FILE.png] [--seed N] [--island N] [--test-scene "
                             "[--model FILE.obj]]\n");
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
