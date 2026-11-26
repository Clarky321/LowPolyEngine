// GPU tier (M5.3): the generated world in the editor's viewport, drawn by the
// engine's renderer from the viewport's draw callback inside Raven's frame:
// sky and islands inside the viewport's rectangle and nowhere else, the world
// following the window's size, and the world's renderer resources released
// when the editor stops.
// Runs from the repository root (tests/rendering/CMakeLists.txt): the renderer
// reads assets/shaders.

#include "EditorPixels.hpp"

#include "application/EditorShell.hpp"
#include "application/ShellLayout.hpp"
#include "platform/sdl3/Sdl3Window.hpp"
#include "viewport/WorldViewport.hpp"

#include "engine/app/Application.hpp"
#include "engine/core/Image.hpp"
#include "engine/render/Renderer.hpp"
#include "engine/world/Sky.hpp"

#include <Raven.h>
#include <SDL3/SDL.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

using lpe_gpu_test::difference;
using lpe_gpu_test::luminance;
using lpe_gpu_test::pixelAt;
using lpe_gpu_test::Rgb;

namespace {

// Behind Raven's frame; Raven covers the whole window, so it never shows.
constexpr engine::render::Color kMagenta{1.0F, 0.0F, 1.0F, 1.0F};
constexpr int kWidth = 1024;
constexpr int kHeight = 640;
constexpr std::uint64_t kFrames = 20;

std::filesystem::path outputPath(std::string_view name)
{
    const auto directory = std::filesystem::temp_directory_path() / "lpe_gpu_test";
    std::error_code ignored;
    std::filesystem::create_directories(directory, ignored);
    auto path = directory / name;
    std::filesystem::remove(path, ignored);
    return path;
}

engine::app::ApplicationDesc shellDesc(std::uint64_t frames)
{
    engine::app::ApplicationDesc desc{
        .window = {.title = "lpe editor viewport GPU test", .width = kWidth, .height = kHeight},
        .clearColor = kMagenta,
        .maxFrames = frames,
    };
    desc.ui = engine::ui::UiDesc{.layoutXml = std::string(editor::layouts::shell()),
                                 .layoutName = std::string(editor::layouts::kShellName)};
    return desc;
}

void checkCleanRun(const engine::app::RunSummary& summary)
{
    CHECK(summary.openGlErrors == 0);
    CHECK(summary.failedScreenshots == 0);
    CHECK(summary.ui.frames == summary.renderedFrames);
    CHECK(summary.ui.glErrors == 0);
    CHECK(summary.ui.stateMismatches == 0);
    CHECK(summary.ui.ravenErrors == 0);
    CHECK(summary.ui.blocksAfterShutdown == 0);
}

// The sky dome's colours in 8-bit display values (the renderer writes vertex
// colours as they are, ADR 0009), finely sampled over every elevation.
std::vector<Rgb> skyColours()
{
    const engine::world::SkyGradient sky = engine::world::floatingWorldSky();
    std::vector<Rgb> colours;
    constexpr int kSamples = 1024;
    for (int i = 0; i <= kSamples; ++i) {
        const float elevation = -1.0F + 2.0F * static_cast<float>(i) / kSamples;
        const engine::math::Vec3 c = engine::world::skyColor(sky, elevation);
        colours.push_back({static_cast<int>(std::lround(c.r * 255.0F)),
                           static_cast<int>(std::lround(c.g * 255.0F)),
                           static_cast<int>(std::lround(c.b * 255.0F))});
    }
    return colours;
}

// A pixel of the sky: within a few steps of one of its colours. Islands,
// clouds and water are far from all of them.
bool isSky(const Rgb& pixel, const std::vector<Rgb>& sky)
{
    for (const Rgb& colour : sky) {
        if (difference(pixel, colour) <= 6) {
            return true;
        }
    }
    return false;
}

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    [[nodiscard]] bool contains(int px, int py) const
    {
        return px >= x && py >= y && px < x + width && py < y + height;
    }
    // Grown (or, with a negative margin, shrunk) on every side.
    [[nodiscard]] Rect grown(int margin) const
    {
        return {x - margin, y - margin, width + 2 * margin, height + 2 * margin};
    }
};

Rect viewportRect(const std::vector<editor::ShellArea>& areas)
{
    const editor::ShellArea& a = lpe_gpu_test::area(areas, "viewport");
    return {a.x, a.y, a.width, a.height};
}

// What the viewport's rectangle shows: pixels of the sky, of the world (not
// sky) and of an empty, black viewport. The border pixels, where the area's
// rounding may differ from Raven's by a pixel, are left out.
struct ViewportPixels {
    std::size_t total = 0;
    std::size_t sky = 0;
    std::size_t world = 0;
    std::size_t black = 0;
};

ViewportPixels classify(const engine::core::Image& frame, const Rect& viewport)
{
    const std::vector<Rgb> sky = skyColours();
    const Rect inside = viewport.grown(-2);
    ViewportPixels pixels;
    for (int y = inside.y; y < inside.y + inside.height; ++y) {
        for (int x = inside.x; x < inside.x + inside.width; ++x) {
            const Rgb pixel = pixelAt(frame, x, y);
            ++pixels.total;
            if (luminance(pixel) < 10) {
                ++pixels.black;
            } else if (isSky(pixel, sky)) {
                ++pixels.sky;
            } else {
                ++pixels.world;
            }
        }
    }
    return pixels;
}

// The editor, with what the checks need: the shell's areas, the world's draws
// and the renderer's resources once the editor has stopped. It saves the last
// frame; given a size to resize to, it first changes the window's size at frame 3, waits
// until the framebuffer follows and a few frames more, then saves that frame
// and ends the run.
class EditorProbe final : public engine::app::Application {
public:
    EditorProbe(std::filesystem::path screenshot, std::uint64_t frames,
                engine::platform::PixelSize resizeTo = {})
        : screenshot_(std::move(screenshot)), frames_(frames), resizeTo_(resizeTo),
          resize_(resizeTo.width > 0 && resizeTo.height > 0)
    {
    }

    std::optional<engine::core::Error> start(engine::app::AppContext& context) override
    {
        initialSize = context.framebufferSize();
        return shell_.start(context);
    }

    void update(engine::app::AppContext& context, const engine::core::FrameTime& time) override
    {
        shell_.update(context, time);
        if (!resize_) {
            if (time.frameIndex + 1 == frames_) {
                context.requestScreenshot(screenshot_);
            }
            return;
        }
        const engine::platform::PixelSize size = context.framebufferSize();
        if (time.frameIndex == 3) {
            areasBefore = shell_.areas();
            SDL_Window* window =
                engine::platform::detail::NativeWindowAccess::handle(context.window());
            resizeRequested =
                window != nullptr && SDL_SetWindowSize(window, resizeTo_.width, resizeTo_.height);
        }
        if (resizeRequested &&
            (size.width != initialSize.width || size.height != initialSize.height)) {
            ++framesAfterResize;
        }
        if (framesAfterResize == 8) {
            finalSize = size;
            context.requestScreenshot(screenshot_);
        } else if (framesAfterResize > 8) {
            context.requestQuit();
        }
    }

    void render(engine::app::AppContext& context) override { shell_.render(context); }

    void stop(engine::app::AppContext& context) override
    {
        if (const editor::WorldViewport* world = shell_.world()) {
            drawnFrames = world->drawnFrames();
            lastDrawnSize = world->lastDrawnSize();
        }
        countsBeforeStop = context.renderer().resourceCounts();
        shell_.stop(context);
        countsAfterStop = context.renderer().resourceCounts();
    }

    [[nodiscard]] const std::vector<editor::ShellArea>& areas() const { return shell_.areas(); }

    engine::platform::PixelSize initialSize;
    engine::platform::PixelSize finalSize;
    std::vector<editor::ShellArea> areasBefore;
    bool resizeRequested = false;
    int framesAfterResize = 0;
    std::uint64_t drawnFrames = 0;
    editor::ViewportSize lastDrawnSize;
    engine::render::ResourceCounts countsBeforeStop;
    engine::render::ResourceCounts countsAfterStop;

private:
    editor::EditorShell shell_{editor::EditorOptions{}};
    std::filesystem::path screenshot_;
    std::uint64_t frames_;
    // A plain size and a flag rather than std::optional (GCC's -O3 reports
    // false "may be used uninitialized" warnings for optionals).
    engine::platform::PixelSize resizeTo_;
    bool resize_ = false;
};

// The shell as M5.2 drew it, the viewport given the keyboard as the editor
// gives it, but nothing drawn in it: the frame the editor's must equal outside
// the viewport.
class EmptyShell final : public engine::app::Application {
public:
    EmptyShell(std::filesystem::path screenshot, std::uint64_t frames)
        : screenshot_(std::move(screenshot)), frames_(frames)
    {
    }

    std::optional<engine::core::Error> start(engine::app::AppContext& context) override
    {
        if (context.ui() == nullptr) {
            return engine::core::Error{"no Raven UI"};
        }
        Rwidget_focus(Rwidget_find(context.ui()->mainWindow(), "viewport"));
        return std::nullopt;
    }
    void update(engine::app::AppContext& context, const engine::core::FrameTime& time) override
    {
        if (time.frameIndex + 1 == frames_) {
            context.requestScreenshot(screenshot_);
        }
    }
    void render(engine::app::AppContext& /*context*/) override {}

private:
    std::filesystem::path screenshot_;
    std::uint64_t frames_;
};

} // namespace

TEST_CASE("The viewport shows the world inside its rectangle and nowhere else",
          "[editor][viewport][gpu]")
{
    const auto worldPath = outputPath("editor-viewport-world.png");
    const auto emptyPath = outputPath("editor-viewport-empty.png");

    EditorProbe editor(worldPath, kFrames);
    const auto worldRun = engine::app::run(editor, shellDesc(kFrames));
    REQUIRE(worldRun.hasValue());
    checkCleanRun(worldRun.value());
    CHECK(worldRun.value().renderedFrames == kFrames);

    EmptyShell empty(emptyPath, kFrames);
    const auto emptyRun = engine::app::run(empty, shellDesc(kFrames));
    REQUIRE(emptyRun.hasValue());
    checkCleanRun(emptyRun.value());

    const auto worldImage = engine::core::readPng(worldPath);
    const auto emptyImage = engine::core::readPng(emptyPath);
    REQUIRE(worldImage.hasValue());
    REQUIRE(emptyImage.hasValue());
    const engine::core::Image& withWorld = worldImage.value();
    const engine::core::Image& without = emptyImage.value();
    REQUIRE(withWorld.width == without.width);
    REQUIRE(withWorld.height == without.height);

    // Drawn in every frame, at the viewport's size.
    const Rect viewport = viewportRect(editor.areas());
    CHECK(editor.drawnFrames == kFrames);
    CHECK(std::abs(editor.lastDrawnSize.width - viewport.width) <= 1);
    CHECK(std::abs(editor.lastDrawnSize.height - viewport.height) <= 1);

    // Inside: sky and islands, clouds and water over all of it; nothing of the
    // empty viewport, which is black without the world.
    const ViewportPixels inside = classify(withWorld, viewport);
    const ViewportPixels insideEmpty = classify(without, viewport);
    INFO("viewport " << viewport.width << "x" << viewport.height << " at (" << viewport.x << ", "
                     << viewport.y << "): " << inside.sky << " sky, " << inside.world << " world, "
                     << inside.black << " black of " << inside.total);
    CHECK(insideEmpty.black == insideEmpty.total);
    CHECK(inside.black == 0);
    CHECK(inside.sky > inside.total / 4);
    CHECK(inside.world > inside.total / 20);

    // Outside: the frame is the empty shell's, pixel for pixel, but for the
    // console (M5.7), which shows the editor's log in one run and nothing in
    // the other.
    const Rect beyond = viewport.grown(2);
    const editor::ShellArea& consoleArea = lpe_gpu_test::area(editor.areas(), "console");
    const Rect console =
        Rect{consoleArea.x, consoleArea.y, consoleArea.width, consoleArea.height}.grown(2);
    std::size_t outside = 0;
    std::size_t changed = 0;
    for (int y = 0; y < withWorld.height; ++y) {
        for (int x = 0; x < withWorld.width; ++x) {
            if (beyond.contains(x, y) || console.contains(x, y)) {
                continue;
            }
            ++outside;
            if (difference(pixelAt(withWorld, x, y), pixelAt(without, x, y)) > 2) {
                ++changed;
            }
        }
    }
    INFO(changed << " of " << outside << " pixels outside the viewport changed");
    CHECK(outside > 0);
    CHECK(changed == 0);

    // The world and its sky held renderer resources, and gave them all back.
    CHECK(editor.countsBeforeStop.meshes > 0);
    CHECK(editor.countsBeforeStop.materials == 2);
    CHECK(editor.countsAfterStop.meshes == 0);
    CHECK(editor.countsAfterStop.textures == 0);
    CHECK(editor.countsAfterStop.materials == 0);

    std::error_code ignored;
    std::filesystem::remove(worldPath, ignored);
    std::filesystem::remove(emptyPath, ignored);
}

TEST_CASE("The viewport follows the window's size", "[editor][viewport][gpu]")
{
    const auto path = outputPath("editor-viewport-resized.png");
    const engine::platform::PixelSize target{800, 700};
    // Room for the window system to apply the new size; the probe ends the
    // run eight frames after the framebuffer changed.
    EditorProbe editor(path, 0, target);
    const auto result = engine::app::run(editor, shellDesc(600));
    REQUIRE(result.hasValue());
    checkCleanRun(result.value());
    REQUIRE(editor.resizeRequested);
    REQUIRE(editor.framesAfterResize > 8);

    const auto image = engine::core::readPng(path);
    REQUIRE(image.hasValue());
    const engine::core::Image& frame = image.value();
    CHECK(frame.width == editor.finalSize.width);
    CHECK(frame.height == editor.finalSize.height);
    CHECK((frame.width != editor.initialSize.width || frame.height != editor.initialSize.height));

    // The viewport's area changed with the window, and the world was drawn at
    // its new size, filling it.
    const Rect before = viewportRect(editor.areasBefore);
    const Rect after = viewportRect(editor.areas());
    INFO("viewport " << before.width << "x" << before.height << " before, " << after.width << "x"
                     << after.height << " after");
    CHECK((after.width != before.width || after.height != before.height));
    CHECK(after.x + after.width <= frame.width);
    CHECK(after.y + after.height <= frame.height);
    CHECK(std::abs(editor.lastDrawnSize.width - after.width) <= 1);
    CHECK(std::abs(editor.lastDrawnSize.height - after.height) <= 1);
    const ViewportPixels inside = classify(frame, after);
    CHECK(inside.black == 0);
    CHECK(inside.sky > inside.total / 4);
    CHECK(inside.world > inside.total / 20);

    // Raven still covers the whole, resized window.
    std::size_t magenta = 0;
    for (int y = 0; y < frame.height; ++y) {
        for (int x = 0; x < frame.width; ++x) {
            magenta += difference(pixelAt(frame, x, y), {255, 0, 255}) <= 8 ? 1U : 0U;
        }
    }
    CHECK(magenta == 0);
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}
