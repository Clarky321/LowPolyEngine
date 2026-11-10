#include "engine/platform/Input.hpp"

#include <cmath>

// Platform-independent input bookkeeping, testable without SDL.

namespace engine::platform {

namespace {

// Values outside the enum (Count or a bad cast) map to "no slot".
template <std::size_t N, typename Enum>
bool toIndex(Enum value, std::size_t& index) noexcept
{
    index = static_cast<std::size_t>(value);
    return index < N;
}

template <std::size_t N>
void applyButton(std::bitset<N>& down, std::bitset<N>& pressed, std::bitset<N>& released,
                 std::size_t index, bool isDown) noexcept
{
    if (isDown && !down.test(index)) {
        pressed.set(index);
    } else if (!isDown && down.test(index)) {
        released.set(index);
    }
    down.set(index, isDown);
}

} // namespace

void InputState::beginFrame() noexcept
{
    keysPressed_.reset();
    keysReleased_.reset();
    buttonsPressed_.reset();
    buttonsReleased_.reset();
    mouseDelta_ = MouseDelta{};
    wheelSteps_ = 0.0F;
}

void InputState::setKey(Key key, bool down) noexcept
{
    std::size_t index = 0;
    if (toIndex<kKeyCount>(key, index)) {
        applyButton(keysDown_, keysPressed_, keysReleased_, index, down);
    }
}

void InputState::setMouseButton(MouseButton button, bool down) noexcept
{
    std::size_t index = 0;
    if (toIndex<kMouseButtonCount>(button, index)) {
        applyButton(buttonsDown_, buttonsPressed_, buttonsReleased_, index, down);
    }
}

void InputState::addMouseMotion(float dx, float dy) noexcept
{
    // A broken driver value must not turn the camera orientation into NaN.
    if (std::isfinite(dx) && std::isfinite(dy)) {
        mouseDelta_.x += dx;
        mouseDelta_.y += dy;
    }
}

void InputState::addWheelSteps(float steps) noexcept
{
    if (std::isfinite(steps)) {
        wheelSteps_ += steps;
    }
}

void InputState::releaseAll() noexcept
{
    keysReleased_ |= keysDown_;
    keysDown_.reset();
    buttonsReleased_ |= buttonsDown_;
    buttonsDown_.reset();
}

bool InputState::isDown(Key key) const noexcept
{
    std::size_t index = 0;
    return toIndex<kKeyCount>(key, index) && keysDown_.test(index);
}

bool InputState::wasPressed(Key key) const noexcept
{
    std::size_t index = 0;
    return toIndex<kKeyCount>(key, index) && keysPressed_.test(index);
}

bool InputState::wasReleased(Key key) const noexcept
{
    std::size_t index = 0;
    return toIndex<kKeyCount>(key, index) && keysReleased_.test(index);
}

bool InputState::isDown(MouseButton button) const noexcept
{
    std::size_t index = 0;
    return toIndex<kMouseButtonCount>(button, index) && buttonsDown_.test(index);
}

bool InputState::wasPressed(MouseButton button) const noexcept
{
    std::size_t index = 0;
    return toIndex<kMouseButtonCount>(button, index) && buttonsPressed_.test(index);
}

bool InputState::wasReleased(MouseButton button) const noexcept
{
    std::size_t index = 0;
    return toIndex<kMouseButtonCount>(button, index) && buttonsReleased_.test(index);
}

} // namespace engine::platform
