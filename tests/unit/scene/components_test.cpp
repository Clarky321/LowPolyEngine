#include "engine/camera/Camera.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/DirectionalLightComponent.hpp"
#include "engine/scene/NameComponent.hpp"
#include "engine/scene/StaticMeshComponent.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

using engine::scene::CameraComponent;
using engine::scene::DirectionalLightComponent;
using engine::scene::NameComponent;
using engine::scene::StaticMeshComponent;
using engine::scene::TransformComponent;
using engine::scene::validate;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

template <typename T>
std::string errorOf(const T& component)
{
    const auto error = validate(component);
    REQUIRE(error.has_value());
    return error->message;
}

float above(float value)
{
    return std::nextafter(value, kInf);
}

float below(float value)
{
    return std::nextafter(value, -kInf);
}

} // namespace

TEST_CASE("Default components are valid", "[scene][components]")
{
    CHECK_FALSE(validate(NameComponent{"Lighthouse"}).has_value());
    CHECK_FALSE(validate(TransformComponent{}).has_value());
    CHECK_FALSE(validate(CameraComponent{}).has_value());
    CHECK_FALSE(validate(DirectionalLightComponent{}).has_value());

    // The default transform is the identity: no offset, no rotation, scale 1.
    const TransformComponent transform;
    CHECK(transform.position == engine::math::Vec3{0.0F});
    CHECK(transform.rotation.w == 1.0F);
    CHECK(transform.rotation.x == 0.0F);
    CHECK(transform.rotation.y == 0.0F);
    CHECK(transform.rotation.z == 0.0F);
    CHECK(transform.scale == engine::math::Vec3{1.0F});

    // The same lens as engine::camera::Camera.
    const engine::camera::Camera camera;
    const CameraComponent lens;
    CHECK(lens.verticalFovRadians == camera.verticalFovRadians);
    CHECK(lens.nearPlane == camera.nearPlane);
    CHECK(lens.farPlane == camera.farPlane);
}

TEST_CASE("A name follows the scene's name rules", "[scene][components]")
{
    CHECK_FALSE(validate(NameComponent{"Маяк 1"}).has_value());
    CHECK_FALSE(validate(NameComponent{std::string(256, 'a')}).has_value());

    CHECK(errorOf(NameComponent{}) == "name component: value: must not be empty");
    CHECK(errorOf(NameComponent{std::string(257, 'a')}) ==
          "name component: value: 257 bytes, at most 256 allowed");
    CHECK(errorOf(NameComponent{"ab\xC3"}) == "name component: value: invalid UTF-8 at byte 2");
    CHECK(errorOf(NameComponent{"a\tb"}) ==
          "name component: value: control character U+0009 at byte 1");
}

TEST_CASE("A transform rejects non-finite and out-of-range positions", "[scene][components]")
{
    constexpr float kMax = TransformComponent::kMaxCoordinate;
    TransformComponent transform;
    transform.position = {kMax, -kMax, 0.0F};
    CHECK_FALSE(validate(transform).has_value());

    transform.position = {kNaN, 0.0F, 0.0F};
    CHECK(errorOf(transform) == "transform component: position.x is nan, must be finite");
    transform.position = {0.0F, kInf, 0.0F};
    CHECK(errorOf(transform) == "transform component: position.y is inf, must be finite");
    transform.position = {0.0F, 0.0F, -kInf};
    CHECK(errorOf(transform) == "transform component: position.z is -inf, must be finite");
    transform.position = {above(kMax), 0.0F, 0.0F};
    CHECK(errorOf(transform) ==
          "transform component: position.x is 100000.01, must be in [-100000, 100000]");
    transform.position = {0.0F, 0.0F, below(-kMax)};
    CHECK(errorOf(transform).starts_with("transform component: position.z is -100000.01,"));
}

TEST_CASE("A transform accepts rotations close to unit length only", "[scene][components]")
{
    TransformComponent transform;
    transform.rotation = engine::math::Quat{0.0F, 0.0F, 1.0F, 0.0F};
    CHECK_FALSE(validate(transform).has_value());
    // Within the tolerance of 1e-3 on either side.
    transform.rotation = engine::math::Quat{1.0009F, 0.0F, 0.0F, 0.0F};
    CHECK_FALSE(validate(transform).has_value());
    transform.rotation = engine::math::Quat{0.0F, -0.9991F, 0.0F, 0.0F};
    CHECK_FALSE(validate(transform).has_value());
    // A hand-written 45 degree turn about +Y with four digits.
    transform.rotation = engine::math::Quat{0.7071F, 0.0F, 0.7071F, 0.0F};
    CHECK_FALSE(validate(transform).has_value());

    transform.rotation = engine::math::Quat{0.0F, 0.0F, 0.0F, 0.0F};
    CHECK(errorOf(transform) ==
          "transform component: rotation has length 0, must be within 0.001 of 1");
    transform.rotation = engine::math::Quat{2.0F, 0.0F, 0.0F, 0.0F};
    CHECK(errorOf(transform) ==
          "transform component: rotation has length 2, must be within 0.001 of 1");
    transform.rotation = engine::math::Quat{1.002F, 0.0F, 0.0F, 0.0F};
    CHECK(errorOf(transform).starts_with("transform component: rotation has length 1.00"));
    // Huge finite values do not overflow the length check.
    transform.rotation = engine::math::Quat{3.0e38F, 3.0e38F, 0.0F, 0.0F};
    CHECK(errorOf(transform).starts_with("transform component: rotation has length 4.24"));

    transform.rotation = engine::math::Quat{kNaN, 0.0F, 0.0F, 0.0F};
    CHECK(errorOf(transform) == "transform component: rotation.w is nan, must be finite");
    transform.rotation = engine::math::Quat{1.0F, kInf, 0.0F, 0.0F};
    CHECK(errorOf(transform) == "transform component: rotation.x is inf, must be finite");
    transform.rotation = engine::math::Quat{1.0F, 0.0F, -kInf, 0.0F};
    CHECK(errorOf(transform) == "transform component: rotation.y is -inf, must be finite");
    transform.rotation = engine::math::Quat{1.0F, 0.0F, 0.0F, kNaN};
    CHECK(errorOf(transform) == "transform component: rotation.z is nan, must be finite");
}

TEST_CASE("A transform accepts positive scales within the limits only", "[scene][components]")
{
    TransformComponent transform;
    transform.scale = {TransformComponent::kMinScale, TransformComponent::kMaxScale, 2.5F};
    CHECK_FALSE(validate(transform).has_value());

    transform.scale = {0.0F, 1.0F, 1.0F};
    CHECK(errorOf(transform) == "transform component: scale.x is 0, must be in [0.0001, 10000]");
    transform.scale = {1.0F, -1.0F, 1.0F};
    CHECK(errorOf(transform) == "transform component: scale.y is -1, must be in [0.0001, 10000]");
    transform.scale = {1.0F, 1.0F, below(TransformComponent::kMinScale)};
    CHECK(errorOf(transform).starts_with("transform component: scale.z is 9.99"));
    transform.scale = {above(TransformComponent::kMaxScale), 1.0F, 1.0F};
    CHECK(errorOf(transform).starts_with("transform component: scale.x is 10000.001,"));
    transform.scale = {1.0F, kNaN, 1.0F};
    CHECK(errorOf(transform) == "transform component: scale.y is nan, must be finite");
    transform.scale = {1.0F, 1.0F, kInf};
    CHECK(errorOf(transform) == "transform component: scale.z is inf, must be finite");
}

TEST_CASE("A static mesh needs a mesh; its material may be null", "[scene][components]")
{
    const engine::assets::AssetId mesh{
        engine::core::Uuid{0x1111111111114111ULL, 0x8111111111111111ULL}};
    const engine::assets::AssetId material{
        engine::core::Uuid{0x2222222222224222ULL, 0x8222222222222222ULL}};
    CHECK_FALSE(validate(StaticMeshComponent{mesh, material}).has_value());
    CHECK_FALSE(validate(StaticMeshComponent{mesh, {}}).has_value());
    CHECK(errorOf(StaticMeshComponent{}) ==
          "static mesh component: mesh is the null asset ID, must name a mesh asset");
    CHECK(errorOf(StaticMeshComponent{{}, material}) ==
          "static mesh component: mesh is the null asset ID, must name a mesh asset");
}

TEST_CASE("A camera's lens stays in the ranges of the projection", "[scene][components]")
{
    CameraComponent camera;
    camera.verticalFovRadians = CameraComponent::kMinVerticalFovRadians;
    CHECK_FALSE(validate(camera).has_value());
    camera.verticalFovRadians = CameraComponent::kMaxVerticalFovRadians;
    CHECK_FALSE(validate(camera).has_value());

    camera.verticalFovRadians = below(CameraComponent::kMinVerticalFovRadians);
    CHECK(errorOf(camera).starts_with("camera component: verticalFovRadians is 0.017453298,"));
    camera.verticalFovRadians = above(CameraComponent::kMaxVerticalFovRadians);
    CHECK(errorOf(camera) == "camera component: verticalFovRadians is 3.1241395, must be in "
                             "[0.0174533, 3.1241393]");
    camera.verticalFovRadians = 0.0F;
    CHECK(errorOf(camera).starts_with("camera component: verticalFovRadians is 0,"));
    camera.verticalFovRadians = kNaN;
    CHECK(errorOf(camera) == "camera component: verticalFovRadians is nan, must be finite");
    camera.verticalFovRadians = 1.0F;

    camera.nearPlane = CameraComponent::kMinNearPlane;
    CHECK_FALSE(validate(camera).has_value());
    camera.nearPlane = 0.0F;
    CHECK(errorOf(camera) == "camera component: nearPlane is 0, must be at least 0.001");
    camera.nearPlane = -1.0F;
    CHECK(errorOf(camera) == "camera component: nearPlane is -1, must be at least 0.001");
    camera.nearPlane = kInf;
    CHECK(errorOf(camera) == "camera component: nearPlane is inf, must be finite");
    camera.nearPlane = 1.0F;

    camera.farPlane = 1.001F;
    CHECK(errorOf(camera) ==
          "camera component: farPlane is 1.001, must be greater than nearPlane * 1.001 (1.001)");
    camera.farPlane = above(1.001F);
    CHECK_FALSE(validate(camera).has_value());
    camera.farPlane = 0.5F;
    CHECK(errorOf(camera).starts_with("camera component: farPlane is 0.5, must be greater"));
    camera.farPlane = kNaN;
    CHECK(errorOf(camera) == "camera component: farPlane is nan, must be finite");
    camera.farPlane = 1.0e30F;
    CHECK_FALSE(validate(camera).has_value());
    // A near plane so far away that nearPlane * 1.001 overflows has no valid far plane.
    camera.nearPlane = std::numeric_limits<float>::max();
    camera.farPlane = std::numeric_limits<float>::max();
    CHECK(errorOf(camera).starts_with("camera component: farPlane is"));
}

TEST_CASE("Every valid lens is used by the projection unchanged", "[scene][components]")
{
    // camera::projectionMatrix clamps or replaces values outside its ranges;
    // for the extremes a CameraComponent allows it must not change anything.
    const CameraComponent extremes[] = {
        {CameraComponent::kMinVerticalFovRadians, CameraComponent::kMinNearPlane,
         above(CameraComponent::kMinNearPlane * CameraComponent::kMinFarToNearRatio)},
        {CameraComponent::kMaxVerticalFovRadians, 2.0F, 1.0e30F},
        {1.0F, 1000.0F, above(1000.0F * CameraComponent::kMinFarToNearRatio)},
    };
    for (const CameraComponent& lens : extremes) {
        REQUIRE_FALSE(validate(lens).has_value());
        engine::camera::Camera camera;
        camera.verticalFovRadians = lens.verticalFovRadians;
        camera.nearPlane = lens.nearPlane;
        camera.farPlane = lens.farPlane;
        const engine::math::Mat4 expected =
            glm::perspective(lens.verticalFovRadians, 1.5F, lens.nearPlane, lens.farPlane);
        CHECK(engine::camera::projectionMatrix(camera, 1.5F) == expected);
    }
}

TEST_CASE("A directional light needs a direction and a non-negative color", "[scene][components]")
{
    DirectionalLightComponent light;
    light.direction = {0.0F, -1.0e-30F, 0.0F};
    CHECK_FALSE(validate(light).has_value());
    light.direction = {3.0e38F, 3.0e38F, -3.0e38F};
    CHECK_FALSE(validate(light).has_value());

    light.direction = {0.0F, 0.0F, 0.0F};
    CHECK(errorOf(light) ==
          "directional light component: direction is (0, 0, 0), must not be zero");
    light.direction = {-0.0F, 0.0F, -0.0F};
    CHECK(errorOf(light).ends_with("must not be zero"));
    light.direction = {kNaN, -1.0F, 0.0F};
    CHECK(errorOf(light) == "directional light component: direction.x is nan, must be finite");
    light.direction = {0.0F, -kInf, 0.0F};
    CHECK(errorOf(light) == "directional light component: direction.y is -inf, must be finite");
    light.direction = {0.0F, -1.0F, kInf};
    CHECK(errorOf(light) == "directional light component: direction.z is inf, must be finite");
    light.direction = {0.0F, -1.0F, 0.0F};

    light.color = {0.0F, 0.0F, 0.0F};
    light.intensity = 0.0F;
    CHECK_FALSE(validate(light).has_value());
    light.color = {-0.5F, 1.0F, 1.0F};
    CHECK(errorOf(light) == "directional light component: color.r is -0.5, must be at least 0");
    light.color = {1.0F, kNaN, 1.0F};
    CHECK(errorOf(light) == "directional light component: color.g is nan, must be finite");
    light.color = {1.0F, 1.0F, kInf};
    CHECK(errorOf(light) == "directional light component: color.b is inf, must be finite");
    light.color = {1.0F, 0.9F, 0.8F};

    light.intensity = -1.0F;
    CHECK(errorOf(light) == "directional light component: intensity is -1, must be at least 0");
    light.intensity = kInf;
    CHECK(errorOf(light) == "directional light component: intensity is inf, must be finite");
    light.intensity = 4.0F;
    CHECK_FALSE(validate(light).has_value());

    // Finite on their own, but the renderer's color * intensity would overflow.
    light.color = {3.0e38F, 0.0F, 0.0F};
    light.intensity = 10.0F;
    CHECK(errorOf(light) ==
          "directional light component: color * intensity is (inf, 0, 0), must be finite");
}
