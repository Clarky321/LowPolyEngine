#include "WorldScene.hpp"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <utility>

namespace island_preview {

namespace {

// A drifting cloud smaller than this share of its size is not drawn.
constexpr float kMinCloudScale = 1.0e-3F;

} // namespace

std::optional<engine::core::Error>
WorldScene::upload(engine::render::Renderer& renderer,
                   const engine::worldgen::Archipelago& archipelago,
                   const engine::worldgen::WorldGenSettings& settings)
{
    release(renderer);
    settings_ = settings;
    const auto fail = [&](engine::core::Error error) {
        release(renderer);
        return std::optional<engine::core::Error>{std::move(error)};
    };

    // Island colours are vertex colours (palettes): one white lit material
    // lets the sun shade them. Water and clouds show their own colours, unlit:
    // water reads as light-filled against the shaded rock, a waterfall and the
    // clouds stay bright on every side.
    auto lit = renderer.createMaterial(
        {.baseColor = {1.0F, 1.0F, 1.0F, 1.0F}, .shading = engine::render::Shading::Lit});
    if (!lit) {
        return fail(lit.error());
    }
    litMaterial_ = lit.value();
    auto unlit = renderer.createMaterial(
        {.baseColor = {1.0F, 1.0F, 1.0F, 1.0F}, .shading = engine::render::Shading::Unlit});
    if (!unlit) {
        return fail(unlit.error());
    }
    unlitMaterial_ = unlit.value();

    for (const auto& island : archipelago.islands) {
        Island uploaded;
        uploaded.position = island.shape.island.anchor;
        auto mesh = renderer.createMesh(island.mesh);
        if (!mesh) {
            return fail(mesh.error());
        }
        uploaded.mesh = mesh.value();
        // Translucent through its vertex colours' alpha.
        if (!island.water.mesh.positions.empty()) {
            auto water = renderer.createMesh(island.water.mesh);
            if (!water) {
                renderer.destroyMesh(uploaded.mesh);
                return fail(water.error());
            }
            uploaded.water = water.value();
            waterOrder_.push_back(islands_.size());
        }
        islands_.push_back(uploaded);
    }
    for (const auto& cluster : archipelago.clouds.clusters) {
        auto mesh = renderer.createMesh(cluster.mesh);
        if (!mesh) {
            return fail(mesh.error());
        }
        Cloud cloud{.mesh = mesh.value(), .cluster = cluster};
        cloud.cluster.mesh = {};
        clouds_.push_back(std::move(cloud));
    }
    return std::nullopt;
}

void WorldScene::release(engine::render::Renderer& renderer) noexcept
{
    for (const Island& island : islands_) {
        renderer.destroyMesh(island.mesh);
        renderer.destroyMesh(island.water);
    }
    for (const Cloud& cloud : clouds_) {
        renderer.destroyMesh(cloud.mesh);
    }
    renderer.destroyMaterial(litMaterial_);
    renderer.destroyMaterial(unlitMaterial_);
    islands_.clear();
    clouds_.clear();
    waterOrder_.clear();
    litMaterial_ = {};
    unlitMaterial_ = {};
}

std::size_t WorldScene::meshCount() const noexcept
{
    return islands_.size() + waterOrder_.size() + clouds_.size();
}

std::size_t WorldScene::materialCount() const noexcept
{
    return (litMaterial_.isNull() ? 0U : 1U) + (unlitMaterial_.isNull() ? 0U : 1U);
}

void WorldScene::drawOpaque(engine::render::Renderer& renderer, double seconds) const
{
    for (const Island& island : islands_) {
        renderer.drawMesh(island.mesh, litMaterial_,
                          glm::translate(engine::math::Mat4{1.0F}, island.position));
    }
    // Clouds where the wind has moved them; a cloud shrinks away at the edge
    // of the world before it wraps around.
    for (const Cloud& cloud : clouds_) {
        const engine::worldgen::CloudDrift drift =
            engine::worldgen::cloudDrift(cloud.cluster, settings_, seconds);
        if (drift.scale < kMinCloudScale) {
            continue;
        }
        const engine::math::Mat4 model =
            glm::scale(glm::translate(engine::math::Mat4{1.0F}, drift.position),
                       engine::math::Vec3{drift.scale});
        renderer.drawMesh(cloud.mesh, unlitMaterial_, model);
    }
}

void WorldScene::drawWater(engine::render::Renderer& renderer, const engine::math::Mat4& view,
                           const engine::math::Mat4& projection,
                           const engine::math::Vec3& cameraPosition)
{
    if (waterOrder_.empty()) {
        return;
    }
    renderer.setView({
        .view = view,
        .projection = projection,
        .blend = engine::render::BlendMode::Alpha,
    });
    const auto distance = [&](std::size_t index) {
        const engine::math::Vec3 offset = islands_[index].position - cameraPosition;
        return glm::dot(offset, offset);
    };
    std::sort(waterOrder_.begin(), waterOrder_.end(),
              [&](std::size_t a, std::size_t b) { return distance(a) > distance(b); });
    for (const std::size_t index : waterOrder_) {
        const Island& island = islands_[index];
        renderer.drawMesh(island.water, unlitMaterial_,
                          glm::translate(engine::math::Mat4{1.0F}, island.position));
    }
}

} // namespace island_preview
