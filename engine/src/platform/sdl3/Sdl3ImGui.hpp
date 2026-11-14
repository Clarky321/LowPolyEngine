#pragma once

// Internal to the SDL3 platform layer: hands OS events to the Dear ImGui SDL3
// backend while a debug UI exists (ADR 0018). Never include this outside
// engine/src/platform/sdl3/.

#include <SDL3/SDL.h>

namespace engine::platform {

// Gives the event to Dear ImGui if its SDL3 backend is running; otherwise does
// nothing.
void forwardEventToDebugUi(const SDL_Event& event);

} // namespace engine::platform
