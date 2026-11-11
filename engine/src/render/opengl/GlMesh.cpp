#include "GlMesh.hpp"

#include "GlDiagnostics.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace engine::render::opengl {

namespace {

// Buffer layout relies on tightly packed GLM vectors (the GLM default).
static_assert(sizeof(math::Vec2) == 2 * sizeof(float));
static_assert(sizeof(math::Vec3) == 3 * sizeof(float));
static_assert(sizeof(math::Vec4) == 4 * sizeof(float));

// One vertex buffer binding per attribute.
constexpr GLuint kPositionBinding = 0;
constexpr GLuint kColorBinding = 1;
constexpr GLuint kNormalBinding = 2;
constexpr GLuint kUvBinding = 3;

// glDrawElements takes the index count as GLsizei (signed 32-bit).
constexpr std::size_t kMaxIndexCount =
    static_cast<std::size_t>(std::numeric_limits<GLsizei>::max());

GLsizeiptr byteSize(std::size_t count, std::size_t elementSize)
{
    return static_cast<GLsizeiptr>(count * elementSize);
}

// Byte offsets of the four attribute ranges in a vertex buffer for
// `capacity` vertices (positions start at 0), and the buffer's total size.
struct VertexRanges {
    GLintptr colors = 0;
    GLintptr normals = 0;
    GLintptr uvs = 0;
    GLsizeiptr size = 0;
};

VertexRanges rangesFor(std::uint32_t capacity)
{
    VertexRanges ranges;
    ranges.colors = byteSize(capacity, sizeof(math::Vec3));
    ranges.normals = ranges.colors + byteSize(capacity, sizeof(math::Vec4));
    ranges.uvs = ranges.normals + byteSize(capacity, sizeof(math::Vec3));
    ranges.size = ranges.uvs + byteSize(capacity, sizeof(math::Vec2));
    return ranges;
}

// Room for the requested count plus 50 %, so a slowly growing mesh does not
// reallocate on every update. Zero-sized buffer storage is not allowed.
std::uint32_t grownCapacity(std::size_t needed, std::uint32_t current)
{
    const std::size_t grown = std::max(needed, std::size_t{current} + std::size_t{current} / 2);
    const std::size_t limit = std::numeric_limits<std::uint32_t>::max();
    return static_cast<std::uint32_t>(std::clamp<std::size_t>(grown, 1, limit));
}

// A float attribute with `components` values per vertex, read from `binding`.
void describeAttribute(GLuint layout, GLuint location, GLuint binding, GLint components)
{
    glVertexArrayAttribFormat(layout, location, components, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(layout, location, binding);
}

void setAttributeEnabled(GLuint layout, GLuint location, bool enabled)
{
    if (enabled) {
        glEnableVertexArrayAttrib(layout, location);
    } else {
        glDisableVertexArrayAttrib(layout, location);
    }
}

template <typename T>
void uploadRange(GLuint buffer, GLintptr offset, const std::vector<T>& values)
{
    glNamedBufferSubData(buffer, offset, byteSize(values.size(), sizeof(T)), values.data());
}

} // namespace

void setMissingAttributeDefaults()
{
    glVertexAttrib3f(kNormalLocation, 0.0F, 0.0F, 0.0F);
    glVertexAttrib2f(kUvLocation, 0.0F, 0.0F);
}

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
    const VertexRanges ranges = rangesFor(vertexCapacity);

    GLuint name = 0;
    glCreateBuffers(1, &name);
    GlBuffer vertices{name};
    glNamedBufferStorage(vertices.get(), ranges.size, nullptr, kFlags);

    glCreateBuffers(1, &name);
    GlBuffer indices{name};
    glNamedBufferStorage(indices.get(), byteSize(indexCapacity, sizeof(std::uint32_t)), nullptr,
                         kFlags);

    // Direct state access (core in 4.5): set up by name, no binding needed.
    glCreateVertexArrays(1, &name);
    GlVertexArray layout{name};
    glVertexArrayVertexBuffer(layout.get(), kPositionBinding, vertices.get(), 0,
                              static_cast<GLsizei>(sizeof(math::Vec3)));
    glVertexArrayVertexBuffer(layout.get(), kColorBinding, vertices.get(), ranges.colors,
                              static_cast<GLsizei>(sizeof(math::Vec4)));
    glVertexArrayVertexBuffer(layout.get(), kNormalBinding, vertices.get(), ranges.normals,
                              static_cast<GLsizei>(sizeof(math::Vec3)));
    glVertexArrayVertexBuffer(layout.get(), kUvBinding, vertices.get(), ranges.uvs,
                              static_cast<GLsizei>(sizeof(math::Vec2)));

    describeAttribute(layout.get(), kPositionLocation, kPositionBinding, 3);
    describeAttribute(layout.get(), kColorLocation, kColorBinding, 4);
    describeAttribute(layout.get(), kNormalLocation, kNormalBinding, 3);
    describeAttribute(layout.get(), kUvLocation, kUvBinding, 2);
    // Positions and colors are always there; normals and UVs are enabled per
    // upload, depending on the data.
    glEnableVertexArrayAttrib(layout.get(), kPositionLocation);
    glEnableVertexArrayAttrib(layout.get(), kColorLocation);

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
    const VertexRanges ranges = rangesFor(vertexCapacity_);
    if (!mesh.positions.empty()) {
        uploadRange(vertices_.get(), 0, mesh.positions);
        uploadRange(vertices_.get(), ranges.colors, mesh.colors);
        if (mesh.hasNormals()) {
            uploadRange(vertices_.get(), ranges.normals, mesh.normals);
        }
        if (mesh.hasUvs()) {
            uploadRange(vertices_.get(), ranges.uvs, mesh.uvs);
        }
    }
    // A disabled array makes the shader read the defaults from
    // setMissingAttributeDefaults(); stale data from an earlier upload is unused.
    setAttributeEnabled(layout_.get(), kNormalLocation, mesh.hasNormals());
    setAttributeEnabled(layout_.get(), kUvLocation, mesh.hasUvs());
    hasUvs_ = mesh.hasUvs();
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
