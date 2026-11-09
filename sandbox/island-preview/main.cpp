// Island preview sandbox: the first end-to-end executable (docs/ENGINE_PLAN.md).
// M1 scope: engine loop, input, free-fly camera and a static test scene.

#include "engine/app/Application.hpp"
#include "engine/core/Log.hpp"
#include "engine/core/Version.hpp"

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
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

// Frames rendered before a --screenshot is taken when --frames is not given.
// Long enough for the frame statistics to fill their first window.
constexpr std::uint64_t kDefaultScreenshotFrames = 120;

// Process exit codes, so smoke tests can tell failures apart.
constexpr int kExitStartupFailed = 1;
constexpr int kExitUsage = 2;
constexpr int kExitGlErrors = 3;
constexpr int kExitScreenshotFailed = 4;

struct Options {
    // Stop after this many frames (smoke tests); empty means run until closed.
    std::optional<std::uint64_t> maxFrames;
    // Relative to the working directory; scripts/windows/run.bat and the CTest
    // tests start the program from the repository root.
    std::filesystem::path assetDirectory = "assets";
    // Save the last frame as PNG and exit.
    std::optional<std::filesystem::path> screenshotPath;
};

std::optional<std::uint64_t> parsePositiveInt(std::string_view text)
{
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value == 0) {
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
        } else if (arg == "--screenshot" && hasValue) {
            options.screenshotPath = std::filesystem::path(argv[++i]);
        } else {
            return std::nullopt;
        }
    }
    if (options.screenshotPath && !options.maxFrames) {
        options.maxFrames = kDefaultScreenshotFrames;
    }
    return options;
}

using engine::geometry::MeshData;

// Bring-up geometry in normalized device coordinates (identity view), counter-clockwise.
MeshData makeTriangle()
{
    MeshData mesh;
    mesh.positions = {{-0.6F, -0.5F, 0.0F}, {0.6F, -0.5F, 0.0F}, {0.0F, 0.6F, 0.0F}};
    mesh.colors = {
        {0.95F, 0.45F, 0.25F, 1.0F}, {0.30F, 0.75F, 0.45F, 1.0F}, {0.30F, 0.55F, 0.90F, 1.0F}};
    mesh.indices = {0, 1, 2};
    return mesh;
}

// Indexed quad (4 shared vertices, 2 triangles) farther away than the triangle
// (larger NDC depth). It is drawn after the triangle, so only the depth test
// keeps it behind.
MeshData makeBackdropQuad()
{
    MeshData mesh;
    mesh.positions = {
        {-0.15F, -0.75F, 0.5F}, {0.9F, -0.75F, 0.5F}, {0.9F, 0.25F, 0.5F}, {-0.15F, 0.25F, 0.5F}};
    mesh.colors = {{0.98F, 0.86F, 0.45F, 1.0F},
                   {0.98F, 0.70F, 0.35F, 1.0F},
                   {1.00F, 0.95F, 0.80F, 1.0F},
                   {1.00F, 0.98F, 0.90F, 1.0F}};
    mesh.indices = {0, 1, 2, 0, 2, 3};
    return mesh;
}

class IslandPreview final : public engine::app::Application {
public:
    explicit IslandPreview(const Options& options) : options_(options) {}

    std::optional<engine::core::Error> start(engine::app::AppContext& context) override
    {
        auto& renderer = context.renderer();
        auto triangle = renderer.createMesh(makeTriangle());
        if (!triangle) {
            return triangle.error();
        }
        triangle_ = triangle.value();
        auto quad = renderer.createMesh(makeBackdropQuad());
        if (!quad) {
            return quad.error();
        }
        quad_ = quad.value();
        return std::nullopt;
    }

    void stop(engine::app::AppContext& context) override
    {
        context.renderer().destroyMesh(triangle_);
        context.renderer().destroyMesh(quad_);
    }

    void update(engine::app::AppContext& context, const engine::core::FrameTime& time) override
    {
        using engine::platform::Key;
        const auto& input = context.input();
        if (input.wasPressed(Key::Escape)) {
            context.requestQuit();
            return;
        }
        if (input.wasPressed(Key::F12)) {
            // Relative to the working directory (the repository root with run.bat);
            // the folder is ignored by Git.
            context.requestScreenshot(
                std::format("screenshots/island-preview-{:06}.png", time.frameIndex));
        }

        // The last frame of a --screenshot run is saved.
        if (options_.screenshotPath && options_.maxFrames &&
            time.frameIndex + 1 == *options_.maxFrames) {
            context.requestScreenshot(*options_.screenshotPath);
        }
    }

    void render(engine::app::AppContext& context) override
    {
        context.renderer().drawMesh(triangle_);
        context.renderer().drawMesh(quad_);
    }

private:
    Options options_;
    engine::render::MeshHandle triangle_;
    engine::render::MeshHandle quad_;
};

} // namespace

int main(int argc, char** argv)
{
    const auto options = parseOptions(argc, argv);
    if (!options) {
        std::fprintf(stderr,
                     "usage: island-preview [--frames N] [--assets DIR] [--screenshot FILE.png]\n");
        return kExitUsage;
    }

    const engine::app::ApplicationDesc desc{
        .window = {.title = "LowPolyEngine " +
                            engine::core::toString(engine::core::engineVersion()) +
                            " - Island Preview"},
        .context = {.debug = kDebugBuild},
        .assetDirectory = options->assetDirectory,
        .clearColor = kClearColor,
        .maxFrames = options->maxFrames,
    };

    IslandPreview app(*options);
    const auto result = engine::app::run(app, desc);
    if (!result) {
        engine::core::logError("app", "{}", result.error().message);
        return kExitStartupFailed;
    }
    if (const auto errors = result.value().openGlErrors; errors > 0) {
        engine::core::logError("app", "{} OpenGL error(s) occurred", errors);
        return kExitGlErrors;
    }
    if (result.value().failedScreenshots > 0) {
        return kExitScreenshotFailed;
    }
    return 0;
}
