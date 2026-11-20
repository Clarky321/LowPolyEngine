#pragma once

// How the generated world is looked at (M3; moved from island-preview in M5.3,
// where island-preview and the editor's viewport share it): the sunlight it is
// lit with and the camera that shows the whole archipelago.

#include "engine/camera/Camera.hpp"
#include "engine/core/Math.hpp"
#include "engine/render/Renderer.hpp"
#include "engine/worldgen/Archipelago.hpp"

namespace engine::world {

// A warm sun from above and to the side and a cool ambient, so tops are
// bright, cliffs shaded and undersides cool and darker (ADR 0011).
[[nodiscard]] render::LightDesc archipelagoSunlight();

// A camera at `position` looking at `target` (they must differ).
[[nodiscard]] camera::Camera cameraLookingAt(const math::Vec3& position, const math::Vec3& target);

// Overview of the archipelago: from above and to the side of the islands'
// centre, far enough to see all of them, looking a little below the centre.
[[nodiscard]] camera::Camera overviewCamera(const worldgen::Archipelago& archipelago);

} // namespace engine::world
