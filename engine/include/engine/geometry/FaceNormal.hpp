#pragma once

#include "engine/core/Math.hpp"

#include <optional>
#include <span>

namespace engine::geometry {

// A polygon counts as degenerate when twice its area is at most this fraction
// of its squared size (the largest squared distance from the first corner).
// For a triangle that is an angle of about 1e-10 radians, far below what float
// coordinates (about 1e-7 relative precision) can express: only polygons whose
// corners are numerically on one line or one point are rejected. Relative, so
// tiny and huge polygons are treated alike.
inline constexpr double kDegenerateFaceTolerance = 1.0e-10;

// Unit normal of a flat polygon whose corners run counter-clockwise seen from
// its front, for flat shading. The polygon's area vector (the sum of the cross
// products of its fan triangles) is computed in double, so very small or large
// finite sizes neither underflow nor overflow; for a non-planar polygon it is
// the normal of its best-fitting plane. Empty when the polygon has fewer than 3
// corners, a non-finite coordinate or is degenerate (kDegenerateFaceTolerance).
[[nodiscard]] std::optional<math::Vec3> faceNormal(std::span<const math::Vec3> corners);

} // namespace engine::geometry
