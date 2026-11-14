// The Dear ImGui SDL3 platform backend (ADR 0018): input, cursor, clipboard
// and display size from the engine's SDL window.

#include "Sdl3ImGui.hpp"

#include "../../debug/ImGuiBackends.hpp"
#include "Sdl3Window.hpp"

#include "engine/platform/GlContext.hpp"
#include "engine/platform/Window.hpp"

#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include <SDL3/SDL.h>

#include <string>

namespace engine::platform {

void forwardEventToDebugUi(const SDL_Event& event)
{
    // The backend keeps its data in the ImGui context; without one it is not
    // running.
    if (ImGui::GetCurrentContext() != nullptr &&
        ImGui::GetIO().BackendPlatformUserData != nullptr) {
        ImGui_ImplSDL3_ProcessEvent(&event);
    }
}

} // namespace engine::platform

namespace engine::debug::detail {

std::optional<core::Error> initPlatformBackend(platform::Window& window,
                                               platform::GlContext& /*context*/)
{
    SDL_Window* handle = platform::detail::NativeWindowAccess::handle(window);
    if (handle == nullptr) {
        return core::Error{"debug UI: the window has no native handle"};
    }
    // The current OpenGL context is the window's (GlContext::create made it
    // current); the backend only uses it for multi-viewport support.
    if (!ImGui_ImplSDL3_InitForOpenGL(handle, SDL_GL_GetCurrentContext())) {
        return core::Error{std::string("debug UI: the SDL3 backend failed to start: ") +
                           SDL_GetError()};
    }
    return std::nullopt;
}

void shutdownPlatformBackend() noexcept
{
    ImGui_ImplSDL3_Shutdown();
}

void newPlatformFrame()
{
    ImGui_ImplSDL3_NewFrame();
}

} // namespace engine::debug::detail
