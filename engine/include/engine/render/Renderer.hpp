#pragma once

#include "engine/core/Image.hpp"
#include "engine/core/Math.hpp"
#include "engine/core/Result.hpp"
#include "engine/geometry/MeshData.hpp"
#include "engine/platform/GlContext.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

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
    // Log driver debug messages. Reports details only with a debug context.
    bool debugOutput = false;
};

// Identifies a mesh uploaded to the GPU. A default handle is null; a handle
// stays detectably invalid after its mesh was destroyed, even if the slot is reused.
struct MeshHandle {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;

    [[nodiscard]] bool isNull() const noexcept { return generation == 0; }
    friend bool operator==(const MeshHandle&, const MeshHandle&) = default;
};

enum class DepthMode {
    // Nearer surfaces hide farther ones (3D scenes).
    TestAndWrite,
    // Draw order decides (screen overlays).
    Off,
};

enum class BlendMode {
    Opaque,
    // Color alpha blends over what is already drawn.
    Alpha,
};

// Camera and pipeline state for the following draw calls.
struct ViewDesc {
    math::Mat4 view{1.0F};
    math::Mat4 projection{1.0F};
    DepthMode depth = DepthMode::TestAndWrite;
    BlendMode blend = BlendMode::Opaque;
};

// Work submitted in one frame.
struct RenderStats {
    std::uint32_t drawCalls = 0;
    std::uint64_t triangles = 0;
};

// Forward renderer, OpenGL 4.5 backend. No OpenGL type appears in this header;
// the implementation lives in engine/src/render/opengl/.
//
// Uses the OpenGL context that is current on the calling thread: create, use
// and destroy the renderer on that thread, and destroy it before the context.
// Destroying the renderer releases all meshes it still owns.
class Renderer {
public:
    [[nodiscard]] static core::Result<Renderer> create(const RendererDesc& desc);

    ~Renderer();
    Renderer(Renderer&& other) noexcept;
    Renderer& operator=(Renderer&& other) noexcept;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Uploads a mesh. Fails if the data does not pass geometry::validate().
    [[nodiscard]] core::Result<MeshHandle> createMesh(const geometry::MeshData& mesh);

    // Replaces a mesh's data. Reuses the GPU buffers while the data fits, so
    // meshes rebuilt every frame (e.g. text) do not reallocate each time.
    [[nodiscard]] std::optional<core::Error> updateMesh(MeshHandle handle,
                                                        const geometry::MeshData& mesh);

    // Releases the GPU buffers. Null and already destroyed handles are ignored.
    void destroyMesh(MeshHandle handle) noexcept;

    [[nodiscard]] bool isValid(MeshHandle handle) const noexcept;

    // Sets the viewport to the framebuffer size, clears color and depth and
    // resets the view to identity matrices (normalized device coordinates).
    void beginFrame(int framebufferWidth, int framebufferHeight, const Color& clearColor);

    // Camera matrices and depth/blend state for the following draw calls.
    void setView(const ViewDesc& view);

    // Draws a mesh with a model (object-to-world) matrix. Drawing an invalid
    // handle draws nothing and counts as an error (see errorCount()).
    void drawMesh(MeshHandle handle, const math::Mat4& model = math::Mat4{1.0F});

    // Checks the OpenGL error flag. Call once per frame after drawing.
    void endFrame();

    // Reads back the current frame (call after endFrame, before the buffers are
    // swapped). Fails if no frame was started yet.
    [[nodiscard]] core::Result<core::Image> captureFrame();

    // Errors detected so far: OpenGL errors and draws with invalid handles.
    [[nodiscard]] std::uint32_t errorCount() const noexcept;

    // Statistics of the last completed frame.
    [[nodiscard]] const RenderStats& lastFrameStats() const noexcept;

private:
    struct Impl;
    explicit Renderer(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

} // namespace engine::render
