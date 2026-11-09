// Island preview sandbox: the first end-to-end executable (docs/ENGINE_PLAN.md).
// M0.5 scope: OpenGL 4.5 core context and a test triangle.

#include "engine/core/Version.hpp"
#include "engine/platform/GlContext.hpp"
#include "engine/platform/Platform.hpp"
#include "engine/platform/Window.hpp"
#include "engine/render/Renderer.hpp"

#include <charconv>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string_view>
#include <system_error>

namespace {

#ifdef NDEBUG
constexpr bool kDebugBuild = false;
#else
constexpr bool kDebugBuild = true;
#endif

// Placeholder sky tone until the scene has real lighting.
constexpr engine::render::Color kClearColor{0.53F, 0.74F, 0.86F, 1.0F};

// Process exit codes, so smoke tests can tell failures apart.
constexpr int kExitUsage = 2;
constexpr int kExitStartupFailed = 1;
constexpr int kExitGlErrors = 3;

struct Options {
    // Stop after this many frames (smoke tests); empty means run until closed.
    std::optional<int> maxFrames;
    // Relative to the working directory; scripts/windows/run.bat and the CTest
    // tests start the program from the repository root.
    std::filesystem::path assetDirectory = "assets";
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
        const bool hasValue = i + 1 < argc;
        if (arg == "--frames" && hasValue) {
            options.maxFrames = parsePositiveInt(argv[++i]);
            if (!options.maxFrames) {
                return std::nullopt;
            }
        } else if (arg == "--assets" && hasValue) {
            options.assetDirectory = argv[++i];
        } else {
            return std::nullopt;
        }
    }
    return options;
}

int fail(const engine::core::Error& error)
{
    std::fprintf(stderr, "error: %s\n", error.message.c_str());
    return kExitStartupFailed;
}

} // namespace

int main(int argc, char** argv)
{
    const auto options = parseOptions(argc, argv);
    if (!options) {
        std::fprintf(stderr, "usage: island-preview [--frames N] [--assets DIR]\n");
        return kExitUsage;
    }

    // Declaration order is the required lifetime order: each object is
    // destroyed before the one it depends on (platform > window > context > renderer).
    auto platform = engine::platform::Platform::initialize();
    if (!platform) {
        return fail(platform.error());
    }

    const engine::platform::WindowDesc windowDesc{
        .title = "LowPolyEngine " + engine::core::toString(engine::core::engineVersion()) +
                 " - Island Preview",
        .openGl = true,
    };
    auto window = engine::platform::Window::create(platform.value(), windowDesc);
    if (!window) {
        return fail(window.error());
    }

    auto context = engine::platform::GlContext::create(
        window.value(), engine::platform::GlContextDesc{.debug = kDebugBuild});
    if (!context) {
        return fail(context.error());
    }
    if (!context.value().vsyncEnabled()) {
        std::fprintf(stderr, "warning: vsync is not available; the frame rate is not limited\n");
    }

    auto renderer = engine::render::Renderer::create({
        .glProcLoader = context.value().procLoader(),
        .shaderDirectory = options->assetDirectory / "shaders",
        .debugOutput = kDebugBuild,
    });
    if (!renderer) {
        return fail(renderer.error());
    }

    for (int frame = 0; !options->maxFrames || frame < *options->maxFrames; ++frame) {
        if (platform.value().pollEvents().quitRequested) {
            break;
        }
        const auto size = window.value().pixelSize();
        renderer.value().beginFrame(size.width, size.height, kClearColor);
        renderer.value().drawTestTriangle();
        renderer.value().endFrame();
        // With vsync this waits for the display refresh and paces the loop.
        context.value().swapBuffers();
    }

    if (const auto errors = renderer.value().errorCount(); errors > 0) {
        std::fprintf(stderr, "error: %u OpenGL error(s) occurred\n", errors);
        return kExitGlErrors;
    }
    return 0;
}
