#pragma once

// A scene's entities on the GPU (M5.6): the meshes of its entities with a
// static mesh, uploaded once per mesh asset, and drawn where their transforms
// put them (SceneDraws.hpp), with one default material in the scene's light.
// The editor's viewport draws it inside the generated world; like WorldScene
// it uses the renderer's API only and never stores the renderer.
//
// The view does not follow the scene by itself: update() reads the scene
// again when its owner knows it changed (the editor: the document's version),
// not every frame. Meshes stay uploaded while the view lives, also when no
// entity uses them any more, so adding, removing, undoing and redoing
// entities read and upload nothing again: the renderer's resources grow only
// with the distinct mesh assets the scene has used, one mesh each, plus the
// material. A mesh that cannot be read or uploaded is reported once, and its
// entities are skipped.
//
// The view belongs to one project, as its MeshAssets do: release() it before
// it shows another project's scene.

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/render/Renderer.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/world/MeshAssets.hpp"
#include "engine/world/SceneDraws.hpp"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace engine::world {

// The material every scene mesh is drawn with until material files exist
// (M6): a white base color, so the mesh's vertex colors show as they are, lit
// by the scene's light.
[[nodiscard]] render::MaterialDesc defaultSceneMaterial();

class SceneView {
public:
    SceneView() = default;
    SceneView(const SceneView&) = delete;
    SceneView& operator=(const SceneView&) = delete;
    SceneView(SceneView&&) = default;
    SceneView& operator=(SceneView&&) = default;
    // release() must be called while the renderer exists; the handles are
    // not destroyed here.
    ~SceneView() = default;

    // Reads the scene again: its mesh entities and their matrices, and its
    // light (sceneLight() with `worldSun` as the fallback). A mesh asset an
    // entity uses for the first time is read through `registry` and
    // uploaded. Returns the problems found by this call: each mesh that
    // could not be read or uploaded, once for the view's life, and the
    // default material if it could not be created.
    std::vector<std::string> update(render::Renderer& renderer, const scene::Scene& scene,
                                    const assets::AssetRegistry& registry,
                                    const render::LightDesc& worldSun);

    // Sets the scene's light and draws every entity whose mesh is uploaded,
    // with the renderer's current view.
    void draw(render::Renderer& renderer) const;

    // Destroys every mesh and the material, and forgets the scene and the
    // meshes read. Safe to call twice.
    void release(render::Renderer& renderer) noexcept;

    // The scene's mesh entities at the last update(), drawn or not.
    [[nodiscard]] const std::vector<MeshDraw>& draws() const noexcept { return draws_; }
    // Of them, those draw() draws: their mesh is uploaded.
    [[nodiscard]] std::size_t drawnCount() const noexcept;
    // The light draw() sets.
    [[nodiscard]] const render::LightDesc& light() const noexcept { return light_; }
    // Meshes and materials the view holds in the renderer.
    [[nodiscard]] std::size_t meshCount() const noexcept;
    [[nodiscard]] std::size_t materialCount() const noexcept;
    // The meshes read, with their CPU data.
    [[nodiscard]] const MeshAssets& meshAssets() const noexcept { return assets_; }

private:
    [[nodiscard]] bool isDrawn(assets::AssetId mesh) const;

    MeshAssets assets_;
    // Every mesh asset the view has met: its uploaded mesh, or a null handle
    // when it could not be read or uploaded.
    std::map<assets::AssetId, render::MeshHandle> meshes_;
    std::vector<MeshDraw> draws_;
    render::LightDesc light_;
    render::MaterialHandle material_;
};

} // namespace engine::world
