// GPU tier: renderer behavior checked through frame readback. Draws simple
// meshes in normalized device coordinates and inspects individual pixels with
// a small tolerance (exact values can differ between drivers).
// Runs from the repository root (tests/rendering/CMakeLists.txt) so assets/ is found.

#include "engine/core/Image.hpp"
#include "engine/debug/TextMesh.hpp"
#include "engine/geometry/MeshData.hpp"
#include "engine/platform/GlContext.hpp"
#include "engine/platform/Platform.hpp"
#include "engine/platform/Window.hpp"
#include "engine/render/Renderer.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using engine::core::Image;
using engine::geometry::MeshData;
using engine::math::Mat4;
using engine::math::Vec3;
using engine::math::Vec4;
using engine::render::Color;
using engine::render::MeshHandle;
using engine::render::Renderer;

namespace {

constexpr Color kClear{0.25F, 0.5F, 0.75F, 1.0F};

struct Rgb {
    int r = 0;
    int g = 0;
    int b = 0;
};

// Owns everything a renderer needs; members are destroyed in reverse order.
struct GpuRenderer {
    std::optional<engine::platform::Platform> platform;
    std::optional<engine::platform::Window> window;
    std::optional<engine::platform::GlContext> context;
    std::optional<Renderer> renderer;
    int width = 0;
    int height = 0;
};

GpuRenderer createRenderer()
{
    GpuRenderer gpu;
    auto platform = engine::platform::Platform::initialize();
    REQUIRE(platform.hasValue());
    gpu.platform.emplace(std::move(platform).value());

    auto window = engine::platform::Window::create(*gpu.platform, {.title = "lpe renderer test",
                                                                   .width = 128,
                                                                   .height = 128,
                                                                   .resizable = false,
                                                                   .openGl = true});
    REQUIRE(window.hasValue());
    gpu.window.emplace(std::move(window).value());

    auto context =
        engine::platform::GlContext::create(*gpu.window, {.debug = true, .vsync = false});
    REQUIRE(context.hasValue());
    gpu.context.emplace(std::move(context).value());

    auto renderer = Renderer::create({.glProcLoader = gpu.context->procLoader(),
                                      .shaderDirectory = "assets/shaders",
                                      .debugOutput = true});
    REQUIRE(renderer.hasValue());
    gpu.renderer.emplace(std::move(renderer).value());

    const auto size = gpu.window->pixelSize();
    gpu.width = size.width;
    gpu.height = size.height;
    REQUIRE(gpu.width > 0);
    REQUIRE(gpu.height > 0);
    return gpu;
}

Image renderFrame(GpuRenderer& gpu, const std::function<void(Renderer&)>& draw,
                  Color clear = kClear)
{
    Renderer& renderer = *gpu.renderer;
    renderer.beginFrame(gpu.width, gpu.height, clear);
    draw(renderer);
    renderer.endFrame();
    auto image = renderer.captureFrame();
    REQUIRE(image.hasValue());
    gpu.context->swapBuffers();
    return std::move(image).value();
}

// x and y as fractions of the image size; y = 0 is the top row.
Rgb pixelAt(const Image& image, float fx, float fy)
{
    const int x = static_cast<int>(fx * static_cast<float>(image.width - 1));
    const int y = static_cast<int>(fy * static_cast<float>(image.height - 1));
    const auto offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
                         static_cast<std::size_t>(x)) *
                        3;
    return Rgb{image.pixels[offset], image.pixels[offset + 1], image.pixels[offset + 2]};
}

bool near(const Rgb& actual, const Rgb& expected, int tolerance = 3)
{
    return std::abs(actual.r - expected.r) <= tolerance &&
           std::abs(actual.g - expected.g) <= tolerance &&
           std::abs(actual.b - expected.b) <= tolerance;
}

std::string describe(const Rgb& c)
{
    return "(" + std::to_string(c.r) + ", " + std::to_string(c.g) + ", " + std::to_string(c.b) +
           ")";
}

const Rgb kClearRgb{64, 128, 191};
const Rgb kRed{255, 0, 0};
const Rgb kBlue{0, 0, 255};

MeshData quad(float z, const Vec4& color, float halfSize = 1.0F)
{
    MeshData mesh;
    mesh.positions = {{-halfSize, -halfSize, z},
                      {halfSize, -halfSize, z},
                      {halfSize, halfSize, z},
                      {-halfSize, halfSize, z}};
    mesh.colors.assign(4, color);
    mesh.indices = {0, 1, 2, 0, 2, 3};
    return mesh;
}

MeshData triangle(const Vec4& color, bool clockwise = false)
{
    MeshData mesh;
    mesh.positions = {{-0.2F, -0.2F, 0.0F}, {0.2F, -0.2F, 0.0F}, {0.0F, 0.2F, 0.0F}};
    mesh.colors.assign(3, color);
    mesh.indices =
        clockwise ? std::vector<std::uint32_t>{0, 2, 1} : std::vector<std::uint32_t>{0, 1, 2};
    return mesh;
}

MeshHandle upload(Renderer& renderer, const MeshData& mesh)
{
    auto handle = renderer.createMesh(mesh);
    REQUIRE(handle.hasValue());
    return handle.value();
}

} // namespace

TEST_CASE("beginFrame clears to the clear color", "[render][gpu]")
{
    auto gpu = createRenderer();
    const Image image = renderFrame(gpu, [](Renderer&) {});
    CHECK(image.width == gpu.width);
    CHECK(image.height == gpu.height);
    INFO("center " << describe(pixelAt(image, 0.5F, 0.5F)));
    CHECK(near(pixelAt(image, 0.5F, 0.5F), kClearRgb));
    CHECK(near(pixelAt(image, 0.0F, 0.0F), kClearRgb));
    CHECK(gpu.renderer->errorCount() == 0);
}

TEST_CASE("A mesh is drawn with its vertex colors", "[render][gpu]")
{
    auto gpu = createRenderer();
    const MeshHandle mesh = upload(*gpu.renderer, quad(0.0F, {1.0F, 0.0F, 0.0F, 1.0F}, 0.5F));
    const Image image = renderFrame(gpu, [&](Renderer& r) { r.drawMesh(mesh); });
    INFO("center " << describe(pixelAt(image, 0.5F, 0.5F)));
    CHECK(near(pixelAt(image, 0.5F, 0.5F), kRed));
    CHECK(near(pixelAt(image, 0.05F, 0.05F), kClearRgb));
    CHECK(gpu.renderer->lastFrameStats().drawCalls == 1);
    CHECK(gpu.renderer->lastFrameStats().triangles == 2);
    CHECK(gpu.renderer->errorCount() == 0);
}

TEST_CASE("Clockwise (back-facing) triangles are culled", "[render][gpu]")
{
    auto gpu = createRenderer();
    const MeshHandle front = upload(*gpu.renderer, triangle({1.0F, 0.0F, 0.0F, 1.0F}, false));
    const MeshHandle back = upload(*gpu.renderer, triangle({0.0F, 0.0F, 1.0F, 1.0F}, true));

    CHECK(
        near(pixelAt(renderFrame(gpu, [&](Renderer& r) { r.drawMesh(front); }), 0.5F, 0.5F), kRed));
    CHECK(near(pixelAt(renderFrame(gpu, [&](Renderer& r) { r.drawMesh(back); }), 0.5F, 0.5F),
               kClearRgb));
}

TEST_CASE("The nearer surface wins regardless of draw order", "[render][gpu]")
{
    auto gpu = createRenderer();
    const MeshHandle nearQuad = upload(*gpu.renderer, quad(-0.5F, {0.0F, 0.0F, 1.0F, 1.0F}));
    const MeshHandle farQuad = upload(*gpu.renderer, quad(0.5F, {1.0F, 0.0F, 0.0F, 1.0F}));

    const Image farFirst = renderFrame(gpu, [&](Renderer& r) {
        r.drawMesh(farQuad);
        r.drawMesh(nearQuad);
    });
    const Image nearFirst = renderFrame(gpu, [&](Renderer& r) {
        r.drawMesh(nearQuad);
        r.drawMesh(farQuad);
    });
    CHECK(near(pixelAt(farFirst, 0.5F, 0.5F), kBlue));
    CHECK(near(pixelAt(nearFirst, 0.5F, 0.5F), kBlue));
}

TEST_CASE("Model, view and projection matrices are applied in order", "[render][gpu]")
{
    auto gpu = createRenderer();
    const MeshHandle mesh = upload(*gpu.renderer, triangle({1.0F, 0.0F, 0.0F, 1.0F}));

    // Model matrix: move right by half the screen (NDC x = 0.5 is 3/4 of the width).
    const Mat4 right = glm::translate(Mat4{1.0F}, Vec3{0.5F, 0.0F, 0.0F});
    const Image moved = renderFrame(gpu, [&](Renderer& r) { r.drawMesh(mesh, right); });
    CHECK(near(pixelAt(moved, 0.75F, 0.5F), kRed));
    CHECK(near(pixelAt(moved, 0.5F, 0.5F), kClearRgb));

    // View matrix translating by +0.5 shifts the scene right (the camera moved left).
    const Image viewed = renderFrame(gpu, [&](Renderer& r) {
        r.setView({.view = glm::translate(Mat4{1.0F}, Vec3{0.5F, 0.0F, 0.0F})});
        r.drawMesh(mesh);
    });
    CHECK(near(pixelAt(viewed, 0.75F, 0.5F), kRed));

    // Projection applied last: scaling x by 0.5 pulls the moved triangle to NDC 0.25.
    const Image projected = renderFrame(gpu, [&](Renderer& r) {
        r.setView({.projection = glm::scale(Mat4{1.0F}, Vec3{0.5F, 1.0F, 1.0F})});
        r.drawMesh(mesh, right);
    });
    CHECK(near(pixelAt(projected, 0.625F, 0.5F), kRed));
    CHECK(near(pixelAt(projected, 0.75F, 0.5F), kClearRgb));
    CHECK(gpu.renderer->errorCount() == 0);
}

TEST_CASE("Alpha blending mixes the mesh color with the frame", "[render][gpu]")
{
    auto gpu = createRenderer();
    const MeshHandle veil = upload(*gpu.renderer, quad(0.0F, {1.0F, 1.0F, 1.0F, 0.5F}));
    const Image blended = renderFrame(
        gpu,
        [&](Renderer& r) {
            r.setView({.depth = engine::render::DepthMode::Off,
                       .blend = engine::render::BlendMode::Alpha});
            r.drawMesh(veil);
        },
        Color{0.0F, 0.0F, 0.0F, 1.0F});
    INFO("center " << describe(pixelAt(blended, 0.5F, 0.5F)));
    CHECK(near(pixelAt(blended, 0.5F, 0.5F), Rgb{128, 128, 128}));

    // The next frame starts opaque again.
    const Image opaque = renderFrame(gpu, [&](Renderer& r) { r.drawMesh(veil); });
    CHECK(near(pixelAt(opaque, 0.5F, 0.5F), Rgb{255, 255, 255}));
}

TEST_CASE("updateMesh replaces the data and grows the buffers", "[render][gpu]")
{
    auto gpu = createRenderer();
    const MeshHandle mesh = upload(*gpu.renderer, triangle({1.0F, 0.0F, 0.0F, 1.0F}));
    REQUIRE_FALSE(gpu.renderer->updateMesh(mesh, quad(0.0F, {0.0F, 0.0F, 1.0F, 1.0F})).has_value());

    const Image image = renderFrame(gpu, [&](Renderer& r) { r.drawMesh(mesh); });
    CHECK(near(pixelAt(image, 0.5F, 0.5F), kBlue));
    CHECK(near(pixelAt(image, 0.05F, 0.05F), kBlue));
    CHECK(gpu.renderer->lastFrameStats().triangles == 2);

    // Shrinking again reuses the buffers.
    REQUIRE_FALSE(gpu.renderer->updateMesh(mesh, triangle({1.0F, 0.0F, 0.0F, 1.0F})).has_value());
    const Image shrunk = renderFrame(gpu, [&](Renderer& r) { r.drawMesh(mesh); });
    CHECK(near(pixelAt(shrunk, 0.5F, 0.5F), kRed));
    CHECK(near(pixelAt(shrunk, 0.05F, 0.05F), kClearRgb));
    CHECK(gpu.renderer->errorCount() == 0);
}

TEST_CASE("Invalid mesh data is rejected with a reason", "[render][gpu]")
{
    auto gpu = createRenderer();
    MeshData broken = triangle({1.0F, 0.0F, 0.0F, 1.0F});
    broken.indices = {0, 1, 5};

    const auto created = gpu.renderer->createMesh(broken);
    REQUIRE_FALSE(created.hasValue());
    CHECK(created.error().message.find("index 2 has value 5") != std::string::npos);

    // A failed update keeps the previous data.
    const MeshHandle mesh = upload(*gpu.renderer, triangle({1.0F, 0.0F, 0.0F, 1.0F}));
    const auto updated = gpu.renderer->updateMesh(mesh, broken);
    REQUIRE(updated.has_value());
    CHECK(updated->message.find("updateMesh") != std::string::npos);
    const Image image = renderFrame(gpu, [&](Renderer& r) { r.drawMesh(mesh); });
    CHECK(near(pixelAt(image, 0.5F, 0.5F), kRed));
}

TEST_CASE("Destroyed and null handles are detected", "[render][gpu]")
{
    auto gpu = createRenderer();
    Renderer& renderer = *gpu.renderer;
    const MeshHandle mesh = upload(renderer, quad(0.0F, {1.0F, 0.0F, 0.0F, 1.0F}));
    CHECK(renderer.isValid(mesh));
    CHECK_FALSE(renderer.isValid(MeshHandle{}));

    renderer.destroyMesh(mesh);
    renderer.destroyMesh(mesh); // second destroy is ignored
    renderer.destroyMesh(MeshHandle{});
    CHECK_FALSE(renderer.isValid(mesh));
    CHECK(renderer.updateMesh(mesh, quad(0.0F, Vec4{1.0F})).has_value());

    // The slot is reused, but the old handle must not reach the new mesh.
    const MeshHandle replacement = upload(renderer, quad(0.0F, {0.0F, 0.0F, 1.0F, 1.0F}));
    CHECK(replacement != mesh);
    const Image image = renderFrame(gpu, [&](Renderer& r) { r.drawMesh(mesh); });
    CHECK(near(pixelAt(image, 0.5F, 0.5F), kClearRgb));
    CHECK(renderer.errorCount() == 1);
    CHECK(renderer.lastFrameStats().drawCalls == 0);
}

TEST_CASE("Overlay meshes use pixel coordinates from the top-left corner", "[render][gpu]")
{
    auto gpu = createRenderer();
    // A red rectangle over the top-left quarter of the framebuffer.
    engine::debug::TextMeshBuilder overlay;
    overlay.addRect({0.0F, 0.0F},
                    {static_cast<float>(gpu.width) / 2.0F, static_cast<float>(gpu.height) / 2.0F},
                    {1.0F, 0.0F, 0.0F, 1.0F});
    const MeshHandle mesh = upload(*gpu.renderer, overlay.mesh());

    const Image image = renderFrame(gpu, [&](Renderer& r) {
        r.setView({.projection = engine::debug::overlayProjection(gpu.width, gpu.height),
                   .depth = engine::render::DepthMode::Off,
                   .blend = engine::render::BlendMode::Alpha});
        r.drawMesh(mesh);
    });
    // Visible at all also proves the quads face the viewer after the y flip.
    CHECK(near(pixelAt(image, 0.25F, 0.25F), kRed));
    CHECK(near(pixelAt(image, 0.75F, 0.75F), kClearRgb));
    CHECK(near(pixelAt(image, 0.25F, 0.75F), kClearRgb));
}

TEST_CASE("Renderer info names the API and the driver", "[render][gpu]")
{
    auto gpu = createRenderer();
    const auto& info = gpu.renderer->info();
    CHECK(info.api == "OpenGL 4.5 Core");
    CHECK_FALSE(info.driverVersion.empty());
    CHECK_FALSE(info.device.empty());
    CHECK_FALSE(info.vendor.empty());
}

TEST_CASE("captureFrame fails before the first frame", "[render][gpu]")
{
    auto gpu = createRenderer();
    const auto image = gpu.renderer->captureFrame();
    REQUIRE_FALSE(image.hasValue());
    CHECK(image.error().message.find("no frame") != std::string::npos);
}
