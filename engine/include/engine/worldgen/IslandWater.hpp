#pragma once

// Local water of one island (ADR 0016): lakes in basins of the top surface, a
// stream that runs downhill to the rim and fills the hollows on its way as
// ponds, and a waterfall where the stream falls over the rim. There is no world sea
// level and no global water plane: all water belongs to an island and lies on
// or within it. Positions are island-local, like the island's mesh (the rim's
// anchor at the origin, +Y up). Pure CPU data; nothing here depends on the
// renderer.

#include "engine/core/Math.hpp"
#include "engine/geometry/MeshData.hpp"
#include "engine/worldgen/IslandLayout.hpp"
#include "engine/worldgen/IslandShape.hpp"
#include "engine/worldgen/WorldGenSettings.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace engine::worldgen {

// The top surface is sampled on a grid of kWaterGridCells x kWaterGridCells
// square cells over the island's bounding square; lakes and streams follow
// these samples.
inline constexpr int kWaterGridCells = 48;

// A basin holds a lake only if all of it lies within this normalized distance
// from the anchor, away from the steep edge of the top.
inline constexpr float kLakeMaxDistance = 0.9F;

// A stream's source lies within this normalized distance from the anchor.
inline constexpr float kStreamSourceMaxDistance = 0.7F;

// A stream's surface lies this far above the sampled terrain, in metres.
inline constexpr float kStreamLift = 0.12F;

// A waterfall's top edge lies this far outside the rim, in metres.
inline constexpr float kWaterfallMargin = 0.15F;

// Every water triangle has at least this area, in square metres.
inline constexpr double kMinWaterTriangleArea = 1.0e-6;

// The triangles of one water body in IslandWater::mesh.
struct WaterMeshRange {
    std::size_t firstTriangle = 0;
    std::size_t triangleCount = 0;

    friend bool operator==(const WaterMeshRange&, const WaterMeshRange&) = default;
};

// A lake or pond: a flat surface in a basin of the top surface.
struct Lake {
    // XZ position of the basin's lowest sample.
    math::Vec2 deepest{0.0F};
    // Heights above the rim: the terrain at the lowest sample, the water
    // surface, and the level at which the basin would overflow;
    // bottom < level <= spillLevel.
    float bottom = 0.0F;
    float level = 0.0F;
    float spillLevel = 0.0F;
    // A pond that the stream runs through: filled up to its spill level, where
    // the stream flows on. Other lakes are filled only partly.
    bool overflows = false;
    // Grid samples under the surface.
    std::size_t samples = 0;
    WaterMeshRange mesh{};

    friend bool operator==(const Lake&, const Lake&) = default;
};

enum class StreamMouth {
    // Leaves the island over its rim.
    Rim,
    // Flows into one of the island's lakes.
    Lake,
};

// One run of a stream: a ribbon of water following its centre line downhill.
// A stream that passes through hollows fills them as ponds and is stored as
// one run per stretch between them: from its spring or a pond's outflow to the
// next pond or the rim, in the order the water flows.
struct Stream {
    // Centre line from the source to the mouth, on the water surface; its
    // height never rises.
    std::vector<math::Vec3> path;
    float width = 0.0F;
    // Whether the run starts at a pond's outflow (index into
    // IslandWater::lakes) rather than at the spring.
    bool fromLake = false;
    std::size_t sourceLake = 0;
    StreamMouth mouth = StreamMouth::Rim;
    // Index into IslandWater::lakes when the mouth is a lake.
    std::size_t lake = 0;
    WaterMeshRange mesh{};

    friend bool operator==(const Stream&, const Stream&) = default;
};

// A waterfall: a ribbon falling from the rim where a stream leaves the island,
// curving away from it and fading out below.
struct Waterfall {
    // Centre of its top edge, kWaterfallMargin outside the rim, at the height
    // of the stream's mouth.
    math::Vec3 top{0.0F};
    // Horizontal unit direction away from the island (from the anchor through
    // the stream's mouth).
    math::Vec2 outward{1.0F, 0.0F};
    // Width of its top edge; it widens as it falls.
    float width = 0.0F;
    // How far it falls.
    float length = 0.0F;
    // How much further out its bottom edge lies than its top edge: the water
    // leaves the rim horizontally and falls freely, so the centre of the
    // section at fraction f of the fall (0 at the top, 1 at the bottom) is
    // top + outward * drift * f - (0, length * f^2, 0).
    float drift = 0.0F;
    WaterMeshRange mesh{};

    friend bool operator==(const Waterfall&, const Waterfall&) = default;
};

struct IslandWater {
    // The stream's ponds first, in the order it reaches them, then the other
    // lakes.
    std::vector<Lake> lakes;
    std::vector<Stream> streams;
    std::vector<Waterfall> waterfalls;
    // All of them in one flat-shaded mesh: lakes, then streams, then
    // waterfalls. Translucent: the alpha of its vertex colours is below 1, so
    // it is drawn with alpha blending after the opaque geometry. Island-local;
    // empty when the island has no water.
    geometry::MeshData mesh;

    [[nodiscard]] bool empty() const noexcept
    {
        return lakes.empty() && streams.empty() && waterfalls.empty();
    }
};

// Generates the water of one island from its own random stream
// (RandomStream::Water; lakes, the stream and the waterfall draw from separate
// keys of it). The settings must be the ones the island was shaped with (they
// pass validate()). water.maxLakesPerIsland limits lakes and ponds together.
// `islands` are the placed islands of the archipelago (the island itself among
// them is recognized by its index): a waterfall whose fall would pass through
// the volume of another island is left out. Islands without a suitable basin
// get no lake, and a stream needs a way down from a spring on the island's
// higher ground to the rim.
[[nodiscard]] IslandWater generateIslandWater(const IslandShape& shape,
                                              const WorldGenSettings& settings,
                                              std::span<const PlacedIsland> islands = {});

} // namespace engine::worldgen
