#include "engine/camera/Camera.hpp"

#include <glm/geometric.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <numbers>

using Catch::Approx;
using engine::camera::Camera;
using engine::math::Vec3;
using engine::math::Vec4;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kMargin = 1e-5F;

bool approxEqual(const Vec3& a, const Vec3& b)
{
    return glm::length(a - b) < kMargin;
}

// Clip space -> normalized device coordinates.
Vec3 toNdc(const Vec4& clip)
{
    return Vec3(clip) / clip.w;
}

} // namespace

TEST_CASE("Default camera looks along -Z with +X to the right", "[camera]")
{
    const Camera camera;
    CHECK(approxEqual(forwardDirection(camera), {0, 0, -1}));
    CHECK(approxEqual(rightDirection(camera), {1, 0, 0}));
    CHECK(approxEqual(upDirection(camera), {0, 1, 0}));
}

TEST_CASE("Positive yaw turns left, positive pitch looks up", "[camera]")
{
    Camera camera;
    camera.yawRadians = kPi / 2.0F;
    CHECK(approxEqual(forwardDirection(camera), {-1, 0, 0}));
    CHECK(approxEqual(rightDirection(camera), {0, 0, -1}));

    camera = Camera{};
    camera.pitchRadians = kPi / 4.0F;
    const Vec3 forward = forwardDirection(camera);
    CHECK(forward.y == Approx(std::sqrt(0.5F)));
    CHECK(forward.z == Approx(-std::sqrt(0.5F)));
}

TEST_CASE("Camera basis stays orthonormal for any orientation", "[camera]")
{
    for (float yaw = -3.0F; yaw <= 3.0F; yaw += 0.7F) {
        for (float pitch = -1.5F; pitch <= 1.5F; pitch += 0.5F) {
            Camera camera;
            camera.yawRadians = yaw;
            camera.pitchRadians = pitch;
            const Vec3 f = forwardDirection(camera);
            const Vec3 r = rightDirection(camera);
            const Vec3 u = upDirection(camera);
            CHECK(glm::length(f) == Approx(1.0F));
            CHECK(glm::length(r) == Approx(1.0F));
            CHECK(glm::length(u) == Approx(1.0F));
            CHECK(glm::dot(f, r) == Approx(0.0F).margin(kMargin));
            CHECK(glm::dot(f, u) == Approx(0.0F).margin(kMargin));
            CHECK(u.y >= 0.0F); // the image is never upside down
        }
    }
}

TEST_CASE("Pitch beyond straight up is clamped in the view", "[camera]")
{
    Camera camera;
    camera.pitchRadians = 10.0F;
    const Vec3 forward = forwardDirection(camera);
    CHECK(forward.y == Approx(std::sin(engine::camera::kMaxPitchRadians)));
    // Even an unclamped input produces a finite view matrix.
    const auto view = viewMatrix(camera);
    CHECK(std::isfinite(view[3][0]));
    CHECK(std::isfinite(view[0][0]));
}

TEST_CASE("The view matrix moves the camera to the origin looking along -Z", "[camera]")
{
    Camera camera;
    camera.position = {3.0F, 2.0F, 10.0F};
    camera.yawRadians = 0.6F;
    camera.pitchRadians = -0.3F;
    const auto view = viewMatrix(camera);

    CHECK(approxEqual(Vec3(view * Vec4(camera.position, 1.0F)), {0, 0, 0}));
    const Vec3 ahead = camera.position + forwardDirection(camera) * 5.0F;
    CHECK(approxEqual(Vec3(view * Vec4(ahead, 1.0F)), {0, 0, -5}));
    const Vec3 right = camera.position + rightDirection(camera) * 2.0F;
    CHECK(approxEqual(Vec3(view * Vec4(right, 1.0F)), {2, 0, 0}));
}

TEST_CASE("The projection maps near and far planes to depth -1 and 1", "[camera]")
{
    Camera camera;
    camera.nearPlane = 0.5F;
    camera.farPlane = 100.0F;
    const auto projection = projectionMatrix(camera, 16.0F / 9.0F);

    CHECK(toNdc(projection * Vec4(0, 0, -0.5F, 1)).z == Approx(-1.0F));
    CHECK(toNdc(projection * Vec4(0, 0, -100.0F, 1)).z == Approx(1.0F));

    // A point on the top edge of the vertical field of view lands at y = 1.
    const float top = std::tan(camera.verticalFovRadians / 2.0F) * 10.0F;
    CHECK(toNdc(projection * Vec4(0, top, -10.0F, 1)).y == Approx(1.0F));
    // The horizontal extent scales with the aspect ratio.
    CHECK(toNdc(projection * Vec4(top * 16.0F / 9.0F, 0, -10.0F, 1)).x == Approx(1.0F));
}

TEST_CASE("Degenerate projection parameters still give finite matrices", "[camera]")
{
    Camera camera;
    camera.nearPlane = 0.0F;
    camera.farPlane = -5.0F;
    camera.verticalFovRadians = std::numeric_limits<float>::quiet_NaN();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (const float aspect : {0.0F, -1.0F, nan, 1.5F}) {
        const auto projection = projectionMatrix(camera, aspect);
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                CHECK(std::isfinite(projection[column][row]));
            }
        }
    }
}

TEST_CASE("aspectRatio handles zero-sized framebuffers", "[camera]")
{
    CHECK(engine::camera::aspectRatio(1280, 720) == Approx(16.0F / 9.0F));
    CHECK(engine::camera::aspectRatio(0, 720) == 1.0F);
    CHECK(engine::camera::aspectRatio(1280, 0) == 1.0F);
    CHECK(engine::camera::aspectRatio(-5, -5) == 1.0F);
}
