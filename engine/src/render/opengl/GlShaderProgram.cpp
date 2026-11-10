#include "GlShaderProgram.hpp"

#include <cstddef>
#include <string>
#include <utility>

namespace engine::render::opengl {

namespace {

// Reads a shader or program info log; getParameter/getLog are the matching
// glGetShaderiv/glGetShaderInfoLog or glGetProgramiv/glGetProgramInfoLog pair.
// Drivers fill the log mainly on failure and some leave it empty even then,
// so an empty log gets a placeholder text.
template <typename GetParameter, typename GetLog>
std::string infoLog(GLuint object, GetParameter getParameter, GetLog getLog)
{
    GLint length = 0;
    getParameter(object, GL_INFO_LOG_LENGTH, &length);
    if (length <= 1) {
        return "(driver returned no info log)";
    }
    std::string log(static_cast<std::size_t>(length), '\0');
    GLsizei written = 0;
    getLog(object, length, &written, log.data());
    log.resize(static_cast<std::size_t>(written));
    return log;
}

core::Result<GlShader> compileShader(GLenum stage, const ShaderSource& source)
{
    GlShader shader{glCreateShader(stage)};
    if (!shader) {
        return core::Error{"glCreateShader failed for '" + std::string(source.name) + "'"};
    }

    // Passing the length means the source does not need a terminating null.
    const GLchar* code = source.code.data();
    const auto length = static_cast<GLint>(source.code.size());
    glShaderSource(shader.get(), 1, &code, &length);
    glCompileShader(shader.get());

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader.get(), GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        return core::Error{"failed to compile shader '" + std::string(source.name) + "':\n" +
                           infoLog(shader.get(), glGetShaderiv, glGetShaderInfoLog)};
    }
    return shader;
}

} // namespace

core::Result<GlShaderProgram> GlShaderProgram::create(const ShaderSource& vertex,
                                                      const ShaderSource& fragment)
{
    auto vertexShader = compileShader(GL_VERTEX_SHADER, vertex);
    if (!vertexShader) {
        return vertexShader.error();
    }
    auto fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragment);
    if (!fragmentShader) {
        return fragmentShader.error();
    }

    GlProgram program{glCreateProgram()};
    if (!program) {
        return core::Error{"glCreateProgram failed"};
    }
    glAttachShader(program.get(), vertexShader.value().get());
    glAttachShader(program.get(), fragmentShader.value().get());
    glLinkProgram(program.get());
    // The linked program keeps the compiled code; the shader objects can go.
    glDetachShader(program.get(), vertexShader.value().get());
    glDetachShader(program.get(), fragmentShader.value().get());

    GLint linked = GL_FALSE;
    glGetProgramiv(program.get(), GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        return core::Error{"failed to link shader program ('" + std::string(vertex.name) + "' + '" +
                           std::string(fragment.name) + "'):\n" +
                           infoLog(program.get(), glGetProgramiv, glGetProgramInfoLog)};
    }
    return GlShaderProgram{std::move(program)};
}

GlShaderProgram::GlShaderProgram(GlProgram program) noexcept : program_(std::move(program)) {}

void GlShaderProgram::use() const
{
    glUseProgram(program_.get());
}

} // namespace engine::render::opengl
