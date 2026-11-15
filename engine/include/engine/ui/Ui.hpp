#pragma once

// The engine's UI for applications built on Raven, the editor's interface
// (ADR 0019, ADR 0027). An application turns it on with ApplicationDesc::ui;
// the engine then owns Raven's process-wide state (Rinit and Rshutdown, once
// each), shows one Raven window, made from the application's XML layout, in
// the engine's SDL3 window, hands it the window's input, and draws it with
// OpenGL between Renderer::beginFrame and endFrame. Dear ImGui (the debug UI,
// ADR 0018) is not combined with it.
//
// No Raven, SDL or OpenGL type appears here. A program that builds its
// interface with Raven's C API (the editor) includes Raven's headers itself
// and uses RavenId as Raven's Rid; the engine's other headers never include
// them.

#include <cstddef>
#include <cstdint>
#include <string>

namespace engine::ui {

// A Raven object handle (Raven's Rid) as a plain integer.
using RavenId = std::uint32_t;
// Raven's R_INVALID_ID.
inline constexpr RavenId kInvalidRavenId = 0xFFFFFFFFU;

struct UiDesc {
    // The main window: a Raven XML document whose root is <Window>, as text.
    // The application reads it; Raven loads nothing by itself (no live
    // reload, no auto-loading from search paths: ADR 0026).
    std::string layoutXml{};
    // Names the layout in messages (e.g. the file it was read from).
    std::string layoutName = "layout";
};

// The running UI, valid inside the Application callbacks (AppContext::ui()).
class Ui {
public:
    // The Raven window shown in the engine's window: the application finds
    // its widgets here (Rwidget_find) and sets their callbacks. The engine
    // owns it; once Raven closes it (an accepted close request), it returns
    // kInvalidRavenId and the loop ends.
    [[nodiscard]] virtual RavenId mainWindow() const noexcept = 0;

    // Pixels per logical unit Raven lays out with: the window's display scale.
    [[nodiscard]] virtual float scale() const noexcept = 0;

protected:
    Ui() = default;
    ~Ui() = default;
    Ui(const Ui&) = default;
    Ui& operator=(const Ui&) = default;
};

// What the UI did during a run (RunSummary::ui).
struct UiRunStats {
    bool enabled = false;
    // Frames the UI's OpenGL backend drew.
    std::uint64_t frames = 0;
    // OpenGL errors inside the UI's backend; RunSummary::openGlErrors includes them.
    std::uint32_t glErrors = 0;
    // Frames after which the OpenGL state was not as before: the backend's own
    // comparison and the one around Raven's whole frame.
    std::uint32_t stateMismatches = 0;
    // Errors and warnings Raven reported (also in the log).
    std::uint32_t ravenErrors = 0;
    // Raven's allocations still held after Rshutdown: 0 unless Raven leaks.
    std::size_t blocksAfterShutdown = 0;
};

} // namespace engine::ui
