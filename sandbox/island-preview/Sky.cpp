#include "Sky.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <utility>

namespace island_preview {

using engine::math::Vec3;
using engine::math::Vec4;

SkyGradient floatingWorldSky()
{
    return SkyGradient{{
        {-1.0F, Vec3{0.46F, 0.52F, 0.74F}}, // abyss straight down
        {-0.3F, Vec3{0.72F, 0.74F, 0.88F}}, // lavender haze
        {0.0F, Vec3{0.97F, 0.88F, 0.78F}},  // warm horizon
        {0.25F, Vec3{0.72F, 0.84F, 0.93F}}, // light sky
        {1.0F, Vec3{0.38F, 0.58F, 0.84F}},  // zenith
    }};
}

Vec3 skyColor(const SkyGradient& gradient, float elevation)
{
    const auto& stops = gradient.stops;
    if (stops.empty()) {
        return Vec3{0.0F};
    }
    if (!(elevation > stops.front().elevation)) {
        return stops.front().color;
    }
    for (std::size_t i = 1; i < stops.size(); ++i) {
        if (elevation <= stops[i].elevation) {
            const SkyStop& low = stops[i - 1U];
            const SkyStop& high = stops[i];
            const float span = high.elevation - low.elevation;
            const float t = span > 0.0F ? (elevation - low.elevation) / span : 1.0F;
            return low.color + (high.color - low.color) * t;
        }
    }
    return stops.back().color;
}

engine::geometry::MeshData buildSkyDome(const SkyGradient& gradient, int bands, int segments)
{
    bands = std::max(2, bands + bands % 2);
    segments = std::max(3, segments);
    engine::geometry::MeshData mesh;

    // Rows of vertices from the bottom pole to the top pole; the poles are
    // single vertices.
    const auto addVertex = [&](const Vec3& position) {
        mesh.positions.push_back(position);
        mesh.colors.push_back(Vec4{skyColor(gradient, position.y), 1.0F});
    };
    addVertex(Vec3{0.0F, -1.0F, 0.0F});
    for (int row = 1; row < bands; ++row) {
        const double latitude =
            -std::numbers::pi / 2.0 + std::numbers::pi * static_cast<double>(row) / bands;
        // The horizon row lies exactly at elevation 0.
        const double y = row * 2 == bands ? 0.0 : std::sin(latitude);
        const double ring = std::cos(latitude);
        for (int column = 0; column < segments; ++column) {
            const double longitude =
                2.0 * std::numbers::pi * static_cast<double>(column) / segments;
            addVertex(Vec3{static_cast<float>(ring * std::cos(longitude)), static_cast<float>(y),
                           static_cast<float>(ring * std::sin(longitude))});
        }
    }
    addVertex(Vec3{0.0F, 1.0F, 0.0F});

    const auto bottom = std::uint32_t{0};
    const auto top = static_cast<std::uint32_t>(mesh.positions.size() - 1U);
    const auto at = [segments](int row, int column) {
        return static_cast<std::uint32_t>(1 + (row - 1) * segments + column % segments);
    };
    // Every triangle is turned to face the centre, so it is visible from inside
    // with back-face culling.
    const auto addTriangle = [&mesh](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
        const Vec3& pa = mesh.positions[a];
        const Vec3 normal = glm::cross(mesh.positions[b] - pa, mesh.positions[c] - pa);
        const Vec3 centre = (pa + mesh.positions[b] + mesh.positions[c]) / 3.0F;
        if (glm::dot(normal, centre) > 0.0F) {
            std::swap(b, c);
        }
        mesh.indices.insert(mesh.indices.end(), {a, b, c});
    };
    for (int column = 0; column < segments; ++column) {
        addTriangle(bottom, at(1, column), at(1, column + 1));
        addTriangle(top, at(bands - 1, column), at(bands - 1, column + 1));
    }
    for (int row = 1; row + 1 < bands; ++row) {
        for (int column = 0; column < segments; ++column) {
            addTriangle(at(row, column), at(row + 1, column), at(row + 1, column + 1));
            addTriangle(at(row, column), at(row + 1, column + 1), at(row, column + 1));
        }
    }
    return mesh;
}

} // namespace island_preview
