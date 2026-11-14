#pragma once

// Dear ImGui as a development and debug UI (ADR 0018), not the editor (M5).
// The application loop owns one when ApplicationDesc::debugUi is set: a
// program builds its UI windows with the ImGui API (#include <imgui.h>) in
// Application::update, and the loop draws them over the frame after
// Application::render, so they are part of screenshots. The SDL3 and OpenGL 3
// backends stay inside the platform and renderer layers; no SDL or OpenGL type
// appears here.

#include "engine/core/Result.hpp"

namespace engine::platform {
class GlContext;
class Window;
} // namespace engine::platform

namespace engine::debug {

class DebugUi {
public:
    // Creates the ImGui context and its backends for the window, whose OpenGL
    // context must be current. Only one debug UI can exist at a time. ImGui
    // keeps no settings file (no imgui.ini is written).
    [[nodiscard]] static core::Result<DebugUi> create(platform::Window& window,
                                                      platform::GlContext& context);

    ~DebugUi();
    DebugUi(DebugUi&& other) noexcept;
    DebugUi& operator=(DebugUi&& other) noexcept;
    DebugUi(const DebugUi&) = delete;
    DebugUi& operator=(const DebugUi&) = delete;

    // Starts a UI frame: after the frame's OS events, before ImGui windows are
    // built. A frame already started is ended first.
    void beginFrame();

    // Draws the frame's UI over what is rendered, between
    // Renderer::beginFrame and Renderer::endFrame. Does nothing without a
    // started frame.
    void render();

    // Ends a started frame without drawing it (e.g. while minimized).
    void endFrame();

    // Whether the UI uses the mouse or the keyboard in this frame: the pointer
    // is over one of its windows, or a field is being edited. The application
    // should not react to them then (no camera movement, no shortcuts).
    [[nodiscard]] bool wantsMouse() const noexcept;
    [[nodiscard]] bool wantsKeyboard() const noexcept;

private:
    DebugUi() = default;
    void shutdown() noexcept;

    // False for moved-from instances.
    bool active_ = false;
    bool frameStarted_ = false;
};

} // namespace engine::debug
