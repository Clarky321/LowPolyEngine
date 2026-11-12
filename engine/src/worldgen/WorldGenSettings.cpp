#include "engine/worldgen/WorldGenSettings.hpp"

#include "FloatingPointRules.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace engine::worldgen {

namespace {

// Calls visit(path, value, limits, defaultValue) for every numeric field in the
// order of declaration. Settings is WorldGenSettings or const WorldGenSettings.
template <typename Settings, typename Visit>
void forEachNumericField(Settings& s, const WorldGenSettings& d, Visit&& visit)
{
    using namespace limits;
    visit("bounds.min.x", s.bounds.min.x, kCoordinate, d.bounds.min.x);
    visit("bounds.min.y", s.bounds.min.y, kCoordinate, d.bounds.min.y);
    visit("bounds.min.z", s.bounds.min.z, kCoordinate, d.bounds.min.z);
    visit("bounds.max.x", s.bounds.max.x, kCoordinate, d.bounds.max.x);
    visit("bounds.max.y", s.bounds.max.y, kCoordinate, d.bounds.max.y);
    visit("bounds.max.z", s.bounds.max.z, kCoordinate, d.bounds.max.z);

    visit("islands.count", s.islands.count, kIslandCount, d.islands.count);
    visit("islands.altitude.min", s.islands.altitude.min, kCoordinate, d.islands.altitude.min);
    visit("islands.altitude.max", s.islands.altitude.max, kCoordinate, d.islands.altitude.max);
    visit("islands.radius.min", s.islands.radius.min, kIslandRadius, d.islands.radius.min);
    visit("islands.radius.max", s.islands.radius.max, kIslandRadius, d.islands.radius.max);
    visit("islands.minHorizontalSpacing", s.islands.minHorizontalSpacing, kIslandSpacing,
          d.islands.minHorizontalSpacing);
    visit("islands.minVerticalSpacing", s.islands.minVerticalSpacing, kIslandSpacing,
          d.islands.minVerticalSpacing);

    visit("shape.ellipticity", s.shape.ellipticity, kEllipticity, d.shape.ellipticity);
    visit("shape.outlineRoughness", s.shape.outlineRoughness, kOutlineRoughness,
          d.shape.outlineRoughness);
    visit("shape.outlineWarp", s.shape.outlineWarp, kOutlineWarp, d.shape.outlineWarp);

    visit("elevation.maxHeightRatio", s.elevation.maxHeightRatio, kMaxHeightRatio,
          d.elevation.maxHeightRatio);
    visit("elevation.ridgeWeight", s.elevation.ridgeWeight, kRidgeWeight, d.elevation.ridgeWeight);
    visit("elevation.plateauChance", s.elevation.plateauChance, kPlateauChance,
          d.elevation.plateauChance);
    visit("elevation.terraceSteps", s.elevation.terraceSteps, kTerraceSteps,
          d.elevation.terraceSteps);

    visit("underside.depthRatio.min", s.underside.depthRatio.min, kDepthRatio,
          d.underside.depthRatio.min);
    visit("underside.depthRatio.max", s.underside.depthRatio.max, kDepthRatio,
          d.underside.depthRatio.max);
    visit("underside.ledgeCount", s.underside.ledgeCount, kLedgeCount, d.underside.ledgeCount);
    visit("underside.protrusion", s.underside.protrusion, kProtrusion, d.underside.protrusion);
    visit("underside.roughness", s.underside.roughness, kUndersideRoughness, d.underside.roughness);

    visit("noise.frequency", s.noise.frequency, kNoiseFrequency, d.noise.frequency);
    visit("noise.fractal.octaves", s.noise.fractal.octaves, kNoiseOctaves, d.noise.fractal.octaves);
    visit("noise.fractal.lacunarity", s.noise.fractal.lacunarity, kNoiseLacunarity,
          d.noise.fractal.lacunarity);
    visit("noise.fractal.persistence", s.noise.fractal.persistence, kNoisePersistence,
          d.noise.fractal.persistence);
    visit("noise.warpStrength", s.noise.warpStrength, kNoiseWarpStrength, d.noise.warpStrength);

    visit("water.lakeChance", s.water.lakeChance, kChance, d.water.lakeChance);
    visit("water.maxLakesPerIsland", s.water.maxLakesPerIsland, kLakesPerIsland,
          d.water.maxLakesPerIsland);
    visit("water.streamChance", s.water.streamChance, kChance, d.water.streamChance);
    visit("water.waterfallChance", s.water.waterfallChance, kChance, d.water.waterfallChance);

    visit("clouds.clusterCount", s.clouds.clusterCount, kCloudClusterCount, d.clouds.clusterCount);
    visit("clouds.altitude.min", s.clouds.altitude.min, kCoordinate, d.clouds.altitude.min);
    visit("clouds.altitude.max", s.clouds.altitude.max, kCoordinate, d.clouds.altitude.max);
    visit("clouds.size.min", s.clouds.size.min, kCloudSize, d.clouds.size.min);
    visit("clouds.size.max", s.clouds.size.max, kCloudSize, d.clouds.size.max);
    visit("clouds.density", s.clouds.density, kCloudDensity, d.clouds.density);
    visit("clouds.windDirectionDegrees", s.clouds.windDirectionDegrees, kWindDirectionDegrees,
          d.clouds.windDirectionDegrees);
    visit("clouds.windSpeed", s.clouds.windSpeed, kWindSpeed, d.clouds.windSpeed);
    visit("clouds.islandClearance", s.clouds.islandClearance, kCloudClearance,
          d.clouds.islandClearance);

    visit("mesh.resolution", s.mesh.resolution, kMeshResolution, d.mesh.resolution);
}

template <typename T>
bool inRange(T value, const Limits<T>& range)
{
    // Also false for NaN.
    return value >= range.min && value <= range.max;
}

// Numbers in messages: floats in plain decimal notation (std::format's
// shortest form would write 0.0005 as 5e-04).
template <typename T>
std::string text(T value)
{
    if constexpr (std::is_floating_point_v<T>) {
        return std::format("{:g}", value);
    } else {
        return std::format("{}", value);
    }
}

template <typename T>
T clampValue(T value, const Limits<T>& range, T fallback)
{
    if constexpr (std::is_floating_point_v<T>) {
        if (std::isnan(value)) {
            return fallback;
        }
    }
    return std::clamp(value, range.min, range.max);
}

// The first combination that does not hold, or an empty string.
std::string checkCombinations(const WorldGenSettings& s)
{
    using namespace limits;
    const WorldBounds& bounds = s.bounds;

    struct Axis {
        const char* name;
        float min;
        float max;
    };
    for (const Axis axis :
         {Axis{"x", bounds.min.x, bounds.max.x}, Axis{"y", bounds.min.y, bounds.max.y},
          Axis{"z", bounds.min.z, bounds.max.z}}) {
        if (!(axis.max - axis.min >= kMinWorldExtent)) {
            return std::format(
                "bounds.max.{} ({:g}) must be at least {:g} above bounds.min.{} ({:g})", axis.name,
                axis.max, kMinWorldExtent, axis.name, axis.min);
        }
    }

    const double extentX = static_cast<double>(bounds.max.x) - static_cast<double>(bounds.min.x);
    const double extentY = static_cast<double>(bounds.max.y) - static_cast<double>(bounds.min.y);
    const double extentZ = static_cast<double>(bounds.max.z) - static_cast<double>(bounds.min.z);
    const double diagonal = std::sqrt(extentX * extentX + extentY * extentY + extentZ * extentZ);
    if (diagonal > static_cast<double>(kMaxWorldDiagonal)) {
        return std::format(
            "bounds span a diagonal of {:.1f} m, expected at most {:g} (the camera's "
            "far plane)",
            diagonal, kMaxWorldDiagonal);
    }

    struct OrderedRange {
        const char* path;
        const FloatRange& range;
    };
    for (const OrderedRange& ordered :
         {OrderedRange{"islands.altitude", s.islands.altitude},
          OrderedRange{"islands.radius", s.islands.radius},
          OrderedRange{"underside.depthRatio", s.underside.depthRatio},
          OrderedRange{"clouds.altitude", s.clouds.altitude},
          OrderedRange{"clouds.size", s.clouds.size}}) {
        if (ordered.range.min > ordered.range.max) {
            return std::format("{0}.min ({1:g}) is above {0}.max ({2:g})", ordered.path,
                               ordered.range.min, ordered.range.max);
        }
    }

    const double width = std::min(extentX, extentZ);
    const double largestRadius = static_cast<double>(s.islands.radius.max);
    if (2.0 * largestRadius > width) {
        return std::format(
            "islands.radius.max is {:g}, so an island needs {:g} m, but the bounds are "
            "only {:g} m wide",
            s.islands.radius.max, 2.0 * largestRadius, width);
    }

    const double highestTop = static_cast<double>(s.islands.altitude.max) +
                              static_cast<double>(s.elevation.maxHeightRatio) * largestRadius;
    if (highestTop > static_cast<double>(bounds.max.y)) {
        return std::format("islands.altitude.max is {:g}, so island tops reach {:g}, above "
                           "bounds.max.y ({:g})",
                           s.islands.altitude.max, highestTop, bounds.max.y);
    }
    const double lowestUnderside = static_cast<double>(s.islands.altitude.min) -
                                   static_cast<double>(s.underside.depthRatio.max) * largestRadius;
    if (lowestUnderside < static_cast<double>(bounds.min.y)) {
        return std::format("islands.altitude.min is {:g}, so island undersides reach {:g}, below "
                           "bounds.min.y ({:g})",
                           s.islands.altitude.min, lowestUnderside, bounds.min.y);
    }

    if (static_cast<double>(s.islands.minHorizontalSpacing) > width) {
        return std::format("islands.minHorizontalSpacing is {:g}, larger than the world ({:g} m "
                           "across)",
                           s.islands.minHorizontalSpacing, width);
    }
    if (static_cast<double>(s.islands.minVerticalSpacing) > extentY) {
        return std::format(
            "islands.minVerticalSpacing is {:g}, larger than the world ({:g} m high)",
            s.islands.minVerticalSpacing, extentY);
    }

    if (s.clouds.altitude.min < bounds.min.y) {
        return std::format("clouds.altitude.min is {:g}, below bounds.min.y ({:g})",
                           s.clouds.altitude.min, bounds.min.y);
    }
    if (s.clouds.altitude.max > bounds.max.y) {
        return std::format("clouds.altitude.max is {:g}, above bounds.max.y ({:g})",
                           s.clouds.altitude.max, bounds.max.y);
    }
    return {};
}

} // namespace

std::optional<core::Error> validate(const WorldGenSettings& settings)
{
    if (settings.generatorVersion != kGeneratorVersion) {
        return core::Error{
            std::format("generatorVersion is {}, but this build generates version {}",
                        settings.generatorVersion, kGeneratorVersion)};
    }

    const WorldGenSettings defaults{};
    std::string problem;
    forEachNumericField(
        settings, defaults, [&problem](std::string_view path, auto value, const auto& range, auto) {
            if (problem.empty() && !inRange(value, range)) {
                problem = std::format("{} is {}, outside its range {} to {}", path, text(value),
                                      text(range.min), text(range.max));
            }
        });
    if (problem.empty()) {
        problem = checkCombinations(settings);
    }
    if (problem.empty()) {
        return std::nullopt;
    }
    return core::Error{std::move(problem)};
}

WorldGenSettings clampToLimits(WorldGenSettings settings)
{
    const WorldGenSettings defaults{};
    forEachNumericField(settings, defaults,
                        [](std::string_view, auto& value, const auto& range, auto fallback) {
                            value = clampValue(value, range, fallback);
                        });
    return settings;
}

} // namespace engine::worldgen
