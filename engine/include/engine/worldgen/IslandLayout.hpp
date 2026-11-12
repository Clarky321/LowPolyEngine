#pragma once

// Placement of the islands of an archipelago in 3D (ADR 0013, Amendment 1).
//
// Each requested island k (0 to islands.count - 1) draws candidates from its own
// generator, Pcg32(deriveSeed(seed, RandomStream::IslandLayout, k,
// generatorVersion)), in a fixed order, and takes the first candidate that keeps
// the spacing to every island placed before it. An island that finds no such
// candidate within kPlacementAttemptsPerIsland attempts is left out, and the
// layout says so in its warning; placement never loops without bound. Because
// every island has its own generator, a layout with more islands starts with
// the islands of a layout with fewer, as long as they all fit.

#include "engine/core/Math.hpp"
#include "engine/core/Result.hpp"
#include "engine/worldgen/WorldGenSettings.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::worldgen {

inline constexpr int kPlacementAttemptsPerIsland = 100;

// One placed island and its bounding volume: a vertical cylinder of `radius`
// around the anchor, from anchor.y - undersideDepth to anchor.y + topHeight.
struct PlacedIsland {
    // Number of the requested island, 0 to islands.count - 1. Islands that do
    // not fit leave gaps in these numbers.
    std::uint32_t index = 0;
    // Centre of the rim, where the top surface meets the underside.
    math::Vec3 anchor{0.0F};
    float radius = 0.0F;
    // elevation.maxHeightRatio * radius: no point of the top is higher.
    float topHeight = 0.0F;
    // A ratio drawn from underside.depthRatio, times the radius.
    float undersideDepth = 0.0F;
    // deriveSeed(seed, RandomStream::IslandShape, index, generatorVersion),
    // the seed of everything that shapes this island.
    std::uint64_t shapeSeed = 0;

    friend bool operator==(const PlacedIsland&, const PlacedIsland&) = default;
};

struct IslandLayout {
    // In the order of their index.
    std::vector<PlacedIsland> islands;
    int requestedCount = 0;
    // Empty when every requested island was placed; otherwise it says how many
    // were left out, for the caller to log.
    std::string warning;

    friend bool operator==(const IslandLayout&, const IslandLayout&) = default;
};

// Places the islands of valid settings. Invalid settings give validate()'s
// error. Every island lies inside the bounds (within float rounding) with its
// rim at an altitude from islands.altitude, and every two islands keep
// islands.minHorizontalSpacing between their cylinders or
// islands.minVerticalSpacing between their height intervals.
[[nodiscard]] core::Result<IslandLayout> placeIslands(const WorldGenSettings& settings);

} // namespace engine::worldgen
