#pragma once

// Internal to the SDL3 platform layer: shares Window's native handle with
// GlContext. Never include this outside engine/src/platform/sdl3/.

#include "engine/platform/Window.hpp"

#include <SDL3/SDL.h>

#include <memory>

namespace engine::platform {

struct SdlWindowDeleter {
    void operator()(SDL_Window* window) const noexcept { SDL_DestroyWindow(window); }
};

struct Window::Impl {
    std::unique_ptr<SDL_Window, SdlWindowDeleter> handle;
    bool openGl = false;
};

} // namespace engine::platform
