#pragma once

// Low-poly clouds in the air around and between the islands (ADR 0017):
// clusters of rounded, flat-bottomed puffs placed inside the world bounds with
// a clearance from every island volume, from their own random stream
// (RandomStream::Clouds). Their slow drift along the wind is a display effect,
// computed from the initial placement and the time; it is not part of the
// generated data. Pure CPU data; nothing here depends on the renderer.

#include "engine/core/Math.hpp"
#include "engine/geometry/MeshData.hpp"
#include "engine/worldgen/IslandLayout.hpp"
#include "engine/worldgen/WorldGenSettings.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace engine::worldgen {

// A cluster's volume is a vertical cylinder of its radius, from its flat base
// up to this many times its radius.
inline constexpr float kCloudHeightRatio = 0.65F;

// A cluster has 1 + round(8 * density) puffs, at most this many.
inline constexpr int kMaxCloudPuffs = 9;

// Every cloud triangle has at least this area, in square metres.
inline constexpr double kMinCloudTriangleArea = 1.0e-6;

// One rounded shape of a cluster: a low-poly sphere around `centre`
// (cluster-local) with `radius`, three quarters as high as wide, its vertices
// moved in and out by up to 12 %, cut flat at the cluster's base.
struct CloudPuff {
    math::Vec3 centre{0.0F};
    float radius = 0.0F;

    friend bool operator==(const CloudPuff&, const CloudPuff&) = default;
};

struct CloudCluster {
    // The number of the requested cluster (0 to clouds.clusterCount - 1).
    std::uint64_t index = 0;
    // World position of the centre of its flat base when generated (time 0);
    // anchor.y lies within clouds.altitude.
    math::Vec3 anchor{0.0F};
    // Horizontal reach from the anchor, within clouds.size.
    float radius = 0.0F;
    // kCloudHeightRatio * radius: no puff reaches higher above the base.
    float height = 0.0F;
    // clouds.density varied per cluster, in [0, 1]; it sets the puff count.
    float density = 0.0F;
    // The largest first, at the centre.
    std::vector<CloudPuff> puffs;
    // All puffs in one opaque, flat-shaded mesh, cluster-local (the anchor at
    // the origin, the base at y = 0): white where faces look up, lavender
    // where they look down.
    geometry::MeshData mesh;
};

struct CloudField {
    // In order of their index.
    std::vector<CloudCluster> clusters;
    // clouds.clusterCount.
    std::size_t requestedCount = 0;
    // Empty when every requested cluster was placed.
    std::string warning;

    [[nodiscard]] std::uint64_t triangleCount() const noexcept;
};

// Places and shapes the cloud clusters. The settings must pass validate().
// Cluster k draws from Pcg32(deriveSeed(seed, RandomStream::Clouds, k,
// generatorVersion)): up to 100 attempts of radius, x, z and base altitude,
// the first that fits inside the bounds and keeps clouds.islandClearance from
// every island volume of `islands` (horizontally or vertically); its shape
// draws from a key of the same seed. Clusters that do not fit are left out,
// and the warning says how many were placed.
[[nodiscard]] CloudField generateClouds(const WorldGenSettings& settings,
                                        std::span<const PlacedIsland> islands);

// Where a cluster is shown after `seconds`.
struct CloudDrift {
    // Its anchor moved along the wind, wrapped around in X and Z within the
    // world bounds; the height stays.
    math::Vec3 position{0.0F};
    // 1, falling smoothly to 0 within one cluster radius of the bounds' edges
    // in X and Z, where the cloud wraps around: it shrinks away before it
    // reappears on the other side.
    float scale = 1.0F;
};

// The drift of a cluster at windSpeed metres per second towards
// windDirectionDegrees (0 is +X, 90 is +Z). A display effect: only the time
// moves it. At time 0 it is at its anchor with scale 1.
[[nodiscard]] CloudDrift cloudDrift(const CloudCluster& cluster, const WorldGenSettings& settings,
                                    double seconds);

} // namespace engine::worldgen
