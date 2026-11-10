#pragma once

// Static scene for flying around in M1: checker ground, a pyramid, boxes and
// corner pillars. Placeholder content until world generation (M3).

#include "engine/core/Math.hpp"
#include "engine/geometry/MeshData.hpp"

#include <vector>

namespace island_preview {

struct SceneObject {
    engine::geometry::MeshData mesh;
    engine::math::Vec3 position{0.0F};
    // Rotation about the vertical axis over time; 0 for static objects.
    float spinRadiansPerSecond = 0.0F;
};

[[nodiscard]] std::vector<SceneObject> buildTestScene();

} // namespace island_preview
