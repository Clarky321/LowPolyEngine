// Island preview sandbox: the first end-to-end executable (docs/ENGINE_PLAN.md).
// M0.4 scope: open a window and run an event loop until it is closed.

#include "engine/core/Version.hpp"
#include "engine/platform/Platform.hpp"
#include "engine/platform/Window.hpp"

#include <charconv>
#include <chrono>
#include <cstdio>
#include <optional>
#include <string_view>
#include <system_error>
#include <thread>

namespace {

struct Options {
    // Stop after this many frames. Used by the CTest smoke test; empty means
    // run until the window is closed.
    std::optional<int> maxFrames;
};

std::optional<int> parsePositiveInt(std::string_view text)
{
    int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value <= 0) {
        return std::nullopt;
    }
    return value;
}

std::optional<Options> parseOptions(int argc, char** argv)
{
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--frames" && i + 1 < argc) {
            options.maxFrames = parsePositiveInt(argv[++i]);
            if (!options.maxFrames) {
                return std::nullopt;
            }
        } else {
            return std::nullopt;
        }
    }
    return options;
}

} // namespace

int main(int argc, char** argv)
{
    const auto options = parseOptions(argc, argv);
    if (!options) {
        std::fprintf(stderr, "usage: island-preview [--frames N]\n");
        return 2;
    }

    auto platform = engine::platform::Platform::initialize();
    if (!platform) {
        std::fprintf(stderr, "error: %s\n", platform.error().message.c_str());
        return 1;
    }

    const engine::platform::WindowDesc windowDesc{
        .title = "LowPolyEngine " + engine::core::toString(engine::core::engineVersion()) +
                 " - Island Preview",
    };
    // Declared after the platform, so it is destroyed first (Window.hpp contract).
    auto window = engine::platform::Window::create(platform.value(), windowDesc);
    if (!window) {
        std::fprintf(stderr, "error: %s\n", window.error().message.c_str());
        return 1;
    }

    // Nothing is rendered yet; M0.5 adds the OpenGL context and vsync-paced
    // buffer swaps. Until then, sleep so this loop does not spin a CPU core.
    constexpr auto kFrameInterval = std::chrono::milliseconds(16);
    for (int frame = 0; !options->maxFrames || frame < *options->maxFrames; ++frame) {
        if (platform.value().pollEvents().quitRequested) {
            break;
        }
        std::this_thread::sleep_for(kFrameInterval);
    }
    return 0;
}
