#pragma once

// On-screen diagnostics of the island preview: title, frame timing, renderer,
// window and camera state, controls.

#include "engine/camera/Camera.hpp"
#include "engine/core/FrameClock.hpp"
#include "engine/debug/TextMesh.hpp"
#include "engine/platform/Window.hpp"
#include "engine/render/Renderer.hpp"

#include <string>
#include <vector>

namespace island_preview {

struct OverlayData {
    engine::core::FrameStatsSnapshot frame;
    engine::render::RendererInfo renderer;
    engine::render::RenderStats renderStats;
    engine::platform::PixelSize framebuffer;
    engine::camera::Camera camera;
    float moveSpeed = 0.0F;
    // Under the title, e.g. the milestone and what is shown.
    std::string subtitle;
    // Facts about the scene (seed, islands, water, generation time), one entry
    // per line; none for the test scene.
    std::vector<std::string> scene;
};

// Replaces the builder's contents with the overlay for this frame.
void buildOverlay(engine::debug::TextMeshBuilder& builder, const OverlayData& data);

} // namespace island_preview
