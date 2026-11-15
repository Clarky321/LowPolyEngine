#pragma once

// lpe-editor's command line (M5.2): --frames N, --screenshot FILE, --size WxH.

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
    // Stop after this many frames (smoke tests, screenshots); empty: run until
    // the window is closed.
    std::optional<std::uint64_t> maxFrames;
    // Save the last frame as PNG (made by the editor itself) and exit.
    std::optional<std::filesystem::path> screenshotPath;
    // The window's size in screen coordinates.
    int width = kDefaultWindowWidth;
    int height = kDefaultWindowHeight;
};

// The options of the arguments after the program name. An unknown argument, a
// missing value or an invalid one is an error that names it.
[[nodiscard]] engine::core::Result<EditorOptions>
parseEditorOptions(std::span<const std::string_view> arguments);

// The usage line printed with such an error.
inline constexpr std::string_view kEditorUsage =
    "usage: lpe-editor [--frames N] [--screenshot FILE.png] [--size WIDTHxHEIGHT]";

} // namespace editor
