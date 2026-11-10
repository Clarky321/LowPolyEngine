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
#include <string>

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

// Identifies a texture uploaded to the GPU; null and stale handles behave like
// MeshHandle's.
struct TextureHandle {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;

    [[nodiscard]] bool isNull() const noexcept { return generation == 0; }
    friend bool operator==(const TextureHandle&, const TextureHandle&) = default;
};

enum class TextureFilter {
    // The closest texel: hard pixel edges.
    Nearest,
    // Blends neighboring texels (and mip levels): smooth.
    Linear,
};

enum class TextureWrap {
    // UVs outside [0, 1] tile the texture.
    Repeat,
    // UVs outside [0, 1] repeat the border texels.
    ClampToEdge,
};

// How a texture is sampled. Defaults suit color textures on 3D surfaces.
struct TextureDesc {
    TextureFilter filter = TextureFilter::Linear;
    TextureWrap wrap = TextureWrap::Repeat;
    // A full mip chain down to 1x1, generated from the image, avoids shimmering
    // when the texture is drawn smaller than its size.
    bool mipmaps = true;
};

// Identifies a material owned by the renderer; null and stale handles behave
// like MeshHandle's.
struct MaterialHandle {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;

    [[nodiscard]] bool isNull() const noexcept { return generation == 0; }
    friend bool operator==(const MaterialHandle&, const MaterialHandle&) = default;
};

// Surface appearance of a draw (ADR 0010). In M2 it is one color; later M2
// steps add a base-color texture and lit/unlit shading.
struct MaterialDesc {
    // RGBA, each component in [0, 1]. Multiplies the mesh's vertex colors, so
    // white keeps them; alpha matters with BlendMode::Alpha. Display-space
    // values, like vertex colors (ADR 0009).
    Color baseColor{1.0F, 1.0F, 1.0F, 1.0F};
};

// Checks that every base color component is finite and in [0, 1]. The error
// names the first offending component. GPU-free.
[[nodiscard]] std::optional<core::Error> validate(const MaterialDesc& desc);

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

// What the renderer runs on, for diagnostics (logs, debug overlay).
struct RendererInfo {
    // Graphics API and version the renderer targets, e.g. "OpenGL 4.5 Core".
    std::string api;
    // Version string reported by the driver (may be newer than the target).
    std::string driverVersion;
    // GPU / renderer name and vendor as reported by the driver.
    std::string device;
    std::string vendor;
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
// Destroying the renderer releases all meshes and textures it still owns.
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

    // Uploads an image (RGB8 or RGBA8) as a texture. Fails if the image does
    // not pass core::validate(). Image rows run top to bottom; the upload puts
    // the bottom row at V = 0, so UV (0, 0) is the image's bottom-left corner
    // (ADR 0007, 0009). Texel values are used as stored: no sRGB decoding.
    [[nodiscard]] core::Result<TextureHandle> createTexture(const core::Image& image,
                                                            const TextureDesc& desc = {});

    // Releases the texture. Null and already destroyed handles are ignored.
    void destroyTexture(TextureHandle handle) noexcept;

    [[nodiscard]] bool isValid(TextureHandle handle) const noexcept;

    // Sets the viewport to the framebuffer size, clears color and depth and
    // resets the view to identity matrices (normalized device coordinates).
    void beginFrame(int framebufferWidth, int framebufferHeight, const Color& clearColor);

    // Camera matrices and depth/blend state for the following draw calls.
    void setView(const ViewDesc& view);

    // Stores a material. Fails if the description does not pass validate().
    [[nodiscard]] core::Result<MaterialHandle> createMaterial(const MaterialDesc& desc);

    // Forgets the material. Null and already destroyed handles are ignored.
    void destroyMaterial(MaterialHandle handle) noexcept;

    [[nodiscard]] bool isValid(MaterialHandle handle) const noexcept;

    // Draws a mesh with a model (object-to-world) matrix and the built-in
    // default material (white base color: the vertex colors as they are).
    // Drawing an invalid handle draws nothing and counts as an error (see
    // errorCount()).
    void drawMesh(MeshHandle handle, const math::Mat4& model = math::Mat4{1.0F});

    // Draws a mesh with a material: output color = vertex color x base color.
    // An invalid mesh or material handle draws nothing and counts as one error.
    void drawMesh(MeshHandle mesh, MaterialHandle material,
                  const math::Mat4& model = math::Mat4{1.0F});

    // Checks the OpenGL error flag. Call once per frame after drawing.
    void endFrame();

    // Reads back the current frame (call after endFrame, before the buffers are
    // swapped). Fails if no frame was started yet.
    [[nodiscard]] core::Result<core::Image> captureFrame();

    // Errors detected so far: OpenGL errors and draws with invalid handles.
    [[nodiscard]] std::uint32_t errorCount() const noexcept;

    // Statistics of the last completed frame.
    [[nodiscard]] const RenderStats& lastFrameStats() const noexcept;

    [[nodiscard]] const RendererInfo& info() const noexcept;

private:
    struct Impl;
    explicit Renderer(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

} // namespace engine::render
