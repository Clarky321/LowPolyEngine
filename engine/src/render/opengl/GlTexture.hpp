#pragma once

#include "GlHandle.hpp"

#include "engine/core/Image.hpp"
#include "engine/core/Result.hpp"
#include "engine/render/Renderer.hpp"

#include <utility>

namespace engine::render::opengl {

// GPU copy of a core::Image. Internal to the OpenGL backend.
//
// Immutable storage (glTextureStorage2D) in GL_RGB8 or GL_RGBA8: texel values
// are used as stored, without sRGB decoding (ADR 0009). The image's bottom row
// becomes texture row 0 (V = 0), so UV (0, 0) is its bottom-left corner. The
// sampling state from TextureDesc is set on the texture object itself.
class GlTexture {
public:
    // The image must already have passed core::validate().
    [[nodiscard]] static core::Result<GlTexture> create(const core::Image& image,
                                                        const TextureDesc& desc);

    // Binds the texture to a texture unit for the following draws.
    void bind(GLuint unit) const;

private:
    explicit GlTexture(GlTextureObject texture) noexcept : texture_(std::move(texture)) {}

    GlTextureObject texture_;
};

} // namespace engine::render::opengl
