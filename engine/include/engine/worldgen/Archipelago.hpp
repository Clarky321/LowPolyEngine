#pragma once

// A whole generated archipelago: the steps of M3.4 to M3.7, the local water
// of M3.9 and the clouds of M3.10 run in order for valid settings. Pure CPU
// data; the caller uploads the meshes, draws each island and its water at the
// island's anchor and each cloud cluster where it has drifted (cloudDrift).

#include "engine/core/Result.hpp"
#include "engine/geometry/MeshData.hpp"
#include "engine/worldgen/Clouds.hpp"
#include "engine/worldgen/IslandLayout.hpp"
#include "engine/worldgen/IslandPalette.hpp"
#include "engine/worldgen/IslandShape.hpp"
#include "engine/worldgen/IslandWater.hpp"
#include "engine/worldgen/WorldGenSettings.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace engine::worldgen {

struct GeneratedIsland {
    IslandShape shape{};
    ChosenPalette palette{};
    // Top and underside in one painted, flat-shaded mesh, island-local (the
    // anchor shape.island.anchor at the origin).
    geometry::MeshData mesh;
    // Lakes, stream and waterfall of the island, with their translucent mesh
    // in the same island-local coordinates.
    IslandWater water;
};

// Water bodies of a whole archipelago: lakes (ponds included), islands with a
// stream (whatever its number of runs) and waterfalls.
struct WaterCounts {
    std::size_t lakes = 0;
    std::size_t streams = 0;
    std::size_t waterfalls = 0;

    friend bool operator==(const WaterCounts&, const WaterCounts&) = default;
};

struct Archipelago {
    // Placement, including the warning if islands were left out.
    IslandLayout layout;
    // One per placed island, in layout order.
    std::vector<GeneratedIsland> islands;
    // The cloud clusters around and between the islands.
    CloudField clouds;

    // Triangles of all island, water and cloud meshes.
    [[nodiscard]] std::uint64_t triangleCount() const noexcept;
    [[nodiscard]] WaterCounts waterCounts() const noexcept;
};

// Places the islands, shapes them, builds their meshes with mesh.resolution,
// paints them with their palettes, generates their water (with the layout's
// islands as obstacles for waterfalls) and places the clouds clear of the
// islands. Invalid settings give validate()'s error.
[[nodiscard]] core::Result<Archipelago> generateArchipelago(const WorldGenSettings& settings);

} // namespace engine::worldgen
