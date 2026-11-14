#pragma once

// island-preview's generated world on the GPU (M3.12): one mesh per island,
// per island's water and per cloud cluster, and the two materials they are
// drawn with. Uploading a new world first releases everything the previous one
// held, so regenerating keeps the renderer's resource counts level.

#include "engine/core/Math.hpp"
#include "engine/core/Result.hpp"
#include "engine/render/Renderer.hpp"
#include "engine/worldgen/Archipelago.hpp"
#include "engine/worldgen/Clouds.hpp"
#include "engine/worldgen/WorldGenSettings.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace island_preview {

class WorldScene {
public:
    WorldScene() = default;
    WorldScene(const WorldScene&) = delete;
    WorldScene& operator=(const WorldScene&) = delete;
    WorldScene(WorldScene&&) = default;
    WorldScene& operator=(WorldScene&&) = default;
    // release() must be called while the renderer exists; the handles are
    // not destroyed here.
    ~WorldScene() = default;

    // Releases the current world, then uploads the archipelago generated with
    // `settings` (the clouds' wind and bounds). On failure nothing stays
    // uploaded.
    [[nodiscard]] std::optional<engine::core::Error>
    upload(engine::render::Renderer& renderer, const engine::worldgen::Archipelago& archipelago,
           const engine::worldgen::WorldGenSettings& settings);

    // Destroys every mesh and material of the world.
    void release(engine::render::Renderer& renderer) noexcept;

    [[nodiscard]] bool empty() const noexcept { return islands_.empty() && clouds_.empty(); }

    // Meshes and materials the world holds in the renderer.
    [[nodiscard]] std::size_t meshCount() const noexcept;
    [[nodiscard]] std::size_t materialCount() const noexcept;

    // The islands, lit, and the clouds where the wind has moved them after
    // `seconds`, with the renderer's current view and light.
    void drawOpaque(engine::render::Renderer& renderer, double seconds) const;

    // The islands' water, blended over the opaque geometry, island by island
    // from back to front as seen from `cameraPosition`.
    void drawWater(engine::render::Renderer& renderer, const engine::math::Mat4& view,
                   const engine::math::Mat4& projection, const engine::math::Vec3& cameraPosition);

private:
    struct Island {
        engine::render::MeshHandle mesh;
        // Null when the island has no water.
        engine::render::MeshHandle water;
        engine::math::Vec3 position{0.0F};
    };
    struct Cloud {
        engine::render::MeshHandle mesh;
        // Without its mesh data, which is in the renderer.
        engine::worldgen::CloudCluster cluster;
    };

    std::vector<Island> islands_;
    std::vector<Cloud> clouds_;
    // Islands are lit; water and clouds show their own colours.
    engine::render::MaterialHandle litMaterial_;
    engine::render::MaterialHandle unlitMaterial_;
    engine::worldgen::WorldGenSettings settings_;
    // Indices of islands with water, sorted back to front each frame.
    std::vector<std::size_t> waterOrder_;
};

} // namespace island_preview
