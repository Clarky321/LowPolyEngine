#include "Sdl3ImGui.hpp"
#include "Sdl3Input.hpp"

#include "engine/platform/Platform.hpp"

#include <SDL3/SDL.h>

// Only the declaration of SDL_SetMainReady is needed. SDL_MAIN_HANDLED keeps
// SDL_main.h from redefining main or emitting its own entry point here.
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>

#include <string>
#include <utility>

namespace engine::platform {

core::Result<Platform> Platform::initialize()
{
    // The application provides its own main() instead of SDL_main, so tell SDL
    // that platform entry-point setup is already done.
    SDL_SetMainReady();

    // SDL_InitSubSystem is reference counted, which makes several Platform
    // instances safe. Video also initializes the event subsystem.
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        return core::Error{std::string("SDL video initialization failed: ") + SDL_GetError()};
    }

    Platform platform;
    platform.active_ = true;
    return platform;
}

Platform::~Platform()
{
    shutdown();
}

Platform::Platform(Platform&& other) noexcept : active_(std::exchange(other.active_, false)) {}

Platform& Platform::operator=(Platform&& other) noexcept
{
    if (this != &other) {
        shutdown();
        active_ = std::exchange(other.active_, false);
    }
    return *this;
}

void Platform::shutdown() noexcept
{
    if (!active_) {
        return;
    }
    active_ = false;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    // SDL_Quit releases SDL's remaining global state; only call it once no
    // subsystem is in use anymore (e.g. by another Platform instance).
    if (SDL_WasInit(0) == 0) {
        SDL_Quit();
    }
}

PlatformEvents Platform::pollEvents(InputState& input)
{
    input.beginFrame();
    PlatformEvents events;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        // The debug UI sees every event; the application asks it whether it
        // uses the mouse or keyboard (DebugUi::wantsMouse, wantsKeyboard).
        forwardEventToDebugUi(event);
        switch (event.type) {
        case SDL_EVENT_QUIT:
        // With a single window, closing it ends the application.
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            events.quitRequested = true;
            break;
        default:
            applyInputEvent(event, input);
            break;
        }
    }
    return events;
}

} // namespace engine::platform
