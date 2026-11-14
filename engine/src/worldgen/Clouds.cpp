#include "engine/worldgen/Clouds.hpp"

#include "FloatingPointRules.hpp"
#include "SubKeys.hpp"

#include "engine/geometry/FaceNormal.hpp"
#include "engine/worldgen/Random.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <map>
#include <utility>
#include <vector>

namespace engine::worldgen {

namespace {

using detail::subKey;

// Constants of ADR 0017. Changing any of them changes worlds.
constexpr int kPlacementAttempts = 100;
constexpr int kOffsetAttempts = 16;
constexpr float kDensityVariation = 0.25F; // a cluster's density differs by up to this
constexpr int kPuffsPerDensity = 8;        // 1 + round(density * 8) puffs
constexpr float kMinCorePuff = 0.45F;      // the first, central puff, times the radius
constexpr float kMaxCorePuff = 0.55F;
constexpr float kMinPuff = 0.25F; // the others, times the radius
constexpr float kMaxPuff = 0.45F;
constexpr double kPuffRise = 0.3;     // puff centres above the base, times their radius
constexpr double kPuffFlatten = 0.75; // a puff's height over its width
constexpr double kPuffMargin = 1.15;  // a puff's centre keeps this many radii from the edge
constexpr double kAcross = 0.55;      // a cluster's width across its long axis, share of its length
constexpr float kMinJitter = 0.88F;   // vertex distance from the puff centre, times its radius
constexpr float kMaxJitter = 1.12F;
constexpr double kPi = 3.14159265358979323846;

// Display-space colours by the facing of a face: white where it faces up, a
// light lavender on the sides, a cooler lavender blue where it faces down.
const math::Vec4 kCloudBelow{0.76F, 0.78F, 0.92F, 1.0F};
const math::Vec4 kCloudAbove{1.0F, 1.0F, 1.0F, 1.0F};

using Point = std::array<double, 3>;
using Face = std::array<std::size_t, 3>;

Point normalized(const Point& p)
{
    const double length = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
    return Point{p[0] / length, p[1] / length, p[2] / length};
}

// The unit sphere as an icosahedron subdivided once: 42 vertices, 80 faces,
// counter-clockwise seen from outside. Each edge of the icosahedron gets its
// midpoint, pushed out onto the sphere, the first time a face uses it.
struct Icosphere {
    std::vector<Point> vertices;
    std::vector<Face> faces;
};

Icosphere buildIcosphere()
{
    const double phi = (1.0 + std::sqrt(5.0)) / 2.0;
    const std::array<Point, 12> corners{{{-1.0, phi, 0.0},
                                         {1.0, phi, 0.0},
                                         {-1.0, -phi, 0.0},
                                         {1.0, -phi, 0.0},
                                         {0.0, -1.0, phi},
                                         {0.0, 1.0, phi},
                                         {0.0, -1.0, -phi},
                                         {0.0, 1.0, -phi},
                                         {phi, 0.0, -1.0},
                                         {phi, 0.0, 1.0},
                                         {-phi, 0.0, -1.0},
                                         {-phi, 0.0, 1.0}}};
    constexpr std::array<Face, 20> faces{
        {{0, 11, 5},  {0, 5, 1},  {0, 1, 7},  {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4},
         {11, 10, 2}, {10, 7, 6}, {7, 1, 8},  {3, 9, 4},  {3, 4, 2},   {3, 2, 6}, {3, 6, 8},
         {3, 8, 9},   {4, 9, 5},  {2, 4, 11}, {6, 2, 10}, {8, 6, 7},   {9, 8, 1}}};
    Icosphere sphere;
    for (const Point& corner : corners) {
        sphere.vertices.push_back(normalized(corner));
    }
    std::map<std::pair<std::size_t, std::size_t>, std::size_t> middles;
    const auto middle = [&](std::size_t a, std::size_t b) {
        const std::pair<std::size_t, std::size_t> key{std::min(a, b), std::max(a, b)};
        const auto found = middles.find(key);
        if (found != middles.end()) {
            return found->second;
        }
        const Point& p = sphere.vertices[a];
        const Point& q = sphere.vertices[b];
        sphere.vertices.push_back(normalized(Point{p[0] + q[0], p[1] + q[1], p[2] + q[2]}));
        const std::size_t index = sphere.vertices.size() - 1U;
        middles.emplace(key, index);
        return index;
    };
    for (const Face& face : faces) {
        const std::size_t ab = middle(face[0], face[1]);
        const std::size_t bc = middle(face[1], face[2]);
        const std::size_t ca = middle(face[2], face[0]);
        sphere.faces.push_back(Face{face[0], ab, ca});
        sphere.faces.push_back(Face{face[1], bc, ab});
        sphere.faces.push_back(Face{face[2], ca, bc});
        sphere.faces.push_back(Face{ab, bc, ca});
    }
    return sphere;
}

const Icosphere& unitIcosphere()
{
    static const Icosphere sphere = buildIcosphere();
    return sphere;
}

// Appends a triangle with its own vertices, the face normal and a colour from
// its facing. Triangles smaller than kMinCloudTriangleArea are left out.
void addTriangle(geometry::MeshData& mesh, const math::Vec3& a, const math::Vec3& b,
                 const math::Vec3& c)
{
    const double abX = static_cast<double>(b.x) - static_cast<double>(a.x);
    const double abY = static_cast<double>(b.y) - static_cast<double>(a.y);
    const double abZ = static_cast<double>(b.z) - static_cast<double>(a.z);
    const double acX = static_cast<double>(c.x) - static_cast<double>(a.x);
    const double acY = static_cast<double>(c.y) - static_cast<double>(a.y);
    const double acZ = static_cast<double>(c.z) - static_cast<double>(a.z);
    const double nX = abY * acZ - abZ * acY;
    const double nY = abZ * acX - abX * acZ;
    const double nZ = abX * acY - abY * acX;
    if (!(0.5 * std::sqrt(nX * nX + nY * nY + nZ * nZ) >= kMinCloudTriangleArea)) {
        return;
    }
    const std::array<math::Vec3, 3> corners{a, b, c};
    const auto normal = geometry::faceNormal(corners);
    if (!normal) {
        return;
    }
    const float t = 0.5F + 0.5F * normal->y;
    const math::Vec4 color = kCloudBelow + (kCloudAbove - kCloudBelow) * t;
    const auto first = static_cast<std::uint32_t>(mesh.positions.size());
    for (const math::Vec3& corner : corners) {
        mesh.positions.push_back(corner);
        mesh.normals.push_back(*normal);
        mesh.colors.push_back(color);
    }
    mesh.indices.push_back(first);
    mesh.indices.push_back(first + 1U);
    mesh.indices.push_back(first + 2U);
}

// A puff's sphere, flattened to kPuffFlatten of its width, with each vertex at
// its own distance (radius x jitter) from the centre; everything below the
// cluster's base is moved up onto it.
void addPuff(geometry::MeshData& mesh, const CloudPuff& puff, const std::vector<float>& jitter)
{
    const Icosphere& sphere = unitIcosphere();
    std::vector<math::Vec3> corners;
    corners.reserve(sphere.vertices.size());
    for (std::size_t i = 0; i < sphere.vertices.size(); ++i) {
        const Point& direction = sphere.vertices[i];
        const double distance = static_cast<double>(puff.radius) * static_cast<double>(jitter[i]);
        const auto x =
            static_cast<float>(static_cast<double>(puff.centre.x) + direction[0] * distance);
        const auto y = static_cast<float>(static_cast<double>(puff.centre.y) +
                                          direction[1] * distance * kPuffFlatten);
        const auto z =
            static_cast<float>(static_cast<double>(puff.centre.z) + direction[2] * distance);
        corners.push_back(math::Vec3{x, y < 0.0F ? 0.0F : y, z});
    }
    for (const Face& face : sphere.faces) {
        addTriangle(mesh, corners[face[0]], corners[face[1]], corners[face[2]]);
    }
}

// Whether a cluster volume keeps `clearance` from an island volume: apart
// horizontally, or apart vertically.
bool keepsClearance(const math::Vec3& anchor, float radius, float height,
                    const PlacedIsland& island, float clearance)
{
    const double dx = static_cast<double>(anchor.x) - static_cast<double>(island.anchor.x);
    const double dz = static_cast<double>(anchor.z) - static_cast<double>(island.anchor.z);
    const double reach = static_cast<double>(radius) + static_cast<double>(island.radius) +
                         static_cast<double>(clearance);
    if (dx * dx + dz * dz >= reach * reach) {
        return true;
    }
    const double cloudLow = static_cast<double>(anchor.y);
    const double cloudHigh = static_cast<double>(anchor.y) + static_cast<double>(height);
    const double islandLow =
        static_cast<double>(island.anchor.y) - static_cast<double>(island.undersideDepth);
    const double islandHigh =
        static_cast<double>(island.anchor.y) + static_cast<double>(island.topHeight);
    return std::max(islandLow - cloudHigh, cloudLow - islandHigh) >= static_cast<double>(clearance);
}

// The puffs and the mesh of a placed cluster, from the first key of its seed.
void shapeCluster(CloudCluster& cluster, std::uint64_t seed, float density)
{
    Pcg32 random{subKey(seed, 0U)};
    cluster.density = std::clamp(
        random.nextFloat(density - kDensityVariation, density + kDensityVariation), 0.0F, 1.0F);
    const int puffs = 1 + static_cast<int>(std::floor(static_cast<double>(cluster.density) *
                                                          static_cast<double>(kPuffsPerDensity) +
                                                      0.5));
    // The direction of the cluster's long axis: a uniform direction without
    // trigonometry (the first point of the ring between radius 1/4 and 1), +X
    // if none of the attempts lands in it.
    double axisX = 1.0;
    double axisZ = 0.0;
    for (int attempt = 0; attempt < kOffsetAttempts; ++attempt) {
        const auto a = static_cast<double>(random.nextFloat(-1.0F, 1.0F));
        const auto b = static_cast<double>(random.nextFloat(-1.0F, 1.0F));
        const double lengthSquared = a * a + b * b;
        if (lengthSquared >= 0.0625 && lengthSquared <= 1.0) {
            const double length = std::sqrt(lengthSquared);
            axisX = a / length;
            axisZ = b / length;
            break;
        }
    }
    const std::size_t vertices = unitIcosphere().vertices.size();
    for (int p = 0; p < puffs; ++p) {
        CloudPuff puff;
        if (p == 0) {
            puff.radius = cluster.radius * random.nextFloat(kMinCorePuff, kMaxCorePuff);
            puff.centre = math::Vec3{
                0.0F, static_cast<float>(kPuffRise * static_cast<double>(puff.radius)), 0.0F};
        } else {
            puff.radius = cluster.radius * random.nextFloat(kMinPuff, kMaxPuff);
            // A uniform point of the unit disc without trigonometry; the
            // centre if none of the attempts lands in it.
            double u = 0.0;
            double v = 0.0;
            for (int attempt = 0; attempt < kOffsetAttempts; ++attempt) {
                const auto a = static_cast<double>(random.nextFloat(-1.0F, 1.0F));
                const auto b = static_cast<double>(random.nextFloat(-1.0F, 1.0F));
                if (a * a + b * b <= 1.0) {
                    u = a;
                    v = b;
                    break;
                }
            }
            const double reach = static_cast<double>(cluster.radius) -
                                 kPuffMargin * static_cast<double>(puff.radius);
            // Inside an ellipse along the long axis, kAcross as wide.
            const double along = u * reach;
            const double across = v * reach * kAcross;
            puff.centre =
                math::Vec3{static_cast<float>(axisX * along - axisZ * across),
                           static_cast<float>(kPuffRise * static_cast<double>(puff.radius)),
                           static_cast<float>(axisZ * along + axisX * across)};
        }
        std::vector<float> jitter(vertices);
        for (float& value : jitter) {
            value = random.nextFloat(kMinJitter, kMaxJitter);
        }
        addPuff(cluster.mesh, puff, jitter);
        cluster.puffs.push_back(puff);
    }
}

} // namespace

std::uint64_t CloudField::triangleCount() const noexcept
{
    std::uint64_t count = 0;
    for (const CloudCluster& cluster : clusters) {
        count += cluster.mesh.triangleCount();
    }
    return count;
}

CloudField generateClouds(const WorldGenSettings& settings, std::span<const PlacedIsland> islands)
{
    CloudField field;
    const CloudSettings& clouds = settings.clouds;
    const WorldBounds& bounds = settings.bounds;
    field.requestedCount = static_cast<std::size_t>(std::max(clouds.clusterCount, 0));
    for (std::size_t k = 0; k < field.requestedCount; ++k) {
        const std::uint64_t seed =
            deriveSeed(settings.seed, RandomStream::Clouds, static_cast<std::uint64_t>(k),
                       settings.generatorVersion);
        Pcg32 random{seed};
        CloudCluster cluster;
        bool placed = false;
        for (int attempt = 0; attempt < kPlacementAttempts && !placed; ++attempt) {
            // Always four draws, so later attempts do not depend on the checks.
            const float radius = random.nextFloat(clouds.size.min, clouds.size.max);
            const float x = random.nextFloat(bounds.min.x + radius, bounds.max.x - radius);
            const float z = random.nextFloat(bounds.min.z + radius, bounds.max.z - radius);
            const float y = random.nextFloat(clouds.altitude.min, clouds.altitude.max);
            const float height = kCloudHeightRatio * radius;
            const bool fits = bounds.max.x - bounds.min.x >= 2.0F * radius &&
                              bounds.max.z - bounds.min.z >= 2.0F * radius &&
                              y + height <= bounds.max.y;
            if (!fits) {
                continue;
            }
            const math::Vec3 anchor{x, y, z};
            const bool clear =
                std::all_of(islands.begin(), islands.end(), [&](const PlacedIsland& island) {
                    return keepsClearance(anchor, radius, height, island, clouds.islandClearance);
                });
            if (!clear) {
                continue;
            }
            cluster.index = static_cast<std::uint64_t>(k);
            cluster.anchor = anchor;
            cluster.radius = radius;
            cluster.height = height;
            placed = true;
        }
        if (!placed) {
            continue;
        }
        shapeCluster(cluster, seed, clouds.density);
        field.clusters.push_back(std::move(cluster));
    }
    if (field.clusters.size() < field.requestedCount) {
        field.warning = std::format(
            "placed {} of {} cloud clusters; the others found no room inside the bounds "
            "{:g} m away from the islands",
            field.clusters.size(), field.requestedCount, clouds.islandClearance);
    }
    return field;
}

CloudDrift cloudDrift(const CloudCluster& cluster, const WorldGenSettings& settings, double seconds)
{
    const CloudSettings& clouds = settings.clouds;
    const WorldBounds& bounds = settings.bounds;
    const double angle = static_cast<double>(clouds.windDirectionDegrees) * kPi / 180.0;
    const double speed = static_cast<double>(clouds.windSpeed);
    // Position along one axis, wrapped into [low, high), and its distance from
    // the nearer edge (the seam where it wraps).
    struct Wrapped {
        double position;
        double seam;
    };
    const auto wrap = [&](float start, float low, float high, double velocity) {
        const double extent = static_cast<double>(high) - static_cast<double>(low);
        double offset = std::fmod(
            static_cast<double>(start) - static_cast<double>(low) + velocity * seconds, extent);
        if (offset < 0.0) {
            offset += extent;
        }
        if (offset >= extent) {
            offset -= extent;
        }
        return Wrapped{static_cast<double>(low) + offset, std::min(offset, extent - offset)};
    };
    const Wrapped x = wrap(cluster.anchor.x, bounds.min.x, bounds.max.x, std::cos(angle) * speed);
    const Wrapped z = wrap(cluster.anchor.z, bounds.min.z, bounds.max.z, std::sin(angle) * speed);
    const double t =
        std::clamp(std::min(x.seam, z.seam) / static_cast<double>(cluster.radius), 0.0, 1.0);
    CloudDrift drift;
    drift.position = math::Vec3{static_cast<float>(x.position), cluster.anchor.y,
                                static_cast<float>(z.position)};
    drift.scale = static_cast<float>(t * t * (3.0 - 2.0 * t));
    return drift;
}

} // namespace engine::worldgen
