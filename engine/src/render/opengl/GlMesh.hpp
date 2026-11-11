#pragma once

#include "GlHandle.hpp"

#include "engine/core/Result.hpp"
#include "engine/geometry/MeshData.hpp"

#include <cstdint>
#include <optional>

namespace engine::render::opengl {

// Vertex attribute locations shared with assets/shaders/mesh.vert.
inline constexpr GLuint kPositionLocation = 0;
inline constexpr GLuint kColorLocation = 1;
inline constexpr GLuint kNormalLocation = 2;
inline constexpr GLuint kUvLocation = 3;

// Sets the values a shader reads for an optional attribute that a mesh does
// not have (its array is disabled): normal (0, 0, 0), meaning "no normal",
// and UV (0, 0). They are context state, so the renderer sets them once at
// startup on the context's thread.
void setMissingAttributeDefaults();

// GPU copy of a MeshData. Internal to the OpenGL backend.
//
// One buffer holds four ranges for its vertex capacity: all positions, then
// all colors, normals and UVs (the same structure-of-arrays layout as
// MeshData, so no interleaving copy is needed). Space for normals and UVs is
// always reserved, so an update that adds or removes an optional attribute
// reuses the buffer like any other update. A second buffer holds the indices,
// and a vertex array object describes both; it enables the normal and UV
// arrays only while the current data has them.
// Buffers keep spare capacity, so smaller or equal updates reuse them.
class GlMesh {
public:
    // The data must already have passed geometry::validate().
    [[nodiscard]] static core::Result<GlMesh> create(const geometry::MeshData& mesh);

    // Uploads new data, growing the buffers when it does not fit.
    [[nodiscard]] std::optional<core::Error> update(const geometry::MeshData& mesh);

    // Issues the draw call; the mesh program must be bound.
    void draw() const;

    [[nodiscard]] std::uint32_t indexCount() const noexcept { return indexCount_; }

    // Whether the current data has UVs (textured materials need them).
    [[nodiscard]] bool hasUvs() const noexcept { return hasUvs_; }

private:
    GlMesh() = default;

    std::optional<core::Error> allocate(std::uint32_t vertexCapacity, std::uint32_t indexCapacity);
    void upload(const geometry::MeshData& mesh);

    GlBuffer vertices_;
    GlBuffer indices_;
    GlVertexArray layout_;
    std::uint32_t vertexCapacity_ = 0;
    std::uint32_t indexCapacity_ = 0;
    std::uint32_t indexCount_ = 0;
    bool hasUvs_ = false;
};

} // namespace engine::render::opengl
