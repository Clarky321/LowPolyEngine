// GPU tier (M5.6): a scene's entities on the GPU (engine/world/SceneView).
// The lighthouse mesh drawn where each entity's transform puts it, rotated and
// scaled, in the scene's light or the world's sun; missing, broken and wrongly
// typed meshes skipped without an OpenGL error and reported once; the
// renderer's resources level while entities come and go.
// Runs from the repository root (tests/rendering/CMakeLists.txt): the renderer
// reads assets/shaders.

#include "engine/assets/AssetPath.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/assets/AssetType.hpp"
#include "engine/core/Image.hpp"
#include "engine/core/Uuid.hpp"
#include "engine/platform/GlContext.hpp"
#include "engine/platform/Platform.hpp"
#include "engine/platform/Window.hpp"
#include "engine/render/Renderer.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/world/SceneView.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

using engine::assets::AssetId;
using engine::assets::AssetType;
using engine::math::Mat4;
using engine::math::Vec3;
using engine::math::Vec4;
using engine::render::Renderer;
using engine::render::ResourceCounts;
using engine::scene::EntityId;
using engine::scene::Scene;
using engine::scene::StaticMeshComponent;
using engine::scene::TransformComponent;
using engine::world::SceneView;

namespace {

// The window's size in screen coordinates; its framebuffer may be larger on a
// scaled display, so pixels are computed from the framebuffer's size.
constexpr int kSize = 256;
// The orthographic view: the framebuffer's width over 2 * kHalfExtent world
// units.
constexpr float kHalfExtent = 20.0F;

// Owns everything a renderer needs; members are destroyed in reverse order.
struct Gpu {
    std::optional<engine::platform::Platform> platform;
    std::optional<engine::platform::Window> window;
    std::optional<engine::platform::GlContext> context;
    std::optional<Renderer> renderer;
    int width = 0;
    int height = 0;
};

Gpu createGpu()
{
    Gpu gpu;
    auto platform = engine::platform::Platform::initialize();
    REQUIRE(platform.hasValue());
    gpu.platform.emplace(std::move(platform).value());
    auto window = engine::platform::Window::create(*gpu.platform, {.title = "lpe scene view test",
                                                                   .width = kSize,
                                                                   .height = kSize,
                                                                   .resizable = false,
                                                                   .openGl = true});
    REQUIRE(window.hasValue());
    gpu.window.emplace(std::move(window).value());
    auto context =
        engine::platform::GlContext::create(*gpu.window, {.debug = true, .vsync = false});
    REQUIRE(context.hasValue());
    gpu.context.emplace(std::move(context).value());
    auto renderer = Renderer::create({.glProcLoader = gpu.context->procLoader(),
                                      .shaderDirectory = "assets/shaders",
                                      .debugOutput = true});
    REQUIRE(renderer.hasValue());
    gpu.renderer.emplace(std::move(renderer).value());
    const engine::platform::PixelSize size = gpu.window->pixelSize();
    gpu.width = size.width;
    gpu.height = size.height;
    REQUIRE(gpu.width > 0);
    REQUIRE(gpu.height > 0);
    return gpu;
}

// A project folder with the engine's lighthouse, a box, a broken model and a
// texture; removed at scope exit.
class TempProject {
public:
    TempProject() : root_(std::filesystem::temp_directory_path() / "lpe_gpu_scene_view")
    {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
        std::filesystem::create_directories(root_ / "models");
        std::filesystem::create_directories(root_ / "textures");
        std::filesystem::copy_file("assets/models/lighthouse.obj",
                                   root_ / "models" / "lighthouse.obj");
        std::filesystem::copy_file("tests/rendering/fixtures/broken-model/broken.obj",
                                   root_ / "models" / "broken.obj");
        std::filesystem::copy_file("assets/textures/crate.png", root_ / "textures" / "crate.png");
        std::ofstream box(root_ / "models" / "box.obj", std::ios::binary);
        box << "v -1 -1 1\nv 1 -1 1\nv 1 1 1\nv -1 1 1\nf 1 2 3 4\n";
    }
    ~TempProject()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }
    TempProject(const TempProject&) = delete;
    TempProject& operator=(const TempProject&) = delete;

    [[nodiscard]] const std::filesystem::path& root() const { return root_; }

private:
    std::filesystem::path root_;
};

AssetId assetId(std::uint64_t n)
{
    return AssetId{
        engine::core::Uuid{0xA55E7000A55E4000ULL | (n << 16U), 0x8000000000000000ULL | n}};
}

const AssetId kLighthouse = assetId(1);
const AssetId kBox = assetId(2);
const AssetId kBroken = assetId(3);
const AssetId kMissing = assetId(4);
const AssetId kTexture = assetId(5);
const AssetId kUnregistered = assetId(6);

engine::assets::AssetRegistry registryOf(const TempProject& project)
{
    engine::assets::AssetRegistry registry(project.root());
    const auto add = [&registry](AssetId id, AssetType type, std::string_view path) {
        REQUIRE_FALSE(
            registry.registerAsset(id, type, engine::assets::AssetPath::parse(path).value())
                .has_value());
    };
    add(kLighthouse, AssetType::Mesh, "models/lighthouse.obj");
    add(kBox, AssetType::Mesh, "models/box.obj");
    add(kBroken, AssetType::Mesh, "models/broken.obj");
    add(kMissing, AssetType::Mesh, "models/missing.obj");
    add(kTexture, AssetType::Texture, "textures/crate.png");
    return registry;
}

Scene makeScene()
{
    auto scene = Scene::create("Lighthouses", engine::core::UuidGenerator(11U));
    REQUIRE(scene.hasValue());
    return std::move(scene).value();
}

EntityId addMesh(Scene& scene, AssetId mesh, std::optional<TransformComponent> transform)
{
    auto entity = scene.createEntity();
    REQUIRE(entity.hasValue());
    if (transform) {
        REQUIRE_FALSE(scene.addComponent(entity.value(), *transform).has_value());
    }
    REQUIRE_FALSE(scene.addComponent(entity.value(), StaticMeshComponent{mesh, {}}).has_value());
    return entity.value();
}

TransformComponent placed(Vec3 position, float rollDegrees, Vec3 scale)
{
    TransformComponent t;
    t.position = position;
    t.rotation = glm::angleAxis(glm::radians(rollDegrees), Vec3{0.0F, 0.0F, 1.0F});
    t.scale = scale;
    return t;
}

// Looking down -Z at the XY plane: x and y map linearly to pixels.
const Mat4 kView = glm::lookAt(Vec3{0.0F, 0.0F, 50.0F}, Vec3{0.0F}, Vec3{0.0F, 1.0F, 0.0F});
const Mat4 kProjection =
    glm::ortho(-kHalfExtent, kHalfExtent, -kHalfExtent, kHalfExtent, 0.1F, 100.0F);

struct Pixel {
    int x = 0;
    int y = 0;
};

// Where a world point lands in a frame of the given size (y = 0 is the top
// row).
Pixel pixelOf(const engine::core::Image& frame, const Vec3& world)
{
    const Vec4 clip = kProjection * kView * Vec4{world, 1.0F};
    const float x = (clip.x / clip.w * 0.5F + 0.5F) * static_cast<float>(frame.width);
    const float y = (0.5F - clip.y / clip.w * 0.5F) * static_cast<float>(frame.height);
    return {static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y))};
}

struct Rgb {
    int r = 0;
    int g = 0;
    int b = 0;
};

Rgb pixelAt(const engine::core::Image& image, Pixel at)
{
    REQUIRE(at.x >= 0);
    REQUIRE(at.y >= 0);
    REQUIRE(at.x < image.width);
    REQUIRE(at.y < image.height);
    const int channels = engine::core::channelCount(image.format);
    const std::size_t offset =
        (static_cast<std::size_t>(at.y) * static_cast<std::size_t>(image.width) +
         static_cast<std::size_t>(at.x)) *
        static_cast<std::size_t>(channels);
    return {image.pixels[offset], image.pixels[offset + 1], image.pixels[offset + 2]};
}

std::string describe(const Rgb& c)
{
    return "(" + std::to_string(c.r) + ", " + std::to_string(c.g) + ", " + std::to_string(c.b) +
           ")";
}

bool isBackground(const Rgb& c)
{
    return c.r < 8 && c.g < 8 && c.b < 8;
}

// The face toward the camera in a red light from behind the camera: red, with
// a little ambient in green and blue.
bool isRedLit(const Rgb& c)
{
    return c.r > 220 && c.g > 10 && c.g < 50 && c.b > 10 && c.b < 50;
}

// The same face in the world's sun (white light 0.5, ambient 0.1): grey.
bool isGreyLit(const Rgb& c)
{
    return std::abs(c.r - c.g) < 6 && std::abs(c.g - c.b) < 6 && c.r > 130 && c.r < 175;
}

// The world's sun the scene's light falls back on.
const engine::render::LightDesc kWorldSun{
    .direction = {0.0F, 0.0F, -1.0F}, .color = Vec3{0.5F}, .ambient = Vec3{0.1F}};

engine::core::Image drawFrame(Gpu& gpu, const SceneView& view)
{
    Renderer& renderer = *gpu.renderer;
    renderer.beginFrame(gpu.width, gpu.height, {0.0F, 0.0F, 0.0F, 1.0F});
    renderer.setView({.view = kView, .projection = kProjection});
    view.draw(renderer);
    renderer.endFrame();
    auto image = renderer.captureFrame();
    REQUIRE(image.hasValue());
    gpu.context->swapBuffers();
    return std::move(image).value();
}

bool contains(const std::string& text, std::string_view part)
{
    return text.find(part) != std::string::npos;
}

} // namespace

TEST_CASE("Scene entities draw where their transforms put them, in the scene's light",
          "[gpu][world][scene]")
{
    Gpu gpu = createGpu();
    Renderer& renderer = *gpu.renderer;
    const TempProject project;
    const engine::assets::AssetRegistry registry = registryOf(project);

    // The lighthouse is 7.8 m tall, its base on the origin, its tower about
    // 1.2 m wide; the face of the tower toward +Z faces the camera.
    Scene scene = makeScene();
    // Upright at (-10, -15).
    const EntityId upright =
        addMesh(scene, kLighthouse, placed({-10.0F, -15.0F, 0.0F}, 0.0F, Vec3{1.0F}));
    // Lying on its side: turned 90 degrees about Z, its top points to -X.
    const EntityId lying =
        addMesh(scene, kLighthouse, placed({12.0F, 5.0F, 0.0F}, 90.0F, Vec3{1.0F}));
    // Twice as tall and wide.
    const EntityId tall =
        addMesh(scene, kLighthouse, placed({-8.0F, 0.0F, 0.0F}, 0.0F, Vec3{2.0F}));
    // Without a transform: at the origin, as large as its mesh.
    (void)addMesh(scene, kLighthouse, std::nullopt);
    // Meshes that cannot be drawn, and an entity without a mesh.
    for (const AssetId bad : {kBroken, kMissing, kTexture, kUnregistered}) {
        (void)addMesh(scene, bad, placed({0.0F, 15.0F, 0.0F}, 0.0F, Vec3{3.0F}));
    }
    auto camera = scene.createEntity();
    REQUIRE(camera.hasValue());
    REQUIRE_FALSE(scene.addComponent(camera.value(), engine::scene::CameraComponent{}).has_value());
    // A red light from behind the camera.
    auto light = scene.createEntity();
    REQUIRE(light.hasValue());
    REQUIRE_FALSE(
        scene
            .addComponent(light.value(),
                          engine::scene::DirectionalLightComponent{.direction = {0.0F, 0.0F, -1.0F},
                                                                   .color = {1.0F, 0.0F, 0.0F},
                                                                   .intensity = 1.0F})
            .has_value());

    SceneView view;
    const std::vector<std::string> problems = view.update(renderer, scene, registry, kWorldSun);
    // Each mesh that cannot be drawn, once, naming it.
    REQUIRE(problems.size() == 4);
    CHECK(contains(problems[0], "(mesh 'models/broken.obj')"));
    CHECK(contains(problems[1], "models/missing.obj"));
    CHECK(contains(problems[2], "'textures/crate.png' is a texture, expected a mesh"));
    CHECK(contains(problems[3], "not registered"));
    CHECK(view.draws().size() == 8);
    CHECK(view.drawnCount() == 4);
    CHECK(view.meshCount() == 1);
    CHECK(view.materialCount() == 1);

    const std::uint32_t errorsBefore = renderer.errorCount();
    const engine::core::Image frame = drawFrame(gpu, view);
    CHECK(renderer.errorCount() == errorsBefore);

    const auto expectLit = [&](const char* what, Vec3 world) {
        const Rgb colour = pixelAt(frame, pixelOf(frame, world));
        INFO(what << " at (" << world.x << ", " << world.y << "): " << describe(colour));
        CHECK(isRedLit(colour));
    };
    const auto expectEmpty = [&](const char* what, Vec3 world) {
        const Rgb colour = pixelAt(frame, pixelOf(frame, world));
        INFO(what << " at (" << world.x << ", " << world.y << "): " << describe(colour));
        CHECK(isBackground(colour));
    };
    // The upright tower's front from 1 m to 5 m; nothing beside or above it.
    expectLit("upright tower, low", {-10.0F, -14.0F, 0.0F});
    expectLit("upright tower, high", {-10.0F, -10.0F, 0.0F});
    expectEmpty("beside the upright tower", {-13.0F, -12.0F, 0.0F});
    expectEmpty("above the upright tower", {-10.0F, -6.5F, 0.0F});
    // The lying tower runs from x = 12 towards -X; where it would stand
    // upright there is nothing.
    expectLit("lying tower", {8.0F, 5.0F, 0.0F});
    expectLit("lying tower, near its top", {6.5F, 5.0F, 0.0F});
    expectEmpty("where the lying tower would stand", {12.0F, 10.0F, 0.0F});
    // The tall tower reaches 15.6 m up, past where a tower of scale 1 ends.
    expectLit("tall tower, high", {-8.0F, 10.0F, 0.0F});
    expectEmpty("above the tall tower", {-8.0F, 16.5F, 0.0F});
    // The entity without a transform stands on the origin.
    expectLit("tower without a transform", {0.0F, 3.0F, 0.0F});
    // Where the meshes that cannot be drawn would be: nothing.
    expectEmpty("the meshes that cannot be drawn", {0.0F, 18.0F, 0.0F});

    // Without the scene's light, the world's sun: the same faces in grey.
    REQUIRE_FALSE(scene.destroyEntity(light.value()).has_value());
    CHECK(view.update(renderer, scene, registry, kWorldSun).empty());
    const engine::core::Image sunlit = drawFrame(gpu, view);
    const Rgb grey = pixelAt(sunlit, pixelOf(sunlit, {-10.0F, -10.0F, 0.0F}));
    INFO("in the world's sun: " << describe(grey));
    CHECK(isGreyLit(grey));

    // An entity moved and one removed: the next update shows it.
    TransformComponent moved = placed({10.0F, -15.0F, 0.0F}, 0.0F, Vec3{1.0F});
    REQUIRE_FALSE(scene.replaceComponent(upright, moved).has_value());
    REQUIRE_FALSE(scene.destroyEntity(tall).has_value());
    (void)lying;
    CHECK(view.update(renderer, scene, registry, kWorldSun).empty());
    const engine::core::Image changed = drawFrame(gpu, view);
    CHECK(isBackground(pixelAt(changed, pixelOf(changed, {-10.0F, -10.0F, 0.0F}))));
    CHECK(isGreyLit(pixelAt(changed, pixelOf(changed, {10.0F, -10.0F, 0.0F}))));
    CHECK(isBackground(pixelAt(changed, pixelOf(changed, {-8.0F, 10.0F, 0.0F}))));

    view.release(renderer);
    CHECK(view.meshCount() == 0);
    CHECK(view.materialCount() == 0);
    CHECK(view.draws().empty());
    view.release(renderer);
    CHECK(renderer.errorCount() == errorsBefore);
}

TEST_CASE("A scene view's renderer resources stay level while entities come and go",
          "[gpu][world][scene]")
{
    Gpu gpu = createGpu();
    Renderer& renderer = *gpu.renderer;
    const TempProject project;
    const engine::assets::AssetRegistry registry = registryOf(project);
    const ResourceCounts empty = renderer.resourceCounts();

    Scene scene = makeScene();
    SceneView view;
    // No mesh entity: nothing in the renderer, not even the material.
    CHECK(view.update(renderer, scene, registry, kWorldSun).empty());
    CHECK(renderer.resourceCounts() == empty);

    // Three lighthouses: one mesh and the material.
    std::vector<EntityId> towers;
    for (int i = 0; i < 3; ++i) {
        towers.push_back(
            addMesh(scene, kLighthouse,
                    placed({static_cast<float>(i) * 5.0F, 0.0F, 0.0F}, 0.0F, Vec3{1.0F})));
    }
    CHECK(view.update(renderer, scene, registry, kWorldSun).empty());
    const ResourceCounts one{
        .meshes = empty.meshes + 1, .textures = empty.textures, .materials = empty.materials + 1};
    CHECK(renderer.resourceCounts() == one);
    (void)drawFrame(gpu, view);

    // Removed and added again, several times: no new upload.
    for (int round = 0; round < 5; ++round) {
        const auto place = scene.placeOf(towers[1]);
        REQUIRE(place.hasValue());
        REQUIRE_FALSE(scene.destroyEntity(towers[1]).has_value());
        CHECK(view.update(renderer, scene, registry, kWorldSun).empty());
        CHECK(renderer.resourceCounts() == one);
        CHECK(view.drawnCount() == 2);
        REQUIRE(scene.restoreEntity(towers[1], place.value()).hasValue());
        REQUIRE_FALSE(
            scene.addComponent(towers[1], StaticMeshComponent{kLighthouse, {}}).has_value());
        CHECK(view.update(renderer, scene, registry, kWorldSun).empty());
        CHECK(renderer.resourceCounts() == one);
        CHECK(view.drawnCount() == 3);
        (void)drawFrame(gpu, view);
    }

    // A second mesh asset: one more mesh. Every entity gone: the meshes stay
    // until the view is released.
    const EntityId box = addMesh(scene, kBox, std::nullopt);
    CHECK(view.update(renderer, scene, registry, kWorldSun).empty());
    CHECK(renderer.resourceCounts().meshes == empty.meshes + 2);
    for (const EntityId entity : scene.entityIds()) {
        REQUIRE_FALSE(scene.destroyEntity(entity).has_value());
    }
    (void)box;
    CHECK(view.update(renderer, scene, registry, kWorldSun).empty());
    CHECK(view.drawnCount() == 0);
    CHECK(renderer.resourceCounts().meshes == empty.meshes + 2);
    CHECK(renderer.resourceCounts().materials == empty.materials + 1);
    (void)drawFrame(gpu, view);

    view.release(renderer);
    CHECK(renderer.resourceCounts() == empty);
    CHECK(renderer.errorCount() == 0);
}
