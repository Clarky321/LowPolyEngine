// lpe-editor (M5): the LowPolyEngine editor on Raven (ADR 0019, ADR 0027).
// M5.2: the shell (toolbar, viewport, hierarchy, inspector, console); M5.3:
// the generated world in the viewport with a free-fly camera.
//
//   lpe-editor [--frames N] [--screenshot FILE.png] [--size WIDTHxHEIGHT]
//
// Run it from the repository root: the renderer reads its shaders from
// assets/shaders. Without --frames the window stays open until it is closed.
//
// In the viewport (click it to give it the keyboard; it has it at the start):
// hold the right mouse button to look around, W/A/S/D to fly, E up, Q down,
// Shift faster, the mouse wheel changes the speed.

#include "application/EditorOptions.hpp"
#include "application/EditorShell.hpp"
#include "application/ShellLayout.hpp"

#include "engine/app/Application.hpp"
#include "engine/core/Log.hpp"
#include "engine/core/Version.hpp"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace {

#ifdef NDEBUG
constexpr bool kDebugBuild = false;
#else
constexpr bool kDebugBuild = true;
#endif

// Behind Raven's frame, which covers the whole window.
constexpr engine::render::Color kClearColor{0.08F, 0.09F, 0.11F, 1.0F};

// Process exit codes, so smoke tests can tell failures apart.
constexpr int kExitStartupFailed = 1;
constexpr int kExitUsage = 2;
constexpr int kExitGlErrors = 3;
constexpr int kExitScreenshotFailed = 4;
constexpr int kExitUiProblems = 5;

} // namespace

int main(int argc, char** argv)
{
    std::vector<std::string_view> arguments;
    for (int i = 1; i < argc; ++i) {
        arguments.emplace_back(argv[i]);
    }
    const auto options = editor::parseEditorOptions(arguments);
    if (!options) {
        const std::string usage(editor::kEditorUsage);
        std::fprintf(stderr, "lpe-editor: %s\n%s\n", options.error().message.c_str(),
                     usage.c_str());
        return kExitUsage;
    }

    engine::app::ApplicationDesc desc{
        .window = {.title = "LowPolyEngine " +
                            engine::core::toString(engine::core::engineVersion()) + " - Editor",
                   .width = options.value().width,
                   .height = options.value().height},
        .context = {.debug = kDebugBuild},
        .clearColor = kClearColor,
        .maxFrames = options.value().maxFrames,
    };
    desc.ui = engine::ui::UiDesc{.layoutXml = std::string(editor::layouts::shell()),
                                 .layoutName = std::string(editor::layouts::kShellName)};

    editor::EditorShell shell(options.value());
    const auto result = engine::app::run(shell, desc);
    if (!result) {
        engine::core::logError("editor", "{}", result.error().message);
        return kExitStartupFailed;
    }
    const engine::app::RunSummary& summary = result.value();
    const engine::ui::UiRunStats& ui = summary.ui;
    engine::core::logInfo("editor",
                          "{} frames, {} UI frames, {} OpenGL errors, {} OpenGL state "
                          "mismatches, {} Raven errors, {} Raven blocks after shutdown",
                          summary.renderedFrames, ui.frames, summary.openGlErrors,
                          ui.stateMismatches, ui.ravenErrors, ui.blocksAfterShutdown);
    if (summary.openGlErrors > 0) {
        engine::core::logError("editor", "{} OpenGL error(s) occurred", summary.openGlErrors);
        return kExitGlErrors;
    }
    if (summary.failedScreenshots > 0) {
        return kExitScreenshotFailed;
    }
    if (ui.stateMismatches > 0 || ui.ravenErrors > 0 || ui.blocksAfterShutdown > 0) {
        engine::core::logError("editor", "the UI left OpenGL state changed, reported errors or "
                                         "kept memory after shutdown");
        return kExitUiProblems;
    }
    return 0;
}
