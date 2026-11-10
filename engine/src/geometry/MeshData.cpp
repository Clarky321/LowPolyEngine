#include "engine/geometry/MeshData.hpp"

#include "MeshLimits.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <iterator>
#include <string>
#include <string_view>

namespace engine::geometry {

namespace {

bool isFinite(const math::Vec2& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y);
}

bool isFinite(const math::Vec3& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool isFinite(const math::Vec4& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::isfinite(v.w);
}

// Computed in double: large finite components cannot overflow, and no extra
// float rounding is added near the tolerance boundary.
double lengthOf(const math::Vec3& v) noexcept
{
    const auto x = static_cast<double>(v.x);
    const auto y = static_cast<double>(v.y);
    const auto z = static_cast<double>(v.z);
    return std::sqrt(x * x + y * y + z * z);
}

// An optional attribute is absent (no entries) or has one entry per position.
std::optional<core::Error> checkOptionalCount(std::string_view attribute, std::size_t count,
                                              std::size_t vertexCount)
{
    if (count == 0 || count == vertexCount) {
        return std::nullopt;
    }
    if (vertexCount == 0) {
        return core::Error{
            std::format("mesh has 0 positions but {} {} (expected 0)", count, attribute)};
    }
    return core::Error{std::format("mesh has {} positions but {} {} (expected 0 or {})",
                                   vertexCount, count, attribute, vertexCount)};
}

// When both meshes have vertices, they must agree on an optional attribute.
std::optional<core::Error> checkSameLayout(std::string_view attribute, bool inTarget, bool inSource)
{
    if (inTarget == inSource) {
        return std::nullopt;
    }
    if (inTarget) {
        return core::Error{
            std::format("cannot append: the target has {} but the source has none", attribute)};
    }
    return core::Error{
        std::format("cannot append: the source has {} but the target has none", attribute)};
}

// Makes room for `extra` more elements, growing geometrically so that repeated
// appends stay linear overall. Afterwards, inserting `extra` elements cannot
// reallocate, so it cannot fail.
template <typename T>
void reserveMore(std::vector<T>& values, std::size_t extra)
{
    const std::size_t needed = values.size() + extra;
    if (needed > values.capacity()) {
        values.reserve(std::max(needed, std::min(values.capacity() * 2, values.max_size())));
    }
}

template <typename T>
void appendValues(std::vector<T>& target, const std::vector<T>& source)
{
    target.insert(target.end(), source.begin(), source.end());
}

} // namespace

std::optional<core::Error> validate(const MeshData& mesh)
{
    const std::size_t vertexCount = mesh.positions.size();
    if (vertexCount > detail::kMaxVertexCount) {
        return core::Error{"mesh has " + std::to_string(vertexCount) +
                           " vertices; 32-bit indices allow at most 4294967295"};
    }
    if (mesh.colors.size() != vertexCount) {
        return core::Error{"mesh has " + std::to_string(vertexCount) + " positions but " +
                           std::to_string(mesh.colors.size()) + " colors"};
    }
    if (auto error = checkOptionalCount("normals", mesh.normals.size(), vertexCount)) {
        return error;
    }
    if (auto error = checkOptionalCount("uvs", mesh.uvs.size(), vertexCount)) {
        return error;
    }
    if (mesh.indices.size() % 3 != 0) {
        return core::Error{"mesh index count " + std::to_string(mesh.indices.size()) +
                           " is not a multiple of 3 (triangle list)"};
    }
    for (std::size_t i = 0; i < mesh.indices.size(); ++i) {
        if (mesh.indices[i] >= vertexCount) {
            return core::Error{"mesh index " + std::to_string(i) + " has value " +
                               std::to_string(mesh.indices[i]) + ", but there are only " +
                               std::to_string(vertexCount) + " vertices"};
        }
    }
    // The counts above guarantee one normal/UV per position when present.
    const bool checkNormals = mesh.hasNormals();
    const bool checkUvs = mesh.hasUvs();
    for (std::size_t i = 0; i < vertexCount; ++i) {
        if (!isFinite(mesh.positions[i])) {
            return core::Error{"mesh position " + std::to_string(i) + " is not finite"};
        }
        if (!isFinite(mesh.colors[i])) {
            return core::Error{"mesh color " + std::to_string(i) + " is not finite"};
        }
        if (checkNormals) {
            if (!isFinite(mesh.normals[i])) {
                return core::Error{"mesh normal " + std::to_string(i) + " is not finite"};
            }
            const double length = lengthOf(mesh.normals[i]);
            if (std::abs(length - 1.0) > static_cast<double>(kNormalLengthTolerance)) {
                return core::Error{
                    std::format("mesh normal {} has length {:.6g}, expected 1 +/- {}", i, length,
                                kNormalLengthTolerance)};
            }
        }
        if (checkUvs && !isFinite(mesh.uvs[i])) {
            return core::Error{"mesh uv " + std::to_string(i) + " is not finite"};
        }
    }
    return std::nullopt;
}

std::optional<Bounds> computeBounds(const MeshData& mesh)
{
    if (mesh.positions.empty()) {
        return std::nullopt;
    }
    Bounds bounds{mesh.positions.front(), mesh.positions.front()};
    for (const math::Vec3& p : mesh.positions) {
        bounds.min = math::Vec3(std::min(bounds.min.x, p.x), std::min(bounds.min.y, p.y),
                                std::min(bounds.min.z, p.z));
        bounds.max = math::Vec3(std::max(bounds.max.x, p.x), std::max(bounds.max.y, p.y),
                                std::max(bounds.max.z, p.z));
    }
    return bounds;
}

std::optional<core::Error> append(MeshData& target, const MeshData& source)
{
    if (&target == &source) {
        // A vector cannot insert a range of itself; append a copy instead.
        const MeshData copy = source;
        return append(target, copy);
    }

    // Every check comes before the first change, so a failure leaves target as it was.
    if (!target.positions.empty() && !source.positions.empty()) {
        if (auto error = checkSameLayout("normals", target.hasNormals(), source.hasNormals())) {
            return error;
        }
        if (auto error = checkSameLayout("uvs", target.hasUvs(), source.hasUvs())) {
            return error;
        }
    }
    if (!detail::fitsVertexCount(target.positions.size(), source.positions.size())) {
        return core::Error{std::format(
            "cannot append {} vertices to a mesh with {}: 32-bit indices allow at most {} vertices",
            source.positions.size(), target.positions.size(), detail::kMaxVertexCount)};
    }
    // Fits in 32 bits, checked just above.
    const auto offset = static_cast<std::uint32_t>(target.positions.size());
    for (std::size_t i = 0; i < source.indices.size(); ++i) {
        if (!detail::fitsShiftedIndex(source.indices[i], offset)) {
            return core::Error{std::format(
                "cannot append: source index {} has value {}, which does not fit in 32 bits "
                "after adding the offset {}",
                i, source.indices[i], offset)};
        }
    }

    // Reserve everything first: if memory runs out, target's contents are still
    // unchanged, and the inserts below can no longer reallocate.
    reserveMore(target.positions, source.positions.size());
    reserveMore(target.colors, source.colors.size());
    reserveMore(target.normals, source.normals.size());
    reserveMore(target.uvs, source.uvs.size());
    reserveMore(target.indices, source.indices.size());

    appendValues(target.positions, source.positions);
    appendValues(target.colors, source.colors);
    appendValues(target.normals, source.normals);
    appendValues(target.uvs, source.uvs);
    std::transform(source.indices.begin(), source.indices.end(), std::back_inserter(target.indices),
                   [offset](std::uint32_t index) { return index + offset; });
    return std::nullopt;
}

} // namespace engine::geometry
