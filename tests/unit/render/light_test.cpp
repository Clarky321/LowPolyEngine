// The light description and the normal matrix are checked without a GPU
// (engine/src/render/Lighting.cpp, ADR 0011).

#include "render/Lighting.hpp"

#include "engine/render/Renderer.hpp"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <optional>
#include <string>

using engine::math::Mat4;
using engine::math::Vec3;
using engine::render::LightDesc;
using engine::render::normalMatrix;
using engine::render::validate;

namespace {

bool mentions(const std::optional<engine::core::Error>& error, const std::string& text)
{
    return error.has_value() && error->message.find(text) != std::string::npos;
}

bool near(const Vec3& actual, const Vec3& expected, float tolerance = 1.0e-5F)
{
    return glm::length(actual - expected) <= tolerance;
}

// The world-space direction of a normal after the model matrix.
Vec3 transformedNormal(const Mat4& model, const Vec3& normal)
{
    return glm::normalize(normalMatrix(model) * normal);
}

} // namespace

TEST_CASE("The default light leaves lit surfaces unchanged", "[render][light]")
{
    const LightDesc light;
    CHECK(light.direction == Vec3{0.0F, -1.0F, 0.0F});
    CHECK(light.color == Vec3{0.0F});
    CHECK(light.ambient == Vec3{1.0F});
    CHECK_FALSE(validate(light).has_value());
}

TEST_CASE("Any non-zero direction and non-negative colors are valid", "[render][light]")
{
    // The direction is normalized by the renderer, so its length is free.
    CHECK_FALSE(validate(LightDesc{.direction = {0.001F, 0.0F, 0.0F}}).has_value());
    CHECK_FALSE(validate(LightDesc{.direction = {-3.0F, -40.0F, 7.0F}}).has_value());
    // Above 1 is allowed; the result saturates.
    CHECK_FALSE(validate(LightDesc{.color = Vec3{2.0F}, .ambient = Vec3{0.0F}}).has_value());
}

TEST_CASE("Zero or non-finite directions and bad colors are rejected", "[render][light]")
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    CHECK(mentions(validate(LightDesc{.direction = Vec3{0.0F}}),
                   "light direction is (0, 0, 0); it must be finite and not zero"));
    CHECK(
        mentions(validate(LightDesc{.direction = {nan, -1.0F, 0.0F}}), "light direction is (nan"));
    CHECK(mentions(validate(LightDesc{.direction = {0.0F, -infinity, 0.0F}}), "-inf"));
    // Too short to normalize: the squared length underflows to 0.
    CHECK(mentions(validate(LightDesc{.direction = {1.0e-30F, 0.0F, 0.0F}}), "not zero"));

    CHECK(mentions(validate(LightDesc{.color = {1.0F, -0.5F, 0.0F}}), "light color green is -0.5"));
    CHECK(mentions(validate(LightDesc{.ambient = {0.0F, 0.0F, infinity}}),
                   "light ambient blue is inf"));
    CHECK(mentions(validate(LightDesc{.ambient = {nan, 0.0F, 0.0F}}),
                   "components must be finite and >= 0"));
    // The direction is checked first, then color, then ambient.
    CHECK(
        mentions(validate(LightDesc{.direction = Vec3{0.0F}, .color = Vec3{-1.0F}}), "direction"));
    CHECK(mentions(validate(LightDesc{.color = Vec3{-1.0F}, .ambient = Vec3{-1.0F}}), "color"));
}

TEST_CASE("Translation, rotation and uniform scale keep normals as they are", "[render][light]")
{
    const Vec3 normal = glm::normalize(Vec3{1.0F, 2.0F, 3.0F});
    CHECK(near(transformedNormal(Mat4{1.0F}, normal), normal));
    CHECK(near(transformedNormal(glm::translate(Mat4{1.0F}, Vec3{5.0F, -2.0F, 9.0F}), normal),
               normal));
    CHECK(near(transformedNormal(glm::scale(Mat4{1.0F}, Vec3{3.0F}), normal), normal));

    // Rotated normals turn with the object: +X turned 90 degrees about +Y is -Z.
    const Mat4 rotation = glm::rotate(Mat4{1.0F}, glm::radians(90.0F), Vec3{0.0F, 1.0F, 0.0F});
    CHECK(near(transformedNormal(rotation, Vec3{1.0F, 0.0F, 0.0F}), Vec3{0.0F, 0.0F, -1.0F}));
}

TEST_CASE("Normals stay perpendicular to their faces under non-uniform scale", "[render][light]")
{
    // The face x + z = 0 has the normal (1, 0, 1) / sqrt(2). Stretched 2x along
    // X it becomes x / 2 + z = 0 with the normal (1, 0, 2) / sqrt(5); the model
    // matrix itself would give (2, 0, 1) / sqrt(5).
    const Mat4 stretch = glm::scale(Mat4{1.0F}, Vec3{2.0F, 1.0F, 1.0F});
    const Vec3 normal = glm::normalize(Vec3{1.0F, 0.0F, 1.0F});
    CHECK(near(transformedNormal(stretch, normal), glm::normalize(Vec3{1.0F, 0.0F, 2.0F})));

    // Any mix of scale, rotation and shear: the normal stays perpendicular to
    // every direction within the transformed face.
    Mat4 model = glm::rotate(Mat4{1.0F}, 0.7F, glm::normalize(Vec3{1.0F, 1.0F, 0.0F}));
    model = glm::scale(model, Vec3{0.5F, 3.0F, 1.5F});
    model[1][0] = 0.8F; // shear
    const Vec3 faceNormal = glm::normalize(Vec3{0.3F, -0.5F, 0.8F});
    const Vec3 along = glm::normalize(glm::cross(faceNormal, Vec3{1.0F, 0.0F, 0.0F}));
    const Vec3 across = glm::cross(faceNormal, along);
    const Vec3 transformed = transformedNormal(model, faceNormal);
    const Vec3 transformedAlong{model * engine::math::Vec4{along, 0.0F}};
    const Vec3 transformedAcross{model * engine::math::Vec4{across, 0.0F}};
    CHECK(std::abs(glm::dot(transformed, transformedAlong)) < 1.0e-5F);
    CHECK(std::abs(glm::dot(transformed, transformedAcross)) < 1.0e-5F);
}

TEST_CASE("Mirrored and flattened models keep sensible normals", "[render][light]")
{
    // Mirrored in X, the face that pointed to +X now points to -X.
    const Mat4 mirror = glm::scale(Mat4{1.0F}, Vec3{-1.0F, 1.0F, 1.0F});
    CHECK(near(transformedNormal(mirror, Vec3{1.0F, 0.0F, 0.0F}), Vec3{-1.0F, 0.0F, 0.0F}));
    CHECK(near(transformedNormal(mirror, Vec3{0.0F, 1.0F, 0.0F}), Vec3{0.0F, 1.0F, 0.0F}));

    // Flattened along Z (determinant 0): a face in the XY plane keeps facing +Z
    // instead of turning into infinities.
    const Mat4 flat = glm::scale(Mat4{1.0F}, Vec3{2.0F, 3.0F, 0.0F});
    const Vec3 normal = normalMatrix(flat) * Vec3{0.0F, 0.0F, 1.0F};
    CHECK(std::isfinite(normal.x));
    CHECK(near(glm::normalize(normal), Vec3{0.0F, 0.0F, 1.0F}));
}
