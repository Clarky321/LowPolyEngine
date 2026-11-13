#pragma once

// Island colours from palette data (ADR 0015). Each island gets one of the
// built-in palettes, tinted slightly per island, and its mesh is painted face
// by face: the top by elevation, slope and distance from the rim, the
// underside in strata of stone. Colours become vertex colours; shaders hold no
// palette. Colours are in display space, like all colours of the renderer
// (ADR 0009).

#include "engine/core/Math.hpp"
#include "engine/core/Result.hpp"
#include "engine/worldgen/IslandLayout.hpp"
#include "engine/worldgen/IslandMesh.hpp"
#include "engine/worldgen/IslandShape.hpp"
#include "engine/worldgen/WorldGenSettings.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace engine::worldgen {

// RGB colours of one island, each component in [0, 1].
struct IslandPalette {
    math::Vec3 grass{0.0F};
    math::Vec3 soil{0.0F};
    math::Vec3 sand{0.0F};
    math::Vec3 rock{0.0F};
    math::Vec3 snow{0.0F};
    // The underside: stone just below the rim, deepStone at the tip.
    math::Vec3 stone{0.0F};
    math::Vec3 deepStone{0.0F};

    friend bool operator==(const IslandPalette&, const IslandPalette&) = default;
};

// The palettes islands choose from, in a fixed order (an island stores the
// index of its base palette).
[[nodiscard]] std::span<const IslandPalette> builtInPalettes();

// The first colour with a component that is not finite or outside [0, 1],
// named by its entry, e.g. "rock.g is 1.2, outside 0 to 1".
[[nodiscard]] std::optional<core::Error> validate(const IslandPalette& palette);

// Kind of top surface, chosen per face.
enum class TopSurface { Grass, Soil, Sand, Rock, Snow };

// Thresholds of classifyTop (ADR 0015).
inline constexpr float kRockSlope = 0.7F;      // faces with normal.y below this are rock
inline constexpr float kSnowLine = 0.82F;      // elevation at and above this is snow
inline constexpr float kBeachDistance = 0.88F; // normalized distance at and above this ...
inline constexpr float kBeachHeight = 0.1F;    // ... with elevation below this is sand
inline constexpr float kSoilSlope = 0.86F;     // faces with normal.y below this are soil

// The surface of a top face from its elevation (height above the rim divided
// by the island's topHeight), the y component of its unit normal and the
// normalized distance of its centre from the anchor (1 on the rim). The rules
// apply in this order: rock if normalY < kRockSlope; snow if elevation >=
// kSnowLine; sand if rimDistance >= kBeachDistance and elevation <
// kBeachHeight; soil if normalY < kSoilSlope; otherwise grass.
[[nodiscard]] TopSurface classifyTop(float elevation, float normalY, float rimDistance);

// Number of stone strata on the underside: face centres at depth fraction d
// (depth below the rim divided by the island's undersideDepth) take stratum
// min(floor(d * 4), 3), coloured from stone (0) to deepStone (3) in equal steps.
inline constexpr int kStoneStrata = 4;

// The colour of stone stratum k, 0 <= k < kStoneStrata.
[[nodiscard]] math::Vec3 stoneStratum(const IslandPalette& palette, int stratum);

// The palette of one island and the built-in palette it is based on.
struct ChosenPalette {
    std::size_t basePalette = 0;
    IslandPalette palette{};

    friend bool operator==(const ChosenPalette&, const ChosenPalette&) = default;
};

// One palette per island of the layout, in layout order. Island k draws from
// Pcg32(deriveSeed(seed, RandomStream::IslandPalette, index, generatorVersion)):
// a base palette, a brightness and a tint per colour channel. Its base palette
// differs from that of the nearest island placed before it, so neighbours look
// different; the tint makes every island's colours its own.
[[nodiscard]] std::vector<ChosenPalette> choosePalettes(const IslandLayout& layout,
                                                        const WorldGenSettings& settings);

// Writes the palette's colours (alpha 1) into both parts of an island mesh,
// one colour per face: classifyTop for the top, stone strata for the underside.
void paintIsland(IslandMesh& mesh, const IslandShape& shape, const IslandPalette& palette);

} // namespace engine::worldgen
