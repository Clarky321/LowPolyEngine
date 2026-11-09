#pragma once

#include "engine/core/Math.hpp"
#include "engine/geometry/MeshData.hpp"

#include <array>

namespace engine::geometry {

// Simple flat-shaded shapes for test scenes and debugging. Every face has its
// own vertices and one color, and triangles wind counter-clockwise seen from
// outside (MeshData convention). Invalid sizes (zero, negative or non-finite)
// give an empty mesh.

// Color order of makeBox faces.
enum class BoxFace {
    PositiveX,
    NegativeX,
    PositiveY,
    NegativeY,
    PositiveZ,
    NegativeZ,
};

// Axis-aligned box centered on the origin: 6 faces, 24 vertices, 12 triangles.
[[nodiscard]] MeshData makeBox(const math::Vec3& halfExtents,
                               const std::array<math::Vec4, 6>& faceColors);

// Square pyramid: base on y = 0 centered on the origin, apex at (0, height, 0).
// Color order: sides facing +X, -X, +Z, -Z, then the base.
[[nodiscard]] MeshData makePyramid(float halfBase, float height,
                                   const std::array<math::Vec4, 5>& faceColors);

// Upper limit for makeCheckerPlane, to keep a typo from allocating gigabytes.
inline constexpr int kMaxCheckerCells = 1024;

// Flat grid on y = 0, centered on the origin and facing +Y, with cells in
// alternating colors (colorA at the -X/-Z corner). cellsPerSide in 1..kMaxCheckerCells.
[[nodiscard]] MeshData makeCheckerPlane(int cellsPerSide, float cellSize, const math::Vec4& colorA,
                                        const math::Vec4& colorB);

} // namespace engine::geometry
