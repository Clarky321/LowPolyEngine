#pragma once

// The shape of one floating island as height data (ADR 0014): its outline seen
// from above, the height of its top surface above the rim and the depth of its
// rocky underside below the rim. All queries take positions in island-local
// metres in the XZ plane, relative to the island's anchor (the centre of its
// rim). Top and underside are both 0 on and outside the outline, so they meet
// at the rim, at the anchor's height. The mesh is built from these values in a
// later step; nothing here depends on the renderer.

#include "engine/core/Math.hpp"
#include "engine/worldgen/IslandLayout.hpp"
#include "engine/worldgen/Noise.hpp"
#include "engine/worldgen/WorldGenSettings.hpp"

#include <cstdint>

namespace engine::worldgen {

// The per-island values every query needs: drawn once from the island's seeds
// by makeIslandShape(), copied from the settings otherwise.
struct IslandShape {
    PlacedIsland island{};

    // Outline: an ellipse (long axis direction, long and short radius), whose
    // radius in each direction is scaled by 1 + outlineVariation * noise, with
    // the noise in [-1, 1]; the outline never leaves island.radius.
    math::Vec2 longAxis{1.0F, 0.0F};
    float longRadius = 0.0F;
    float shortRadius = 0.0F;
    float outlineVariation = 0.0F;
    float outlineWarp = 0.0F;

    // Top surface.
    NoiseSettings relief{};
    float ridgeWeight = 0.0F;
    // Whether this island got plateaus (terraceSteps 0) or terraces.
    bool terraced = false;
    int terraceSteps = 0;

    // Underside.
    int ledgeCount = 0;
    float protrusion = 0.0F;
    float roughness = 0.0F;

    // Noise keys (ADR 0014): outline, its warp, ledge jitter, spurs and rock
    // detail come from the island's shape seed; relief warp, fBm and ridges
    // from its terrain seed.
    std::uint64_t outlineKey = 0;
    std::uint64_t outlineWarpKey = 0;
    std::uint64_t ledgeKey = 0;
    std::uint64_t spurKey = 0;
    std::uint64_t detailKey = 0;
    std::uint64_t reliefWarpKey = 0;
    std::uint64_t reliefKey = 0;
    std::uint64_t ridgeKey = 0;

    friend bool operator==(const IslandShape&, const IslandShape&) = default;
};

// Draws the shape of a placed island. The settings must be the ones the island
// was placed with (they pass validate()). The island's terrain seed is
// deriveSeed(settings.seed, RandomStream::Terrain, island.index, version).
[[nodiscard]] IslandShape makeIslandShape(const PlacedIsland& island,
                                          const WorldGenSettings& settings);

// Distance from the anchor to the outline along a direction (normalized here;
// a zero direction counts as +X). In (0, island.radius].
[[nodiscard]] float outlineRadius(const IslandShape& shape, math::Vec2 direction);

// Normalized distance: 0 at the anchor, 1 on the outline, above 1 outside.
[[nodiscard]] float outlineDistance(const IslandShape& shape, math::Vec2 local);

// 1 in the island's interior, falling smoothly to 0 at the outline; 0 on and
// outside it.
[[nodiscard]] float islandMask(const IslandShape& shape, math::Vec2 local);

// Height of the top surface above the rim, in [0, island.topHeight].
[[nodiscard]] float topHeightAt(const IslandShape& shape, math::Vec2 local);

// Depth of the underside below the rim, in [0, island.undersideDepth].
[[nodiscard]] float undersideDepthAt(const IslandShape& shape, math::Vec2 local);

} // namespace engine::worldgen
