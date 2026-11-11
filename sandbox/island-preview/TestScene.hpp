#pragma once

// Static scene for flying around: checker ground, a pyramid, boxes, corner
// pillars, a row of material swatches, a textured crate and a lighthouse
// imported from an OBJ file, all lit by one directional light. Placeholder
// content until world generation (M3).
//
// Shapes have white vertex colors; their hue comes from each object's
// material (M2.6) or its texture (M2.7), their brightness from the light
// (M2.8). The ground keeps its two greens as vertex colors with a white
// material: all these ways of coloring are first-class.

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

// Files the scene needs are optional at startup: one that is missing or
// broken is logged, and the objects that use it are drawn without the texture
// or left out (models).

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

// An object whose mesh is loaded from a model file (OBJ subset, ADR 0012).
struct SceneModel {
    // OBJ file in <asset directory>/models/.
    std::string file;
    // Its baseColorTexture stays null here, as in SceneObject.
    engine::render::MaterialDesc material;
    engine::math::Vec3 position{0.0F};
    // Index into TestScene::textures for the material's base color texture.
    std::optional<std::size_t> texture{};
};

struct TestScene {
    std::vector<engine::geometry::MeshData> meshes;
    std::vector<SceneTexture> textures;
    std::vector<SceneObject> objects;
    std::vector<SceneModel> models;
    // Every material of the scene is lit.
    engine::render::LightDesc light;
};

[[nodiscard]] TestScene buildTestScene();

} // namespace island_preview
