#include "editor/panels/EulerAngles.hpp"

#include <glm/ext/quaternion_geometric.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>
#include <glm/trigonometric.hpp>

#include <cmath>

namespace editor {

using engine::math::Quat;
using engine::math::Vec3;

namespace {

// Below this cosine of the pitch, yaw and roll can no longer be told apart.
constexpr float kGimbalCosine = 1.0e-5F;

// Into (-180, 180], with values near 0 shown as 0 and values near a half turn
// as 180 (a half turn computed in float lands on either side of -180).
float tidy(float degrees) noexcept
{
    if (std::abs(degrees - 180.0F) < kEulerSnapDegrees ||
        std::abs(degrees + 180.0F) < kEulerSnapDegrees) {
        return 180.0F;
    }
    if (degrees < -180.0F) {
        degrees += 360.0F;
    }
    if (degrees > 180.0F) {
        degrees -= 360.0F;
    }
    if (std::abs(degrees) < kEulerSnapDegrees) {
        return 0.0F;
    }
    return degrees;
}

} // namespace

Quat quatFromEulerDegrees(const Vec3& degrees) noexcept
{
    const Vec3 radians = glm::radians(degrees);
    const Quat yaw = glm::angleAxis(radians.y, Vec3{0.0F, 1.0F, 0.0F});
    const Quat pitch = glm::angleAxis(radians.x, Vec3{1.0F, 0.0F, 0.0F});
    const Quat roll = glm::angleAxis(radians.z, Vec3{0.0F, 0.0F, 1.0F});
    return glm::normalize(yaw * pitch * roll);
}

Vec3 eulerDegreesFromQuat(const Quat& rotation) noexcept
{
    const float length = glm::length(rotation);
    if (!std::isfinite(length) || length <= 0.0F) {
        return Vec3{0.0F};
    }
    // m[column][row]. With M = Ry(y) Rx(x) Rz(z):
    //   row 1, column 2 is -sin(x);
    //   row 0 and row 2 of column 2 are sin(y) cos(x) and cos(y) cos(x);
    //   row 1 of columns 0 and 1 are cos(x) sin(z) and cos(x) cos(z).
    const glm::mat3 m = glm::mat3_cast(rotation / length);
    const float sinX = -m[2][1];
    const float cosX = std::sqrt(m[0][1] * m[0][1] + m[1][1] * m[1][1]);
    float x = std::atan2(sinX, cosX);
    float y = 0.0F;
    float z = 0.0F;
    if (cosX > kGimbalCosine) {
        y = std::atan2(m[2][0], m[2][2]);
        z = std::atan2(m[0][1], m[1][1]);
    } else {
        // Pitch +-90 degrees: the roll is shown as 0. Column 0 is then
        // (cos(y), 0, -sin(y)).
        x = sinX > 0.0F ? glm::radians(90.0F) : glm::radians(-90.0F);
        y = std::atan2(-m[0][2], m[0][0]);
    }
    return Vec3{tidy(glm::degrees(x)), tidy(glm::degrees(y)), tidy(glm::degrees(z))};
}

} // namespace editor
