// The inspector panel (editor/src/panels/InspectorPanel, M5.8), headless:
// Raven's null backend and host platform with the editor's shell layout, a
// document in memory, and clicks, keys, text and drags injected as the SDL3
// host injects them; frames run as in the editor (input, the panel's update,
// Raven's frame). Every field reaches the scene through one command; a
// continuous edit is one undo step; a refused value leaves the scene as it
// was, shows the stored value again and the component's message under its
// section; undo restores the exact stored value.

#include "../ui/RavenSession.hpp"
#include "DocumentTestSupport.hpp"

#include "application/ShellLayout.hpp"
#include "editor/document/EditorDocument.hpp"
#include "editor/panels/EulerAngles.hpp"
#include "editor/panels/InspectorFields.hpp"
#include "panels/InspectorPanel.hpp"

#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/DirectionalLightComponent.hpp"
#include "engine/scene/NameComponent.hpp"
#include "engine/scene/ParentComponent.hpp"
#include "engine/scene/PrefabInstanceComponent.hpp"
#include "engine/scene/ScriptComponent.hpp"
#include "engine/scene/StaticMeshComponent.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <glm/ext/quaternion_geometric.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <catch2/catch_test_macros.hpp>

#include <Raven.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

using editor::ComponentType;
using editor::InspectorPanel;
using engine::math::Quat;
using engine::math::Vec3;
using engine::scene::CameraComponent;
using engine::scene::DirectionalLightComponent;
using engine::scene::EntityId;
using engine::scene::NameComponent;
using engine::scene::ParentComponent;
using engine::scene::ScriptComponent;
using engine::scene::StaticMeshComponent;
using engine::scene::TransformComponent;
using lpe_test::RavenErrors;
using lpe_test::RavenSession;

// The rotation the test project's floating lighthouse stores: 25 degrees
// about Z, with the bits M4 wrote.
const Quat kStoredRotation{0.976296F, 0.0F, 0.0F, 0.21643962F};

struct Entities {
    EntityId tower;
    EntityId lamp;
    EntityId camera;
    EntityId sun;
    EntityId brain;
    EntityId instance;
    EntityId unnamed;
};

class Fixture {
public:
    explicit Fixture(bool withDocument = true)
    {
        REQUIRE(session.started());
        const std::string_view xml = editor::layouts::shell();
        window = Rwindow_create_from_xml(xml.data(), xml.size());
        REQUIRE(window != R_INVALID_ID);
        Rwindow_show(window);
        Rhost_window_set_size(window, 1280, 760, 1.0F);
        Rhost_inject_focus(window, R_TRUE);
        Rdraw_frame();

        engine::project::Project project = lpe_test::memoryProject();
        engine::scene::Scene& scene = project.startScene;
        e.tower = add(scene, "Tower");
        e.lamp = add(scene, "Lamp");
        TransformComponent lamp;
        lamp.position = Vec3{1.0F, 2.0F, 3.0F};
        lamp.rotation = kStoredRotation;
        lamp.scale = Vec3{1.0F, 2.0F, 1.0F};
        REQUIRE_FALSE(scene.addComponent(e.lamp, lamp));
        REQUIRE_FALSE(scene.addComponent(e.lamp, StaticMeshComponent{lpe_test::kMesh, {}}));
        REQUIRE_FALSE(scene.addComponent(e.lamp, ParentComponent{e.tower}));
        e.camera = add(scene, "Camera");
        REQUIRE_FALSE(scene.addComponent(e.camera, CameraComponent{}));
        e.sun = add(scene, "Sun");
        REQUIRE_FALSE(scene.addComponent(e.sun, DirectionalLightComponent{}));
        e.brain = add(scene, "Brain");
        REQUIRE_FALSE(scene.addComponent(e.brain, ScriptComponent{lpe_test::kScript, true}));
        e.instance = add(scene, "Instance");
        REQUIRE_FALSE(scene.addComponent(
            e.instance,
            engine::scene::PrefabInstanceComponent{
                lpe_test::kPrefab,
                engine::scene::PrefabInstanceId{engine::core::Uuid{0x1234ULL, 0x5678ULL}}, 1}));
        e.unnamed = scene.createEntity().value();
        REQUIRE_FALSE(scene.addComponent(e.unnamed, TransformComponent{}));
        document = std::make_unique<editor::EditorDocument>(std::move(project));

        const auto error = panel.attach(window, withDocument ? document.get() : nullptr);
        INFO((error ? error->message : std::string()));
        REQUIRE_FALSE(error.has_value());
        frame();
    }

    ~Fixture()
    {
        panel.detach();
        session.stop();
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    Fixture(Fixture&&) = delete;
    Fixture& operator=(Fixture&&) = delete;

    void frame()
    {
        panel.update();
        Rdraw_frame();
    }

    // Selects an entity as the hierarchy or the viewport would.
    void select(EntityId entity)
    {
        document->selection().select(entity);
        frame();
    }

    [[nodiscard]] Rid widget(const char* name) const
    {
        const Rid found = Rwidget_find(window, name);
        REQUIRE(found != R_INVALID_ID);
        return found;
    }
    [[nodiscard]] bool shown(const char* name) const
    {
        return Rwidget_get_visibility(widget(name)) == R_VISIBLE;
    }
    [[nodiscard]] double value(const char* spin) const { return Rspin_box_get_value(widget(spin)); }
    [[nodiscard]] std::string text(const char* label) const
    {
        const char* t = Rlabel_get_text(widget(label));
        return t != nullptr ? t : "";
    }

    struct Point {
        int x = 0;
        int y = 0;
    };
    [[nodiscard]] Point centreOf(const char* name) const
    {
        float x = 0.0F;
        float y = 0.0F;
        float width = 0.0F;
        float height = 0.0F;
        Rwidget_get_rect(widget(name), &x, &y, &width, &height);
        REQUIRE(width > 0.0F);
        return {static_cast<int>(x + width * 0.5F), static_cast<int>(y + height * 0.5F)};
    }

    // A left click in the middle of a widget. A middle click on the (empty)
    // hierarchy first: two quick left clicks on one spot would be a double
    // click.
    void click(const char* name)
    {
        const Point elsewhere = centreOf("hierarchy-tree");
        Rhost_inject_mouse_button(window, elsewhere.x, elsewhere.y, R_MOUSE_MIDDLE, R_TRUE, 0);
        Rhost_inject_mouse_button(window, elsewhere.x, elsewhere.y, R_MOUSE_MIDDLE, R_FALSE, 0);
        const Point at = centreOf(name);
        Rhost_inject_mouse_move(window, at.x, at.y, 0);
        Rhost_inject_mouse_button(window, at.x, at.y, R_MOUSE_LEFT, R_TRUE, 0);
        Rhost_inject_mouse_button(window, at.x, at.y, R_MOUSE_LEFT, R_FALSE, 0);
    }

    void key(RKey which, bool repeat = false)
    {
        Rhost_inject_key(window, which, R_TRUE, 0, repeat ? R_TRUE : R_FALSE);
        Rhost_inject_key(window, which, R_FALSE, 0, R_FALSE);
    }

    void type(std::string_view typed)
    {
        for (const char letter : typed) {
            Rhost_inject_text(window,
                              static_cast<std::uint32_t>(static_cast<unsigned char>(letter)));
        }
    }

    // A click on a spin box (which starts typing, its value selected), the
    // text, and Enter; then a frame.
    void typeInto(const char* spin, std::string_view typed)
    {
        click(spin);
        type(typed);
        key(R_KEY_ENTER);
        frame();
    }

    // A drag sideways from the middle of a spin box by `pixels`, in four moves.
    void drag(const char* spin, int pixels)
    {
        const Point from = centreOf(spin);
        Rhost_inject_mouse_move(window, from.x, from.y, 0);
        Rhost_inject_mouse_button(window, from.x, from.y, R_MOUSE_LEFT, R_TRUE, 0);
        for (int step = 1; step <= 4; ++step) {
            Rhost_inject_mouse_move(window, from.x + pixels * step / 4, from.y, 0);
        }
        Rhost_inject_mouse_button(window, from.x + pixels, from.y, R_MOUSE_LEFT, R_FALSE, 0);
        frame();
    }

    template <typename T>
    [[nodiscard]] T stored(EntityId entity) const
    {
        auto component = document->scene().component<T>(entity);
        REQUIRE(component.hasValue());
        return component.value();
    }

    RavenSession session;
    Rid window = R_INVALID_ID;
    std::unique_ptr<editor::EditorDocument> document;
    Entities e;
    InspectorPanel panel;

private:
    static EntityId add(engine::scene::Scene& scene, std::string name)
    {
        auto created = scene.createEntity();
        REQUIRE(created.hasValue());
        REQUIRE_FALSE(scene.addComponent(created.value(), NameComponent{std::move(name)}));
        return created.value();
    }
};

void succeed(const std::optional<engine::core::Error>& error)
{
    INFO((error ? error->message : std::string()));
    REQUIRE_FALSE(error.has_value());
}

} // namespace

TEST_CASE("With nothing selected, or no project, the inspector says so", "[editor][inspector][ui]")
{
    SECTION("A project, nothing selected")
    {
        Fixture f;
        CHECK(f.shown("inspector-empty"));
        CHECK_FALSE(f.shown("inspector-entity"));
        CHECK_FALSE(f.panel.entity().has_value());
        f.select(f.e.lamp);
        CHECK_FALSE(f.shown("inspector-empty"));
        CHECK(f.shown("inspector-entity"));
        // The entity goes: the inspector is empty again.
        succeed(f.document->execute(std::make_unique<editor::DeleteEntity>(f.e.lamp)));
        f.frame();
        CHECK(f.shown("inspector-empty"));
        CHECK_FALSE(f.panel.entity().has_value());
    }
    SECTION("No project")
    {
        Fixture f(false);
        CHECK(f.shown("inspector-empty"));
        CHECK_FALSE(f.shown("inspector-entity"));
        f.document->selection().select(f.e.lamp);
        f.frame();
        CHECK(f.shown("inspector-empty"));
    }
    CHECK(RavenErrors::instance().count() == 0);
}

TEST_CASE("The inspector shows a section for each of the entity's components",
          "[editor][inspector][ui]")
{
    Fixture f;
    f.select(f.e.lamp);
    REQUIRE(f.panel.entity() == f.e.lamp);
    CHECK(f.text("inspector-id") == "Entity " + f.e.lamp.toString());
    CHECK(f.shown("inspector-section-name"));
    CHECK(f.shown("inspector-section-transform"));
    CHECK(f.shown("inspector-section-mesh"));
    CHECK(f.shown("inspector-section-parent"));
    CHECK_FALSE(f.shown("inspector-section-camera"));
    CHECK_FALSE(f.shown("inspector-section-light"));
    CHECK_FALSE(f.shown("inspector-section-script"));
    CHECK_FALSE(f.shown("inspector-section-prefab"));

    CHECK(std::string(Rtext_input_get_text(f.widget("inspector-name"))) == "Lamp");
    CHECK(f.value("inspector-position-x") == 1.0);
    CHECK(f.value("inspector-position-z") == 3.0);
    CHECK(std::abs(f.value("inspector-rotation-z") - 25.0) < 1.0e-3);
    CHECK(f.value("inspector-rotation-x") == 0.0);
    CHECK(f.value("inspector-scale-y") == 2.0);
    // The mesh list: a.obj, b.obj; the material list starts with "default".
    CHECK(Rcombo_box_get_item_count(f.widget("inspector-mesh")) == 2);
    CHECK(Rcombo_box_get_selected(f.widget("inspector-mesh")) == 0);
    CHECK(std::string(Rcombo_box_get_item_text(f.widget("inspector-material"), 0)) == "default");
    CHECK(Rcombo_box_get_selected(f.widget("inspector-material")) == 0);
    CHECK(f.text("inspector-parent") == "Parent: Tower");
    // The Add list: what it lacks, in the inspector's order.
    const Rid add = f.widget("inspector-add");
    REQUIRE(Rcombo_box_get_item_count(add) == 3);
    CHECK(std::string(Rcombo_box_get_item_text(add, 0)) == "Camera");
    CHECK(std::string(Rcombo_box_get_item_text(add, 1)) == "Directional light");
    CHECK(std::string(Rcombo_box_get_item_text(add, 2)) == "Script");

    f.select(f.e.camera);
    CHECK(f.shown("inspector-section-camera"));
    CHECK_FALSE(f.shown("inspector-section-transform"));
    CHECK(std::abs(f.value("inspector-fov") - 60.0) < 1.0e-4);
    CHECK(f.value("inspector-near") == static_cast<double>(0.1F));
    CHECK(f.value("inspector-far") == 1000.0);

    f.select(f.e.sun);
    CHECK(f.shown("inspector-section-light"));
    CHECK(f.value("inspector-direction-y") == -1.0);
    CHECK(f.value("inspector-color-g") == 1.0);
    CHECK(f.value("inspector-intensity") == 1.0);

    f.select(f.e.brain);
    CHECK(f.shown("inspector-section-script"));
    CHECK(Rcombo_box_get_selected(f.widget("inspector-script")) == 0);
    CHECK(Rcheckbox_is_checked(f.widget("inspector-script-enabled")) != R_FALSE);

    f.select(f.e.instance);
    CHECK(f.shown("inspector-section-prefab"));
    CHECK(f.text("inspector-prefab") ==
          "Prefab instance of prefabs/a.prefab.json, entity 1 (edited with prefabs in M6)");

    // Without a name: no name section.
    f.select(f.e.unnamed);
    CHECK_FALSE(f.shown("inspector-section-name"));
    CHECK(f.shown("inspector-section-transform"));
    CHECK(RavenErrors::instance().count() == 0);
}

TEST_CASE("A typed value is one undo step; undo restores the exact stored value",
          "[editor][inspector][ui]")
{
    Fixture f;
    f.select(f.e.lamp);
    const TransformComponent before = f.stored<TransformComponent>(f.e.lamp);

    f.typeInto("inspector-position-x", "12.5");
    const TransformComponent after = f.stored<TransformComponent>(f.e.lamp);
    CHECK(after.position == Vec3{12.5F, 2.0F, 3.0F});
    // Only the position: the rotation keeps its stored bits.
    CHECK(after.rotation == kStoredRotation);
    CHECK(after.scale == before.scale);
    CHECK(f.document->undoCount() == 1);
    CHECK(f.document->undoName() == "Change transform");
    CHECK(f.value("inspector-position-x") == 12.5);

    REQUIRE_FALSE(f.document->undo());
    f.frame();
    CHECK(f.stored<TransformComponent>(f.e.lamp) == before);
    // The field shows the stored value once the keyboard leaves it.
    f.click("inspector-name");
    f.frame();
    CHECK(f.value("inspector-position-x") == 1.0);
    CHECK(RavenErrors::instance().count() == 0);
}

TEST_CASE("A drag, a held key and a held step are one undo step; each press is another",
          "[editor][inspector][ui]")
{
    Fixture f;
    f.select(f.e.lamp);

    // 40 pixels at 4 pixels a step of 0.1: 1.0 more, in several changes.
    f.drag("inspector-position-y", 40);
    CHECK(std::abs(f.stored<TransformComponent>(f.e.lamp).position.y - 3.0F) < 1.0e-4F);
    CHECK(f.document->undoCount() == 1);

    // Two presses of Up: two steps; Up held (repeats): one more step.
    Rwidget_focus(f.widget("inspector-scale-x"));
    f.key(R_KEY_UP);
    f.key(R_KEY_UP);
    f.frame();
    CHECK(f.document->undoCount() == 3);
    f.key(R_KEY_UP);
    f.key(R_KEY_UP, true);
    f.key(R_KEY_UP, true);
    f.frame();
    CHECK(f.document->undoCount() == 4);
    CHECK(std::abs(f.stored<TransformComponent>(f.e.lamp).scale.x - 1.5F) < 1.0e-5F);

    // Undo goes back one gesture at a time.
    REQUIRE_FALSE(f.document->undo());
    CHECK(std::abs(f.stored<TransformComponent>(f.e.lamp).scale.x - 1.2F) < 1.0e-5F);
    while (f.document->canUndo()) {
        REQUIRE_FALSE(f.document->undo());
    }
    CHECK(f.stored<TransformComponent>(f.e.lamp).position.y == 2.0F);
    CHECK(f.stored<TransformComponent>(f.e.lamp).scale.x == 1.0F);
}

TEST_CASE("A refused value changes nothing and shows the component's message",
          "[editor][inspector][ui]")
{
    Fixture f;
    f.select(f.e.lamp);

    f.typeInto("inspector-scale-y", "0");
    CHECK(f.stored<TransformComponent>(f.e.lamp).scale.y == 2.0F);
    CHECK(f.document->undoCount() == 0);
    CHECK(f.value("inspector-scale-y") == 2.0);
    CHECK(f.panel.message(ComponentType::Transform) ==
          "transform component: scale.y is 0, must be in [0.0001, 10000]");
    CHECK(f.shown("inspector-message-transform"));
    CHECK(f.text("inspector-message-transform") == f.panel.message(ComponentType::Transform));

    // Far beyond the world: refused too.
    f.typeInto("inspector-position-z", "1e30");
    CHECK(f.stored<TransformComponent>(f.e.lamp).position.z == 3.0F);
    CHECK(f.panel.message(ComponentType::Transform)
              .starts_with("transform component: position.z is "));

    // The next accepted change of the component clears the message.
    f.typeInto("inspector-scale-y", "3");
    CHECK(f.stored<TransformComponent>(f.e.lamp).scale.y == 3.0F);
    CHECK(f.panel.message(ComponentType::Transform).empty());
    CHECK_FALSE(f.shown("inspector-message-transform"));

    // A camera's far plane before its near plane; a negative intensity.
    f.select(f.e.camera);
    f.typeInto("inspector-far", "0.05");
    CHECK(f.stored<CameraComponent>(f.e.camera).farPlane == 1000.0F);
    CHECK(f.panel.message(ComponentType::Camera).starts_with("camera component: farPlane"));
    CHECK(f.value("inspector-far") == 1000.0);
    f.select(f.e.sun);
    // Another entity: the camera's message is gone.
    CHECK(f.panel.message(ComponentType::Camera).empty());
    f.typeInto("inspector-intensity", "-1");
    CHECK(f.stored<DirectionalLightComponent>(f.e.sun).intensity == 1.0F);
    CHECK(f.panel.message(ComponentType::DirectionalLight) ==
          "directional light component: intensity is -1, must be at least 0");
    CHECK(f.document->undoCount() == 1);
}

TEST_CASE("The rotation is edited as angles and is not rewritten while being edited",
          "[editor][inspector][ui]")
{
    Fixture f;
    f.select(f.e.lamp);
    f.typeInto("inspector-rotation-x", "100");
    const Quat turned = f.stored<TransformComponent>(f.e.lamp).rotation;
    CHECK(std::abs(std::abs(glm::dot(turned, editor::quatFromEulerDegrees({100.0F, 0.0F, 25.0F}))) -
                   1.0F) < 1.0e-5F);
    // Still being edited: the angles stay as typed.
    CHECK(f.value("inspector-rotation-x") == 100.0);
    CHECK(std::abs(f.value("inspector-rotation-z") - 25.0) < 1.0e-3);
    // The keyboard leaves: the same rotation, shown in its ranges.
    f.click("inspector-name");
    f.frame();
    CHECK(std::abs(f.value("inspector-rotation-x") - 80.0) < 1.0e-3);
    CHECK(std::abs(std::abs(f.value("inspector-rotation-y")) - 180.0) < 1.0e-3);
    CHECK(f.document->undoCount() == 1);
}

TEST_CASE("Every spin box reaches the scene through one command", "[editor][inspector][ui]")
{
    struct Case {
        const char* spin;
        EntityId Entities::* entity;
        const char* typed;
    };
    const Case cases[] = {
        {"inspector-position-x", &Entities::lamp, "4"},
        {"inspector-position-y", &Entities::lamp, "5"},
        {"inspector-position-z", &Entities::lamp, "6"},
        {"inspector-rotation-x", &Entities::lamp, "10"},
        {"inspector-rotation-y", &Entities::lamp, "20"},
        {"inspector-rotation-z", &Entities::lamp, "30"},
        {"inspector-scale-x", &Entities::lamp, "1.5"},
        {"inspector-scale-y", &Entities::lamp, "2.5"},
        {"inspector-scale-z", &Entities::lamp, "3.5"},
        {"inspector-fov", &Entities::camera, "75"},
        {"inspector-near", &Entities::camera, "0.5"},
        {"inspector-far", &Entities::camera, "500"},
        {"inspector-direction-x", &Entities::sun, "0.5"},
        {"inspector-direction-y", &Entities::sun, "-2"},
        {"inspector-direction-z", &Entities::sun, "0.25"},
        {"inspector-color-r", &Entities::sun, "0.5"},
        {"inspector-color-g", &Entities::sun, "0.25"},
        {"inspector-color-b", &Entities::sun, "0.75"},
        {"inspector-intensity", &Entities::sun, "2"},
    };
    Fixture f;
    std::size_t steps = 0;
    for (const Case& c : cases) {
        INFO("field " << c.spin);
        const EntityId entity = f.e.*c.entity;
        f.select(entity);
        const auto before = f.document->version();
        f.typeInto(c.spin, c.typed);
        ++steps;
        CHECK(f.document->undoCount() == steps);
        CHECK(f.document->version() != before);
    }
    // What the scene holds now.
    const auto camera = f.stored<CameraComponent>(f.e.camera);
    CHECK(std::abs(camera.verticalFovRadians - glm::radians(75.0F)) < 1.0e-6F);
    CHECK(camera.nearPlane == 0.5F);
    CHECK(camera.farPlane == 500.0F);
    const auto sun = f.stored<DirectionalLightComponent>(f.e.sun);
    CHECK(sun.color == Vec3{0.5F, 0.25F, 0.75F});
    CHECK(sun.intensity == 2.0F);
    // The direction is stored normalized.
    CHECK(std::abs(glm::length(sun.direction) - 1.0F) < 1.0e-5F);
    const auto lamp = f.stored<TransformComponent>(f.e.lamp);
    CHECK(lamp.position == Vec3{4.0F, 5.0F, 6.0F});
    CHECK(lamp.scale == Vec3{1.5F, 2.5F, 3.5F});
    const Vec3 angles = editor::eulerDegreesFromQuat(lamp.rotation);
    CHECK(std::abs(angles.x - 10.0F) < 1.0e-3F);
    CHECK(std::abs(angles.y - 20.0F) < 1.0e-3F);
    CHECK(std::abs(angles.z - 30.0F) < 1.0e-3F);
    // Undone, all of it is as it was.
    while (f.document->canUndo()) {
        REQUIRE_FALSE(f.document->undo());
    }
    CHECK(f.stored<TransformComponent>(f.e.lamp).rotation == kStoredRotation);
    CHECK(f.stored<CameraComponent>(f.e.camera) == CameraComponent{});
    CHECK(f.stored<DirectionalLightComponent>(f.e.sun) == DirectionalLightComponent{});
    CHECK(RavenErrors::instance().count() == 0);
}

TEST_CASE("The name: Enter renames, Escape restores, an empty name is refused",
          "[editor][inspector][ui]")
{
    Fixture f;
    f.select(f.e.lamp);
    const Rid name = f.widget("inspector-name");

    f.click("inspector-name");
    Rtext_input_select_all(name);
    f.type("Lantern");
    f.key(R_KEY_ENTER);
    f.frame();
    CHECK(f.stored<NameComponent>(f.e.lamp).value == "Lantern");
    CHECK(f.document->undoCount() == 1);

    Rtext_input_select_all(name);
    f.type("Gone");
    f.key(R_KEY_ESCAPE);
    f.frame();
    CHECK(std::string(Rtext_input_get_text(name)) == "Lantern");
    CHECK(f.document->undoCount() == 1);

    Rtext_input_select_all(name);
    f.key(R_KEY_BACKSPACE);
    f.key(R_KEY_ENTER);
    f.frame();
    CHECK(f.stored<NameComponent>(f.e.lamp).value == "Lantern");
    CHECK(f.panel.message(ComponentType::Name) == "name component: value: must not be empty");
    CHECK(std::string(Rtext_input_get_text(name)) == "Lantern");

    // Leaving the field renames too.
    Rtext_input_select_all(name);
    f.type("Beacon");
    f.click("inspector-position-x");
    f.frame();
    CHECK(f.stored<NameComponent>(f.e.lamp).value == "Beacon");
    CHECK(f.panel.message(ComponentType::Name).empty());
    CHECK(f.document->undoCount() == 2);
}

TEST_CASE("Assets are chosen from the project's lists; a script is enabled and disabled",
          "[editor][inspector][ui]")
{
    Fixture f;
    f.select(f.e.lamp);
    Rwidget_focus(f.widget("inspector-mesh"));
    f.key(R_KEY_DOWN);
    f.frame();
    CHECK(f.stored<StaticMeshComponent>(f.e.lamp).mesh == lpe_test::kOtherMesh);
    Rwidget_focus(f.widget("inspector-material"));
    f.key(R_KEY_DOWN);
    f.frame();
    CHECK(f.stored<StaticMeshComponent>(f.e.lamp).material == lpe_test::kMaterial);
    CHECK(f.document->undoCount() == 2);
    REQUIRE_FALSE(f.document->undo());
    f.frame();
    CHECK(f.stored<StaticMeshComponent>(f.e.lamp).material.isNull());
    CHECK(Rcombo_box_get_selected(f.widget("inspector-material")) == 0);

    f.select(f.e.brain);
    f.click("inspector-script-enabled");
    f.frame();
    CHECK_FALSE(f.stored<ScriptComponent>(f.e.brain).enabled);
    CHECK(f.document->undoCount() == 2);
    REQUIRE_FALSE(f.document->undo());
    f.frame();
    CHECK(f.stored<ScriptComponent>(f.e.brain).enabled);
    CHECK(Rcheckbox_is_checked(f.widget("inspector-script-enabled")) != R_FALSE);
}

TEST_CASE("Components are added from the Add list and removed by their section's button",
          "[editor][inspector][ui]")
{
    Fixture f;
    f.select(f.e.camera);
    // Camera's entity lacks a transform, a light, a mesh and a script.
    const Rid add = f.widget("inspector-add");
    REQUIRE(Rcombo_box_get_item_count(add) == 4);
    CHECK(std::string(Rcombo_box_get_item_text(add, 0)) == "Transform");
    f.click("inspector-add-button");
    f.frame();
    CHECK(f.document->scene().hasComponent<TransformComponent>(f.e.camera));
    CHECK(f.shown("inspector-section-transform"));
    CHECK(f.document->undoCount() == 1);
    CHECK(f.document->undoName() == "Add transform");
    // The list lost it.
    CHECK(Rcombo_box_get_item_count(add) == 3);

    // A static mesh starts with the first mesh asset.
    REQUIRE(std::string(Rcombo_box_get_item_text(add, 1)) == "Static mesh");
    Rcombo_box_set_selected(add, 1);
    f.click("inspector-add-button");
    f.frame();
    CHECK(f.stored<StaticMeshComponent>(f.e.camera).mesh == lpe_test::kMesh);

    f.click("inspector-remove-camera");
    f.frame();
    CHECK_FALSE(f.document->scene().hasComponent<CameraComponent>(f.e.camera));
    CHECK_FALSE(f.shown("inspector-section-camera"));
    CHECK(f.document->undoCount() == 3);
    REQUIRE_FALSE(f.document->undo());
    f.frame();
    CHECK(f.stored<CameraComponent>(f.e.camera) == CameraComponent{});
    CHECK(f.shown("inspector-section-camera"));

    // Everything added: the list and its button are disabled. The sections
    // are then taller than the panel: the Add row is at the end of the scroll.
    f.select(f.e.lamp);
    for (int i = 0; i < 6 && Rcombo_box_get_item_count(add) > 0; ++i) {
        Rscroll_box_scroll_to_end(f.widget("inspector-entity"));
        f.frame();
        f.click("inspector-add-button");
        f.frame();
    }
    CHECK(Rcombo_box_get_item_count(add) == 0);
    CHECK(Rwidget_is_enabled(f.widget("inspector-add-button")) == R_FALSE);
    CHECK(editor::addableComponents(f.document->scene(), f.e.lamp).empty());
    CHECK(f.panel.addMessage().empty());
    CHECK(RavenErrors::instance().count() == 0);
}

TEST_CASE("The inspector needs its widgets in the layout", "[editor][inspector][ui]")
{
    RavenSession session;
    REQUIRE(session.started());
    constexpr std::string_view kNoInspector =
        R"(<Window Title="no inspector" Width="320" Height="200"><VBox Name="inspector"/></Window>)";
    const Rid window = lpe_test::createHostWindow(kNoInspector, 320, 200);
    REQUIRE(window != R_INVALID_ID);
    InspectorPanel panel;
    const auto error = panel.attach(window, nullptr);
    REQUIRE(error.has_value());
    CHECK(error->message == "the shell layout has no widget named 'inspector-empty'");
    CHECK_FALSE(panel.attached());
    session.stop();
}
