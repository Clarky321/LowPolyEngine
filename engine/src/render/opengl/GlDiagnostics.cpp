#include "GlDiagnostics.hpp"

#include <glad/gl.h>

#include <cstddef>
#include <cstdio>
#include <string_view>

namespace engine::render::opengl {

namespace {

// glGetError can keep returning an error when no context is current; the
// limit keeps the drain loop from spinning forever in that case.
constexpr int kMaxErrorsPerCheck = 16;

const char* errorName(GLenum error)
{
    switch (error) {
    case GL_INVALID_ENUM:
        return "GL_INVALID_ENUM";
    case GL_INVALID_VALUE:
        return "GL_INVALID_VALUE";
    case GL_INVALID_OPERATION:
        return "GL_INVALID_OPERATION";
    case GL_INVALID_FRAMEBUFFER_OPERATION:
        return "GL_INVALID_FRAMEBUFFER_OPERATION";
    case GL_OUT_OF_MEMORY:
        return "GL_OUT_OF_MEMORY";
    case GL_STACK_UNDERFLOW:
        return "GL_STACK_UNDERFLOW";
    case GL_STACK_OVERFLOW:
        return "GL_STACK_OVERFLOW";
    case GL_CONTEXT_LOST:
        return "GL_CONTEXT_LOST";
    default:
        return "unknown GL error";
    }
}

const char* debugTypeName(GLenum type)
{
    switch (type) {
    case GL_DEBUG_TYPE_ERROR:
        return "error";
    case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
        return "deprecated";
    case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
        return "undefined behavior";
    case GL_DEBUG_TYPE_PORTABILITY:
        return "portability";
    case GL_DEBUG_TYPE_PERFORMANCE:
        return "performance";
    default:
        return "other";
    }
}

const char* debugSeverityName(GLenum severity)
{
    switch (severity) {
    case GL_DEBUG_SEVERITY_HIGH:
        return "high";
    case GL_DEBUG_SEVERITY_MEDIUM:
        return "medium";
    case GL_DEBUG_SEVERITY_LOW:
        return "low";
    default:
        return "notification";
    }
}

// Called by the driver. Synchronous output (enabled below) means it runs on
// the thread of the failing GL call, inside that call, which makes a debugger
// breakpoint here point at the culprit. Must not throw.
void GLAD_API_PTR onDebugMessage(GLenum /*source*/, GLenum type, GLuint id, GLenum severity,
                                 GLsizei length, const GLchar* message, const void* /*userParam*/)
{
    // Notifications (e.g. buffer placement details) are too noisy to print.
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) {
        return;
    }
    const std::string_view text(message, length > 0 ? static_cast<std::size_t>(length) : 0U);
    std::fprintf(stderr, "[render] GL debug (%s, %s, id %u): %.*s\n", debugTypeName(type),
                 debugSeverityName(severity), id, static_cast<int>(text.size()), text.data());
}

} // namespace

bool enableDebugOutput()
{
    GLint flags = 0;
    glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
    if ((flags & GL_CONTEXT_FLAG_DEBUG_BIT) == 0) {
        return false;
    }
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(&onDebugMessage, nullptr);
    return true;
}

void disableDebugOutput()
{
    glDebugMessageCallback(nullptr, nullptr);
    glDisable(GL_DEBUG_OUTPUT);
}

std::uint32_t checkGlErrors(const char* where)
{
    std::uint32_t count = 0;
    for (int i = 0; i < kMaxErrorsPerCheck; ++i) {
        const GLenum error = glGetError();
        if (error == GL_NO_ERROR) {
            break;
        }
        std::fprintf(stderr, "[render] OpenGL error after %s: %s (0x%04X)\n", where,
                     errorName(error), error);
        ++count;
    }
    return count;
}

} // namespace engine::render::opengl
