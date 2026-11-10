#pragma once

// RAII ownership of OpenGL object names. Internal to the OpenGL backend.
//
// Destroying a handle calls into OpenGL, so it must happen on the thread whose
// current context created the object, while that context is still alive.

#include <glad/gl.h>

#include <utility>

namespace engine::render::opengl {

template <typename Deleter>
class GlHandle {
public:
    GlHandle() = default;
    explicit GlHandle(GLuint name) noexcept : name_(name) {}

    ~GlHandle() { reset(); }

    GlHandle(GlHandle&& other) noexcept : name_(std::exchange(other.name_, 0)) {}
    GlHandle& operator=(GlHandle&& other) noexcept
    {
        if (this != &other) {
            reset();
            name_ = std::exchange(other.name_, 0);
        }
        return *this;
    }
    GlHandle(const GlHandle&) = delete;
    GlHandle& operator=(const GlHandle&) = delete;

    [[nodiscard]] GLuint get() const noexcept { return name_; }
    [[nodiscard]] explicit operator bool() const noexcept { return name_ != 0; }

    void reset() noexcept
    {
        // Name 0 is never a valid object, so it marks "empty" without extra state.
        if (name_ != 0) {
            Deleter{}(name_);
            name_ = 0;
        }
    }

private:
    GLuint name_ = 0;
};

struct GlBufferDeleter {
    void operator()(GLuint name) const noexcept { glDeleteBuffers(1, &name); }
};
struct GlVertexArrayDeleter {
    void operator()(GLuint name) const noexcept { glDeleteVertexArrays(1, &name); }
};
struct GlShaderDeleter {
    void operator()(GLuint name) const noexcept { glDeleteShader(name); }
};
struct GlProgramDeleter {
    void operator()(GLuint name) const noexcept { glDeleteProgram(name); }
};

using GlBuffer = GlHandle<GlBufferDeleter>;
using GlVertexArray = GlHandle<GlVertexArrayDeleter>;
using GlShader = GlHandle<GlShaderDeleter>;
using GlProgram = GlHandle<GlProgramDeleter>;

} // namespace engine::render::opengl
