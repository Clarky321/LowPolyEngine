#include "engine/worldgen/IslandLayout.hpp"

#include "FloatingPointRules.hpp"

#include "engine/worldgen/Random.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <utility>

namespace engine::worldgen {

namespace {

// True if the two islands keep the horizontal spacing between their cylinders
// or the vertical spacing between their height intervals. In double, so the
// squared distances cannot overflow; no square root is needed.
bool keepsSpacing(const PlacedIsland& a, const PlacedIsland& b, double minHorizontal,
                  double minVertical)
{
    const double dx = static_cast<double>(a.anchor.x) - static_cast<double>(b.anchor.x);
    const double dz = static_cast<double>(a.anchor.z) - static_cast<double>(b.anchor.z);
    const double reach =
        static_cast<double>(a.radius) + static_cast<double>(b.radius) + minHorizontal;
    if (dx * dx + dz * dz >= reach * reach) {
        return true;
    }
    const double aLow = static_cast<double>(a.anchor.y) - static_cast<double>(a.undersideDepth);
    const double aHigh = static_cast<double>(a.anchor.y) + static_cast<double>(a.topHeight);
    const double bLow = static_cast<double>(b.anchor.y) - static_cast<double>(b.undersideDepth);
    const double bHigh = static_cast<double>(b.anchor.y) + static_cast<double>(b.topHeight);
    return std::max(bLow - aHigh, aLow - bHigh) >= minVertical;
}

} // namespace

core::Result<IslandLayout> placeIslands(const WorldGenSettings& settings)
{
    if (auto error = validate(settings)) {
        return std::move(*error);
    }
    const WorldBounds& bounds = settings.bounds;
    const IslandSettings& islands = settings.islands;
    const auto minHorizontal = static_cast<double>(islands.minHorizontalSpacing);
    const auto minVertical = static_cast<double>(islands.minVerticalSpacing);

    IslandLayout layout;
    layout.requestedCount = islands.count;
    layout.islands.reserve(static_cast<std::size_t>(islands.count));
    for (int k = 0; k < islands.count; ++k) {
        const auto index = static_cast<std::uint32_t>(k);
        Pcg32 rng{deriveSeed(settings.seed, RandomStream::IslandLayout, index,
                             settings.generatorVersion)};
        for (int attempt = 0; attempt < kPlacementAttemptsPerIsland; ++attempt) {
            // Five draws per attempt, always in this order. The settings
            // guarantee that the largest island fits the bounds horizontally
            // and, at every altitude of the range, vertically.
            PlacedIsland candidate;
            candidate.index = index;
            candidate.radius = rng.nextFloat(islands.radius.min, islands.radius.max);
            const float depthRatio =
                rng.nextFloat(settings.underside.depthRatio.min, settings.underside.depthRatio.max);
            candidate.anchor.x =
                rng.nextFloat(bounds.min.x + candidate.radius, bounds.max.x - candidate.radius);
            candidate.anchor.z =
                rng.nextFloat(bounds.min.z + candidate.radius, bounds.max.z - candidate.radius);
            candidate.anchor.y = rng.nextFloat(islands.altitude.min, islands.altitude.max);
            candidate.topHeight = settings.elevation.maxHeightRatio * candidate.radius;
            candidate.undersideDepth = depthRatio * candidate.radius;

            const bool fits = std::all_of(
                layout.islands.begin(), layout.islands.end(), [&](const PlacedIsland& other) {
                    return keepsSpacing(candidate, other, minHorizontal, minVertical);
                });
            if (fits) {
                candidate.shapeSeed = deriveSeed(settings.seed, RandomStream::IslandShape, index,
                                                 settings.generatorVersion);
                layout.islands.push_back(candidate);
                break;
            }
        }
    }

    const auto placedCount = static_cast<int>(layout.islands.size());
    if (placedCount < islands.count) {
        layout.warning = std::format("placed {} of {} islands: {} found no position that keeps "
                                     "the spacing in {} attempts each",
                                     placedCount, islands.count, islands.count - placedCount,
                                     kPlacementAttemptsPerIsland);
    }
    return layout;
}

} // namespace engine::worldgen
