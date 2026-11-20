// The editor camera's input from the viewport widget (editor/src/viewport/
// ViewportInput, M5.3), headless: Raven's null backend and host platform, with
// keys, text, buttons, pointer moves and the wheel injected as the SDL3 host
// injects them. Keys reach the camera only while the viewport has the
// keyboard; a text field with the keyboard never moves it.

#include "../ui/RavenSession.hpp"

#include "application/ShellLayout.hpp"
#include "viewport/ViewportInput.hpp"

#include "engine/camera/Camera.hpp"
#include "engine/camera/FreeFlyController.hpp"

#include <glm/geometric.hpp>

#include <catch2/catch_test_macros.hpp>

#include <Raven.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace {

using engine::camera::FlyInput;
using engine::math::Vec2;
using engine::math::Vec3;
using lpe_test::RavenErrors;
using lpe_test::RavenSession;

// A text field above a viewport that fills the rest of the window.
constexpr std::string_view kLayout =
    R"(<Window Title="viewport input test" Width="640" Height="480">
  <VBox>
    <TextInput Name="name" Placeholder="Name"/>
    <Viewport Name="viewport" Slot.Fill="1"/>
  </VBox>
</Window>)";

// A point in the window's pixels.
struct Point {
    int x = 0;
    int y = 0;
};

// The middle of a widget, in pixels of its window.
Point centreOf(Rid window, Rid widget)
{
    const float scale = Rwindow_get_scale(window);
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    Rwidget_get_rect(widget, &x, &y, &width, &height);
    return {static_cast<int>(std::lround((x + width * 0.5F) * scale)),
            static_cast<int>(std::lround((y + height * 0.5F) * scale))};
}

// The test window with its two widgets and the input attached to the viewport.
struct Fixture {
    explicit Fixture(float scale = 1.0F)
    {
        REQUIRE(session.started());
        window = Rwindow_create_from_xml(kLayout.data(), kLayout.size());
        REQUIRE(window != R_INVALID_ID);
        Rwindow_show(window);
        Rhost_window_set_size(window, static_cast<int>(std::lround(640.0F * scale)),
                              static_cast<int>(std::lround(480.0F * scale)), scale);
        Rhost_inject_focus(window, R_TRUE);
        Rdraw_frame();
        viewport = Rwidget_find(window, "viewport");
        name = Rwidget_find(window, "name");
        REQUIRE(viewport != R_INVALID_ID);
        REQUIRE(name != R_INVALID_ID);
        input.attach(viewport);
    }

    ~Fixture()
    {
        input.detach();
        session.stop();
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    Fixture(Fixture&&) = delete;
    Fixture& operator=(Fixture&&) = delete;

    void moveTo(Point point) const { Rhost_inject_mouse_move(window, point.x, point.y, 0); }
    void button(Point point, RMouseButton which, bool down) const
    {
        Rhost_inject_mouse_button(window, point.x, point.y, which, down ? R_TRUE : R_FALSE, 0);
    }
    void click(Rid widget) const
    {
        const Point point = centreOf(window, widget);
        moveTo(point);
        button(point, R_MOUSE_LEFT, true);
        button(point, R_MOUSE_LEFT, false);
    }
    void key(RKey which, bool down) const
    {
        Rhost_inject_key(window, which, down ? R_TRUE : R_FALSE, 0, R_FALSE);
    }
    // A letter typed as the host forwards it: the key, then its text.
    void type(RKey which, char letter) const
    {
        key(which, true);
        Rhost_inject_text(window, static_cast<std::uint32_t>(static_cast<unsigned char>(letter)));
        key(which, false);
    }

    RavenSession session;
    Rid window = R_INVALID_ID;
    Rid viewport = R_INVALID_ID;
    Rid name = R_INVALID_ID;
    editor::ViewportInput input;
};

bool still(const FlyInput& fly)
{
    return fly.move == Vec3{0.0F} && fly.look == Vec2{0.0F} && !fly.fast && fly.speedSteps == 0.0F;
}

bool near(const Vec2& a, const Vec2& b)
{
    return glm::length(a - b) < 1.0e-3F;
}

} // namespace

TEST_CASE("The viewport's keys fly the camera only while the viewport has the keyboard",
          "[editor][viewport]")
{
    Fixture f;

    SECTION("Before the viewport has the keyboard, its keys do nothing")
    {
        REQUIRE(Rwidget_is_focused(f.viewport) == R_FALSE);
        f.key(R_KEY_W, true);
        CHECK(still(f.input.take()));
        f.key(R_KEY_W, false);
    }

    SECTION("With the keyboard, W/A/S/D, E, Q and Shift fly the camera")
    {
        f.click(f.viewport);
        REQUIRE(Rwidget_is_focused(f.viewport) != R_FALSE);
        f.key(R_KEY_W, true);
        CHECK(f.input.take().move == Vec3{0.0F, 0.0F, 1.0F});
        f.key(R_KEY_D, true);
        f.key(R_KEY_E, true);
        CHECK(f.input.take().move == Vec3{1.0F, 1.0F, 1.0F});
        f.key(R_KEY_W, false);
        f.key(R_KEY_D, false);
        f.key(R_KEY_E, false);
        f.key(R_KEY_S, true);
        f.key(R_KEY_A, true);
        f.key(R_KEY_Q, true);
        CHECK(f.input.take().move == Vec3{-1.0F, -1.0F, -1.0F});
        // Opposite keys cancel out.
        f.key(R_KEY_W, true);
        CHECK(f.input.take().move == Vec3{-1.0F, -1.0F, 0.0F});
        for (const RKey k : {R_KEY_W, R_KEY_S, R_KEY_A, R_KEY_Q}) {
            f.key(k, false);
        }
        CHECK(still(f.input.take()));

        // Either Shift goes faster while it is held.
        f.key(R_KEY_LEFT_SHIFT, true);
        f.key(R_KEY_RIGHT_SHIFT, true);
        f.key(R_KEY_LEFT_SHIFT, false);
        CHECK(f.input.take().fast);
        f.key(R_KEY_RIGHT_SHIFT, false);
        CHECK_FALSE(f.input.take().fast);

        // Other keys are not the camera's.
        f.key(R_KEY_F, true);
        f.key(R_KEY_SPACE, true);
        CHECK(still(f.input.take()));
    }
    CHECK(RavenErrors::instance().count() == 0);
}

TEST_CASE("Typing in a text field never moves the camera", "[editor][viewport]")
{
    Fixture f;
    f.click(f.name);
    REQUIRE(Rwidget_is_focused(f.name) != R_FALSE);
    REQUIRE(Rwindow_wants_text_input(f.window) != R_FALSE);

    engine::camera::Camera camera;
    engine::camera::FreeFlyController controller;
    f.type(R_KEY_W, 'w');
    f.type(R_KEY_A, 'a');
    f.type(R_KEY_S, 's');
    f.type(R_KEY_D, 'd');
    f.key(R_KEY_E, true);
    f.key(R_KEY_LEFT_SHIFT, true);
    const FlyInput fly = f.input.take();
    CHECK(still(fly));
    controller.update(camera, fly, 1.0F);
    CHECK(camera.position == Vec3{0.0F});
    CHECK(std::string(Rtext_input_get_text(f.name)) == "wasd");
    f.key(R_KEY_E, false);
    f.key(R_KEY_LEFT_SHIFT, false);

    // Back in the viewport the same keys fly it.
    f.click(f.viewport);
    f.key(R_KEY_W, true);
    controller.update(camera, f.input.take(), 1.0F);
    CHECK(camera.position != Vec3{0.0F});
    f.key(R_KEY_W, false);
    CHECK(RavenErrors::instance().count() == 0);
}

TEST_CASE("Held keys and looking end when the viewport loses the keyboard", "[editor][viewport]")
{
    Fixture f;
    f.click(f.viewport);
    f.key(R_KEY_W, true);
    f.key(R_KEY_LEFT_SHIFT, true);
    REQUIRE(f.input.take().move.z == 1.0F);

    SECTION("A click on the text field")
    {
        f.click(f.name);
        CHECK(Rwidget_is_focused(f.name) != R_FALSE);
        CHECK(still(f.input.take()));
        // The releases now go to the text field; the viewport stays still.
        f.key(R_KEY_W, false);
        f.key(R_KEY_LEFT_SHIFT, false);
        f.click(f.viewport);
        CHECK(still(f.input.take()));
    }

    SECTION("While looking around")
    {
        const Point middle = centreOf(f.window, f.viewport);
        f.moveTo(middle);
        f.button(middle, R_MOUSE_RIGHT, true);
        REQUIRE(f.input.looking());
        Rwidget_focus(f.name);
        CHECK_FALSE(f.input.looking());
        CHECK(still(f.input.take()));
        f.button(middle, R_MOUSE_RIGHT, false);
    }
    CHECK(RavenErrors::instance().count() == 0);
}

TEST_CASE("The right mouse button looks around, also beyond the viewport", "[editor][viewport]")
{
    Fixture f;
    const Point middle = centreOf(f.window, f.viewport);
    f.moveTo(middle);

    // Without the right button the pointer only points.
    f.moveTo({middle.x + 25, middle.y + 5});
    CHECK(still(f.input.take()));
    f.button({middle.x + 25, middle.y + 5}, R_MOUSE_LEFT, true);
    f.moveTo({middle.x + 40, middle.y + 5});
    f.button({middle.x + 40, middle.y + 5}, R_MOUSE_LEFT, false);
    CHECK(still(f.input.take()));

    // A right press in the viewport gives it the keyboard and starts looking.
    f.moveTo(middle);
    f.button(middle, R_MOUSE_RIGHT, true);
    CHECK(f.input.looking());
    CHECK(Rwidget_is_focused(f.viewport) != R_FALSE);
    f.moveTo({middle.x + 30, middle.y - 10});
    f.moveTo({middle.x + 50, middle.y - 4});
    CHECK(near(f.input.take().look, Vec2{50.0F, -4.0F}));

    // Over the text field and outside the window the viewport keeps the pointer.
    const Point field = centreOf(f.window, f.name);
    f.moveTo(field);
    CHECK(near(f.input.take().look, Vec2{static_cast<float>(field.x - middle.x - 50),
                                         static_cast<float>(field.y - middle.y + 4)}));
    f.moveTo({field.x, -20});
    CHECK(near(f.input.take().look, Vec2{0.0F, static_cast<float>(-20 - field.y)}));

    // Released anywhere, looking ends.
    f.button({field.x, -20}, R_MOUSE_RIGHT, false);
    CHECK_FALSE(f.input.looking());
    f.moveTo(middle);
    CHECK(still(f.input.take()));
    CHECK(RavenErrors::instance().count() == 0);
}

TEST_CASE("Looking around counts pixels at every display scale", "[editor][viewport]")
{
    for (const float scale : {1.0F, 1.5F, 2.0F}) {
        INFO("scale " << scale);
        Fixture f(scale);
        REQUIRE(Rwindow_get_scale(f.window) == scale);
        const Point middle = centreOf(f.window, f.viewport);
        f.moveTo(middle);
        f.button(middle, R_MOUSE_RIGHT, true);
        f.moveTo({middle.x + 60, middle.y + 30});
        CHECK(near(f.input.take().look, Vec2{60.0F, 30.0F}));
        f.button({middle.x + 60, middle.y + 30}, R_MOUSE_RIGHT, false);
        CHECK(RavenErrors::instance().count() == 0);
    }
}

TEST_CASE("The wheel over the viewport changes the speed", "[editor][viewport]")
{
    Fixture f;
    const Point middle = centreOf(f.window, f.viewport);
    f.moveTo(middle);
    Rhost_inject_mouse_wheel(f.window, 0.0F, 2.0F, 0);
    Rhost_inject_mouse_wheel(f.window, 0.0F, -0.5F, 0);
    CHECK(f.input.take().speedSteps == 1.5F);
    CHECK(f.input.take().speedSteps == 0.0F);

    // Over the text field the wheel is not the camera's.
    f.moveTo(centreOf(f.window, f.name));
    Rhost_inject_mouse_wheel(f.window, 0.0F, 3.0F, 0);
    CHECK(f.input.take().speedSteps == 0.0F);
    CHECK(RavenErrors::instance().count() == 0);
}

TEST_CASE("A detached input hears nothing more", "[editor][viewport]")
{
    Fixture f;
    f.click(f.viewport);
    f.input.detach();
    f.key(R_KEY_W, true);
    f.moveTo(centreOf(f.window, f.viewport));
    Rhost_inject_mouse_wheel(f.window, 0.0F, 1.0F, 0);
    CHECK(still(f.input.take()));
    // Attached again, it hears the viewport again.
    f.input.attach(f.viewport);
    f.key(R_KEY_D, true);
    CHECK(f.input.take().move.x == 1.0F);
    CHECK(RavenErrors::instance().count() == 0);
}

TEST_CASE("In the editor shell a click gives the viewport the keyboard back", "[editor][viewport]")
{
    RavenSession session;
    REQUIRE(session.started());
    const std::string_view xml = editor::layouts::shell();
    const Rid window = lpe_test::createHostWindow(xml, 1280, 760);
    REQUIRE(window != R_INVALID_ID);
    const Rid viewport = Rwidget_find(window, "viewport");
    const Rid hierarchy = Rwidget_find(window, "hierarchy");
    REQUIRE(viewport != R_INVALID_ID);
    REQUIRE(hierarchy != R_INVALID_ID);
    editor::ViewportInput input;
    input.attach(viewport);

    Rwidget_focus(viewport);
    REQUIRE(Rwidget_is_focused(viewport) != R_FALSE);
    lpe_test::clickWidget(window, hierarchy);
    CHECK(Rwidget_is_focused(viewport) == R_FALSE);
    Rhost_inject_key(window, R_KEY_W, R_TRUE, 0, R_FALSE);
    CHECK(still(input.take()));
    Rhost_inject_key(window, R_KEY_W, R_FALSE, 0, R_FALSE);

    lpe_test::clickWidget(window, viewport);
    CHECK(Rwidget_is_focused(viewport) != R_FALSE);
    Rhost_inject_key(window, R_KEY_W, R_TRUE, 0, R_FALSE);
    CHECK(input.take().move.z == 1.0F);
    Rhost_inject_key(window, R_KEY_W, R_FALSE, 0, R_FALSE);
    input.detach();
    CHECK(RavenErrors::instance().count() == 0);
}
