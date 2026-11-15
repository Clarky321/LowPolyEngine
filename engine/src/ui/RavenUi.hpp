#pragma once

// Internal to the engine: the one owner of Raven's process-wide state
// (ADR 0019, ADR 0027). engine::app::run creates it when ApplicationDesc::ui
// is set, after the window, the OpenGL context and the renderer, and destroys
// it before them.
//
// Lifetime, in this order:
//   create:   host platform registered, the OpenGL backend registered,
//             Rinit_ex (custom backend, render thread disabled: Raven draws on
//             the main thread, which owns the context), live reload and
//             auto-loading off at once (ADR 0026), the main window built from
//             the layout text, redrawn every frame, attached to the host.
//   frame():  Rdraw_frame between Renderer::beginFrame and endFrame, inside an
//             OpenGL state comparison; then the text input follows Raven's
//             focus.
//   shutdown: Rshutdown (Raven destroys its windows and calls the backend's
//             Shutdown while the context is current), the host platform
//             unregistered, Raven's allocations counted.
// Every failure on the way releases what was made. Only one RavenUi can exist
// at a time.

#include "engine/core/Result.hpp"
#include "engine/ui/Ui.hpp"

#include <cstdint>
#include <memory>

namespace engine::platform {
class Window;
} // namespace engine::platform

namespace engine::ui {

class RavenHost;

enum class RavenBackendKind : std::uint8_t {
    // The engine's OpenGL 4.5 backend on the current context (applications).
    OpenGl,
    // Raven's null backend: draws nothing, needs no context (headless tests).
    Null,
};

class RavenUi final : public Ui {
public:
    [[nodiscard]] static core::Result<std::unique_ptr<RavenUi>>
    create(platform::Window& window, const UiDesc& desc,
           RavenBackendKind backend = RavenBackendKind::OpenGl);

    ~RavenUi();
    RavenUi(const RavenUi&) = delete;
    RavenUi& operator=(const RavenUi&) = delete;
    RavenUi(RavenUi&&) = delete;
    RavenUi& operator=(RavenUi&&) = delete;

    [[nodiscard]] RavenId mainWindow() const noexcept override;
    [[nodiscard]] float scale() const noexcept override;

    // False once Raven closed the main window or ended (Rquit): the loop stops.
    [[nodiscard]] bool isOpen() const noexcept;

    // One Raven frame; does nothing once closed.
    void frame();

    // Ends Raven (once; the destructor does it otherwise) and returns the
    // run's statistics, with Raven's allocations counted after Rshutdown.
    UiRunStats shutdown() noexcept;

    // The statistics so far.
    [[nodiscard]] UiRunStats stats() const noexcept;

    // Whether a RavenUi exists in this process now.
    [[nodiscard]] static bool exists() noexcept;

    // The host platform bridge (for the tests); valid until shutdown().
    [[nodiscard]] RavenHost* host() noexcept { return host_.get(); }

private:
    RavenUi(std::unique_ptr<RavenHost> host, RavenBackendKind backend);

    std::unique_ptr<RavenHost> host_;
    RavenBackendKind backend_;
    RavenId mainWindow_ = kInvalidRavenId;
    bool running_ = false;
    bool ended_ = false;
    std::uint64_t frames_ = 0;
    std::uint32_t frameStateMismatches_ = 0;
    UiRunStats final_{};
};

} // namespace engine::ui
