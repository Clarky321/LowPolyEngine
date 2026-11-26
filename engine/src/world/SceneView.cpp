#include "engine/world/SceneView.hpp"

#include <algorithm>
#include <format>

namespace engine::world {

render::MaterialDesc defaultSceneMaterial()
{
    return render::MaterialDesc{.baseColor = {1.0F, 1.0F, 1.0F, 1.0F},
                                .shading = render::Shading::Lit};
}

std::vector<std::string> SceneView::update(render::Renderer& renderer, const scene::Scene& scene,
                                           const assets::AssetRegistry& registry,
                                           const render::LightDesc& worldSun)
{
    draws_ = meshDraws(scene);
    light_ = sceneLight(scene, worldSun);

    std::vector<assets::AssetId> meshes;
    meshes.reserve(draws_.size());
    for (const MeshDraw& draw : draws_) {
        meshes.push_back(draw.mesh);
    }
    std::vector<std::string> problems = assets_.load(meshes, registry);

    if (material_.isNull() && !draws_.empty()) {
        auto material = renderer.createMaterial(defaultSceneMaterial());
        if (material) {
            material_ = material.value();
        } else {
            problems.push_back("default material: " + material.error().message);
        }
    }

    for (const assets::AssetId mesh : meshes) {
        if (meshes_.contains(mesh)) {
            continue;
        }
        render::MeshHandle handle;
        // A mesh that could not be read was reported by load().
        if (const MeshAsset* asset = assets_.find(mesh); asset != nullptr && asset->mesh) {
            auto uploaded = renderer.createMesh(*asset->mesh);
            if (uploaded) {
                handle = uploaded.value();
            } else {
                problems.push_back(std::format("asset {}: the mesh could not be uploaded: {}",
                                               mesh.toString(), uploaded.error().message));
            }
        }
        meshes_.emplace(mesh, handle);
    }
    return problems;
}

void SceneView::draw(render::Renderer& renderer) const
{
    if (material_.isNull()) {
        return;
    }
    renderer.setLight(light_);
    for (const MeshDraw& draw : draws_) {
        const auto found = meshes_.find(draw.mesh);
        if (found != meshes_.end() && !found->second.isNull()) {
            renderer.drawMesh(found->second, material_, draw.model);
        }
    }
}

void SceneView::release(render::Renderer& renderer) noexcept
{
    for (const auto& [mesh, handle] : meshes_) {
        renderer.destroyMesh(handle);
    }
    renderer.destroyMaterial(material_);
    meshes_.clear();
    material_ = {};
    draws_.clear();
    light_ = {};
    assets_.clear();
}

bool SceneView::isDrawn(assets::AssetId mesh) const
{
    const auto found = meshes_.find(mesh);
    return !material_.isNull() && found != meshes_.end() && !found->second.isNull();
}

std::size_t SceneView::drawnCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        draws_.begin(), draws_.end(), [this](const MeshDraw& draw) { return isDrawn(draw.mesh); }));
}

std::size_t SceneView::meshCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        meshes_.begin(), meshes_.end(), [](const auto& entry) { return !entry.second.isNull(); }));
}

std::size_t SceneView::materialCount() const noexcept
{
    return material_.isNull() ? 0U : 1U;
}

} // namespace engine::world
