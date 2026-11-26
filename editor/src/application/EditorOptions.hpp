#pragma once

// lpe-editor's command line: a project folder (M5.6) and the options --frames N,
// --screenshot FILE and --size WxH (M5.2).

#include "engine/core/Result.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>

namespace editor {

// Frames rendered before a --screenshot is taken when --frames is not given:
// enough for Raven's first layout and a few drawn frames.
inline constexpr std::uint64_t kDefaultScreenshotFrames = 30;
inline constexpr int kDefaultWindowWidth = 1280;
inline constexpr int kDefaultWindowHeight = 760;

struct EditorOptions {
    // The project to open at start (M5.4's folder, with project.json); empty:
    // no project, the viewport shows the default world.
    std::optional<std::filesystem::path> project;
    // Stop after this many frames (smoke tests, screenshots); empty: run until
    // the window is closed.
    std::optional<std::uint64_t> maxFrames;
    // Save the last frame as PNG (made by the editor itself) and exit.
    std::optional<std::filesystem::path> screenshotPath;
    // The window's size in screen coordinates.
    int width = kDefaultWindowWidth;
    int height = kDefaultWindowHeight;
};

// The options of the arguments after the program name. An argument that does
// not start with "--" is the project folder; a second one is an error, like an
// unknown option, a missing value or an invalid one. The error names it.
[[nodiscard]] engine::core::Result<EditorOptions>
parseEditorOptions(std::span<const std::string_view> arguments);

// The usage line printed with such an error.
inline constexpr std::string_view kEditorUsage =
    "usage: lpe-editor [PROJECT_FOLDER] [--frames N] [--screenshot FILE.png] "
    "[--size WIDTHxHEIGHT]";

} // namespace editor
