#pragma once

// On-screen diagnostics of the island preview: title, frame timing, renderer,
// window and camera state, controls.

#include "engine/camera/Camera.hpp"
#include "engine/core/FrameClock.hpp"
#include "engine/debug/TextMesh.hpp"
#include "engine/platform/Window.hpp"
#include "engine/render/Renderer.hpp"

namespace island_preview {

struct OverlayData {
    engine::core::FrameStatsSnapshot frame;
    engine::render::RendererInfo renderer;
    engine::render::RenderStats renderStats;
    engine::platform::PixelSize framebuffer;
    engine::camera::Camera camera;
    float moveSpeed = 0.0F;
};

// Replaces the builder's contents with the overlay for this frame.
void buildOverlay(engine::debug::TextMeshBuilder& builder, const OverlayData& data);

} // namespace island_preview
