#include "engine/geometry/FaceNormal.hpp"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace engine::geometry {

std::optional<math::Vec3> faceNormal(std::span<const math::Vec3> corners)
{
    if (corners.size() < 3) {
        return std::nullopt;
    }
    const auto toDouble = [](const math::Vec3& v) {
        return glm::dvec3{static_cast<double>(v.x), static_cast<double>(v.y),
                          static_cast<double>(v.z)};
    };
    const glm::dvec3 origin = toDouble(corners[0]);
    glm::dvec3 areaVector{0.0};
    glm::dvec3 previous = toDouble(corners[1]) - origin;
    double sizeSquared = glm::dot(previous, previous);
    for (std::size_t i = 2; i < corners.size(); ++i) {
        const glm::dvec3 current = toDouble(corners[i]) - origin;
        areaVector += glm::cross(previous, current);
        sizeSquared = std::max(sizeSquared, glm::dot(current, current));
        previous = current;
    }

    const double length = glm::length(areaVector);
    // Non-finite input propagates into length and sizeSquared; NaN fails both
    // comparisons, infinity the first.
    if (!std::isfinite(length) || !std::isfinite(sizeSquared) ||
        !(length > kDegenerateFaceTolerance * sizeSquared)) {
        return std::nullopt;
    }
    // For a triangle this is exactly the normalized cross product of two edges.
    const glm::dvec3 normal = glm::normalize(areaVector);
    return math::Vec3{static_cast<float>(normal.x), static_cast<float>(normal.y),
                      static_cast<float>(normal.z)};
}

} // namespace engine::geometry
