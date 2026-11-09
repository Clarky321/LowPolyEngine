#pragma once

#include <bitset>
#include <cstddef>
#include <cstdint>

namespace engine::platform {

// Physical keys, named after their position on a US keyboard. A key keeps its
// position on other layouts: Key::W is the key left of E on QWERTY, AZERTY and
// ЙЦУКЕН alike, so WASD movement works everywhere.
// Kept in grouped rows; they are easier to scan than one name per line.
// clang-format off
enum class Key : std::uint8_t {
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Digit0, Digit1, Digit2, Digit3, Digit4, Digit5, Digit6, Digit7, Digit8, Digit9,
    Space, Enter, Escape, Tab, Backspace,
    LeftShift, RightShift, LeftCtrl, RightCtrl, LeftAlt, RightAlt,
    Up, Down, Left, Right,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Count, // number of keys, not a key
};
// clang-format on

enum class MouseButton : std::uint8_t {
    Left,
    Middle,
    Right,
    Count, // number of buttons, not a button
};

inline constexpr std::size_t kKeyCount = static_cast<std::size_t>(Key::Count);
inline constexpr std::size_t kMouseButtonCount = static_cast<std::size_t>(MouseButton::Count);

struct MouseDelta {
    float x = 0.0F;
    // Positive is downward on screen.
    float y = 0.0F;
};

// Keyboard and mouse state of the current frame, independent of the OS layer.
//
// Platform::pollEvents() starts a new frame and applies the OS events to it.
// "Held" state persists across frames; "pressed"/"released" edges, mouse motion
// and wheel steps describe only the current frame. A key pressed and released
// within one frame reports both edges, so a short tap is not lost.
class InputState {
public:
    // Forgets this frame's edges, mouse motion and wheel steps.
    void beginFrame() noexcept;

    // Key-repeat (down while already held) creates no new pressed edge.
    void setKey(Key key, bool down) noexcept;
    void setMouseButton(MouseButton button, bool down) noexcept;
    void addMouseMotion(float dx, float dy) noexcept;
    // Positive steps scroll away from the user.
    void addWheelSteps(float steps) noexcept;
    // Releases every held key and button, e.g. when the window loses focus and
    // would never see the matching key-up events.
    void releaseAll() noexcept;

    [[nodiscard]] bool isDown(Key key) const noexcept;
    [[nodiscard]] bool wasPressed(Key key) const noexcept;
    [[nodiscard]] bool wasReleased(Key key) const noexcept;

    [[nodiscard]] bool isDown(MouseButton button) const noexcept;
    [[nodiscard]] bool wasPressed(MouseButton button) const noexcept;
    [[nodiscard]] bool wasReleased(MouseButton button) const noexcept;

    // Mouse movement in pixels accumulated over this frame.
    [[nodiscard]] MouseDelta mouseDelta() const noexcept { return mouseDelta_; }
    [[nodiscard]] float wheelSteps() const noexcept { return wheelSteps_; }

private:
    std::bitset<kKeyCount> keysDown_;
    std::bitset<kKeyCount> keysPressed_;
    std::bitset<kKeyCount> keysReleased_;
    std::bitset<kMouseButtonCount> buttonsDown_;
    std::bitset<kMouseButtonCount> buttonsPressed_;
    std::bitset<kMouseButtonCount> buttonsReleased_;
    MouseDelta mouseDelta_;
    float wheelSteps_ = 0.0F;
};

} // namespace engine::platform
