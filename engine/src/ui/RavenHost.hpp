#pragma once

// Internal to the engine: Raven's host platform as the UI owner (RavenUi)
// sees it, with no SDL type (ADR 0027). The implementation is the SDL3 host
// bridge in engine/src/platform/sdl3/Sdl3RavenHost.cpp, which also receives
// the window's raw events from Platform::pollEvents while a Raven window is
// attached.

#include <raven/raven_core.h>

#include <memory>

namespace engine::platform {
class Window;
} // namespace engine::platform

namespace engine::ui {

class RavenHost {
public:
    virtual ~RavenHost() = default;
    RavenHost(const RavenHost&) = delete;
    RavenHost& operator=(const RavenHost&) = delete;
    RavenHost(RavenHost&&) = delete;
    RavenHost& operator=(RavenHost&&) = delete;

    // Rhost_platform_register with this host's callbacks: before Rinit.
    virtual void registerPlatform() = 0;

    // The Raven window shown in the engine's window, after Rwindow_create:
    // it gets the window's pixel size, display scale and keyboard focus, and
    // from then on the window's input events. One host is attached at a time.
    virtual void attach(Rid window) = 0;
    virtual void detach() noexcept = 0;

    // The attached window, or R_INVALID_ID before attach, after detach, and
    // as soon as Raven has destroyed it (an accepted close request).
    [[nodiscard]] virtual Rid attachedWindow() const noexcept = 0;

    // After each Raven frame: text input on while a Raven text field has the
    // keyboard, and the IME area at its caret (Rwindow_get_text_input_rect).
    virtual void syncTextInput() = 0;

    // Pixels per logical unit given to Raven (the window's display scale).
    [[nodiscard]] virtual float scale() const noexcept = 0;

protected:
    RavenHost() = default;
};

// The SDL3 host bridge for the engine's window (engine/src/platform/sdl3/).
[[nodiscard]] std::unique_ptr<RavenHost> createRavenHost(platform::Window& window);

} // namespace engine::ui
