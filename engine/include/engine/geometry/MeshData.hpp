#pragma once

#include "engine/core/Math.hpp"
#include "engine/core/Result.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace engine::geometry {

// How far a normal's length may differ from 1 and still pass validate().
// Normalizing in float is accurate to about 1e-7, and text formats such as OBJ
// store normals with 4 to 6 decimals (errors up to about 1e-4); both pass.
// A length error of 1e-3 changes diffuse lighting by at most 0.1 %, less than
// one step of an 8-bit color channel (0.4 %), so accepted normals light
// correctly. Zero and unnormalized normals are far outside and are rejected.
inline constexpr float kNormalLengthTolerance = 1.0e-3F;

// CPU-side triangle mesh: the hand-over format between geometry producers
// (primitive builders, later world generation) and the renderer, which uploads
// it to the GPU (ADR 0007). It knows nothing about OpenGL or other GPU APIs.
//
// Attributes are stored as separate arrays (one entry per vertex), so further
// attributes can be added without changing existing producers. Positions and
// colors are required. Normals and UVs are optional: an empty array means the
// attribute is absent, otherwise it holds exactly one entry per position.
// Missing attributes are never filled in automatically.
struct MeshData {
    std::vector<math::Vec3> positions;
    // RGBA in [0, 1], one per position. Flat-shaded faces use vertices of their
    // own with one color each, not vertices shared with neighbor faces.
    std::vector<math::Vec4> colors;
    // Optional: empty, or one unit-length normal per position (within
    // kNormalLengthTolerance). Flat-shaded faces give each of their own
    // vertices the face normal.
    std::vector<math::Vec3> normals;
    // Optional: empty, or one texture coordinate per position. (0, 0) is the
    // bottom-left corner of the image, U points right and V points up
    // (OBJ/OpenGL convention). Values outside [0, 1] are valid; they repeat
    // the texture.
    std::vector<math::Vec2> uvs;
    // Triangle list. Front faces are counter-clockwise when seen from outside.
    std::vector<std::uint32_t> indices;

    [[nodiscard]] std::size_t vertexCount() const noexcept { return positions.size(); }
    [[nodiscard]] std::size_t triangleCount() const noexcept { return indices.size() / 3; }
    [[nodiscard]] bool empty() const noexcept { return indices.empty(); }
    [[nodiscard]] bool hasNormals() const noexcept { return !normals.empty(); }
    [[nodiscard]] bool hasUvs() const noexcept { return !uvs.empty(); }
};

// Axis-aligned bounding box.
struct Bounds {
    math::Vec3 min{0.0F};
    math::Vec3 max{0.0F};
};

// Checks the invariants every consumer relies on, and reports the first
// failure in this order: vertex count within 32-bit indices; one color per
// position; normals and UVs each absent or one per position; whole triangles;
// indices inside the vertex range; then, vertex by vertex, finite position,
// finite color, finite normal of unit length, finite UV. An empty mesh is
// valid. The error names the attribute and the index of the offending element.
// Only checks; never changes the data (normals are not normalized).
[[nodiscard]] std::optional<core::Error> validate(const MeshData& mesh);

// Bounds of all positions; empty if the mesh has no vertices.
[[nodiscard]] std::optional<Bounds> computeBounds(const MeshData& mesh);

// Appends `source` to `target`, shifting the source indices by target's
// original vertex count. Normals and UVs are concatenated; a mesh without
// vertices matches any attribute layout, so appending to an empty mesh keeps
// the source's layout and appending an empty mesh changes nothing. Appending a
// mesh to itself works.
//
// Fails, leaving `target` unchanged, if both meshes have vertices but only one
// has normals (or UVs), if the result would exceed the vertex count 32-bit
// indices can address, or if a shifted index would not fit in 32 bits.
// Values are not checked otherwise: appending two meshes that pass validate()
// gives a mesh that passes validate().
[[nodiscard]] std::optional<core::Error> append(MeshData& target, const MeshData& source);

} // namespace engine::geometry
