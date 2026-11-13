#pragma once

// A closed, flat-shaded mesh per island (ADR 0014, Amendment 1).
//
// The top surface is a jittered square grid with alternating diagonals, mapped
// onto the island's outline: the grid's border becomes the rim. The underside
// is a set of rings of constant normalized distance through the same rim
// directions, closed by a fan to the tip under the anchor. Top and underside
// share the rim vertices exactly, so together they enclose the island without
// gaps. Positions are island-local (the anchor at the origin, +Y up); the
// caller places the meshes at the island's anchor.

#include "engine/geometry/MeshData.hpp"
#include "engine/worldgen/IslandShape.hpp"

#include <cstdint>

namespace engine::worldgen {

// Every triangle of an island mesh has at least this area, in square metres.
inline constexpr double kMinIslandTriangleArea = 1.0e-7;

// meshHash() rounds every coordinate to a multiple of this step (metres).
inline constexpr double kMeshHashStep = 1.0 / 1024.0;

struct IslandMesh {
    // Faces up (counter-clockwise seen from above); its border is the rim.
    geometry::MeshData top;
    // Faces down (counter-clockwise seen from below); its outer ring is the rim.
    geometry::MeshData underside;
};

// Builds both parts with `resolution` grid cells across the island
// (mesh.resolution, clamped to limits::kMeshResolution). For n cells the top
// has 2 n^2 triangles and the underside 4 n (2 m - 1) with m = ceil(n / 2)
// rings, so an island has at most 6 n^2 triangles. Every triangle has its own
// three vertices with the face normal (flat shading) and white vertex colors,
// ready for palette colors; there are no UVs.
[[nodiscard]] IslandMesh buildIslandMesh(const IslandShape& shape, int resolution);

// Hash of a mesh for repeatability tests: the vertex and index counts, every
// position coordinate rounded to a multiple of kMeshHashStep (a non-finite
// coordinate counts as one fixed value), and every index, folded with mix64 in
// order. Rounding makes the hash insensitive to differences far below a
// millimetre, so it can be compared across platforms (ADR 0003).
[[nodiscard]] std::uint64_t meshHash(const geometry::MeshData& mesh);

} // namespace engine::worldgen
