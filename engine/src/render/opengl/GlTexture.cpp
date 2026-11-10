#include "GlTexture.hpp"

#include "GlDiagnostics.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace engine::render::opengl {

namespace {

// OpenGL 4.5 guarantees GL_MAX_TEXTURE_SIZE >= 16384, so every image that
// passes core::validate() fits without querying the driver.
static_assert(core::kMaxImageExtent <= 16384);

// Levels of a full mip chain: the largest side halves down to 1.
GLsizei mipLevelCount(int width, int height)
{
    const int largest = std::max(width, height);
    GLsizei levels = 1;
    while ((largest >> levels) > 0) {
        ++levels;
    }
    return levels;
}

GLint minificationFilter(const TextureDesc& desc)
{
    if (desc.filter == TextureFilter::Nearest) {
        return desc.mipmaps ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST;
    }
    return desc.mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR;
}

GLint magnificationFilter(const TextureDesc& desc)
{
    return desc.filter == TextureFilter::Nearest ? GL_NEAREST : GL_LINEAR;
}

GLint wrapMode(const TextureDesc& desc)
{
    return desc.wrap == TextureWrap::Repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
}

} // namespace

core::Result<GlTexture> GlTexture::create(const core::Image& image, const TextureDesc& desc)
{
    const bool rgba = image.format == core::PixelFormat::Rgba8;
    const GLenum internalFormat = rgba ? GL_RGBA8 : GL_RGB8;
    const GLenum pixelFormat = rgba ? GL_RGBA : GL_RGB;
    const GLsizei levels = desc.mipmaps ? mipLevelCount(image.width, image.height) : 1;

    GLuint name = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &name);
    GlTextureObject texture{name};
    glTextureStorage2D(texture.get(), levels, internalFormat, image.width, image.height);

    // Rows are tightly packed in core::Image; the default unpack alignment is 4.
    // core::Image stores the top row first, OpenGL puts the first uploaded row
    // at V = 0 (the bottom), so the rows are uploaded bottom up, one at a time
    // (no flipped copy of the image is needed).
    const auto rowBytes = static_cast<std::size_t>(image.width) *
                          static_cast<std::size_t>(core::channelCount(image.format));
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int row = 0; row < image.height; ++row) {
        const std::uint8_t* source = image.pixels.data() + static_cast<std::size_t>(row) * rowBytes;
        glTextureSubImage2D(texture.get(), 0, 0, image.height - 1 - row, image.width, 1,
                            pixelFormat, GL_UNSIGNED_BYTE, source);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (desc.mipmaps) {
        glGenerateTextureMipmap(texture.get());
    }

    glTextureParameteri(texture.get(), GL_TEXTURE_MIN_FILTER, minificationFilter(desc));
    glTextureParameteri(texture.get(), GL_TEXTURE_MAG_FILTER, magnificationFilter(desc));
    glTextureParameteri(texture.get(), GL_TEXTURE_WRAP_S, wrapMode(desc));
    glTextureParameteri(texture.get(), GL_TEXTURE_WRAP_T, wrapMode(desc));

    if (checkGlErrors("texture upload") > 0) {
        return core::Error{"uploading a " + std::to_string(image.width) + "x" +
                           std::to_string(image.height) +
                           " texture failed (see the OpenGL error above)"};
    }
    return GlTexture{std::move(texture)};
}

} // namespace engine::render::opengl
