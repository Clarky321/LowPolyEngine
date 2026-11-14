// GPU tier: island-preview's generated world on the GPU and the Dear ImGui
// debug UI (M3.12). Regenerating the world keeps the renderer's resource
// counts level; the debug UI starts, draws and stops without OpenGL errors.
// Runs from the repository root (tests/rendering/CMakeLists.txt).

#include "WorldScene.hpp"

#include "engine/debug/DebugUi.hpp"
#include "engine/platform/GlContext.hpp"
#include "engine/platform/Platform.hpp"
#include "engine/platform/Window.hpp"
#include "engine/render/Renderer.hpp"
#include "engine/worldgen/Archipelago.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <imgui.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

using engine::render::Renderer;
using engine::render::ResourceCounts;
using engine::worldgen::WorldGenSettings;

namespace {

// Owns everything a renderer needs; members are destroyed in reverse order.
struct Gpu {
    std::optional<engine::platform::Platform> platform;
    std::optional<engine::platform::Window> window;
    std::optional<engine::platform::GlContext> context;
    std::optional<Renderer> renderer;
};

Gpu createGpu()
{
    Gpu gpu;
    auto platform = engine::platform::Platform::initialize();
    REQUIRE(platform.hasValue());
    gpu.platform.emplace(std::move(platform).value());
    auto window = engine::platform::Window::create(*gpu.platform, {.title = "lpe world test",
                                                                   .width = 256,
                                                                   .height = 256,
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
    return gpu;
}

engine::worldgen::Archipelago worldOf(std::uint64_t seed)
{
    WorldGenSettings settings;
    settings.seed = seed;
    settings.mesh.resolution = 12;
    auto archipelago = engine::worldgen::generateArchipelago(settings);
    REQUIRE(archipelago.hasValue());
    return std::move(archipelago).value();
}

// Meshes a world needs: one per island, per island with water, per cloud.
std::size_t meshesOf(const engine::worldgen::Archipelago& archipelago)
{
    std::size_t count = archipelago.islands.size() + archipelago.clouds.clusters.size();
    for (const auto& island : archipelago.islands) {
        count += island.water.mesh.positions.empty() ? 0U : 1U;
    }
    return count;
}

// Draws the world once from above, as island-preview does.
void drawFrame(Gpu& gpu, island_preview::WorldScene& world, double seconds)
{
    Renderer& renderer = *gpu.renderer;
    renderer.beginFrame(256, 256, {0.5F, 0.6F, 0.8F, 1.0F});
    const engine::math::Mat4 view =
        glm::lookAt(engine::math::Vec3{0.0F, 600.0F, 600.0F}, engine::math::Vec3{0.0F},
                    engine::math::Vec3{0.0F, 1.0F, 0.0F});
    const engine::math::Mat4 projection = glm::perspective(1.0F, 1.0F, 1.0F, 2000.0F);
    renderer.setView({.view = view, .projection = projection});
    world.drawOpaque(renderer, seconds);
    world.drawWater(renderer, view, projection, engine::math::Vec3{0.0F, 600.0F, 600.0F});
    renderer.endFrame();
    gpu.context->swapBuffers();
}

} // namespace

TEST_CASE("Regenerating the world keeps the renderer's resource counts level", "[gpu][world]")
{
    Gpu gpu = createGpu();
    Renderer& renderer = *gpu.renderer;
    const ResourceCounts empty = renderer.resourceCounts();

    island_preview::WorldScene world;
    const auto first = worldOf(1U);
    REQUIRE_FALSE(world.upload(renderer, first, WorldGenSettings{}).has_value());
    const ResourceCounts loaded = renderer.resourceCounts();
    CHECK(loaded.meshes == empty.meshes + meshesOf(first));
    CHECK(loaded.materials == empty.materials + 2U);
    CHECK(loaded.textures == empty.textures);
    CHECK(world.meshCount() == meshesOf(first));
    CHECK(world.materialCount() == 2U);
    drawFrame(gpu, world, 0.0);

    // Regenerating the same world again and again: the same counts every time.
    for (int round = 0; round < 5; ++round) {
        REQUIRE_FALSE(world.upload(renderer, first, WorldGenSettings{}).has_value());
        CHECK(renderer.resourceCounts() == loaded);
        drawFrame(gpu, world, static_cast<double>(round));
    }

    // Other worlds hold exactly what they need, nothing of the ones before.
    for (const std::uint64_t seed : {2U, 42U, 7U}) {
        const auto other = worldOf(seed);
        REQUIRE_FALSE(world.upload(renderer, other, WorldGenSettings{}).has_value());
        const ResourceCounts counts = renderer.resourceCounts();
        CHECK(counts.meshes == empty.meshes + meshesOf(other));
        CHECK(counts.materials == empty.materials + 2U);
        drawFrame(gpu, world, 1.0);
    }

    world.release(renderer);
    CHECK(renderer.resourceCounts() == empty);
    CHECK(world.empty());
    CHECK(renderer.errorCount() == 0U);
}

TEST_CASE("The debug UI starts, draws a window and stops", "[gpu][debug-ui]")
{
    Gpu gpu = createGpu();
    Renderer& renderer = *gpu.renderer;
    {
        auto ui = engine::debug::DebugUi::create(*gpu.window, *gpu.context);
        REQUIRE(ui.hasValue());
        // Only one at a time.
        const auto second = engine::debug::DebugUi::create(*gpu.window, *gpu.context);
        REQUIRE_FALSE(second.hasValue());
        CHECK(second.error().message.find("only one") != std::string::npos);

        for (int frame = 0; frame < 3; ++frame) {
            ui.value().beginFrame();
            ImGui::Begin("test window");
            ImGui::Text("frame %d", frame);
            float value = 0.5F;
            ImGui::SliderFloat("value", &value, 0.0F, 1.0F);
            ImGui::End();
            renderer.beginFrame(256, 256, {0.2F, 0.2F, 0.2F, 1.0F});
            ui.value().render();
            renderer.endFrame();
            gpu.context->swapBuffers();
        }
        // A frame that is not drawn (as while minimized) is ended cleanly.
        ui.value().beginFrame();
        ui.value().endFrame();
        CHECK_FALSE(ui.value().wantsKeyboard());
    }
    // Gone with its owner: a new one can start.
    auto again = engine::debug::DebugUi::create(*gpu.window, *gpu.context);
    CHECK(again.hasValue());
    CHECK(renderer.errorCount() == 0U);
}
