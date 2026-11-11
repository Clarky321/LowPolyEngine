// GPU tier: textures behind renderer handles. These tests inspect the OpenGL
// objects directly (glad is linked for that; the renderer loads the function
// pointers); drawing with textures is tested in renderer_gpu_test.cpp. Every
// test creates its own context. The renderer creates one texture there itself
// (white, for untextured materials); the tests set it aside, so the other
// texture objects found are exactly the ones the test made.

#include "engine/core/Image.hpp"
#include "engine/platform/GlContext.hpp"
#include "engine/platform/Platform.hpp"
#include "engine/platform/Window.hpp"
#include "engine/render/Renderer.hpp"

#include <glad/gl.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using engine::core::Image;
using engine::core::PixelFormat;
using engine::render::Renderer;
using engine::render::TextureDesc;
using engine::render::TextureFilter;
using engine::render::TextureHandle;
using engine::render::TextureWrap;

namespace {

using Bytes = std::vector<std::uint8_t>;

// Owns everything a renderer needs; members are destroyed in reverse order.
struct GpuContext {
    std::optional<engine::platform::Platform> platform;
    std::optional<engine::platform::Window> window;
    std::optional<engine::platform::GlContext> context;
    std::optional<Renderer> renderer;
    // Texture objects the renderer made for itself at creation.
    std::vector<GLuint> rendererTextures;
};

// All texture objects among the first names the driver hands out.
std::vector<GLuint> allTextureObjects()
{
    std::vector<GLuint> names;
    for (GLuint name = 1; name <= 1024; ++name) {
        if (glIsTexture(name) == GL_TRUE) {
            names.push_back(name);
        }
    }
    return names;
}

GpuContext createRenderer()
{
    GpuContext gpu;
    auto platform = engine::platform::Platform::initialize();
    REQUIRE(platform.hasValue());
    gpu.platform.emplace(std::move(platform).value());

    auto window = engine::platform::Window::create(*gpu.platform, {.title = "lpe texture test",
                                                                   .width = 64,
                                                                   .height = 64,
                                                                   .resizable = false,
                                                                   .openGl = true});
    REQUIRE(window.hasValue());
    gpu.window.emplace(std::move(window).value());

    auto context =
        engine::platform::GlContext::create(*gpu.window, {.debug = true, .vsync = false});
    REQUIRE(context.hasValue());
    gpu.context.emplace(std::move(context).value());

    // Creating the renderer also loads the OpenGL functions these tests call.
    auto renderer = Renderer::create({.glProcLoader = gpu.context->procLoader(),
                                      .shaderDirectory = "assets/shaders",
                                      .debugOutput = true});
    REQUIRE(renderer.hasValue());
    gpu.renderer.emplace(std::move(renderer).value());
    // Only the white texture of untextured materials.
    gpu.rendererTextures = allTextureObjects();
    REQUIRE(gpu.rendererTextures.size() == 1);
    return gpu;
}

// Texture objects made through the renderer's texture API.
std::vector<GLuint> textureObjects(const GpuContext& gpu)
{
    std::vector<GLuint> names = allTextureObjects();
    std::erase_if(names, [&gpu](GLuint name) {
        return std::ranges::find(gpu.rendererTextures, name) != gpu.rendererTextures.end();
    });
    return names;
}

// The one texture object made through the texture API.
GLuint onlyTexture(const GpuContext& gpu)
{
    const std::vector<GLuint> names = textureObjects(gpu);
    REQUIRE(names.size() == 1);
    return names.front();
}

Image solid(int width, int height, PixelFormat format, std::uint8_t value)
{
    const auto bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
                       static_cast<std::size_t>(engine::core::channelCount(format));
    return Image{width, height, Bytes(bytes, value), format};
}

TextureHandle upload(Renderer& renderer, const Image& image, const TextureDesc& desc = {})
{
    auto handle = renderer.createTexture(image, desc);
    if (!handle) {
        FAIL("createTexture failed: " << handle.error().message);
    }
    return handle.value();
}

GLint textureParameter(GLuint texture, GLenum parameter)
{
    GLint value = 0;
    glGetTextureParameteriv(texture, parameter, &value);
    return value;
}

GLint levelParameter(GLuint texture, GLint level, GLenum parameter)
{
    GLint value = 0;
    glGetTextureLevelParameteriv(texture, level, parameter, &value);
    return value;
}

// Level 0 of a texture, row 0 (V = 0) first.
Bytes readLevel0(GLuint texture, GLenum format, std::size_t size)
{
    Bytes texels(size);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTextureImage(texture, 0, format, GL_UNSIGNED_BYTE, static_cast<GLsizei>(size),
                      texels.data());
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    return texels;
}

bool mentions(const engine::core::Result<TextureHandle>& result, const std::string& text)
{
    return !result.hasValue() && result.error().message.find(text) != std::string::npos;
}

} // namespace

TEST_CASE("Textures are created and destroyed through generation-checked handles",
          "[render][gpu][texture]")
{
    auto gpu = createRenderer();
    Renderer& renderer = *gpu.renderer;
    CHECK_FALSE(renderer.isValid(TextureHandle{}));
    CHECK(TextureHandle{}.isNull());

    const TextureHandle first = upload(renderer, solid(4, 4, PixelFormat::Rgba8, 200));
    const TextureHandle second = upload(renderer, solid(2, 2, PixelFormat::Rgb8, 100));
    CHECK_FALSE(first.isNull());
    CHECK(first != second);
    CHECK(renderer.isValid(first));
    CHECK(renderer.isValid(second));
    CHECK(textureObjects(gpu).size() == 2);

    renderer.destroyTexture(first);
    CHECK_FALSE(renderer.isValid(first));
    CHECK(renderer.isValid(second));
    CHECK(textureObjects(gpu).size() == 1);

    // Destroying again, or a null handle, is ignored.
    renderer.destroyTexture(first);
    renderer.destroyTexture(TextureHandle{});
    CHECK(textureObjects(gpu).size() == 1);

    // The freed slot is reused with a new generation: the old handle stays invalid.
    const TextureHandle third = upload(renderer, solid(1, 1, PixelFormat::Rgb8, 1));
    CHECK(third.index == first.index);
    CHECK(third.generation != first.generation);
    CHECK_FALSE(renderer.isValid(first));
    CHECK(renderer.isValid(third));
    CHECK(renderer.errorCount() == 0);
}

TEST_CASE("Invalid images are rejected with a reason and not uploaded", "[render][gpu][texture]")
{
    auto gpu = createRenderer();
    Renderer& renderer = *gpu.renderer;

    const auto empty = renderer.createTexture(Image{});
    CHECK(mentions(empty, "createTexture"));
    CHECK(mentions(empty, "image size"));

    CHECK(mentions(renderer.createTexture(Image{2, 2, {1, 2, 3}}), "expected 12 for 2x2 RGB"));
    // RGB-sized pixels declared as RGBA.
    CHECK(mentions(renderer.createTexture(Image{2, 2, Bytes(12, 0), PixelFormat::Rgba8}),
                   "expected 16 for 2x2 RGBA"));

    CHECK(textureObjects(gpu).empty());
    CHECK(renderer.errorCount() == 0);
}

TEST_CASE("Destroying the renderer releases its textures", "[render][gpu][texture]")
{
    auto gpu = createRenderer();
    for (int i = 0; i < 3; ++i) {
        (void)upload(*gpu.renderer, solid(8, 8, PixelFormat::Rgba8, 50));
    }
    CHECK(textureObjects(gpu).size() == 3);

    // The context is still current; only the renderer goes away, with its own
    // white texture too.
    gpu.renderer.reset();
    CHECK(allTextureObjects().empty());
    CHECK(glGetError() == GL_NO_ERROR);
}

TEST_CASE("The image's bottom row is uploaded to V = 0", "[render][gpu][texture]")
{
    auto gpu = createRenderer();
    Renderer& renderer = *gpu.renderer;
    const TextureDesc noMipmaps{
        .filter = TextureFilter::Nearest, .wrap = TextureWrap::ClampToEdge, .mipmaps = false};

    SECTION("RGB, one column")
    {
        // Image rows from the top: red, green, blue. Width 1 also checks
        // the unpack alignment (3-byte rows).
        const TextureHandle texture =
            upload(renderer, Image{1, 3, {255, 0, 0, 0, 255, 0, 0, 0, 255}}, noMipmaps);
        CHECK(readLevel0(onlyTexture(gpu), GL_RGB, 9) == Bytes{0, 0, 255, 0, 255, 0, 255, 0, 0});
        renderer.destroyTexture(texture);
    }
    SECTION("RGBA, columns stay in order")
    {
        // Top row A B, bottom row C D; texture row 0 must be C D.
        const Bytes a{1, 2, 3, 4};
        const Bytes b{5, 6, 7, 8};
        const Bytes c{9, 10, 11, 12};
        const Bytes d{13, 14, 15, 16};
        Bytes pixels;
        for (const Bytes* texel : {&a, &b, &c, &d}) {
            pixels.insert(pixels.end(), texel->begin(), texel->end());
        }
        (void)upload(renderer, Image{2, 2, pixels, PixelFormat::Rgba8}, noMipmaps);

        Bytes expected;
        for (const Bytes* texel : {&c, &d, &a, &b}) {
            expected.insert(expected.end(), texel->begin(), texel->end());
        }
        CHECK(readLevel0(onlyTexture(gpu), GL_RGBA, 16) == expected);
    }
    CHECK(glGetError() == GL_NO_ERROR);
    CHECK(renderer.errorCount() == 0);
}

TEST_CASE("TextureDesc sets filtering, wrapping and the mip chain", "[render][gpu][texture]")
{
    auto gpu = createRenderer();
    Renderer& renderer = *gpu.renderer;

    struct Case {
        TextureDesc desc;
        GLint minFilter;
        GLint magFilter;
        GLint wrap;
        GLint levels; // for a 5x3 image
    };
    const std::array<Case, 5> cases{{
        {TextureDesc{}, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, GL_REPEAT, 3}, // defaults
        {{TextureFilter::Linear, TextureWrap::ClampToEdge, false},
         GL_LINEAR,
         GL_LINEAR,
         GL_CLAMP_TO_EDGE,
         1},
        {{TextureFilter::Nearest, TextureWrap::Repeat, true},
         GL_NEAREST_MIPMAP_NEAREST,
         GL_NEAREST,
         GL_REPEAT,
         3},
        {{TextureFilter::Nearest, TextureWrap::ClampToEdge, false},
         GL_NEAREST,
         GL_NEAREST,
         GL_CLAMP_TO_EDGE,
         1},
        {{TextureFilter::Linear, TextureWrap::Repeat, false}, GL_LINEAR, GL_LINEAR, GL_REPEAT, 1},
    }};
    for (const PixelFormat format : {PixelFormat::Rgb8, PixelFormat::Rgba8}) {
        for (std::size_t i = 0; i < cases.size(); ++i) {
            INFO("case " << i << ", " << (format == PixelFormat::Rgba8 ? "RGBA" : "RGB"));
            const Case& expected = cases[i];
            const TextureHandle handle = upload(renderer, solid(5, 3, format, 9), expected.desc);
            const GLuint texture = onlyTexture(gpu);

            CHECK(textureParameter(texture, GL_TEXTURE_MIN_FILTER) == expected.minFilter);
            CHECK(textureParameter(texture, GL_TEXTURE_MAG_FILTER) == expected.magFilter);
            CHECK(textureParameter(texture, GL_TEXTURE_WRAP_S) == expected.wrap);
            CHECK(textureParameter(texture, GL_TEXTURE_WRAP_T) == expected.wrap);
            CHECK(textureParameter(texture, GL_TEXTURE_IMMUTABLE_LEVELS) == expected.levels);
            CHECK(levelParameter(texture, 0, GL_TEXTURE_WIDTH) == 5);
            CHECK(levelParameter(texture, 0, GL_TEXTURE_HEIGHT) == 3);
            CHECK(levelParameter(texture, 0, GL_TEXTURE_INTERNAL_FORMAT) ==
                  (format == PixelFormat::Rgba8 ? GL_RGBA8 : GL_RGB8));
            if (expected.levels > 1) {
                // 5x3 halves to 2x1, then 1x1.
                CHECK(levelParameter(texture, 1, GL_TEXTURE_WIDTH) == 2);
                CHECK(levelParameter(texture, 2, GL_TEXTURE_WIDTH) == 1);
            }
            renderer.destroyTexture(handle);
        }
    }
    CHECK(glGetError() == GL_NO_ERROR);
    CHECK(renderer.errorCount() == 0);
}

TEST_CASE("The largest image size is accepted as a texture", "[render][gpu][texture]")
{
    auto gpu = createRenderer();
    Renderer& renderer = *gpu.renderer;
    const int extent = engine::core::kMaxImageExtent;
    (void)upload(renderer, solid(extent, 1, PixelFormat::Rgba8, 7));
    const GLuint texture = onlyTexture(gpu);
    CHECK(levelParameter(texture, 0, GL_TEXTURE_WIDTH) == extent);
    // 16384 halves 14 times down to 1: 15 levels.
    CHECK(textureParameter(texture, GL_TEXTURE_IMMUTABLE_LEVELS) == 15);
    CHECK(glGetError() == GL_NO_ERROR);
    CHECK(renderer.errorCount() == 0);
}
