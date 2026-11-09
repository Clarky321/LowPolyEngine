#include "GlMesh.hpp"

#include "GlDiagnostics.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>

namespace engine::render::opengl {

namespace {

// Buffer layout relies on tightly packed GLM vectors (the GLM default).
static_assert(sizeof(math::Vec3) == 3 * sizeof(float));
static_assert(sizeof(math::Vec4) == 4 * sizeof(float));

constexpr GLuint kPositionBinding = 0;
constexpr GLuint kColorBinding = 1;

// glDrawElements takes the index count as GLsizei (signed 32-bit).
constexpr std::size_t kMaxIndexCount =
    static_cast<std::size_t>(std::numeric_limits<GLsizei>::max());

GLsizeiptr byteSize(std::size_t count, std::size_t elementSize)
{
    return static_cast<GLsizeiptr>(count * elementSize);
}

// Room for the requested count plus 50 %, so a slowly growing mesh does not
// reallocate on every update. Zero-sized buffer storage is not allowed.
std::uint32_t grownCapacity(std::size_t needed, std::uint32_t current)
{
    const std::size_t grown = std::max(needed, std::size_t{current} + std::size_t{current} / 2);
    const std::size_t limit = std::numeric_limits<std::uint32_t>::max();
    return static_cast<std::uint32_t>(std::clamp<std::size_t>(grown, 1, limit));
}

} // namespace

core::Result<GlMesh> GlMesh::create(const geometry::MeshData& mesh)
{
    GlMesh glMesh;
    if (auto error = glMesh.update(mesh)) {
        return std::move(*error);
    }
    return glMesh;
}

std::optional<core::Error> GlMesh::update(const geometry::MeshData& mesh)
{
    const std::size_t vertexCount = mesh.positions.size();
    const std::size_t indexCount = mesh.indices.size();
    if (indexCount > kMaxIndexCount) {
        return core::Error{"mesh has " + std::to_string(indexCount) + " indices; at most " +
                           std::to_string(kMaxIndexCount) + " can be drawn in one call"};
    }

    if (!layout_ || vertexCount > vertexCapacity_ || indexCount > indexCapacity_) {
        if (auto error = allocate(grownCapacity(vertexCount, vertexCapacity_),
                                  grownCapacity(indexCount, indexCapacity_))) {
            return error;
        }
    }
    upload(mesh);
    indexCount_ = static_cast<std::uint32_t>(indexCount);
    if (checkGlErrors("mesh upload") > 0) {
        return core::Error{"uploading mesh data failed (see the OpenGL error above)"};
    }
    return std::nullopt;
}

std::optional<core::Error> GlMesh::allocate(std::uint32_t vertexCapacity,
                                            std::uint32_t indexCapacity)
{
    // Dynamic storage: contents can be replaced with glNamedBufferSubData.
    constexpr GLbitfield kFlags = GL_DYNAMIC_STORAGE_BIT;
    const GLsizeiptr positionBytes = byteSize(vertexCapacity, sizeof(math::Vec3));
    const GLsizeiptr colorBytes = byteSize(vertexCapacity, sizeof(math::Vec4));

    GLuint name = 0;
    glCreateBuffers(1, &name);
    GlBuffer vertices{name};
    glNamedBufferStorage(vertices.get(), positionBytes + colorBytes, nullptr, kFlags);

    glCreateBuffers(1, &name);
    GlBuffer indices{name};
    glNamedBufferStorage(indices.get(), byteSize(indexCapacity, sizeof(std::uint32_t)), nullptr,
                         kFlags);

    // Direct state access (core in 4.5): set up by name, no binding needed.
    glCreateVertexArrays(1, &name);
    GlVertexArray layout{name};
    glVertexArrayVertexBuffer(layout.get(), kPositionBinding, vertices.get(), 0,
                              static_cast<GLsizei>(sizeof(math::Vec3)));
    glVertexArrayVertexBuffer(layout.get(), kColorBinding, vertices.get(), positionBytes,
                              static_cast<GLsizei>(sizeof(math::Vec4)));

    glEnableVertexArrayAttrib(layout.get(), kPositionLocation);
    glVertexArrayAttribFormat(layout.get(), kPositionLocation, 3, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(layout.get(), kPositionLocation, kPositionBinding);

    glEnableVertexArrayAttrib(layout.get(), kColorLocation);
    glVertexArrayAttribFormat(layout.get(), kColorLocation, 4, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(layout.get(), kColorLocation, kColorBinding);

    glVertexArrayElementBuffer(layout.get(), indices.get());

    if (checkGlErrors("mesh allocation") > 0) {
        return core::Error{"allocating GPU buffers for " + std::to_string(vertexCapacity) +
                           " vertices failed (see the OpenGL error above)"};
    }
    vertices_ = std::move(vertices);
    indices_ = std::move(indices);
    layout_ = std::move(layout);
    vertexCapacity_ = vertexCapacity;
    indexCapacity_ = indexCapacity;
    return std::nullopt;
}

void GlMesh::upload(const geometry::MeshData& mesh)
{
    const std::size_t vertexCount = mesh.positions.size();
    if (vertexCount > 0) {
        glNamedBufferSubData(vertices_.get(), 0, byteSize(vertexCount, sizeof(math::Vec3)),
                             mesh.positions.data());
        glNamedBufferSubData(vertices_.get(), byteSize(vertexCapacity_, sizeof(math::Vec3)),
                             byteSize(vertexCount, sizeof(math::Vec4)), mesh.colors.data());
    }
    if (!mesh.indices.empty()) {
        glNamedBufferSubData(indices_.get(), 0,
                             byteSize(mesh.indices.size(), sizeof(std::uint32_t)),
                             mesh.indices.data());
    }
}

void GlMesh::draw() const
{
    if (indexCount_ == 0) {
        return;
    }
    glBindVertexArray(layout_.get());
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount_), GL_UNSIGNED_INT, nullptr);
}

} // namespace engine::render::opengl
