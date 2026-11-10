#include "engine/platform/Input.hpp"

#include <catch2/catch_test_macros.hpp>

#include <limits>

using engine::platform::InputState;
using engine::platform::Key;
using engine::platform::MouseButton;

TEST_CASE("A key press is held across frames, its edge lasts one frame", "[platform][input]")
{
    InputState input;
    input.beginFrame();
    input.setKey(Key::W, true);
    CHECK(input.isDown(Key::W));
    CHECK(input.wasPressed(Key::W));
    CHECK_FALSE(input.wasReleased(Key::W));
    CHECK_FALSE(input.isDown(Key::S));

    input.beginFrame();
    CHECK(input.isDown(Key::W));
    CHECK_FALSE(input.wasPressed(Key::W));

    input.setKey(Key::W, false);
    CHECK_FALSE(input.isDown(Key::W));
    CHECK(input.wasReleased(Key::W));

    input.beginFrame();
    CHECK_FALSE(input.wasReleased(Key::W));
}

TEST_CASE("Key repeat does not create a new pressed edge", "[platform][input]")
{
    InputState input;
    input.setKey(Key::Space, true);
    input.beginFrame();
    input.setKey(Key::Space, true);
    CHECK(input.isDown(Key::Space));
    CHECK_FALSE(input.wasPressed(Key::Space));
}

TEST_CASE("A tap within one frame reports both edges", "[platform][input]")
{
    InputState input;
    input.beginFrame();
    input.setKey(Key::F12, true);
    input.setKey(Key::F12, false);
    CHECK(input.wasPressed(Key::F12));
    CHECK(input.wasReleased(Key::F12));
    CHECK_FALSE(input.isDown(Key::F12));
}

TEST_CASE("A release without a press creates no edge", "[platform][input]")
{
    InputState input;
    input.setKey(Key::Escape, false);
    CHECK_FALSE(input.wasReleased(Key::Escape));
}

TEST_CASE("Mouse buttons have held state and edges", "[platform][input]")
{
    InputState input;
    input.setMouseButton(MouseButton::Right, true);
    CHECK(input.isDown(MouseButton::Right));
    CHECK(input.wasPressed(MouseButton::Right));
    CHECK_FALSE(input.isDown(MouseButton::Left));

    input.beginFrame();
    input.setMouseButton(MouseButton::Right, false);
    CHECK(input.wasReleased(MouseButton::Right));
    CHECK_FALSE(input.isDown(MouseButton::Right));
}

TEST_CASE("Mouse motion and wheel steps accumulate within a frame", "[platform][input]")
{
    InputState input;
    input.addMouseMotion(3.0F, -2.0F);
    input.addMouseMotion(1.5F, 4.0F);
    input.addWheelSteps(1.0F);
    input.addWheelSteps(2.0F);
    CHECK(input.mouseDelta().x == 4.5F);
    CHECK(input.mouseDelta().y == 2.0F);
    CHECK(input.wheelSteps() == 3.0F);

    input.beginFrame();
    CHECK(input.mouseDelta().x == 0.0F);
    CHECK(input.mouseDelta().y == 0.0F);
    CHECK(input.wheelSteps() == 0.0F);
}

TEST_CASE("Non-finite mouse values are ignored", "[platform][input]")
{
    InputState input;
    input.addMouseMotion(std::numeric_limits<float>::quiet_NaN(), 1.0F);
    input.addMouseMotion(1.0F, std::numeric_limits<float>::infinity());
    input.addWheelSteps(std::numeric_limits<float>::quiet_NaN());
    CHECK(input.mouseDelta().x == 0.0F);
    CHECK(input.mouseDelta().y == 0.0F);
    CHECK(input.wheelSteps() == 0.0F);
}

TEST_CASE("releaseAll releases held keys and buttons with edges", "[platform][input]")
{
    InputState input;
    input.setKey(Key::W, true);
    input.setKey(Key::LeftShift, true);
    input.setMouseButton(MouseButton::Right, true);
    input.beginFrame();

    input.releaseAll();
    CHECK_FALSE(input.isDown(Key::W));
    CHECK_FALSE(input.isDown(Key::LeftShift));
    CHECK_FALSE(input.isDown(MouseButton::Right));
    CHECK(input.wasReleased(Key::W));
    CHECK(input.wasReleased(MouseButton::Right));
    CHECK_FALSE(input.wasReleased(Key::A));
}

TEST_CASE("Out-of-range keys and buttons are ignored", "[platform][input]")
{
    InputState input;
    input.setKey(Key::Count, true);
    input.setMouseButton(MouseButton::Count, true);
    CHECK_FALSE(input.isDown(Key::Count));
    CHECK_FALSE(input.wasPressed(Key::Count));
    CHECK_FALSE(input.isDown(MouseButton::Count));
    CHECK_FALSE(input.isDown(static_cast<Key>(250)));
}
