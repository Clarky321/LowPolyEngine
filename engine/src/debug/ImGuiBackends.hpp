#pragma once

// Internal: the two Dear ImGui backends, each in the layer that owns its API
// (ADR 0018). The SDL3 platform backend lives in engine/src/platform/sdl3/
// (Sdl3ImGui.cpp), the OpenGL 3 renderer backend in engine/src/render/opengl/
// (GlImGui.cpp). DebugUi calls them through these functions, so no SDL or
// OpenGL type appears outside those folders. Every function needs the ImGui
// context that DebugUi creates.

#include "engine/core/Result.hpp"

#include <optional>

namespace engine::platform {
class GlContext;
class Window;
} // namespace engine::platform

namespace engine::debug::detail {

// SDL3: window and OpenGL context for input, cursor, clipboard and display size.
[[nodiscard]] std::optional<core::Error> initPlatformBackend(platform::Window& window,
                                                             platform::GlContext& context);
void shutdownPlatformBackend() noexcept;
void newPlatformFrame();

// OpenGL 3: needs the window's OpenGL context to be current.
[[nodiscard]] std::optional<core::Error> initRendererBackend();
void shutdownRendererBackend() noexcept;
void newRendererFrame();
// Draws ImGui's draw data of the frame; the OpenGL state is restored afterwards.
void renderDrawData();

} // namespace engine::debug::detail
