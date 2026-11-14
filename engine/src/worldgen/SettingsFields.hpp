#pragma once

// The numeric fields of WorldGenSettings with their paths, limits and defaults
// in the order of declaration (ADR 0013). One list for validation, clamping and
// the settings file (ADR 0004 Amendment 1), so a new field cannot be missed by
// one of them. The paths are the member paths and the JSON paths.

#include "engine/worldgen/WorldGenSettings.hpp"

namespace engine::worldgen::detail {

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

} // namespace engine::worldgen::detail
