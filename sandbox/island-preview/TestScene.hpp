#pragma once

// Static scene for flying around: checker ground, a pyramid, boxes, corner
// pillars, a row of material swatches and a textured crate. Placeholder
// content until world generation (M3).
//
// Shapes keep only their brightness in the vertex colors (fixed factors until
// lighting, M2.8); their hue comes from each object's material (M2.6) or its
// texture (M2.7). The ground keeps its two greens as vertex colors with a
// white material: all these ways of coloring are first-class.

#include "engine/core/Math.hpp"
#include "engine/geometry/MeshData.hpp"
#include "engine/render/Renderer.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace island_preview {

struct SceneTexture {
    // PNG file in <asset directory>/textures/.
    std::string file;
    engine::render::TextureDesc desc;
};

struct SceneObject {
    // Index into TestScene::meshes; several objects may share one mesh.
    std::size_t mesh = 0;
    // Its baseColorTexture stays null here: textures get handles only once
    // they are loaded, so `texture` names the one to use.
    engine::render::MaterialDesc material;
    engine::math::Vec3 position{0.0F};
    // Rotation about the vertical axis over time; 0 for static objects.
    float spinRadiansPerSecond = 0.0F;
    // Index into TestScene::textures for the material's base color texture.
    std::optional<std::size_t> texture{};
};

struct TestScene {
    std::vector<engine::geometry::MeshData> meshes;
    std::vector<SceneTexture> textures;
    std::vector<SceneObject> objects;
};

[[nodiscard]] TestScene buildTestScene();

} // namespace island_preview
