#pragma once

// Internal to the SDL3 platform layer: maps SDL input events to engine input
// types. Never include this outside engine/src/platform/sdl3/.

#include "engine/platform/Input.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <optional>

namespace engine::platform {

// Empty for keys the engine does not use yet.
[[nodiscard]] std::optional<Key> translateScancode(SDL_Scancode scancode) noexcept;

// Empty for extra mouse buttons (X1, X2).
[[nodiscard]] std::optional<MouseButton> translateMouseButton(std::uint8_t sdlButton) noexcept;

// Applies one SDL keyboard, mouse or focus event to the input state; other
// events are ignored.
void applyInputEvent(const SDL_Event& event, InputState& input) noexcept;

} // namespace engine::platform
