#pragma once

#include "engine/core/Result.hpp"

#include <memory>

namespace engine::platform {

class Window;

// Generic OpenGL function pointer and the loader that resolves entry points by
// name. Same signature as GLAD's loader callback, so the renderer can pass it on.
using GlProc = void (*)();
using GlProcLoader = GlProc (*)(const char* name);

struct GlContextDesc {
    int majorVersion = 4;
    int minorVersion = 5;
    // Ask for a debug context so the driver reports errors and warnings through
    // the debug-output callback. Slower; meant for development builds.
    bool debug = false;
    // Wait for the display's vertical refresh on swap, which also paces the frame loop.
    bool vsync = true;
};

// An OpenGL core-profile context bound to one window.
//
// The context is made current on the calling thread when it is created; all
// OpenGL calls must happen on that thread. Destroy it before its Window.
class GlContext {
public:
    // The window must have been created with WindowDesc::openGl = true.
    [[nodiscard]] static core::Result<GlContext> create(Window& window, const GlContextDesc& desc);

    ~GlContext();
    GlContext(GlContext&& other) noexcept;
    GlContext& operator=(GlContext&& other) noexcept;
    GlContext(const GlContext&) = delete;
    GlContext& operator=(const GlContext&) = delete;

    // Resolves OpenGL functions for the current context (for GLAD).
    [[nodiscard]] GlProcLoader procLoader() const noexcept;

    // Presents the back buffer. Blocks for vertical refresh when vsync is on.
    void swapBuffers();

    // False if vsync was requested but the driver refused it.
    [[nodiscard]] bool vsyncEnabled() const noexcept;

private:
    struct Impl;
    explicit GlContext(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

} // namespace engine::platform
