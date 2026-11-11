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

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using engine::core::Image;
using engine::core::PixelFormat;
using engine::geometry::MeshData;
using engine::math::Mat4;
using engine::math::Vec2;
using engine::math::Vec3;
using engine::math::Vec4;
using engine::render::Color;
using engine::render::MaterialDesc;
using engine::render::MaterialHandle;
using engine::render::MeshHandle;
using engine::render::Renderer;
using engine::render::TextureDesc;
using engine::render::TextureFilter;
using engine::render::TextureHandle;
using engine::render::TextureWrap;

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

// Test-only shaders that show one vertex attribute as color (relative to the
// repository root, where the GPU tests run).
constexpr const char* kNormalAsColorShaders = "tests/rendering/fixtures/normal-as-color/shaders";
constexpr const char* kUvAsColorShaders = "tests/rendering/fixtures/uv-as-color/shaders";

GpuRenderer createRenderer(const char* shaderDirectory = "assets/shaders")
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
                                      .shaderDirectory = shaderDirectory,
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

// y = 0 is the top row.
Rgb pixel(const Image& image, int x, int y)
{
    const auto offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
                         static_cast<std::size_t>(x)) *
                        3;
    return Rgb{image.pixels[offset], image.pixels[offset + 1], image.pixels[offset + 2]};
}

// x and y as fractions of the image size; y = 0 is the top row.
Rgb pixelAt(const Image& image, float fx, float fy)
{
    return pixel(image, static_cast<int>(fx * static_cast<float>(image.width - 1)),
                 static_cast<int>(fy * static_cast<float>(image.height - 1)));
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

// quad() with one normal on all corners and/or UVs covering the unit square:
// (0, 0) at the bottom-left corner, U to the right, V up.
MeshData attributedQuad(const Vec4& color, std::optional<Vec3> normal, bool withUvs,
                        float halfSize = 1.0F)
{
    MeshData mesh = quad(0.0F, color, halfSize);
    if (normal) {
        mesh.normals.assign(4, *normal);
    }
    if (withUvs) {
        mesh.uvs = {{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}};
    }
    return mesh;
}

// Expected colors of the normal-as-color shader (normal * 0.5 + 0.5).
const Rgb kNoNormal{128, 128, 128};
const Rgb kNormalTowardsViewer{128, 128, 255}; // (0, 0, 1)

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

TEST_CASE("Normals and UVs do not change the default mesh shading", "[render][gpu]")
{
    auto gpu = createRenderer();
    const Vec4 red{1.0F, 0.0F, 0.0F, 1.0F};
    const MeshHandle plain = upload(*gpu.renderer, attributedQuad(red, std::nullopt, false));
    const MeshHandle attributed =
        upload(*gpu.renderer, attributedQuad(red, Vec3{0.0F, 0.0F, 1.0F}, true));

    const Image plainImage = renderFrame(gpu, [&](Renderer& r) { r.drawMesh(plain); });
    const Image attributedImage = renderFrame(gpu, [&](Renderer& r) { r.drawMesh(attributed); });
    CHECK(near(pixelAt(plainImage, 0.5F, 0.5F), kRed));
    INFO("attributed " << describe(pixelAt(attributedImage, 0.5F, 0.5F)));
    CHECK(near(pixelAt(attributedImage, 0.5F, 0.5F), kRed));
    CHECK(near(pixelAt(attributedImage, 0.1F, 0.9F), kRed));
    CHECK(gpu.renderer->errorCount() == 0);
}

TEST_CASE("Normals reach the vertex shader; meshes without them read (0, 0, 0)", "[render][gpu]")
{
    auto gpu = createRenderer(kNormalAsColorShaders);
    // (0, 0.6, 0.8) is unit length; shown as (0.5, 0.8, 0.9).
    const Rgb expectedLeft{128, 204, 230};
    const MeshHandle withNormals =
        upload(*gpu.renderer, attributedQuad(Vec4{1.0F}, Vec3{0.0F, 0.6F, 0.8F}, false, 0.5F));
    const MeshHandle withoutNormals =
        upload(*gpu.renderer, attributedQuad(Vec4{1.0F}, std::nullopt, false, 0.5F));

    // Side by side in one frame: the attribute is enabled per mesh.
    const Image image = renderFrame(gpu, [&](Renderer& r) {
        r.drawMesh(withNormals, glm::translate(Mat4{1.0F}, Vec3{-0.5F, 0.0F, 0.0F}));
        r.drawMesh(withoutNormals, glm::translate(Mat4{1.0F}, Vec3{0.5F, 0.0F, 0.0F}));
    });
    INFO("left " << describe(pixelAt(image, 0.25F, 0.5F)) << ", right "
                 << describe(pixelAt(image, 0.75F, 0.5F)));
    CHECK(near(pixelAt(image, 0.25F, 0.5F), expectedLeft));
    CHECK(near(pixelAt(image, 0.75F, 0.5F), kNoNormal));
    CHECK(gpu.renderer->errorCount() == 0);
}

TEST_CASE("UVs reach the vertex shader; meshes without them read (0, 0)", "[render][gpu]")
{
    auto gpu = createRenderer(kUvAsColorShaders);
    const MeshHandle withUvs =
        upload(*gpu.renderer, attributedQuad(Vec4{1.0F}, std::nullopt, true));
    const MeshHandle withoutUvs =
        upload(*gpu.renderer, attributedQuad(Vec4{1.0F}, std::nullopt, false));

    // Red shows U, green V. Top-left quarter: U about 0.25, V about 0.75 (V
    // points up); bottom-right quarter: the other way round.
    const Image image = renderFrame(gpu, [&](Renderer& r) { r.drawMesh(withUvs); });
    INFO("top left " << describe(pixelAt(image, 0.25F, 0.25F)) << ", bottom right "
                     << describe(pixelAt(image, 0.75F, 0.75F)));
    CHECK(near(pixelAt(image, 0.25F, 0.25F), Rgb{64, 191, 0}, 4));
    CHECK(near(pixelAt(image, 0.75F, 0.75F), Rgb{191, 64, 0}, 4));

    const Image without = renderFrame(gpu, [&](Renderer& r) { r.drawMesh(withoutUvs); });
    CHECK(near(pixelAt(without, 0.5F, 0.5F), Rgb{0, 0, 0}));
    CHECK(gpu.renderer->errorCount() == 0);
}

TEST_CASE("updateMesh can add and remove normals and UVs", "[render][gpu]")
{
    auto gpu = createRenderer(kNormalAsColorShaders);
    const Vec4 white{1.0F};
    const MeshHandle mesh = upload(*gpu.renderer, attributedQuad(white, std::nullopt, false));
    const auto centerAfterDraw = [&]() {
        const Image image = renderFrame(gpu, [&](Renderer& r) { r.drawMesh(mesh); });
        return pixelAt(image, 0.5F, 0.5F);
    };
    CHECK(near(centerAfterDraw(), kNoNormal));

    // Same vertex count each time: the buffers are reused while the layout changes.
    REQUIRE_FALSE(
        gpu.renderer->updateMesh(mesh, attributedQuad(white, Vec3{0.0F, 0.0F, 1.0F}, true))
            .has_value());
    CHECK(near(centerAfterDraw(), kNormalTowardsViewer));

    REQUIRE_FALSE(
        gpu.renderer->updateMesh(mesh, attributedQuad(white, std::nullopt, true)).has_value());
    CHECK(near(centerAfterDraw(), kNoNormal));

    REQUIRE_FALSE(
        gpu.renderer->updateMesh(mesh, attributedQuad(white, Vec3{0.0F, 0.0F, 1.0F}, false))
            .has_value());
    CHECK(near(centerAfterDraw(), kNormalTowardsViewer));
    CHECK(gpu.renderer->errorCount() == 0);
}

// --- Materials (M2.6) ----------------------------------------------------------

namespace {

MaterialHandle createMaterial(Renderer& renderer, const Color& baseColor)
{
    auto material = renderer.createMaterial(MaterialDesc{.baseColor = baseColor});
    REQUIRE(material.hasValue());
    return material.value();
}

} // namespace

TEST_CASE("A material's base color multiplies the vertex colors", "[render][gpu]")
{
    auto gpu = createRenderer();
    Renderer& renderer = *gpu.renderer;
    // Vertex color (1, 0.5, 0.25); tinted by (0.5, 1, 1) it becomes (0.5, 0.5, 0.25).
    const MeshHandle mesh = upload(renderer, quad(0.0F, {1.0F, 0.5F, 0.25F, 1.0F}, 0.5F));
    const MaterialHandle tint = createMaterial(renderer, {0.5F, 1.0F, 1.0F, 1.0F});
    const MaterialHandle white = createMaterial(renderer, {1.0F, 1.0F, 1.0F, 1.0F});

    // One mesh, two materials, side by side in one frame.
    const Image image = renderFrame(gpu, [&](Renderer& r) {
        r.drawMesh(mesh, tint, glm::translate(Mat4{1.0F}, Vec3{-0.5F, 0.0F, 0.0F}));
        r.drawMesh(mesh, white, glm::translate(Mat4{1.0F}, Vec3{0.5F, 0.0F, 0.0F}));
    });
    INFO("left " << describe(pixelAt(image, 0.25F, 0.5F)) << ", right "
                 << describe(pixelAt(image, 0.75F, 0.5F)));
    CHECK(near(pixelAt(image, 0.25F, 0.5F), Rgb{128, 128, 64}));
    CHECK(near(pixelAt(image, 0.75F, 0.5F), Rgb{255, 128, 64}));
    CHECK(renderer.lastFrameStats().drawCalls == 2);
    CHECK(renderer.errorCount() == 0);
}

TEST_CASE("The default material reproduces the vertex colors", "[render][gpu]")
{
    auto gpu = createRenderer();
    Renderer& renderer = *gpu.renderer;
    const MeshHandle mesh = upload(renderer, quad(0.0F, {0.2F, 0.4F, 0.6F, 1.0F}));
    const MaterialHandle white = createMaterial(renderer, MaterialDesc{}.baseColor);
    const MaterialHandle black = createMaterial(renderer, {0.0F, 0.0F, 0.0F, 1.0F});
    const Rgb vertexColor{51, 102, 153};

    const Image byDefault = renderFrame(gpu, [&](Renderer& r) { r.drawMesh(mesh); });
    CHECK(near(pixelAt(byDefault, 0.5F, 0.5F), vertexColor));
    const Image byWhite = renderFrame(gpu, [&](Renderer& r) { r.drawMesh(mesh, white); });
    CHECK(near(pixelAt(byWhite, 0.5F, 0.5F), vertexColor));

    // A tinted draw does not leak into later draws with the default material.
    const Image tinted = renderFrame(gpu, [&](Renderer& r) { r.drawMesh(mesh, black); });
    CHECK(near(pixelAt(tinted, 0.5F, 0.5F), Rgb{0, 0, 0}));
    const Image again = renderFrame(gpu, [&](Renderer& r) { r.drawMesh(mesh); });
    CHECK(near(pixelAt(again, 0.5F, 0.5F), vertexColor));
    CHECK(renderer.errorCount() == 0);
}

TEST_CASE("A material's alpha blends like vertex alpha", "[render][gpu]")
{
    auto gpu = createRenderer();
    Renderer& renderer = *gpu.renderer;
    const MeshHandle mesh = upload(renderer, quad(0.0F, Vec4{1.0F}));
    const MaterialHandle halfRed = createMaterial(renderer, {1.0F, 0.0F, 0.0F, 0.5F});
    const Image image = renderFrame(
        gpu,
        [&](Renderer& r) {
            r.setView({.depth = engine::render::DepthMode::Off,
                       .blend = engine::render::BlendMode::Alpha});
            r.drawMesh(mesh, halfRed);
        },
        Color{0.0F, 0.0F, 0.0F, 1.0F});
    INFO("center " << describe(pixelAt(image, 0.5F, 0.5F)));
    CHECK(near(pixelAt(image, 0.5F, 0.5F), Rgb{128, 0, 0}));
    CHECK(renderer.errorCount() == 0);
}

TEST_CASE("Destroyed and null material handles draw nothing and count an error", "[render][gpu]")
{
    auto gpu = createRenderer();
    Renderer& renderer = *gpu.renderer;
    const MeshHandle mesh = upload(renderer, quad(0.0F, {1.0F, 0.0F, 0.0F, 1.0F}));
    const MaterialHandle material = createMaterial(renderer, {0.0F, 1.0F, 0.0F, 1.0F});
    CHECK(renderer.isValid(material));
    CHECK_FALSE(renderer.isValid(MaterialHandle{}));

    renderer.destroyMaterial(material);
    renderer.destroyMaterial(material); // second destroy is ignored
    renderer.destroyMaterial(MaterialHandle{});
    CHECK_FALSE(renderer.isValid(material));

    // The slot is reused, but the old handle must not reach the new material.
    const MaterialHandle replacement = createMaterial(renderer, {0.0F, 0.0F, 1.0F, 1.0F});
    CHECK(replacement.index == material.index);
    CHECK(replacement != material);

    const Image stale = renderFrame(gpu, [&](Renderer& r) { r.drawMesh(mesh, material); });
    CHECK(near(pixelAt(stale, 0.5F, 0.5F), kClearRgb));
    CHECK(renderer.errorCount() == 1);
    CHECK(renderer.lastFrameStats().drawCalls == 0);

    (void)renderFrame(gpu, [&](Renderer& r) { r.drawMesh(mesh, MaterialHandle{}); });
    CHECK(renderer.errorCount() == 2);

    // An invalid mesh counts once, whatever the material.
    (void)renderFrame(gpu, [&](Renderer& r) { r.drawMesh(MeshHandle{}, replacement); });
    CHECK(renderer.errorCount() == 3);
    (void)renderFrame(gpu, [&](Renderer& r) { r.drawMesh(MeshHandle{}, MaterialHandle{}); });
    CHECK(renderer.errorCount() == 4);

    // The replacement itself works.
    const Image fine = renderFrame(gpu, [&](Renderer& r) { r.drawMesh(mesh, replacement); });
    CHECK(near(pixelAt(fine, 0.5F, 0.5F), Rgb{0, 0, 0})); // red x blue
    CHECK(renderer.lastFrameStats().drawCalls == 1);
}

TEST_CASE("createMaterial rejects invalid base colors with a reason", "[render][gpu]")
{
    auto gpu = createRenderer();
    const auto material =
        gpu.renderer->createMaterial(MaterialDesc{.baseColor = {0.0F, 2.0F, 0.0F, 1.0F}});
    REQUIRE_FALSE(material.hasValue());
    CHECK(material.error().message.find("createMaterial") != std::string::npos);
    CHECK(material.error().message.find("base color green is 2") != std::string::npos);
}

// --- Texture sampling (M2.7) ---------------------------------------------------

namespace {

// quad() with UVs from (0, 0) at the bottom-left corner to uvMax at the
// top-right one.
MeshData uvQuad(const Vec4& color, float halfSize = 1.0F, Vec2 uvMax = Vec2{1.0F})
{
    MeshData mesh = quad(0.0F, color, halfSize);
    mesh.uvs = {{0.0F, 0.0F}, {uvMax.x, 0.0F}, uvMax, {0.0F, uvMax.y}};
    return mesh;
}

// An RGB image from texels listed row by row, top row first.
Image rgbImage(int width, int height, const std::vector<Rgb>& texels)
{
    Image image{width, height, {}, PixelFormat::Rgb8};
    for (const Rgb& texel : texels) {
        for (const int channel : {texel.r, texel.g, texel.b}) {
            image.pixels.push_back(static_cast<std::uint8_t>(channel));
        }
    }
    return image;
}

TextureHandle createTexture(Renderer& renderer, const Image& image, const TextureDesc& desc = {})
{
    auto texture = renderer.createTexture(image, desc);
    REQUIRE(texture.hasValue());
    return texture.value();
}

MaterialHandle texturedMaterial(Renderer& renderer, TextureHandle texture,
                                const Color& baseColor = Color{1.0F, 1.0F, 1.0F, 1.0F})
{
    auto material =
        renderer.createMaterial(MaterialDesc{.baseColor = baseColor, .baseColorTexture = texture});
    REQUIRE(material.hasValue());
    return material.value();
}

constexpr TextureDesc kNearestClamp{
    .filter = TextureFilter::Nearest, .wrap = TextureWrap::ClampToEdge, .mipmaps = false};

} // namespace

TEST_CASE("Nearest filtering shows the exact texels", "[render][gpu][texture]")
{
    auto gpu = createRenderer();
    Renderer& renderer = *gpu.renderer;
    // A 4x4 checker; the image's top-left texel is A, so a flipped texture
    // would show B in the top-left corner of the frame.
    const Rgb a{200, 40, 10};
    const Rgb b{30, 90, 220};
    std::vector<Rgb> texels;
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            texels.push_back((row + column) % 2 == 0 ? a : b);
        }
    }
    const TextureHandle texture = createTexture(renderer, rgbImage(4, 4, texels), kNearestClamp);
    const MaterialHandle material = texturedMaterial(renderer, texture);
    const MeshHandle mesh = upload(renderer, uvQuad(Vec4{1.0F}));

    // The quad fills the frame: each texel covers a quarter of its width and height.
    const Image image = renderFrame(gpu, [&](Renderer& r) { r.drawMesh(mesh, material); });
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            const Rgb actual = pixelAt(image, (static_cast<float>(column) + 0.5F) / 4.0F,
                                       (static_cast<float>(row) + 0.5F) / 4.0F);
            INFO("texel row " << row << ", column " << column << ": " << describe(actual));
            CHECK(near(actual, (row + column) % 2 == 0 ? a : b, 0));
        }
    }
    CHECK(renderer.errorCount() == 0);
}

TEST_CASE("Linear filtering blends neighboring texels", "[render][gpu][texture]")
{
    auto gpu = createRenderer();
    Renderer& renderer = *gpu.renderer;
    // Black and white texels; their centers are at U = 0.25 and 0.75.
    const TextureHandle texture = createTexture(
        renderer, rgbImage(2, 1, {Rgb{0, 0, 0}, Rgb{255, 255, 255}}),
        TextureDesc{
            .filter = TextureFilter::Linear, .wrap = TextureWrap::ClampToEdge, .mipmaps = false});
    const MaterialHandle material = texturedMaterial(renderer, texture);
    const MeshHandle mesh = upload(renderer, uvQuad(Vec4{1.0F}));
    const Image image = renderFrame(gpu, [&](Renderer& r) { r.drawMesh(mesh, material); });

    // Every column of the middle row: black up to U = 0.25 (clamped), a linear
    // ramp to U = 0.75, then white.
    int mismatches = 0;
    for (int x = 0; x < image.width; ++x) {
        const float u = (static_cast<float>(x) + 0.5F) / static_cast<float>(image.width);
        const float weight = std::clamp(u * 2.0F - 0.5F, 0.0F, 1.0F);
        const int value = static_cast<int>(std::lround(255.0F * weight));
        const Rgb actual = pixel(image, x, image.height / 2);
        if (!near(actual, Rgb{value, value, value})) {
            ++mismatches;
            UNSCOPED_INFO("column " << x << ": " << describe(actual) << ", expected " << value);
        }
    }
    CHECK(mismatches == 0);
    // Blended, not the nearest texel.
    CHECK(near(pixelAt(image, 0.5F, 0.5F), Rgb{128, 128, 128}, 4));
    CHECK(renderer.errorCount() == 0);
}

TEST_CASE("Repeat tiles the texture; ClampToEdge stretches its border texels",
          "[render][gpu][texture]")
{
    auto gpu = createRenderer();
    Renderer& renderer = *gpu.renderer;
    const Image redBlue = rgbImage(2, 1, {kRed, kBlue});
    const auto drawWith = [&](TextureWrap wrap) {
        const TextureHandle texture = createTexture(
            renderer, redBlue,
            TextureDesc{.filter = TextureFilter::Nearest, .wrap = wrap, .mipmaps = false});
        const MaterialHandle material = texturedMaterial(renderer, texture);
        // U runs from 0 to 2 across the frame.
        const MeshHandle mesh = upload(renderer, uvQuad(Vec4{1.0F}, 1.0F, Vec2{2.0F, 1.0F}));
        return renderFrame(gpu, [&](Renderer& r) { r.drawMesh(mesh, material); });
    };

    const Image repeated = drawWith(TextureWrap::Repeat);
    CHECK(near(pixelAt(repeated, 0.125F, 0.5F), kRed));  // U 0.25
    CHECK(near(pixelAt(repeated, 0.375F, 0.5F), kBlue)); // U 0.75
    CHECK(near(pixelAt(repeated, 0.625F, 0.5F), kRed));  // U 1.25: second copy
    CHECK(near(pixelAt(repeated, 0.875F, 0.5F), kBlue)); // U 1.75

    const Image clamped = drawWith(TextureWrap::ClampToEdge);
    CHECK(near(pixelAt(clamped, 0.125F, 0.5F), kRed));
    CHECK(near(pixelAt(clamped, 0.375F, 0.5F), kBlue));
    CHECK(near(pixelAt(clamped, 0.625F, 0.5F), kBlue)); // the right border texel
    CHECK(near(pixelAt(clamped, 0.875F, 0.5F), kBlue));
    CHECK(renderer.errorCount() == 0);
}

TEST_CASE("Texture, vertex color and base color multiply", "[render][gpu][texture]")
{
    auto gpu = createRenderer();
    Renderer& renderer = *gpu.renderer;
    // Each factor shows in one channel: texture (1, 0.5, 1) x vertex color
    // (0.5, 1, 1) x base color (1, 1, 0.25) = (0.5, 0.5, 0.25).
    const TextureHandle texture = createTexture(renderer, rgbImage(1, 1, {Rgb{255, 128, 255}}));
    const Color baseColor{1.0F, 1.0F, 0.25F, 1.0F};
    const MaterialHandle textured = texturedMaterial(renderer, texture, baseColor);
    const MaterialHandle untextured = createMaterial(renderer, baseColor);
    const MeshHandle mesh = upload(renderer, uvQuad({0.5F, 1.0F, 1.0F, 1.0F}, 0.5F));

    // The untextured draw after the textured one must not see its texture.
    const Image image = renderFrame(gpu, [&](Renderer& r) {
        r.drawMesh(mesh, textured, glm::translate(Mat4{1.0F}, Vec3{-0.5F, 0.0F, 0.0F}));
        r.drawMesh(mesh, untextured, glm::translate(Mat4{1.0F}, Vec3{0.5F, 0.0F, 0.0F}));
    });
    INFO("left " << describe(pixelAt(image, 0.25F, 0.5F)) << ", right "
                 << describe(pixelAt(image, 0.75F, 0.5F)));
    CHECK(near(pixelAt(image, 0.25F, 0.5F), Rgb{128, 128, 64}));
    CHECK(near(pixelAt(image, 0.75F, 0.5F), Rgb{128, 255, 64}));

    // Nor does the default material: drawn nearer, over the textured draw.
    const Image byDefault = renderFrame(gpu, [&](Renderer& r) {
        r.drawMesh(mesh, textured);
        r.drawMesh(mesh, glm::translate(Mat4{1.0F}, Vec3{0.0F, 0.0F, -0.5F}));
    });
    CHECK(near(pixelAt(byDefault, 0.5F, 0.5F), Rgb{128, 255, 255}));
    CHECK(renderer.errorCount() == 0);
}

TEST_CASE("Texture alpha blends like vertex alpha", "[render][gpu][texture]")
{
    auto gpu = createRenderer();
    Renderer& renderer = *gpu.renderer;
    const TextureHandle texture =
        createTexture(renderer, Image{1, 1, {255, 0, 0, 128}, PixelFormat::Rgba8});
    const MaterialHandle material = texturedMaterial(renderer, texture);
    const MeshHandle mesh = upload(renderer, uvQuad(Vec4{1.0F}));
    const Image image = renderFrame(
        gpu,
        [&](Renderer& r) {
            r.setView({.depth = engine::render::DepthMode::Off,
                       .blend = engine::render::BlendMode::Alpha});
            r.drawMesh(mesh, material);
        },
        Color{0.0F, 0.0F, 0.0F, 1.0F});
    INFO("center " << describe(pixelAt(image, 0.5F, 0.5F)));
    CHECK(near(pixelAt(image, 0.5F, 0.5F), Rgb{128, 0, 0}));
    CHECK(renderer.errorCount() == 0);
}

TEST_CASE("A textured material on a mesh without UVs draws nothing and counts an error",
          "[render][gpu][texture]")
{
    auto gpu = createRenderer();
    Renderer& renderer = *gpu.renderer;
    const TextureHandle texture = createTexture(renderer, rgbImage(1, 1, {Rgb{0, 255, 0}}));
    const MaterialHandle textured = texturedMaterial(renderer, texture);
    const MaterialHandle untextured = createMaterial(renderer, {1.0F, 1.0F, 1.0F, 1.0F});
    const MeshHandle mesh = upload(renderer, quad(0.0F, {1.0F, 0.0F, 0.0F, 1.0F}));

    const Image skipped = renderFrame(gpu, [&](Renderer& r) { r.drawMesh(mesh, textured); });
    CHECK(near(pixelAt(skipped, 0.5F, 0.5F), kClearRgb));
    CHECK(renderer.errorCount() == 1);
    CHECK(renderer.lastFrameStats().drawCalls == 0);
    (void)renderFrame(gpu, [&](Renderer& r) { r.drawMesh(mesh, textured); });
    CHECK(renderer.errorCount() == 2);

    // Untextured materials need no UVs.
    const Image plain = renderFrame(gpu, [&](Renderer& r) { r.drawMesh(mesh, untextured); });
    CHECK(near(pixelAt(plain, 0.5F, 0.5F), kRed));
    CHECK(renderer.errorCount() == 2);

    // Once an update adds UVs, the textured draw works: red x green.
    REQUIRE_FALSE(renderer.updateMesh(mesh, uvQuad({1.0F, 0.0F, 0.0F, 1.0F})).has_value());
    const Image drawn = renderFrame(gpu, [&](Renderer& r) { r.drawMesh(mesh, textured); });
    CHECK(near(pixelAt(drawn, 0.5F, 0.5F), Rgb{0, 0, 0}));
    CHECK(renderer.lastFrameStats().drawCalls == 1);
    CHECK(renderer.errorCount() == 2);
}

TEST_CASE("A material whose texture was destroyed reports the invalid texture",
          "[render][gpu][texture]")
{
    auto gpu = createRenderer();
    Renderer& renderer = *gpu.renderer;
    const TextureHandle texture = createTexture(renderer, rgbImage(1, 1, {kRed}));
    const MaterialHandle material = texturedMaterial(renderer, texture);
    const MeshHandle mesh = upload(renderer, uvQuad(Vec4{1.0F}));
    const Image before = renderFrame(gpu, [&](Renderer& r) { r.drawMesh(mesh, material); });
    CHECK(near(pixelAt(before, 0.5F, 0.5F), kRed));

    renderer.destroyTexture(texture);
    CHECK(renderer.isValid(material)); // the material itself is still there
    const Image stale = renderFrame(gpu, [&](Renderer& r) { r.drawMesh(mesh, material); });
    CHECK(near(pixelAt(stale, 0.5F, 0.5F), kClearRgb));
    CHECK(renderer.errorCount() == 1);
    CHECK(renderer.lastFrameStats().drawCalls == 0);

    // A new texture in the freed slot does not revive the material.
    const TextureHandle replacement = createTexture(renderer, rgbImage(1, 1, {kBlue}));
    CHECK(replacement.index == texture.index);
    CHECK(replacement != texture);
    (void)renderFrame(gpu, [&](Renderer& r) { r.drawMesh(mesh, material); });
    CHECK(renderer.errorCount() == 2);

    // A stale texture handle is rejected when the material is created.
    const auto rejected = renderer.createMaterial(MaterialDesc{.baseColorTexture = texture});
    REQUIRE_FALSE(rejected.hasValue());
    INFO(rejected.error().message);
    CHECK(rejected.error().message.find("createMaterial: base color texture (index " +
                                        std::to_string(texture.index) + ", generation " +
                                        std::to_string(texture.generation) +
                                        ") is invalid or destroyed") != std::string::npos);

    const MaterialHandle current = texturedMaterial(renderer, replacement);
    const Image fine = renderFrame(gpu, [&](Renderer& r) { r.drawMesh(mesh, current); });
    CHECK(near(pixelAt(fine, 0.5F, 0.5F), kBlue));
    CHECK(renderer.errorCount() == 2);
}

// --- Directional light and flat shading (M2.8) ----------------------------------

namespace {

using engine::render::LightDesc;
using engine::render::Shading;

// With identity view and projection the viewer looks along +Z. These lights
// shine from the viewer into the screen, so a face whose normal points back
// at the viewer, (0, 0, -1), faces the light.
constexpr Vec3 kIntoScreen{0.0F, 0.0F, 1.0F};
constexpr Vec3 kTowardsViewer{0.0F, 0.0F, -1.0F};

MaterialHandle litMaterial(Renderer& renderer,
                           const Color& baseColor = Color{1.0F, 1.0F, 1.0F, 1.0F})
{
    auto material =
        renderer.createMaterial(MaterialDesc{.baseColor = baseColor, .shading = Shading::Lit});
    REQUIRE(material.hasValue());
    return material.value();
}

Mat4 at(float x)
{
    return glm::translate(Mat4{1.0F}, Vec3{x, 0.0F, 0.0F});
}

} // namespace

TEST_CASE("Lit faces get ambient plus Lambert diffuse light", "[render][gpu][light]")
{
    auto gpu = createRenderer();
    Renderer& renderer = *gpu.renderer;
    const MaterialHandle lit = litMaterial(renderer);
    const Vec4 white{1.0F};
    const MeshHandle facing = upload(renderer, attributedQuad(white, kTowardsViewer, false, 0.3F));
    // 60 degrees away from the light: cos = 0.5.
    const MeshHandle slanted =
        upload(renderer, attributedQuad(white, Vec3{std::sqrt(0.75F), 0.0F, -0.5F}, false, 0.3F));
    const MeshHandle away = upload(renderer, attributedQuad(white, kIntoScreen, false, 0.3F));

    const Image image = renderFrame(gpu, [&](Renderer& r) {
        r.setLight({.direction = kIntoScreen, .color = Vec3{0.5F}, .ambient = Vec3{0.25F}});
        r.drawMesh(facing, lit, at(-0.6F));
        r.drawMesh(slanted, lit, at(0.0F));
        r.drawMesh(away, lit, at(0.6F));
    });
    INFO("facing " << describe(pixelAt(image, 0.2F, 0.5F)) << ", slanted "
                   << describe(pixelAt(image, 0.5F, 0.5F)) << ", away "
                   << describe(pixelAt(image, 0.8F, 0.5F)));
    CHECK(near(pixelAt(image, 0.2F, 0.5F), Rgb{191, 191, 191})); // 0.25 + 0.5
    CHECK(near(pixelAt(image, 0.5F, 0.5F), Rgb{128, 128, 128})); // 0.25 + 0.5 x 0.5
    // Facing away gets the ambient light only: the minimum.
    CHECK(near(pixelAt(image, 0.8F, 0.5F), Rgb{64, 64, 64}));
    CHECK(renderer.errorCount() == 0);
}

TEST_CASE("Light color and ambient act per channel; alpha is not lit", "[render][gpu][light]")
{
    auto gpu = createRenderer();
    Renderer& renderer = *gpu.renderer;
    const MeshHandle mesh = upload(renderer, attributedQuad(Vec4{1.0F}, kTowardsViewer, false));

    const MaterialHandle lit = litMaterial(renderer);
    const Image colored = renderFrame(gpu, [&](Renderer& r) {
        r.setLight({.direction = kIntoScreen,
                    .color = {1.0F, 0.5F, 0.0F},
                    .ambient = {0.0F, 0.0F, 0.25F}});
        r.drawMesh(mesh, lit);
    });
    INFO("colored " << describe(pixelAt(colored, 0.5F, 0.5F)));
    CHECK(near(pixelAt(colored, 0.5F, 0.5F), Rgb{255, 128, 64}));

    // RGB 0.5 after lighting, alpha still 0.5: a quarter over black. A lit
    // alpha (0.25) would give an eighth.
    const MaterialHandle halfAlpha = litMaterial(renderer, {1.0F, 1.0F, 1.0F, 0.5F});
    const Image blended = renderFrame(
        gpu,
        [&](Renderer& r) {
            r.setView({.depth = engine::render::DepthMode::Off,
                       .blend = engine::render::BlendMode::Alpha});
            r.setLight({.direction = kIntoScreen, .color = Vec3{0.5F}, .ambient = Vec3{0.0F}});
            r.drawMesh(mesh, halfAlpha);
        },
        Color{0.0F, 0.0F, 0.0F, 1.0F});
    INFO("blended " << describe(pixelAt(blended, 0.5F, 0.5F)));
    CHECK(near(pixelAt(blended, 0.5F, 0.5F), Rgb{64, 64, 64}));
    CHECK(renderer.errorCount() == 0);
}

TEST_CASE("Unlit materials ignore the light; each frame starts with the default light",
          "[render][gpu][light]")
{
    auto gpu = createRenderer();
    Renderer& renderer = *gpu.renderer;
    const Vec4 color{0.2F, 0.4F, 0.6F, 1.0F};
    const Rgb unchanged{51, 102, 153};
    const MeshHandle mesh = upload(renderer, attributedQuad(color, kTowardsViewer, false, 0.3F));
    // Unlit materials need no normals.
    const MeshHandle withoutNormals =
        upload(renderer, attributedQuad(color, std::nullopt, false, 0.3F));
    const MaterialHandle lit = litMaterial(renderer);
    const MaterialHandle unlit = createMaterial(renderer, {1.0F, 1.0F, 1.0F, 1.0F});

    // The default light leaves lit materials as they are.
    const Image byDefault = renderFrame(gpu, [&](Renderer& r) { r.drawMesh(mesh, lit); });
    CHECK(near(pixelAt(byDefault, 0.5F, 0.5F), unchanged));

    // No light at all: lit turns black, unlit and the default material do not.
    const Image dark = renderFrame(gpu, [&](Renderer& r) {
        r.setLight({.direction = kIntoScreen, .color = Vec3{0.0F}, .ambient = Vec3{0.0F}});
        r.drawMesh(mesh, lit, at(-0.6F));
        r.drawMesh(withoutNormals, unlit, at(0.0F));
        r.drawMesh(mesh, at(0.6F));
    });
    INFO("lit " << describe(pixelAt(dark, 0.2F, 0.5F)) << ", unlit "
                << describe(pixelAt(dark, 0.5F, 0.5F)) << ", default "
                << describe(pixelAt(dark, 0.8F, 0.5F)));
    CHECK(near(pixelAt(dark, 0.2F, 0.5F), Rgb{0, 0, 0}));
    CHECK(near(pixelAt(dark, 0.5F, 0.5F), unchanged));
    CHECK(near(pixelAt(dark, 0.8F, 0.5F), unchanged));

    // beginFrame resets the light.
    const Image next = renderFrame(gpu, [&](Renderer& r) { r.drawMesh(mesh, lit); });
    CHECK(near(pixelAt(next, 0.5F, 0.5F), unchanged));
    CHECK(renderer.errorCount() == 0);
}

TEST_CASE("Normals follow rotation and non-uniform scale", "[render][gpu][light]")
{
    auto gpu = createRenderer();
    Renderer& renderer = *gpu.renderer;
    const MaterialHandle lit = litMaterial(renderer);
    const LightDesc light{.direction = kIntoScreen, .color = Vec3{1.0F}, .ambient = Vec3{0.0F}};

    // A face in the plane x = z, turned 45 degrees from the light: normal
    // (1, 0, -1) / sqrt(2), brightness cos 45 = 0.71.
    MeshData tilted;
    tilted.positions = {
        {-0.5F, -0.5F, -0.5F}, {0.5F, -0.5F, 0.5F}, {0.5F, 0.5F, 0.5F}, {-0.5F, 0.5F, -0.5F}};
    tilted.colors.assign(4, Vec4{1.0F});
    tilted.normals.assign(4, glm::normalize(Vec3{1.0F, 0.0F, -1.0F}));
    tilted.indices = {0, 1, 2, 0, 2, 3};
    const MeshHandle tiltedMesh = upload(renderer, tilted);
    const auto centerWith = [&](MeshHandle mesh, const Mat4& model) {
        const Image image = renderFrame(gpu, [&](Renderer& r) {
            r.setLight(light);
            r.drawMesh(mesh, lit, model);
        });
        return pixelAt(image, 0.5F, 0.5F);
    };

    const Rgb plain = centerWith(tiltedMesh, Mat4{1.0F});
    INFO("plain " << describe(plain));
    CHECK(near(plain, Rgb{180, 180, 180}));

    // Stretched 2x along X the face turns towards the light: its normal is
    // (1, 0, -2) / sqrt(5), brightness 0.89. Transforming the normal with the
    // model matrix itself would give (2, 0, -1) / sqrt(5), brightness 0.45.
    const Rgb stretched = centerWith(tiltedMesh, glm::scale(Mat4{1.0F}, Vec3{2.0F, 1.0F, 1.0F}));
    INFO("stretched " << describe(stretched));
    CHECK(near(stretched, Rgb{228, 228, 228}));

    // A face towards the light, turned 60 degrees about Y: cos 60 = 0.5.
    const MeshHandle facing =
        upload(renderer, attributedQuad(Vec4{1.0F}, kTowardsViewer, false, 0.5F));
    const Rgb rotated =
        centerWith(facing, glm::rotate(Mat4{1.0F}, glm::radians(60.0F), Vec3{0.0F, 1.0F, 0.0F}));
    INFO("rotated " << describe(rotated));
    CHECK(near(rotated, Rgb{128, 128, 128}));
    CHECK(renderer.errorCount() == 0);
}

TEST_CASE("A lit material on a mesh without normals draws nothing and counts an error",
          "[render][gpu][light]")
{
    auto gpu = createRenderer();
    Renderer& renderer = *gpu.renderer;
    const MaterialHandle lit = litMaterial(renderer);
    const MaterialHandle unlit = createMaterial(renderer, {1.0F, 1.0F, 1.0F, 1.0F});
    const Vec4 red{1.0F, 0.0F, 0.0F, 1.0F};
    const MeshHandle mesh = upload(renderer, attributedQuad(red, std::nullopt, false));

    const Image skipped = renderFrame(gpu, [&](Renderer& r) { r.drawMesh(mesh, lit); });
    CHECK(near(pixelAt(skipped, 0.5F, 0.5F), kClearRgb));
    CHECK(renderer.errorCount() == 1);
    CHECK(renderer.lastFrameStats().drawCalls == 0);
    (void)renderFrame(gpu, [&](Renderer& r) { r.drawMesh(mesh, lit); });
    CHECK(renderer.errorCount() == 2);

    const Image plain = renderFrame(gpu, [&](Renderer& r) { r.drawMesh(mesh, unlit); });
    CHECK(near(pixelAt(plain, 0.5F, 0.5F), kRed));
    CHECK(renderer.errorCount() == 2);

    // Once an update adds normals, the lit draw works.
    REQUIRE_FALSE(
        renderer.updateMesh(mesh, attributedQuad(red, kTowardsViewer, false)).has_value());
    const Image drawn = renderFrame(gpu, [&](Renderer& r) { r.drawMesh(mesh, lit); });
    CHECK(near(pixelAt(drawn, 0.5F, 0.5F), kRed));
    CHECK(renderer.lastFrameStats().drawCalls == 1);
    CHECK(renderer.errorCount() == 2);
}

TEST_CASE("setLight ignores an invalid light and counts an error", "[render][gpu][light]")
{
    auto gpu = createRenderer();
    Renderer& renderer = *gpu.renderer;
    const MaterialHandle lit = litMaterial(renderer);
    const MeshHandle mesh = upload(renderer, attributedQuad(Vec4{1.0F}, kTowardsViewer, false));

    const Image image = renderFrame(gpu, [&](Renderer& r) {
        r.setLight({.direction = kIntoScreen, .color = Vec3{0.0F}, .ambient = Vec3{0.25F}});
        r.setLight({.direction = Vec3{0.0F}}); // ignored
        r.setLight({.color = Vec3{-1.0F}});    // ignored
        r.drawMesh(mesh, lit);
    });
    // The first light stays: ambient only.
    INFO("center " << describe(pixelAt(image, 0.5F, 0.5F)));
    CHECK(near(pixelAt(image, 0.5F, 0.5F), Rgb{64, 64, 64}));
    CHECK(renderer.errorCount() == 2);
    CHECK(renderer.lastFrameStats().drawCalls == 1);
}
