#pragma once

// The generated world in the editor's viewport (M5.3): the archipelago of the
// given world settings under the sky, drawn by the engine's renderer from the
// viewport widget's draw callback (ADR 0019, ADR 0027), seen through the editor
// camera (ViewportInput). Raven calls back inside its frame, with OpenGL's
// viewport and scissor set to the widget, so the world is drawn inside the
// widget only and follows its size; the projection uses the widget's aspect.
//
// With a document (M5.6), the scene's entities with a static mesh are drawn in
// the world (engine::world::SceneView). The view reads the scene again when the
// document's version has changed since it last read it, checked once per
// frame in update(), outside Raven's frame: not every frame.

#include "ViewportInput.hpp"

#include "editor/document/EditorDocument.hpp"

#include "engine/camera/Camera.hpp"
#include "engine/camera/FreeFlyController.hpp"
#include "engine/core/FrameClock.hpp"
#include "engine/core/Result.hpp"
#include "engine/render/Renderer.hpp"
#include "engine/world/SceneView.hpp"
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
    // places the camera on the overview of the archipelago, shows the
    // entities of `document`'s scene if there is a document, and attaches to
    // `viewport` (its draw callback and its events). On failure nothing stays
    // uploaded or attached. The document is not owned and must outlive the
    // viewport.
    [[nodiscard]] static engine::core::Result<std::unique_ptr<WorldViewport>>
    create(engine::render::Renderer& renderer, Rid viewport,
           const engine::worldgen::WorldGenSettings& settings,
           const EditorDocument* document = nullptr);

    // Raven holds a pointer to the object while it is attached.
    WorldViewport(const WorldViewport&) = delete;
    WorldViewport& operator=(const WorldViewport&) = delete;
    WorldViewport(WorldViewport&&) = delete;
    WorldViewport& operator=(WorldViewport&&) = delete;
    // release() must have been called while the renderer exists.
    ~WorldViewport();

    // Moves the camera by the input gathered since the previous frame; the
    // clouds drift by the frame's simulation time. Reads the document's scene
    // again if the document changed.
    void update(const engine::core::FrameTime& time);

    // Detaches from the widget and destroys every renderer resource of the
    // world, the sky and the scene's entities. Safe to call twice.
    void release() noexcept;

    [[nodiscard]] const engine::camera::Camera& camera() const noexcept { return camera_; }
    [[nodiscard]] float moveSpeed() const noexcept { return controller_.moveSpeed(); }
    // Frames Raven drew the viewport in, and the widget's size in the last of
    // them (zero before the first).
    [[nodiscard]] std::uint64_t drawnFrames() const noexcept { return drawnFrames_; }
    [[nodiscard]] ViewportSize lastDrawnSize() const noexcept { return lastDrawnSize_; }
    // The scene's entities as the viewport draws them.
    [[nodiscard]] const engine::world::SceneView& scene() const noexcept { return scene_; }
    // How often the viewport has read the document's scene (once at start,
    // then once per change of the document).
    [[nodiscard]] std::uint64_t sceneUpdates() const noexcept { return sceneUpdates_; }

private:
    WorldViewport(engine::render::Renderer& renderer, Rid viewport);

    static void onDraw(const RViewportDrawInfo* info, void* user);
    void draw(const RViewportDrawInfo& info);
    // Reads the document's scene again if its version changed.
    void followDocument();

    // Not owned; set from create() to release().
    engine::render::Renderer* renderer_ = nullptr;
    Rid viewport_ = R_INVALID_ID;
    engine::world::WorldScene world_;
    // Not owned; null without a project.
    const EditorDocument* document_ = nullptr;
    engine::world::SceneView scene_;
    // The document's version the scene view was last updated to, if it was.
    // A plain value and a flag rather than std::optional (GCC's -O3 reports
    // false "may be used uninitialized" warnings for optionals).
    std::uint64_t sceneVersion_ = 0;
    bool sceneRead_ = false;
    std::uint64_t sceneUpdates_ = 0;
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
