#pragma once

#include "GlHandle.hpp"

#include "engine/core/Result.hpp"

#include <string_view>

namespace engine::render::opengl {

struct ShaderSource {
    // Shown in error messages, normally the file path.
    std::string_view name;
    std::string_view code;
};

// A linked vertex + fragment program. Compile and link errors are returned with
// the driver's info log and the shader name, never just logged.
class GlShaderProgram {
public:
    [[nodiscard]] static core::Result<GlShaderProgram> create(const ShaderSource& vertex,
                                                              const ShaderSource& fragment);

    void use() const;

private:
    explicit GlShaderProgram(GlProgram program) noexcept;

    GlProgram program_;
};

} // namespace engine::render::opengl
