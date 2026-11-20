#pragma once

// Stylized sky of the floating world (M3.8; moved from island-preview in
// M5.3): a dome around the camera whose vertex colours follow a vertical
// gradient given as data, drawn with the existing renderer before the scene
// (no depth test, no depth writes, camera rotation only: skyView). There is no
// ground and no sea: below the horizon the sky turns into a hazy abyss.

#include "engine/core/Math.hpp"
#include "engine/geometry/MeshData.hpp"

#include <vector>

namespace engine::world {

// A colour at an elevation: the sine of the angle above the horizon, -1
// straight down, 0 at the horizon, 1 straight up.
struct SkyStop {
    float elevation = 0.0F;
    math::Vec3 color{0.0F};
};

// Stops in ascending elevation; colours between them are interpolated linearly
// in elevation, and beyond the first and last stop they stay constant.
struct SkyGradient {
    std::vector<SkyStop> stops;
};

// Pastel blue above, a warm horizon band, a lavender haze and a deep blue abyss
// below (display-space colours, ADR 0009).
[[nodiscard]] SkyGradient floatingWorldSky();

[[nodiscard]] math::Vec3 skyColor(const SkyGradient& gradient, float elevation);

// A unit sphere around the origin with `bands` latitude bands (even, so the
// horizon is a ring of vertices) and `segments` around: shared vertices with
// the gradient's colours, no normals, every triangle facing the centre.
[[nodiscard]] geometry::MeshData buildSkyDome(const SkyGradient& gradient, int bands = 16,
                                              int segments = 32);

// The view to draw the dome with: the camera's rotation without its position,
// so the sky stays at infinity.
[[nodiscard]] math::Mat4 skyView(const math::Mat4& view);

} // namespace engine::world
