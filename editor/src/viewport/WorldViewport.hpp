#pragma once

// The generated world in the editor's viewport (M5.3): the archipelago of the
// given world settings under the sky, drawn by the engine's renderer from the
// viewport widget's draw callback (ADR 0019, ADR 0027), seen through the editor
// camera (ViewportInput). Raven calls back inside its frame, with OpenGL's
// viewport and scissor set to the widget, so the world is drawn inside the
// widget only and follows its size; the projection uses the widget's aspect.

#include "ViewportInput.hpp"

#include "engine/camera/Camera.hpp"
#include "engine/camera/FreeFlyController.hpp"
#include "engine/core/FrameClock.hpp"
#include "engine/core/Result.hpp"
#include "engine/render/Renderer.hpp"
#include "engine/world/WorldScene.hpp"
#include "engine/worldgen/WorldGenSettings.hpp"

#include <Raven.h>

#include <cstdint>
#include <memory>

namespace editor {

// A size in pixels.
struct ViewportSize {
    int width = 0;
    int height = 0;
};

class WorldViewport {
public:
    // Generates the archipelago of `settings` and uploads it and the sky,
    // places the camera on the overview of the archipelago, and attaches to
    // `viewport` (its draw callback and its events). On failure nothing stays
    // uploaded or attached.
    [[nodiscard]] static engine::core::Result<std::unique_ptr<WorldViewport>>
    create(engine::render::Renderer& renderer, Rid viewport,
           const engine::worldgen::WorldGenSettings& settings);

    // Raven holds a pointer to the object while it is attached.
    WorldViewport(const WorldViewport&) = delete;
    WorldViewport& operator=(const WorldViewport&) = delete;
    WorldViewport(WorldViewport&&) = delete;
    WorldViewport& operator=(WorldViewport&&) = delete;
    // release() must have been called while the renderer exists.
    ~WorldViewport();

    // Moves the camera by the input gathered since the previous frame; the
    // clouds drift by the frame's simulation time.
    void update(const engine::core::FrameTime& time);

    // Detaches from the widget and destroys every renderer resource of the
    // world and the sky. Safe to call twice.
    void release() noexcept;

    [[nodiscard]] const engine::camera::Camera& camera() const noexcept { return camera_; }
    [[nodiscard]] float moveSpeed() const noexcept { return controller_.moveSpeed(); }
    // Frames Raven drew the viewport in, and the widget's size in the last of
    // them (zero before the first).
    [[nodiscard]] std::uint64_t drawnFrames() const noexcept { return drawnFrames_; }
    [[nodiscard]] ViewportSize lastDrawnSize() const noexcept { return lastDrawnSize_; }

private:
    WorldViewport(engine::render::Renderer& renderer, Rid viewport);

    static void onDraw(const RViewportDrawInfo* info, void* user);
    void draw(const RViewportDrawInfo& info);

    // Not owned; set from create() to release().
    engine::render::Renderer* renderer_ = nullptr;
    Rid viewport_ = R_INVALID_ID;
    engine::world::WorldScene world_;
    engine::render::MeshHandle sky_;
    engine::render::LightDesc light_;
    engine::camera::Camera camera_;
    engine::camera::FreeFlyController controller_;
    ViewportInput input_;
    double seconds_ = 0.0;
    std::uint64_t drawnFrames_ = 0;
    ViewportSize lastDrawnSize_;
};

} // namespace editor
