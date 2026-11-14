#include "engine/worldgen/Archipelago.hpp"

#include "FloatingPointRules.hpp"

#include "engine/worldgen/IslandMesh.hpp"

#include <cstddef>
#include <utility>

namespace engine::worldgen {

std::uint64_t Archipelago::triangleCount() const noexcept
{
    std::uint64_t count = 0;
    for (const GeneratedIsland& island : islands) {
        count += island.mesh.triangleCount() + island.water.mesh.triangleCount();
    }
    return count + clouds.triangleCount();
}

WaterCounts Archipelago::waterCounts() const noexcept
{
    WaterCounts counts;
    for (const GeneratedIsland& island : islands) {
        counts.lakes += island.water.lakes.size();
        counts.streams += island.water.streams.empty() ? 0U : 1U;
        counts.waterfalls += island.water.waterfalls.size();
    }
    return counts;
}

core::Result<Archipelago> generateArchipelago(const WorldGenSettings& settings)
{
    auto layout = placeIslands(settings);
    if (!layout) {
        return layout.error();
    }
    Archipelago archipelago;
    archipelago.layout = std::move(layout.value());
    const std::vector<ChosenPalette> palettes = choosePalettes(archipelago.layout, settings);

    archipelago.islands.reserve(archipelago.layout.islands.size());
    for (std::size_t i = 0; i < archipelago.layout.islands.size(); ++i) {
        GeneratedIsland island;
        island.shape = makeIslandShape(archipelago.layout.islands[i], settings);
        island.palette = palettes[i];
        IslandMesh parts = buildIslandMesh(island.shape, settings.mesh.resolution);
        paintIsland(parts, island.shape, island.palette.palette);
        island.mesh = std::move(parts.top);
        if (auto error = geometry::append(island.mesh, parts.underside)) {
            return std::move(*error);
        }
        island.water = generateIslandWater(island.shape, settings, archipelago.layout.islands);
        archipelago.islands.push_back(std::move(island));
    }
    archipelago.clouds = generateClouds(settings, archipelago.layout.islands);
    return archipelago;
}

} // namespace engine::worldgen
