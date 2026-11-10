#pragma once

#include "engine/core/Math.hpp"
#include "engine/geometry/MeshData.hpp"

#include <array>

namespace engine::geometry {

// Simple flat-shaded shapes for test scenes and debugging. Every face has its
// own vertices, one color and one outward unit normal (shared by its
// vertices), and triangles wind counter-clockwise seen from outside (MeshData
// convention). Every vertex also has a UV: seen from outside, U points right
// and V up on every face, so textures are never mirrored, and on vertical
// faces V is world +Y. Invalid sizes (zero, negative or non-finite) give an
// empty mesh.

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
// Normals are the face axes. Each face maps the whole unit square of UV,
// whatever its size (a texture stretches on long faces); the top face has
// U = +X and V = -Z, the bottom face U = +X and V = +Z.
[[nodiscard]] MeshData makeBox(const math::Vec3& halfExtents,
                               const std::array<math::Vec4, 6>& faceColors);

// Square pyramid: base on y = 0 centered on the origin, apex at (0, height, 0).
// Color order: sides facing +X, -X, +Z, -Z, then the base. Each side has its
// slanted face normal and the UVs (0, 0) and (1, 0) along its base edge plus
// (0.5, 1) at the apex. The base faces -Y and maps the unit square with
// U = +X and V = +Z (seen from below).
[[nodiscard]] MeshData makePyramid(float halfBase, float height,
                                   const std::array<math::Vec4, 5>& faceColors);

// Upper limit for makeCheckerPlane, to keep a typo from allocating gigabytes.
inline constexpr int kMaxCheckerCells = 1024;

// Flat grid on y = 0, centered on the origin and facing +Y, with cells in
// alternating colors (colorA at the -X/-Z corner). cellsPerSide in 1..kMaxCheckerCells.
// UVs use one unit per cell, so a texture repeats once per cell: U = +X and
// V = -Z, from (0, 0) at the (-X, +Z) corner to (cellsPerSide, cellsPerSide)
// at the (+X, -Z) corner.
[[nodiscard]] MeshData makeCheckerPlane(int cellsPerSide, float cellSize, const math::Vec4& colorA,
                                        const math::Vec4& colorB);

} // namespace engine::geometry
