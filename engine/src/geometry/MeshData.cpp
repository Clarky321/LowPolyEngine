#include "engine/geometry/MeshData.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace engine::geometry {

namespace {

bool isFinite(const math::Vec3& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool isFinite(const math::Vec4& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::isfinite(v.w);
}

} // namespace

std::optional<core::Error> validate(const MeshData& mesh)
{
    const std::size_t vertexCount = mesh.positions.size();
    if (vertexCount > std::numeric_limits<std::uint32_t>::max()) {
        return core::Error{"mesh has " + std::to_string(vertexCount) +
                           " vertices; 32-bit indices allow at most 4294967295"};
    }
    if (mesh.colors.size() != vertexCount) {
        return core::Error{"mesh has " + std::to_string(vertexCount) + " positions but " +
                           std::to_string(mesh.colors.size()) + " colors"};
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
    for (std::size_t i = 0; i < vertexCount; ++i) {
        if (!isFinite(mesh.positions[i])) {
            return core::Error{"mesh position " + std::to_string(i) + " is not finite"};
        }
        if (!isFinite(mesh.colors[i])) {
            return core::Error{"mesh color " + std::to_string(i) + " is not finite"};
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

void append(MeshData& target, const MeshData& source)
{
    const auto offset = static_cast<std::uint32_t>(target.positions.size());
    target.positions.insert(target.positions.end(), source.positions.begin(),
                            source.positions.end());
    target.colors.insert(target.colors.end(), source.colors.begin(), source.colors.end());
    target.indices.reserve(target.indices.size() + source.indices.size());
    for (const std::uint32_t index : source.indices) {
        target.indices.push_back(index + offset);
    }
}

} // namespace engine::geometry
