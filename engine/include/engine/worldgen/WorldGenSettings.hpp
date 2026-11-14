#pragma once

// One versioned description of a generated world (ADR 0013): everything the
// generator reads besides the code itself. Lengths are in metres, +Y is up,
// angles in degrees (authored data). There is no world sea level and no global
// water plane: water exists only on islands.
//
// Every numeric field has a default (its member initializer) and an inclusive
// range (the constants in `limits`). validate() rejects values outside their
// range and inconsistent combinations, naming the field by its path, e.g.
// "islands.altitude.max"; clampToLimits() moves single fields into their range
// instead. The JSON loader (M3.11) and the WorldGen panel (M3.12) use the same
// rules. Shape, water and cloud fields describe what later steps generate and
// may be refined there while settings are not yet saved to files.

#include "engine/core/Math.hpp"
#include "engine/core/Result.hpp"
#include "engine/worldgen/Noise.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

namespace engine::worldgen {

// The version of the generator in this build. It is part of every derived seed
// (ADR 0003) and is incremented whenever the same settings would produce a
// different world.
inline constexpr std::uint32_t kGeneratorVersion = 1;

// Inclusive limits of one setting.
template <typename T>
struct Limits {
    T min;
    T max;
};

// An authored range of values; valid when min <= max.
struct FloatRange {
    float min = 0.0F;
    float max = 0.0F;

    friend bool operator==(const FloatRange&, const FloatRange&) = default;
};

namespace limits {

// Every coordinate: world bounds, island and cloud altitudes.
inline constexpr Limits<float> kCoordinate{-500.0F, 500.0F};
// Each axis of the bounds spans at least this much.
inline constexpr float kMinWorldExtent = 20.0F;
// The bounds' diagonal stays within the camera's far plane.
inline constexpr float kMaxWorldDiagonal = 1000.0F;

inline constexpr Limits<int> kIslandCount{1, 64};
inline constexpr Limits<float> kIslandRadius{4.0F, 150.0F};
inline constexpr Limits<float> kIslandSpacing{0.0F, 200.0F};

inline constexpr Limits<float> kEllipticity{0.0F, 0.6F};
inline constexpr Limits<float> kOutlineRoughness{0.0F, 1.0F};
inline constexpr Limits<float> kOutlineWarp{0.0F, kMaxWarpStrength};

inline constexpr Limits<float> kMaxHeightRatio{0.05F, 1.0F};
inline constexpr Limits<float> kRidgeWeight{0.0F, 1.0F};
inline constexpr Limits<float> kPlateauChance{0.0F, 1.0F};
inline constexpr Limits<int> kTerraceSteps{0, 8};

inline constexpr Limits<float> kDepthRatio{0.2F, 2.0F};
inline constexpr Limits<int> kLedgeCount{0, 6};
inline constexpr Limits<float> kProtrusion{0.0F, 1.0F};
inline constexpr Limits<float> kUndersideRoughness{0.0F, 1.0F};

inline constexpr Limits<float> kNoiseFrequency{0.001F, 1.0F};
inline constexpr Limits<int> kNoiseOctaves{1, kMaxNoiseOctaves};
inline constexpr Limits<float> kNoiseLacunarity{1.0F, kMaxNoiseLacunarity};
inline constexpr Limits<float> kNoisePersistence{0.0F, 1.0F};
inline constexpr Limits<float> kNoiseWarpStrength{0.0F, kMaxWarpStrength};

inline constexpr Limits<float> kChance{0.0F, 1.0F};
inline constexpr Limits<int> kLakesPerIsland{0, 4};

inline constexpr Limits<int> kCloudClusterCount{0, 128};
inline constexpr Limits<float> kCloudSize{1.0F, 100.0F};
inline constexpr Limits<float> kCloudDensity{0.0F, 1.0F};
inline constexpr Limits<float> kWindDirectionDegrees{0.0F, 360.0F};
inline constexpr Limits<float> kWindSpeed{0.0F, 20.0F};
inline constexpr Limits<float> kCloudClearance{0.0F, 50.0F};

inline constexpr Limits<int> kMeshResolution{8, 256};

} // namespace limits

// Axis-aligned box that contains every island and cloud.
struct WorldBounds {
    math::Vec3 min{-260.0F, -80.0F, -260.0F};
    math::Vec3 max{260.0F, 160.0F, 260.0F};

    friend bool operator==(const WorldBounds&, const WorldBounds&) = default;
};

// Number, placement and size of the islands (M3.4). An island's anchor is the
// centre of its rim, where the top surface meets the underside; its outline
// stays inside a circle of its radius around the anchor.
struct IslandSettings {
    int count = 12;
    // Height of the rim.
    FloatRange altitude{-20.0F, 110.0F};
    // Radius of the island's bounding circle.
    FloatRange radius{16.0F, 55.0F};
    // Two islands keep at least one of these gaps between their volumes.
    float minHorizontalSpacing = 18.0F;
    float minVerticalSpacing = 12.0F;

    friend bool operator==(const IslandSettings&, const IslandSettings&) = default;
};

// Outline of an island seen from above (M3.5).
struct ShapeSettings {
    // Largest flattening of the outline: the short axis is (1 - e) times the long one.
    float ellipticity = 0.3F;
    // Strength of the noise that makes the outline irregular.
    float outlineRoughness = 0.35F;
    // Domain warp of the outline noise, in noise cells.
    float outlineWarp = 0.75F;

    friend bool operator==(const ShapeSettings&, const ShapeSettings&) = default;
};

// Top surface: hills, mountains, cliffs and plateaus (M3.5).
struct ElevationSettings {
    // Highest point of the top above the rim, as a fraction of the radius.
    float maxHeightRatio = 0.4F;
    // Share of ridge noise in the relief (mountain crests).
    float ridgeWeight = 0.4F;
    // Probability that an island gets plateaus and terraces.
    float plateauChance = 0.3F;
    // Number of terrace levels on such an island.
    int terraceSteps = 3;

    friend bool operator==(const ElevationSettings&, const ElevationSettings&) = default;
};

// Rocky mass below the rim (M3.5).
struct UndersideSettings {
    // Depth below the rim as a fraction of the radius; each island draws from it.
    FloatRange depthRatio{0.6F, 1.0F};
    // Number of steps (ledges) in the depth profile.
    int ledgeCount = 3;
    // Strength of spurs and protrusions.
    float protrusion = 0.4F;
    // Irregularity of the rock faces.
    float roughness = 0.5F;

    friend bool operator==(const UndersideSettings&, const UndersideSettings&) = default;
};

// Noise of the relief (M3.2 functions).
struct NoiseSettings {
    // Noise cells per metre.
    float frequency = 0.045F;
    FractalNoiseParams fractal{};
    // Domain warp of the relief, in noise cells.
    float warpStrength = 0.6F;

    friend bool operator==(const NoiseSettings&, const NoiseSettings&) = default;
};

// Local water on islands only (M3.9).
struct WaterSettings {
    bool enabled = true;
    // Probability that a suitable basin holds a lake or pond.
    float lakeChance = 0.5F;
    int maxLakesPerIsland = 2;
    // Probability that an island gets a stream.
    float streamChance = 0.4F;
    // Probability that a stream reaching the rim falls over it.
    float waterfallChance = 0.6F;

    friend bool operator==(const WaterSettings&, const WaterSettings&) = default;
};

// Low-poly cloud clusters between the islands (M3.10).
struct CloudSettings {
    int clusterCount = 20;
    FloatRange altitude{-60.0F, 150.0F};
    // Radius of a cluster.
    FloatRange size{8.0F, 26.0F};
    // How densely a cluster is filled with shapes.
    float density = 0.55F;
    // Direction the wind blows towards in the XZ plane: 0 is +X, 90 is +Z.
    float windDirectionDegrees = 30.0F;
    // Drift speed in metres per second (a display effect, not generated data).
    float windSpeed = 1.5F;
    // Smallest gap between a cloud and an island volume.
    float islandClearance = 6.0F;

    friend bool operator==(const CloudSettings&, const CloudSettings&) = default;
};

// Mesh density (M3.6).
struct MeshSettings {
    // Grid cells across an island's diameter.
    int resolution = 48;

    friend bool operator==(const MeshSettings&, const MeshSettings&) = default;
};

struct WorldGenSettings {
    // Any value is valid.
    std::uint64_t seed = 1;
    // Must equal kGeneratorVersion.
    std::uint32_t generatorVersion = kGeneratorVersion;
    WorldBounds bounds{};
    IslandSettings islands{};
    ShapeSettings shape{};
    ElevationSettings elevation{};
    UndersideSettings underside{};
    NoiseSettings noise{};
    WaterSettings water{};
    CloudSettings clouds{};
    MeshSettings mesh{};

    friend bool operator==(const WorldGenSettings&, const WorldGenSettings&) = default;
};

// The first problem in a fixed order, or nothing if the settings are valid:
// the generator version, then every numeric field against its range in the
// order of declaration (NaN is out of range), then the combinations: each axis
// of the bounds spans at least limits::kMinWorldExtent; the bounds' diagonal is
// at most limits::kMaxWorldDiagonal; every FloatRange has min <= max; the
// largest island fits the bounds horizontally, and its top and underside fit
// them vertically at the lowest and highest altitude; the spacings are not
// larger than the world; cloud altitudes lie within the bounds. Each message
// starts with the path of the field it names.
[[nodiscard]] std::optional<core::Error> validate(const WorldGenSettings& settings);

// The settings with every numeric field moved into its range; NaN becomes the
// field's default. The seed, the generator version and combinations are left
// as they are: validate() still reports those.
[[nodiscard]] WorldGenSettings clampToLimits(WorldGenSettings settings);

// One editable field of the settings, for generic editors such as the
// WorldGen panel: its path (as validate() and the settings file name it), a
// pointer to its value inside the settings it was taken from, its limits (0
// and 1 for the water switch) and its default.
struct SettingsField {
    std::string_view path;
    std::variant<float*, int*, bool*> value;
    double min = 0.0;
    double max = 0.0;
    double defaultValue = 0.0;
};

// Every field below the seed and the generator version, in the order of
// declaration (the order of the settings file): the numeric fields and
// water.enabled. The pointers stay valid as long as `settings` does.
[[nodiscard]] std::vector<SettingsField> settingsFields(WorldGenSettings& settings);

} // namespace engine::worldgen
