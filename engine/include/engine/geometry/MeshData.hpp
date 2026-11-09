#pragma once

#include "engine/core/Math.hpp"
#include "engine/core/Result.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace engine::geometry {

// CPU-side triangle mesh: the hand-over format between geometry producers
// (primitive builders, later world generation) and the renderer, which uploads
// it to the GPU (ADR 0007). It knows nothing about OpenGL or other GPU APIs.
//
// Attributes are stored as separate arrays (one entry per vertex), so further
// attributes can be added without changing existing producers.
struct MeshData {
    std::vector<math::Vec3> positions;
    // RGBA in [0, 1], one per position. Flat-shaded faces use vertices of their
    // own with one color each, not vertices shared with neighbor faces.
    std::vector<math::Vec4> colors;
    // Triangle list. Front faces are counter-clockwise when seen from outside.
    std::vector<std::uint32_t> indices;

    [[nodiscard]] std::size_t vertexCount() const noexcept { return positions.size(); }
    [[nodiscard]] std::size_t triangleCount() const noexcept { return indices.size() / 3; }
    [[nodiscard]] bool empty() const noexcept { return indices.empty(); }
};

// Axis-aligned bounding box.
struct Bounds {
    math::Vec3 min{0.0F};
    math::Vec3 max{0.0F};
};

// Checks the invariants every consumer relies on: one color per position,
// whole triangles, indices inside the vertex range, finite values. An empty
// mesh is valid. The error names the first offending element.
[[nodiscard]] std::optional<core::Error> validate(const MeshData& mesh);

// Bounds of all positions; empty if the mesh has no vertices.
[[nodiscard]] std::optional<Bounds> computeBounds(const MeshData& mesh);

// Appends `source` to `target`, shifting its indices past target's vertices.
void append(MeshData& target, const MeshData& source);

} // namespace engine::geometry
