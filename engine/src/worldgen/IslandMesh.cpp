#include "engine/worldgen/IslandMesh.hpp"

#include "FloatingPointRules.hpp"
#include "SubKeys.hpp"

#include "engine/geometry/FaceNormal.hpp"
#include "engine/worldgen/Random.hpp"
#include "engine/worldgen/WorldGenSettings.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace engine::worldgen {

namespace {

// Interior grid vertices move by up to this share of a cell along each axis.
constexpr float kGridJitter = 0.25F;
// The jitter draws from this key of the island's shape seed (ADR 0014).
constexpr std::uint64_t kJitterKeyIndex = 6U;

const math::Vec4 kWhite{1.0F, 1.0F, 1.0F, 1.0F};

// Appends a triangle with its own three vertices and the face normal. A
// degenerate face (never produced by the construction below) gets a vertical
// normal so the mesh stays valid.
void addTriangle(geometry::MeshData& mesh, const math::Vec3& a, const math::Vec3& b,
                 const math::Vec3& c, float fallbackNormalY)
{
    const std::array<math::Vec3, 3> corners{a, b, c};
    const math::Vec3 normal =
        geometry::faceNormal(corners).value_or(math::Vec3{0.0F, fallbackNormalY, 0.0F});
    const auto first = static_cast<std::uint32_t>(mesh.positions.size());
    for (const math::Vec3& corner : corners) {
        mesh.positions.push_back(corner);
        mesh.normals.push_back(normal);
        mesh.colors.push_back(kWhite);
    }
    mesh.indices.push_back(first);
    mesh.indices.push_back(first + 1U);
    mesh.indices.push_back(first + 2U);
}

// True if the triangle a, b, c is counter-clockwise seen from above (+Y), i.e.
// its projection onto the XZ plane has positive area.
bool facesUp(const math::Vec3& a, const math::Vec3& b, const math::Vec3& c)
{
    const double abX = static_cast<double>(b.x) - static_cast<double>(a.x);
    const double abZ = static_cast<double>(b.z) - static_cast<double>(a.z);
    const double acX = static_cast<double>(c.x) - static_cast<double>(a.x);
    const double acZ = static_cast<double>(c.z) - static_cast<double>(a.z);
    return abZ * acX - abX * acZ > 0.0;
}

// Island-local XZ position of a point (u, v) of the square [-1, 1]^2: the
// square is mapped onto the island by radial normalization. The point keeps
// its direction, and its normalized distance s is max(|u|, |v|), so each
// concentric square of the grid becomes a ring of constant s and the border
// (s = 1) becomes the rim. Unlike smooth square-to-disk maps, cells near the
// corners keep their full radial size. No trigonometry is needed.
math::Vec2 localPosition(const IslandShape& shape, double u, double v, bool onRim)
{
    const double length = std::sqrt(u * u + v * v);
    if (length == 0.0) {
        return math::Vec2{0.0F, 0.0F};
    }
    const double dirX = u / length;
    const double dirZ = v / length;
    const double s = onRim ? 1.0 : std::min(std::max(std::abs(u), std::abs(v)), 1.0);
    const auto radius = static_cast<double>(
        outlineRadius(shape, math::Vec2{static_cast<float>(dirX), static_cast<float>(dirZ)}));
    return math::Vec2{static_cast<float>(dirX * s * radius), static_cast<float>(dirZ * s * radius)};
}

} // namespace

IslandMesh buildIslandMesh(const IslandShape& shape, int resolution)
{
    const int n = std::clamp(resolution, limits::kMeshResolution.min, limits::kMeshResolution.max);
    const auto size = static_cast<std::size_t>(n) + 1U;
    const double cell = 2.0 / static_cast<double>(n);
    Pcg32 jitter{detail::subKey(shape.island.shapeSeed, kJitterKeyIndex)};

    // Top grid, row by row: i along x (u), j along z (v). Border vertices sit
    // on the rim at height 0; interior vertices are jittered and lifted.
    std::vector<math::Vec3> grid(size * size);
    const auto vertex = [&grid, size](int i, int j) -> math::Vec3& {
        return grid[static_cast<std::size_t>(j) * size + static_cast<std::size_t>(i)];
    };
    for (int j = 0; j <= n; ++j) {
        for (int i = 0; i <= n; ++i) {
            const bool onRim = i == 0 || j == 0 || i == n || j == n;
            double u = i == n ? 1.0 : -1.0 + cell * static_cast<double>(i);
            double v = j == n ? 1.0 : -1.0 + cell * static_cast<double>(j);
            if (!onRim) {
                u += cell * static_cast<double>(jitter.nextFloat(-kGridJitter, kGridJitter));
                v += cell * static_cast<double>(jitter.nextFloat(-kGridJitter, kGridJitter));
            }
            const math::Vec2 local = localPosition(shape, u, v, onRim);
            const float height = onRim ? 0.0F : topHeightAt(shape, local);
            vertex(i, j) = math::Vec3{local.x, height, local.y};
        }
    }

    IslandMesh mesh;
    const auto topTriangles = 2U * static_cast<std::size_t>(n) * static_cast<std::size_t>(n);
    mesh.top.positions.reserve(3U * topTriangles);
    mesh.top.normals.reserve(3U * topTriangles);
    mesh.top.colors.reserve(3U * topTriangles);
    mesh.top.indices.reserve(3U * topTriangles);
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const math::Vec3& p00 = vertex(i, j);
            const math::Vec3& p10 = vertex(i + 1, j);
            const math::Vec3& p01 = vertex(i, j + 1);
            const math::Vec3& p11 = vertex(i + 1, j + 1);
            // Alternating diagonals; a corner cell always splits through its
            // corner on the rim, whose two rim edges are nearly collinear.
            bool throughP00 = (i + j) % 2 == 0;
            if ((i == 0 && j == 0) || (i == n - 1 && j == n - 1)) {
                throughP00 = true;
            } else if ((i == n - 1 && j == 0) || (i == 0 && j == n - 1)) {
                throughP00 = false;
            }
            // The mapping bends along the square's diagonals, so a jittered cell
            // there can turn into a non-convex quad; only the diagonal through
            // its reflex corner keeps both triangles facing up.
            const bool p00Works = facesUp(p00, p01, p11) && facesUp(p00, p11, p10);
            const bool p10Works = facesUp(p00, p01, p10) && facesUp(p10, p01, p11);
            if (throughP00 && !p00Works && p10Works) {
                throughP00 = false;
            } else if (!throughP00 && !p10Works && p00Works) {
                throughP00 = true;
            }
            if (throughP00) {
                addTriangle(mesh.top, p00, p01, p11, 1.0F);
                addTriangle(mesh.top, p00, p11, p10, 1.0F);
            } else {
                addTriangle(mesh.top, p00, p01, p10, 1.0F);
                addTriangle(mesh.top, p10, p01, p11, 1.0F);
            }
        }
    }

    // The rim: the grid border in order of increasing angle in the XZ plane.
    std::vector<math::Vec3> rim;
    rim.reserve(4U * static_cast<std::size_t>(n));
    for (int j = 0; j < n; ++j) {
        rim.push_back(vertex(n, j));
    }
    for (int i = n; i > 0; --i) {
        rim.push_back(vertex(i, n));
    }
    for (int j = n; j > 0; --j) {
        rim.push_back(vertex(0, j));
    }
    for (int i = 0; i < n; ++i) {
        rim.push_back(vertex(i, 0));
    }

    // Underside rings at s = 1 - k / m through the rim directions; ring 0 is
    // the rim itself.
    const int rings = (n + 1) / 2;
    const std::size_t sectors = rim.size();
    std::vector<std::vector<math::Vec3>> ring(static_cast<std::size_t>(rings));
    ring[0] = rim;
    for (int k = 1; k < rings; ++k) {
        const double s = 1.0 - static_cast<double>(k) / static_cast<double>(rings);
        std::vector<math::Vec3>& points = ring[static_cast<std::size_t>(k)];
        points.reserve(sectors);
        for (const math::Vec3& edge : rim) {
            const math::Vec2 local{static_cast<float>(static_cast<double>(edge.x) * s),
                                   static_cast<float>(static_cast<double>(edge.z) * s)};
            points.push_back(math::Vec3{local.x, -undersideDepthAt(shape, local), local.y});
        }
    }
    const math::Vec3 tip{0.0F, -undersideDepthAt(shape, math::Vec2{0.0F, 0.0F}), 0.0F};

    const std::size_t undersideTriangles = sectors * (2U * static_cast<std::size_t>(rings) - 1U);
    mesh.underside.positions.reserve(3U * undersideTriangles);
    mesh.underside.normals.reserve(3U * undersideTriangles);
    mesh.underside.colors.reserve(3U * undersideTriangles);
    mesh.underside.indices.reserve(3U * undersideTriangles);
    for (std::size_t k = 0; k + 1U < ring.size(); ++k) {
        const std::vector<math::Vec3>& outer = ring[k];
        const std::vector<math::Vec3>& inner = ring[k + 1U];
        for (std::size_t i = 0; i < sectors; ++i) {
            const std::size_t next = (i + 1U) % sectors;
            const math::Vec3& a = outer[i];
            const math::Vec3& b = outer[next];
            const math::Vec3& c = inner[i];
            const math::Vec3& d = inner[next];
            if ((i + k) % 2U == 0U) {
                addTriangle(mesh.underside, a, b, c, -1.0F);
                addTriangle(mesh.underside, b, d, c, -1.0F);
            } else {
                addTriangle(mesh.underside, a, b, d, -1.0F);
                addTriangle(mesh.underside, a, d, c, -1.0F);
            }
        }
    }
    const std::vector<math::Vec3>& innermost = ring.back();
    for (std::size_t i = 0; i < sectors; ++i) {
        addTriangle(mesh.underside, innermost[i], innermost[(i + 1U) % sectors], tip, -1.0F);
    }
    return mesh;
}

std::uint64_t meshHash(const geometry::MeshData& mesh)
{
    const auto fold = [](std::uint64_t hash, std::uint64_t value) {
        return mix64((hash ^ value) + kSplitMix64Gamma);
    };
    const auto quantize = [](float coordinate) -> std::uint64_t {
        const double steps = static_cast<double>(coordinate) / kMeshHashStep;
        // Beyond 2^62 steps (and for NaN or infinity) the value is not rounded.
        if (!(std::abs(steps) < 0x1p62)) {
            return 0x7FF8000000000000ULL;
        }
        return static_cast<std::uint64_t>(std::llround(steps));
    };
    std::uint64_t hash = fold(0U, mesh.positions.size());
    hash = fold(hash, mesh.indices.size());
    for (const math::Vec3& position : mesh.positions) {
        hash = fold(hash, quantize(position.x));
        hash = fold(hash, quantize(position.y));
        hash = fold(hash, quantize(position.z));
    }
    for (const std::uint32_t index : mesh.indices) {
        hash = fold(hash, index);
    }
    return hash;
}

} // namespace engine::worldgen
