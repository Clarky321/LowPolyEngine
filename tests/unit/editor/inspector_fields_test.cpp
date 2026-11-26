// What the inspector edits (editor/panels/InspectorFields, M5.8), headless:
// each group's values read from a component and written back with every other
// value's bits unchanged, the asset lists, the components the inspector adds
// and the values they start with.

#include "DocumentTestSupport.hpp"

#include "editor/panels/EulerAngles.hpp"
#include "editor/panels/InspectorFields.hpp"

#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/DirectionalLightComponent.hpp"
#include "engine/scene/NameComponent.hpp"
#include "engine/scene/ScriptComponent.hpp"
#include "engine/scene/StaticMeshComponent.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <glm/trigonometric.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <variant>
#include <vector>

using editor::ComponentType;
using editor::ComponentValue;
using editor::FieldGroup;
using editor::FieldValues;
using editor::fieldValues;
using editor::withFieldValues;
using engine::math::Quat;
using engine::math::Vec3;
using engine::scene::CameraComponent;
using engine::scene::DirectionalLightComponent;
using engine::scene::TransformComponent;

namespace {

TransformComponent sampleTransform()
{
    TransformComponent transform;
    transform.position = Vec3{1.25F, -2.5F, 1.0e3F};
    // Stored normalized, but with bits a round trip through angles would not
    // give back.
    transform.rotation = Quat{0.976296F, 0.0F, 0.0F, 0.21643962F};
    transform.scale = Vec3{3.0F, 6.0F, 0.5F};
    return transform;
}

} // namespace

TEST_CASE("Field groups belong to their component and have one or three values",
          "[editor][inspector]")
{
    CHECK(editor::componentTypeOf(FieldGroup::Position) == ComponentType::Transform);
    CHECK(editor::componentTypeOf(FieldGroup::Rotation) == ComponentType::Transform);
    CHECK(editor::componentTypeOf(FieldGroup::Scale) == ComponentType::Transform);
    CHECK(editor::componentTypeOf(FieldGroup::VerticalFov) == ComponentType::Camera);
    CHECK(editor::componentTypeOf(FieldGroup::FarPlane) == ComponentType::Camera);
    CHECK(editor::componentTypeOf(FieldGroup::Direction) == ComponentType::DirectionalLight);
    CHECK(editor::componentTypeOf(FieldGroup::Intensity) == ComponentType::DirectionalLight);
    CHECK(editor::fieldCount(FieldGroup::Color) == 3);
    CHECK(editor::fieldCount(FieldGroup::NearPlane) == 1);
    std::size_t total = 0;
    for (const FieldGroup group : editor::kFieldGroups) {
        total += editor::fieldCount(group);
    }
    // The inspector's spin boxes.
    CHECK(total == 19);
}

TEST_CASE("A transform's groups are read, and written back without touching the rest",
          "[editor][inspector]")
{
    const TransformComponent stored = sampleTransform();
    const ComponentValue value{stored};
    CHECK(fieldValues(value, FieldGroup::Position) == FieldValues{1.25, -2.5, 1000.0});
    CHECK(fieldValues(value, FieldGroup::Scale) == FieldValues{3.0, 6.0, 0.5});
    const FieldValues angles = fieldValues(value, FieldGroup::Rotation);
    CHECK(std::abs(angles[0]) < 1.0e-6);
    CHECK(std::abs(angles[1]) < 1.0e-6);
    CHECK(std::abs(angles[2] - 25.0) < 1.0e-3);

    // A new position: the rotation's and the scale's bits stay as stored.
    const ComponentValue moved = withFieldValues(value, FieldGroup::Position, {4.0, 5.0, 6.0});
    const auto& movedTransform = std::get<TransformComponent>(moved);
    CHECK(movedTransform.position == Vec3{4.0F, 5.0F, 6.0F});
    CHECK(movedTransform.rotation == stored.rotation);
    CHECK(movedTransform.scale == stored.scale);

    // A new rotation is made from the three angles, the rest untouched.
    const ComponentValue turned = withFieldValues(value, FieldGroup::Rotation, {10.0, 20.0, 30.0});
    const auto& turnedTransform = std::get<TransformComponent>(turned);
    CHECK(turnedTransform.rotation == editor::quatFromEulerDegrees(Vec3{10.0F, 20.0F, 30.0F}));
    CHECK(turnedTransform.position == stored.position);
    CHECK(turnedTransform.scale == stored.scale);

    // Writing back what was read keeps position and scale exactly.
    const ComponentValue same = withFieldValues(
        withFieldValues(value, FieldGroup::Position, fieldValues(value, FieldGroup::Position)),
        FieldGroup::Scale, fieldValues(value, FieldGroup::Scale));
    CHECK(same == value);

    // Other components' groups change nothing; a transform's group on another
    // component reads zeros.
    CHECK(withFieldValues(value, FieldGroup::Intensity, {9.0, 0.0, 0.0}) == value);
    const ComponentValue camera{CameraComponent{}};
    CHECK(fieldValues(camera, FieldGroup::Position) == FieldValues{0.0, 0.0, 0.0});
    CHECK(withFieldValues(camera, FieldGroup::Position, {1.0, 2.0, 3.0}) == camera);
}

TEST_CASE("A camera's angle is shown in degrees, its planes as they are", "[editor][inspector]")
{
    CameraComponent stored;
    stored.verticalFovRadians = glm::radians(75.0F);
    stored.nearPlane = 0.25F;
    stored.farPlane = 2500.0F;
    const ComponentValue value{stored};
    CHECK(std::abs(fieldValues(value, FieldGroup::VerticalFov)[0] - 75.0) < 1.0e-4);
    CHECK(fieldValues(value, FieldGroup::NearPlane)[0] == 0.25);
    CHECK(fieldValues(value, FieldGroup::FarPlane)[0] == 2500.0);

    const auto wider = std::get<CameraComponent>(
        withFieldValues(value, FieldGroup::VerticalFov, {90.0, 0.0, 0.0}));
    CHECK(wider.verticalFovRadians == glm::radians(90.0F));
    CHECK(wider.nearPlane == stored.nearPlane);
    CHECK(wider.farPlane == stored.farPlane);
    const auto nearer =
        std::get<CameraComponent>(withFieldValues(value, FieldGroup::NearPlane, {0.5, 0.0, 0.0}));
    CHECK(nearer.nearPlane == 0.5F);
    CHECK(nearer.verticalFovRadians == stored.verticalFovRadians);
}

TEST_CASE("A light's direction, color and intensity", "[editor][inspector]")
{
    DirectionalLightComponent stored;
    stored.direction = Vec3{0.0F, -0.6F, -0.8F};
    stored.color = Vec3{1.0F, 0.86F, 0.7F};
    stored.intensity = 0.6F;
    const ComponentValue value{stored};
    CHECK(fieldValues(value, FieldGroup::Color) ==
          FieldValues{1.0, static_cast<double>(0.86F), static_cast<double>(0.7F)});
    CHECK(fieldValues(value, FieldGroup::Intensity)[0] == static_cast<double>(0.6F));

    const auto aimed = std::get<DirectionalLightComponent>(
        withFieldValues(value, FieldGroup::Direction, {1.0, -1.0, 0.0}));
    // Not normalized here: the scene stores directions normalized.
    CHECK(aimed.direction == Vec3{1.0F, -1.0F, 0.0F});
    CHECK(aimed.color == stored.color);
    CHECK(aimed.intensity == stored.intensity);
    // A value the component refuses is written all the same: validate decides.
    const auto dark = std::get<DirectionalLightComponent>(
        withFieldValues(value, FieldGroup::Intensity, {-1.0, 0.0, 0.0}));
    CHECK(dark.intensity == -1.0F);
    CHECK(engine::scene::validate(dark).has_value());
}

TEST_CASE("The asset lists hold the registry's assets of a type, in path order",
          "[editor][inspector]")
{
    const engine::project::Project project = lpe_test::memoryProject();
    using engine::assets::AssetType;
    const auto meshes = editor::assetChoices(project.assets, AssetType::Mesh);
    REQUIRE(meshes.size() == 2);
    CHECK(meshes[0] == editor::AssetChoice{lpe_test::kMesh, "models/a.obj"});
    CHECK(meshes[1] == editor::AssetChoice{lpe_test::kOtherMesh, "models/b.obj"});
    const auto scripts = editor::assetChoices(project.assets, AssetType::Script);
    REQUIRE(scripts.size() == 1);
    CHECK(scripts[0].id == lpe_test::kScript);
    CHECK(editor::assetChoices(project.assets, AssetType::Texture).empty());
}

TEST_CASE("The inspector adds the editable components an entity lacks, with their defaults",
          "[editor][inspector]")
{
    engine::project::Project project = lpe_test::memoryProject();
    editor::EditorDocument document(std::move(project));
    const auto entity = lpe_test::create(document, {lpe_test::name("Lamp"), lpe_test::at(1.0F)});
    CHECK(editor::addableComponents(document.scene(), entity) ==
          std::vector<ComponentType>{ComponentType::Camera, ComponentType::DirectionalLight,
                                     ComponentType::StaticMesh, ComponentType::Script});
    CHECK(editor::addableComponents(document.scene(), engine::scene::EntityId{}).empty());

    const auto& assets = document.assets();
    auto name = editor::newComponent(ComponentType::Name, assets);
    REQUIRE(name.hasValue());
    CHECK(std::get<engine::scene::NameComponent>(name.value()).value == "Entity");
    auto transform = editor::newComponent(ComponentType::Transform, assets);
    REQUIRE(transform.hasValue());
    CHECK(std::get<TransformComponent>(transform.value()) == TransformComponent{});
    auto camera = editor::newComponent(ComponentType::Camera, assets);
    REQUIRE(camera.hasValue());
    CHECK(std::get<CameraComponent>(camera.value()) == CameraComponent{});
    auto light = editor::newComponent(ComponentType::DirectionalLight, assets);
    REQUIRE(light.hasValue());
    CHECK(std::get<DirectionalLightComponent>(light.value()) == DirectionalLightComponent{});
    // The first mesh in path order, the default material.
    auto mesh = editor::newComponent(ComponentType::StaticMesh, assets);
    REQUIRE(mesh.hasValue());
    const auto& staticMesh = std::get<engine::scene::StaticMeshComponent>(mesh.value());
    CHECK(staticMesh.mesh == lpe_test::kMesh);
    CHECK(staticMesh.material.isNull());
    auto script = editor::newComponent(ComponentType::Script, assets);
    REQUIRE(script.hasValue());
    CHECK(std::get<engine::scene::ScriptComponent>(script.value()) ==
          engine::scene::ScriptComponent{lpe_test::kScript, true});
    // Every default is a value its component accepts.
    for (const ComponentType type : editor::kEditableComponents) {
        auto value = editor::newComponent(type, assets);
        REQUIRE(value.hasValue());
        CHECK_FALSE(document
                        .execute(std::make_unique<editor::AddComponent>(lpe_test::create(document),
                                                                        std::move(value).value()))
                        .has_value());
    }

    // Parents and prefab instances are not added here; without assets of
    // their type, no mesh and no script either.
    CHECK_FALSE(editor::newComponent(ComponentType::Parent, assets).hasValue());
    CHECK_FALSE(editor::newComponent(ComponentType::PrefabInstance, assets).hasValue());
    const engine::assets::AssetRegistry none(std::filesystem::path("empty-project"));
    auto noMesh = editor::newComponent(ComponentType::StaticMesh, none);
    REQUIRE_FALSE(noMesh.hasValue());
    CHECK(noMesh.error().message == "a static mesh needs a mesh asset, and the project has none");
    CHECK_FALSE(editor::newComponent(ComponentType::Script, none).hasValue());
}
