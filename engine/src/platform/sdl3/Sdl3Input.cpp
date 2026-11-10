#include "Sdl3Input.hpp"

namespace engine::platform {

namespace {

// Maps a scancode inside [first, last] to consecutive Key values from firstKey.
std::optional<Key> fromRange(SDL_Scancode scancode, SDL_Scancode first, SDL_Scancode last,
                             Key firstKey) noexcept
{
    const int code = static_cast<int>(scancode);
    if (code < static_cast<int>(first) || code > static_cast<int>(last)) {
        return std::nullopt;
    }
    return static_cast<Key>(static_cast<int>(firstKey) + (code - static_cast<int>(first)));
}

} // namespace

std::optional<Key> translateScancode(SDL_Scancode scancode) noexcept
{
    // SDL orders letters A..Z, digits 1..9 then 0, and F1..F12 contiguously.
    if (auto key = fromRange(scancode, SDL_SCANCODE_A, SDL_SCANCODE_Z, Key::A)) {
        return key;
    }
    if (auto key = fromRange(scancode, SDL_SCANCODE_1, SDL_SCANCODE_9, Key::Digit1)) {
        return key;
    }
    if (auto key = fromRange(scancode, SDL_SCANCODE_F1, SDL_SCANCODE_F12, Key::F1)) {
        return key;
    }

    switch (scancode) {
    case SDL_SCANCODE_0:
        return Key::Digit0;
    case SDL_SCANCODE_SPACE:
        return Key::Space;
    case SDL_SCANCODE_RETURN:
        return Key::Enter;
    case SDL_SCANCODE_ESCAPE:
        return Key::Escape;
    case SDL_SCANCODE_TAB:
        return Key::Tab;
    case SDL_SCANCODE_BACKSPACE:
        return Key::Backspace;
    case SDL_SCANCODE_LSHIFT:
        return Key::LeftShift;
    case SDL_SCANCODE_RSHIFT:
        return Key::RightShift;
    case SDL_SCANCODE_LCTRL:
        return Key::LeftCtrl;
    case SDL_SCANCODE_RCTRL:
        return Key::RightCtrl;
    case SDL_SCANCODE_LALT:
        return Key::LeftAlt;
    case SDL_SCANCODE_RALT:
        return Key::RightAlt;
    case SDL_SCANCODE_UP:
        return Key::Up;
    case SDL_SCANCODE_DOWN:
        return Key::Down;
    case SDL_SCANCODE_LEFT:
        return Key::Left;
    case SDL_SCANCODE_RIGHT:
        return Key::Right;
    default:
        return std::nullopt;
    }
}

std::optional<MouseButton> translateMouseButton(std::uint8_t sdlButton) noexcept
{
    switch (sdlButton) {
    case SDL_BUTTON_LEFT:
        return MouseButton::Left;
    case SDL_BUTTON_MIDDLE:
        return MouseButton::Middle;
    case SDL_BUTTON_RIGHT:
        return MouseButton::Right;
    default:
        return std::nullopt;
    }
}

void applyInputEvent(const SDL_Event& event, InputState& input) noexcept
{
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        if (const auto key = translateScancode(event.key.scancode)) {
            input.setKey(*key, event.key.down);
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (const auto button = translateMouseButton(event.button.button)) {
            input.setMouseButton(*button, event.button.down);
        }
        break;
    case SDL_EVENT_MOUSE_MOTION:
        // Relative motion also works while the cursor is captured (relative mode).
        input.addMouseMotion(event.motion.xrel, event.motion.yrel);
        break;
    case SDL_EVENT_MOUSE_WHEEL: {
        // "Flipped" devices (natural scrolling) report inverted values.
        const float sign = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0F : 1.0F;
        input.addWheelSteps(event.wheel.y * sign);
        break;
    }
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        // Key-up events for keys held during the switch go to the other window.
        input.releaseAll();
        break;
    default:
        break;
    }
}

} // namespace engine::platform
