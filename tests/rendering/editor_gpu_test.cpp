// GPU tier (M5.2, ADR 0027): Raven hosted by the engine on a real window and
// OpenGL context: the editor shell drawn and checked by its pixels, repeated
// runs, the OpenGL state comparison itself, Dear ImGui's renderer against the
// same comparison, and an application without the UI. The world in the
// viewport (M5.3) has its own tests in editor_viewport_gpu_test.cpp.
// Runs from the repository root (tests/rendering/CMakeLists.txt): the renderer
// reads assets/shaders.

#include "EditorPixels.hpp"

#include "application/EditorShell.hpp"
#include "application/ShellLayout.hpp"
#include "render/opengl/GlRavenBackend.hpp"

#include "engine/app/Application.hpp"
#include "engine/core/Image.hpp"
#include "engine/debug/DebugUi.hpp"
#include "engine/platform/GlContext.hpp"
#include "engine/platform/Platform.hpp"
#include "engine/platform/Window.hpp"
#include "engine/render/Renderer.hpp"

#include <Raven.h>
#include <glad/gl.h>
#include <imgui.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

using engine::render::opengl::GlStateGroups;
using engine::render::opengl::sameGlStateAround;
using lpe_gpu_test::brightPixels;
using lpe_gpu_test::centerOf;
using lpe_gpu_test::dominantColor;
using lpe_gpu_test::luminance;
using lpe_gpu_test::near;
using lpe_gpu_test::pixelAt;
using lpe_gpu_test::Rgb;

namespace {

// A colour Raven's dark theme never uses: any pixel of it left after a frame
// means Raven did not cover the window.
constexpr engine::render::Color kMagenta{1.0F, 0.0F, 1.0F, 1.0F};

const editor::ShellArea& area(const editor::EditorShell& shell, std::string_view name)
{
    return lpe_gpu_test::area(shell.areas(), name);
}

// The brightest pixel of the strip just above a docked area: its tab, whose
// title is light text on the dark theme.
int brightestTitlePixel(const engine::core::Image& image, const editor::ShellArea& a)
{
    int brightest = 0;
    for (int y = std::max(a.y - 24, 0); y < a.y - 2; ++y) {
        for (int x = a.x; x < std::min(a.x + 90, image.width); ++x) {
            brightest = std::max(brightest, luminance(pixelAt(image, x, y)));
        }
    }
    return brightest;
}

engine::app::ApplicationDesc shellDesc(std::uint64_t frames)
{
    engine::app::ApplicationDesc desc{
        .window = {.title = "lpe editor GPU test", .width = 1024, .height = 640},
        .clearColor = kMagenta,
        .maxFrames = frames,
    };
    desc.ui = engine::ui::UiDesc{.layoutXml = std::string(editor::layouts::shell()),
                                 .layoutName = std::string(editor::layouts::kShellName)};
    return desc;
}

void checkCleanRun(const engine::app::RunSummary& summary, std::uint64_t frames)
{
    CHECK(summary.renderedFrames == frames);
    CHECK(summary.openGlErrors == 0);
    CHECK(summary.failedScreenshots == 0);
    CHECK(summary.ui.enabled);
    CHECK(summary.ui.frames == frames);
    CHECK(summary.ui.glErrors == 0);
    CHECK(summary.ui.stateMismatches == 0);
    CHECK(summary.ui.ravenErrors == 0);
    CHECK(summary.ui.blocksAfterShutdown == 0);
    CHECK(Ris_initialized() == R_FALSE);
}

// Window, context and renderer for the direct OpenGL checks.
struct Gpu {
    std::optional<engine::platform::Platform> platform;
    std::optional<engine::platform::Window> window;
    std::optional<engine::platform::GlContext> context;
    std::optional<engine::render::Renderer> renderer;
};

Gpu createGpu()
{
    Gpu gpu;
    auto platform = engine::platform::Platform::initialize();
    REQUIRE(platform.hasValue());
    gpu.platform.emplace(std::move(platform).value());
    auto window = engine::platform::Window::create(
        *gpu.platform, {.title = "lpe state test", .width = 320, .height = 240, .openGl = true});
    REQUIRE(window.hasValue());
    gpu.window.emplace(std::move(window).value());
    auto context = engine::platform::GlContext::create(*gpu.window, {.vsync = false});
    REQUIRE(context.hasValue());
    gpu.context.emplace(std::move(context).value());
    auto renderer = engine::render::Renderer::create(
        {.glProcLoader = gpu.context->procLoader(), .shaderDirectory = "assets/shaders"});
    REQUIRE(renderer.hasValue());
    gpu.renderer.emplace(std::move(renderer).value());
    return gpu;
}

// An application that only records whether a Raven UI was there.
class NoUiApp final : public engine::app::Application {
public:
    std::optional<engine::core::Error> start(engine::app::AppContext& context) override
    {
        sawUi = context.ui() != nullptr;
        ravenRunning = Ris_initialized() != R_FALSE;
        return std::nullopt;
    }
    void update(engine::app::AppContext& /*context*/,
                const engine::core::FrameTime& /*time*/) override
    {
    }
    void render(engine::app::AppContext& /*context*/) override {}

    bool sawUi = true;
    bool ravenRunning = true;
};

} // namespace

TEST_CASE("The editor shell is drawn by Raven over the whole window", "[editor][ui][gpu]")
{
    const auto path = std::filesystem::temp_directory_path() / "lpe_gpu_test" / "editor-shell.png";
    std::error_code ignored;
    std::filesystem::remove(path, ignored);

    constexpr std::uint64_t kFrames = 12;
    editor::EditorOptions options;
    options.maxFrames = kFrames;
    options.screenshotPath = path;
    editor::EditorShell shell(options);
    const auto result = engine::app::run(shell, shellDesc(kFrames));
    REQUIRE(result.hasValue());
    checkCleanRun(result.value(), kFrames);

    const auto image = engine::core::readPng(path);
    REQUIRE(image.hasValue());
    const engine::core::Image& frame = image.value();
    REQUIRE(shell.areas().size() == editor::kShellAreas.size());

    // Every area lies inside the frame.
    for (const editor::ShellArea& a : shell.areas()) {
        INFO("area " << a.name);
        CHECK(a.width > 0);
        CHECK(a.height > 0);
        CHECK(a.x >= 0);
        CHECK(a.y >= 0);
        CHECK(a.x + a.width <= frame.width);
        CHECK(a.y + a.height <= frame.height);
    }

    // Raven covered the window: no pixel of the renderer's clear colour is left.
    std::size_t magenta = 0;
    for (int y = 0; y < frame.height; ++y) {
        for (int x = 0; x < frame.width; ++x) {
            magenta += near(pixelAt(frame, x, y), {255, 0, 255}, 8) ? 1U : 0U;
        }
    }
    CHECK(magenta == 0);

    // The viewport shows the world (M5.3), not its empty black; the empty
    // inspector has the theme's panel colour, which is not black; the toolbar
    // is drawn too. The hierarchy's tree (empty without a project) and the
    // console's list (M5.7) share the theme's list background, also not
    // black; the console shows the run's log in light text, the empty tree
    // none.
    const Rgb viewport = centerOf(frame, area(shell, "viewport"));
    const Rgb inspector = centerOf(frame, area(shell, "inspector"));
    const Rgb hierarchy = dominantColor(frame, area(shell, "hierarchy"));
    const Rgb console = dominantColor(frame, area(shell, "console"));
    const Rgb toolbar = centerOf(frame, area(shell, "toolbar"));
    CHECK(luminance(viewport) > 25);
    CHECK(luminance(inspector) > 20);
    CHECK(luminance(hierarchy) > 10);
    CHECK(near(hierarchy, console, 3));
    CHECK(luminance(toolbar) > 15);
    const editor::ShellArea& consoleArea = area(shell, "console");
    const editor::ShellArea& hierarchyArea = area(shell, "hierarchy");
    // Below the console's checkboxes, and below the hierarchy's buttons.
    CHECK(brightPixels(frame, consoleArea.x, consoleArea.y + 40, consoleArea.width,
                       consoleArea.height - 40, 150) > 100);
    CHECK(brightPixels(frame, hierarchyArea.x + 4, hierarchyArea.y + 50, hierarchyArea.width - 8,
                       hierarchyArea.height - 60, 150) == 0);

    // Each docked area has its titled tab above it.
    for (const std::string_view name : {"viewport", "hierarchy", "inspector", "console"}) {
        INFO("tab of " << name);
        CHECK(brightestTitlePixel(frame, area(shell, name)) > 140);
    }
    std::filesystem::remove(path, ignored);
}

TEST_CASE("Raven starts and stops with the application again and again", "[editor][ui][gpu]")
{
    // One Raven at a time, any number in sequence; the renderer is made anew
    // for each run, Raven's memory returns to zero after each.
    for (int run = 0; run < 3; ++run) {
        INFO("run " << run);
        editor::EditorShell shell(editor::EditorOptions{});
        const auto result = engine::app::run(shell, shellDesc(5));
        REQUIRE(result.hasValue());
        checkCleanRun(result.value(), 5);
        RMemoryStats memory{};
        Rmemory_get_stats(&memory);
        CHECK(memory.allocation_count == 0);
    }
}

TEST_CASE("An application without the UI does not start Raven", "[app][ui][gpu]")
{
    NoUiApp app;
    const auto result = engine::app::run(
        app, {.window = {.title = "lpe no UI test", .width = 320, .height = 240}, .maxFrames = 2});
    REQUIRE(result.hasValue());
    CHECK_FALSE(app.sawUi);
    CHECK_FALSE(app.ravenRunning);
    CHECK_FALSE(result.value().ui.enabled);
    CHECK(result.value().renderedFrames == 2);
}

TEST_CASE("The OpenGL state comparison sees what changed", "[ui][gpu]")
{
    Gpu gpu = createGpu();
    gpu.renderer->beginFrame(320, 240, {0.1F, 0.2F, 0.3F, 1.0F});
    CHECK(sameGlStateAround([] {}, GlStateGroups::All));
    // The clear colour (the final validation's fix 2) is part of the restored state.
    CHECK_FALSE(
        sameGlStateAround([] { glClearColor(1.0F, 0.0F, 0.0F, 1.0F); }, GlStateGroups::Restored));
    glClearColor(0.1F, 0.2F, 0.3F, 1.0F);
    // State the backend never writes is only in the full comparison.
    GLint depthFunc = 0;
    glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
    const GLenum other = depthFunc == GL_LESS ? GL_LEQUAL : GL_LESS;
    CHECK(sameGlStateAround([other] { glDepthFunc(other); }, GlStateGroups::Restored));
    CHECK_FALSE(sameGlStateAround([depthFunc] { glDepthFunc(static_cast<GLenum>(depthFunc)); },
                                  GlStateGroups::All));
    CHECK(sameGlStateAround([] {}, GlStateGroups::All));
    gpu.renderer->endFrame();
    CHECK(gpu.renderer->errorCount() == 0);
}

TEST_CASE("Dear ImGui's renderer leaves the OpenGL state as it was", "[debug][ui][gpu]")
{
    // The debug UI (ADR 0018) is checked with the comparison Raven's frame
    // passes, over all of its state.
    Gpu gpu = createGpu();
    auto ui = engine::debug::DebugUi::create(*gpu.window, *gpu.context);
    REQUIRE(ui.hasValue());
    for (int frame = 0; frame < 3; ++frame) {
        gpu.renderer->beginFrame(320, 240, {0.1F, 0.2F, 0.3F, 1.0F});
        ui.value().beginFrame();
        ImGui::Begin("state check");
        ImGui::TextUnformatted("Dear ImGui over the renderer");
        (void)ImGui::Button("Button");
        ImGui::End();
        CHECK(sameGlStateAround([&ui] { ui.value().render(); }, GlStateGroups::All));
        gpu.renderer->endFrame();
    }
    CHECK(gpu.renderer->errorCount() == 0);
}
