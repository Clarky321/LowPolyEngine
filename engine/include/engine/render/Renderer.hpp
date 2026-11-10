#pragma once

#include "engine/core/Result.hpp"
#include "engine/platform/GlContext.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>

namespace engine::render {

struct Color {
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    float a = 1.0F;
};

struct RendererDesc {
    // Resolves OpenGL entry points for the context current on this thread.
    platform::GlProcLoader glProcLoader = nullptr;
    // Folder with the renderer's GLSL sources (assets/shaders/).
    std::filesystem::path shaderDirectory;
    // Print driver debug messages to stderr. Reports details only with a debug context.
    bool debugOutput = false;
};

// Forward renderer, OpenGL 4.5 backend. No OpenGL type appears in this header;
// the implementation lives in engine/src/render/opengl/.
//
// Uses the OpenGL context that is current on the calling thread: create, use
// and destroy the renderer on that thread, and destroy it before the context.
class Renderer {
public:
    [[nodiscard]] static core::Result<Renderer> create(const RendererDesc& desc);

    ~Renderer();
    Renderer(Renderer&& other) noexcept;
    Renderer& operator=(Renderer&& other) noexcept;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Sets the viewport to the framebuffer size and clears color and depth.
    void beginFrame(int framebufferWidth, int framebufferHeight, const Color& clearColor);

    // Draws one vertex-colored triangle. M0.5 bring-up check of buffers, vertex
    // layout and shaders; replaced by mesh drawing in M1.
    void drawTestTriangle();

    // Checks the OpenGL error flag. Call once per frame after drawing.
    void endFrame();

    // OpenGL errors detected so far (setup and frames).
    [[nodiscard]] std::uint32_t errorCount() const noexcept;

private:
    struct Impl;
    explicit Renderer(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

} // namespace engine::render
