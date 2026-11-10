// SDL events through Platform::pollEvents into InputState, with SDL's "dummy"
// video driver (set by CTest). The events are pushed into SDL's real queue, so
// this covers the scancode/button translation of the SDL3 platform layer.

#include "engine/platform/Input.hpp"
#include "engine/platform/Platform.hpp"

#include <SDL3/SDL.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using engine::platform::InputState;
using engine::platform::Key;
using engine::platform::MouseButton;
using engine::platform::Platform;

namespace {

void pushKey(SDL_Scancode scancode, bool down)
{
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.scancode = scancode;
    event.key.down = down;
    REQUIRE(SDL_PushEvent(&event));
}

void pushButton(std::uint8_t button, bool down)
{
    SDL_Event event{};
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = button;
    event.button.down = down;
    REQUIRE(SDL_PushEvent(&event));
}

void pushMotion(float dx, float dy)
{
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.xrel = dx;
    event.motion.yrel = dy;
    REQUIRE(SDL_PushEvent(&event));
}

void pushWheel(float y, SDL_MouseWheelDirection direction)
{
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_WHEEL;
    event.wheel.y = y;
    event.wheel.direction = direction;
    REQUIRE(SDL_PushEvent(&event));
}

void pushType(SDL_EventType type)
{
    SDL_Event event{};
    event.type = type;
    REQUIRE(SDL_PushEvent(&event));
}

} // namespace

TEST_CASE("Key events update the held keys by physical position", "[platform][input][integration]")
{
    auto platform = Platform::initialize();
    REQUIRE(platform.hasValue());
    InputState input;

    pushKey(SDL_SCANCODE_W, true);
    pushKey(SDL_SCANCODE_LSHIFT, true);
    CHECK_FALSE(platform.value().pollEvents(input).quitRequested);
    CHECK(input.isDown(Key::W));
    CHECK(input.wasPressed(Key::W));
    CHECK(input.isDown(Key::LeftShift));

    // Next frame: still held, the edge is gone; then released.
    pushKey(SDL_SCANCODE_W, false);
    (void)platform.value().pollEvents(input);
    CHECK_FALSE(input.isDown(Key::W));
    CHECK(input.wasReleased(Key::W));
    CHECK(input.isDown(Key::LeftShift));
}

TEST_CASE("Letter, digit and function key ranges map to the right keys",
          "[platform][input][integration]")
{
    auto platform = Platform::initialize();
    REQUIRE(platform.hasValue());
    InputState input;

    pushKey(SDL_SCANCODE_A, true);
    pushKey(SDL_SCANCODE_Z, true);
    pushKey(SDL_SCANCODE_1, true);
    pushKey(SDL_SCANCODE_0, true);
    pushKey(SDL_SCANCODE_F12, true);
    pushKey(SDL_SCANCODE_ESCAPE, true);
    (void)platform.value().pollEvents(input);
    CHECK(input.isDown(Key::A));
    CHECK(input.isDown(Key::Z));
    CHECK(input.isDown(Key::Digit1));
    CHECK(input.isDown(Key::Digit0));
    CHECK(input.isDown(Key::F12));
    CHECK(input.isDown(Key::Escape));
    CHECK_FALSE(input.isDown(Key::B));
    CHECK_FALSE(input.isDown(Key::Digit2));
}

TEST_CASE("Unmapped keys are ignored", "[platform][input][integration]")
{
    auto platform = Platform::initialize();
    REQUIRE(platform.hasValue());
    InputState input;

    pushKey(SDL_SCANCODE_KP_ENTER, true);
    pushKey(SDL_SCANCODE_UNKNOWN, true);
    CHECK_FALSE(platform.value().pollEvents(input).quitRequested);
    for (int key = 0; key < static_cast<int>(Key::Count); ++key) {
        CHECK_FALSE(input.isDown(static_cast<Key>(key)));
    }
}

TEST_CASE("Mouse buttons, motion and wheel are reported", "[platform][input][integration]")
{
    auto platform = Platform::initialize();
    REQUIRE(platform.hasValue());
    InputState input;

    pushButton(SDL_BUTTON_RIGHT, true);
    pushButton(SDL_BUTTON_X1, true); // extra buttons are ignored
    pushMotion(5.0F, -3.0F);
    pushMotion(2.0F, 1.0F);
    pushWheel(1.0F, SDL_MOUSEWHEEL_NORMAL);
    pushWheel(2.0F, SDL_MOUSEWHEEL_FLIPPED);
    (void)platform.value().pollEvents(input);
    CHECK(input.isDown(MouseButton::Right));
    CHECK(input.wasPressed(MouseButton::Right));
    CHECK_FALSE(input.isDown(MouseButton::Left));
    CHECK(input.mouseDelta().x == 7.0F);
    CHECK(input.mouseDelta().y == -2.0F);
    CHECK(input.wheelSteps() == -1.0F);

    // Motion is per frame: an empty frame reports none.
    (void)platform.value().pollEvents(input);
    CHECK(input.mouseDelta().x == 0.0F);
    CHECK(input.isDown(MouseButton::Right));
}

TEST_CASE("Losing focus releases held keys", "[platform][input][integration]")
{
    auto platform = Platform::initialize();
    REQUIRE(platform.hasValue());
    InputState input;

    pushKey(SDL_SCANCODE_D, true);
    (void)platform.value().pollEvents(input);
    REQUIRE(input.isDown(Key::D));

    pushType(SDL_EVENT_WINDOW_FOCUS_LOST);
    (void)platform.value().pollEvents(input);
    CHECK_FALSE(input.isDown(Key::D));
    CHECK(input.wasReleased(Key::D));
}

TEST_CASE("A quit event is reported as quitRequested", "[platform][input][integration]")
{
    auto platform = Platform::initialize();
    REQUIRE(platform.hasValue());
    InputState input;

    pushType(SDL_EVENT_QUIT);
    CHECK(platform.value().pollEvents(input).quitRequested);
}
