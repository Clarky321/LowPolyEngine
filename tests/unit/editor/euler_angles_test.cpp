// Euler angles for the inspector (editor/panels/EulerAngles, M5.8), headless:
// the convention (Ry * Rx * Rz in degrees), round trips inside the shown
// ranges, the gimbal lock at +-90 degrees of pitch, quaternions as M4 stores
// them, and values the inspector must never show (-0, -180).

#include "editor/panels/EulerAngles.hpp"

#include "engine/core/Math.hpp"

#include <glm/ext/quaternion_geometric.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <random>

using editor::eulerDegreesFromQuat;
using editor::quatFromEulerDegrees;
using engine::math::Quat;
using engine::math::Vec3;

namespace {

// The same rotation (q and -q are one).
bool sameRotation(const Quat& a, const Quat& b, float tolerance = 1.0e-5F)
{
    return std::abs(std::abs(glm::dot(a, b)) - 1.0F) <= tolerance;
}

bool near(const Vec3& a, const Vec3& b, float tolerance)
{
    return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance &&
           std::abs(a.z - b.z) <= tolerance;
}

Quat about(float degrees, const Vec3& axis)
{
    return glm::angleAxis(glm::radians(degrees), axis);
}

const Vec3 kX{1.0F, 0.0F, 0.0F};
const Vec3 kY{0.0F, 1.0F, 0.0F};
const Vec3 kZ{0.0F, 0.0F, 1.0F};

} // namespace

TEST_CASE("Euler angles: yaw about Y, pitch about X, roll about Z, applied roll first",
          "[editor][inspector][euler]")
{
    CHECK(eulerDegreesFromQuat(Quat{1.0F, 0.0F, 0.0F, 0.0F}) == Vec3{0.0F});
    CHECK(sameRotation(quatFromEulerDegrees(Vec3{0.0F}), Quat{1.0F, 0.0F, 0.0F, 0.0F}));

    // Each axis alone.
    CHECK(sameRotation(quatFromEulerDegrees({30.0F, 0.0F, 0.0F}), about(30.0F, kX)));
    CHECK(sameRotation(quatFromEulerDegrees({0.0F, 45.0F, 0.0F}), about(45.0F, kY)));
    CHECK(sameRotation(quatFromEulerDegrees({0.0F, 0.0F, -60.0F}), about(-60.0F, kZ)));
    CHECK(near(eulerDegreesFromQuat(about(30.0F, kX)), {30.0F, 0.0F, 0.0F}, 1.0e-4F));
    CHECK(near(eulerDegreesFromQuat(about(45.0F, kY)), {0.0F, 45.0F, 0.0F}, 1.0e-4F));
    CHECK(near(eulerDegreesFromQuat(about(-60.0F, kZ)), {0.0F, 0.0F, -60.0F}, 1.0e-4F));

    // The order: R = Ry * Rx * Rz. A roll of 90 then a yaw of 90 sends +X to
    // +Y (the roll) and leaves it there (the yaw turns about Y).
    const Quat combined = quatFromEulerDegrees({0.0F, 90.0F, 90.0F});
    CHECK(sameRotation(combined, about(90.0F, kY) * about(90.0F, kZ)));
    const Vec3 x = combined * kX;
    CHECK(glm::length(x - kY) < 1.0e-5F);
    // A pitch of 90 then a yaw: -Z (the camera's forward) pitched up to +Y.
    const Vec3 forward = quatFromEulerDegrees({90.0F, 0.0F, 0.0F}) * Vec3{0.0F, 0.0F, -1.0F};
    CHECK(glm::length(forward - kY) < 1.0e-5F);
}

TEST_CASE("Euler angles round-trip inside the shown ranges", "[editor][inspector][euler]")
{
    std::mt19937 random(11);
    std::uniform_real_distribution<float> pitch(-89.0F, 89.0F);
    std::uniform_real_distribution<float> other(-179.5F, 179.5F);
    for (int i = 0; i < 2000; ++i) {
        const Vec3 angles{pitch(random), other(random), other(random)};
        INFO("angles " << angles.x << ", " << angles.y << ", " << angles.z);
        const Quat rotation = quatFromEulerDegrees(angles);
        CHECK(std::abs(glm::length(rotation) - 1.0F) < 1.0e-5F);
        const Vec3 back = eulerDegreesFromQuat(rotation);
        CHECK(near(back, angles, 2.0e-3F));
        // Any quaternion's angles give it back (both signs).
        CHECK(sameRotation(quatFromEulerDegrees(back), rotation));
        CHECK(near(eulerDegreesFromQuat(-rotation), back, 1.0e-4F));
    }
}

TEST_CASE("Euler angles of any rotation stay in their ranges and give the rotation back",
          "[editor][inspector][euler]")
{
    std::mt19937 random(23);
    std::normal_distribution<float> component(0.0F, 1.0F);
    for (int i = 0; i < 2000; ++i) {
        const Quat rotation = glm::normalize(
            Quat{component(random), component(random), component(random), component(random)});
        const Vec3 angles = eulerDegreesFromQuat(rotation);
        INFO("angles " << angles.x << ", " << angles.y << ", " << angles.z);
        CHECK(angles.x >= -90.0F);
        CHECK(angles.x <= 90.0F);
        CHECK(angles.y > -180.0F);
        CHECK(angles.y <= 180.0F);
        CHECK(angles.z > -180.0F);
        CHECK(angles.z <= 180.0F);
        CHECK(sameRotation(quatFromEulerDegrees(angles), rotation, 1.0e-4F));
    }
    // A pitch beyond 90 degrees is shown as its other angles, the same rotation.
    const Quat beyond = quatFromEulerDegrees({100.0F, 0.0F, 0.0F});
    const Vec3 shown = eulerDegreesFromQuat(beyond);
    CHECK(near(shown, {80.0F, 180.0F, 180.0F}, 1.0e-3F));
    CHECK(sameRotation(quatFromEulerDegrees(shown), beyond));
}

TEST_CASE("At a pitch of 90 degrees the roll is shown as 0 and the yaw holds both",
          "[editor][inspector][euler]")
{
    for (const float pitch : {90.0F, -90.0F}) {
        for (const float yaw : {0.0F, 30.0F, -120.0F}) {
            for (const float roll : {0.0F, 15.0F, -45.0F}) {
                INFO("pitch " << pitch << ", yaw " << yaw << ", roll " << roll);
                const Quat rotation = quatFromEulerDegrees({pitch, yaw, roll});
                const Vec3 shown = eulerDegreesFromQuat(rotation);
                CHECK(std::abs(shown.x - pitch) < 1.0e-3F);
                CHECK(shown.z == 0.0F);
                // Looking up, the roll turns like the yaw the other way;
                // looking down, the same way.
                const float expectedYaw = pitch > 0.0F ? yaw - roll : yaw + roll;
                CHECK(std::abs(shown.y - expectedYaw) < 1.0e-2F);
                CHECK(sameRotation(quatFromEulerDegrees(shown), rotation, 1.0e-4F));
            }
        }
    }
}

TEST_CASE("Euler angles of the rotations M4 stores, and of broken quaternions",
          "[editor][inspector][euler]")
{
    // The test project's lighthouses (tests/projects/lighthouses): the
    // floating one turned 25 degrees about Z, others yawed.
    CHECK(near(eulerDegreesFromQuat(Quat{0.976296F, 0.0F, 0.0F, 0.21643962F}), {0.0F, 0.0F, 25.0F},
               1.0e-3F));
    CHECK(near(eulerDegreesFromQuat(about(45.0F, kY)), {0.0F, 45.0F, 0.0F}, 1.0e-4F));
    CHECK(near(eulerDegreesFromQuat(about(-30.0F, kY)), {0.0F, -30.0F, 0.0F}, 1.0e-4F));

    // Half turns are shown as +180, never -180; tiny angles as 0, never -0.
    const Vec3 half = eulerDegreesFromQuat(about(180.0F, kY));
    CHECK(near(half, {0.0F, 180.0F, 0.0F}, 1.0e-3F));
    CHECK(eulerDegreesFromQuat(about(-180.0F, kZ)).z > 0.0F);
    const Vec3 tiny = eulerDegreesFromQuat(about(-1.0e-6F, kX));
    CHECK(tiny.x == 0.0F);
    CHECK_FALSE(std::signbit(tiny.x));

    // A quaternion of another length is a rotation all the same; a zero or a
    // non-finite one shows no rotation.
    CHECK(near(eulerDegreesFromQuat(about(45.0F, kY) * 3.0F), {0.0F, 45.0F, 0.0F}, 1.0e-4F));
    CHECK(eulerDegreesFromQuat(Quat{0.0F, 0.0F, 0.0F, 0.0F}) == Vec3{0.0F});
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(eulerDegreesFromQuat(Quat{nan, 0.0F, 0.0F, 0.0F}) == Vec3{0.0F});
}
