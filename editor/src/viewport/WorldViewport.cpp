#include "WorldViewport.hpp"

#include "engine/core/Log.hpp"
#include "engine/world/Sky.hpp"
#include "engine/world/WorldView.hpp"
#include "engine/worldgen/Archipelago.hpp"

#include <chrono>
#include <exception>

namespace editor {

namespace {

// World units per second before the wheel changes it, as in island-preview.
constexpr float kMoveSpeed = 30.0F;

} // namespace

engine::core::Result<std::unique_ptr<WorldViewport>>
WorldViewport::create(engine::render::Renderer& renderer, Rid viewport,
                      const engine::worldgen::WorldGenSettings& settings)
{
    if (Rwidget_is_valid(viewport) == R_FALSE) {
        return engine::core::Error{"the viewport widget does not exist"};
    }
    const auto started = std::chrono::steady_clock::now();
    auto generated = engine::worldgen::generateArchipelago(settings);
    const double milliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
            .count();
    if (!generated) {
        return engine::core::Error{"world generation failed: " + generated.error().message};
    }
    const engine::worldgen::Archipelago& archipelago = generated.value();
    if (!archipelago.layout.warning.empty()) {
        engine::core::logWarning("editor", "archipelago: {}", archipelago.layout.warning);
    }
    if (!archipelago.clouds.warning.empty()) {
        engine::core::logWarning("editor", "clouds: {}", archipelago.clouds.warning);
    }

    std::unique_ptr<WorldViewport> result(new WorldViewport(renderer, viewport));
    if (auto error = result->world_.upload(renderer, archipelago, settings)) {
        result->release();
        return *error;
    }
    auto sky = renderer.createMesh(engine::world::buildSkyDome(engine::world::floatingWorldSky()));
    if (!sky) {
        result->release();
        return sky.error();
    }
    result->sky_ = sky.value();
    result->light_ = engine::world::archipelagoSunlight();
    result->camera_ = engine::world::overviewCamera(archipelago);

    Rviewport_set_draw_callback(viewport, &WorldViewport::onDraw, result.get());
    result->input_.attach(viewport);
    engine::core::logInfo("editor",
                          "viewport: archipelago of seed {}, {} of {} islands, {} triangles, {} "
                          "clouds, generated in {:.1f} ms",
                          settings.seed, archipelago.islands.size(),
                          archipelago.layout.requestedCount, archipelago.triangleCount(),
                          archipelago.clouds.clusters.size(), milliseconds);
    engine::core::logInfo("editor", "in the viewport: hold the right mouse button to look "
                                    "around, W/A/S/D to fly, E and Q up and down, Shift faster, "
                                    "the mouse wheel to change speed");
    return result;
}

WorldViewport::WorldViewport(engine::render::Renderer& renderer, Rid viewport)
    : renderer_(&renderer), viewport_(viewport),
      controller_(engine::camera::FreeFlySettings{.moveSpeed = kMoveSpeed})
{
}

// The editor calls release() in Application::stop, while Raven and the
// renderer still exist; nothing of either is touched here.
WorldViewport::~WorldViewport() = default;

void WorldViewport::update(const engine::core::FrameTime& time)
{
    seconds_ = time.elapsedSeconds;
    controller_.update(camera_, input_.take(), static_cast<float>(time.deltaSeconds));
}

void WorldViewport::release() noexcept
{
    input_.detach();
    if (viewport_ != R_INVALID_ID && Rwidget_is_valid(viewport_) != R_FALSE) {
        Rviewport_set_draw_callback(viewport_, nullptr, nullptr);
    }
    viewport_ = R_INVALID_ID;
    if (renderer_ != nullptr) {
        world_.release(*renderer_);
        renderer_->destroyMesh(sky_);
        sky_ = {};
        renderer_ = nullptr;
    }
}

void WorldViewport::onDraw(const RViewportDrawInfo* info, void* user)
{
    if (info == nullptr || user == nullptr) {
        return;
    }
    // Called from inside Raven's frame: nothing may escape.
    try {
        static_cast<WorldViewport*>(user)->draw(*info);
    } catch (const std::exception& error) {
        engine::core::logError("editor", "viewport: {}", error.what());
    } catch (...) {
        engine::core::logError("editor", "viewport: unknown exception while drawing");
    }
}

void WorldViewport::draw(const RViewportDrawInfo& info)
{
    if (renderer_ == nullptr || info.width <= 0 || info.height <= 0) {
        return;
    }
    ++drawnFrames_;
    lastDrawnSize_ = {info.width, info.height};
    engine::render::Renderer& renderer = *renderer_;
    const engine::math::Mat4 view = engine::camera::viewMatrix(camera_);
    const engine::math::Mat4 projection = engine::camera::projectionMatrix(
        camera_, engine::camera::aspectRatio(info.width, info.height));

    // As island-preview draws it: the sky first, behind everything (no depth
    // test, no depth writes), then the islands and clouds, then the water,
    // blended, roughly back to front.
    renderer.setView({
        .view = engine::world::skyView(view),
        .projection = projection,
        .depth = engine::render::DepthMode::Off,
    });
    renderer.drawMesh(sky_);
    renderer.setView({.view = view, .projection = projection});
    renderer.setLight(light_);
    world_.drawOpaque(renderer, seconds_);
    world_.drawWater(renderer, view, projection, camera_.position);
}

} // namespace editor
