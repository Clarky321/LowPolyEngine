#pragma once

// Static scene for flying around: checker ground, a pyramid, boxes, corner
// pillars and a row of material swatches. Placeholder content until world
// generation (M3).
//
// Shapes keep only their brightness in the vertex colors (fixed factors until
// lighting, M2.8); their hue comes from each object's material (M2.6). The
// ground keeps its two greens as vertex colors with a white material: both
// ways of coloring are first-class.

#include "engine/core/Math.hpp"
#include "engine/geometry/MeshData.hpp"
#include "engine/render/Renderer.hpp"

#include <cstddef>
#include <vector>

namespace island_preview {

struct SceneObject {
    // Index into TestScene::meshes; several objects may share one mesh.
    std::size_t mesh = 0;
    engine::render::MaterialDesc material;
    engine::math::Vec3 position{0.0F};
    // Rotation about the vertical axis over time; 0 for static objects.
    float spinRadiansPerSecond = 0.0F;
};

struct TestScene {
    std::vector<engine::geometry::MeshData> meshes;
    std::vector<SceneObject> objects;
};

[[nodiscard]] TestScene buildTestScene();

} // namespace island_preview
