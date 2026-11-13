#pragma once

// A whole generated archipelago: the steps of M3.4 to M3.7 run in order for
// valid settings. Pure CPU data; the caller uploads the meshes and draws each
// island at its anchor.

#include "engine/core/Result.hpp"
#include "engine/geometry/MeshData.hpp"
#include "engine/worldgen/IslandLayout.hpp"
#include "engine/worldgen/IslandPalette.hpp"
#include "engine/worldgen/IslandShape.hpp"
#include "engine/worldgen/WorldGenSettings.hpp"

#include <cstdint>
#include <vector>

namespace engine::worldgen {

struct GeneratedIsland {
    IslandShape shape{};
    ChosenPalette palette{};
    // Top and underside in one painted, flat-shaded mesh, island-local (the
    // anchor shape.island.anchor at the origin).
    geometry::MeshData mesh;
};

struct Archipelago {
    // Placement, including the warning if islands were left out.
    IslandLayout layout;
    // One per placed island, in layout order.
    std::vector<GeneratedIsland> islands;

    [[nodiscard]] std::uint64_t triangleCount() const noexcept;
};

// Places the islands, shapes them, builds their meshes with mesh.resolution
// and paints them with their palettes. Invalid settings give validate()'s
// error.
[[nodiscard]] core::Result<Archipelago> generateArchipelago(const WorldGenSettings& settings);

} // namespace engine::worldgen
