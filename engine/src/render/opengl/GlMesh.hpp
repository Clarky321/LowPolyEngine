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

// GPU copy of a MeshData. Internal to the OpenGL backend.
//
// One buffer holds all positions followed by all colors (the same
// structure-of-arrays layout as MeshData, so no interleaving copy is needed),
// a second buffer holds the indices, and a vertex array object describes both.
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
};

} // namespace engine::render::opengl
