#include "../HandlePool.hpp"
#include "GlDiagnostics.hpp"
#include "GlHandle.hpp"
#include "GlMesh.hpp"
#include "GlShaderProgram.hpp"

#include "engine/core/FileSystem.hpp"
#include "engine/core/Log.hpp"
#include "engine/render/Renderer.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

namespace engine::render {

namespace {

using opengl::GlMesh;
using opengl::GlShaderProgram;

constexpr int kRequiredGlMajor = 4;
constexpr int kRequiredGlMinor = 5;

// Names the renderer expects in assets/shaders/mesh.vert.
constexpr const char* kMeshVertexShader = "mesh.vert";
constexpr const char* kMeshFragmentShader = "mesh.frag";
constexpr const char* kMvpUniform = "uModelViewProjection";
constexpr const char* kPositionAttribute = "aPosition";
constexpr const char* kColorAttribute = "aColor";

struct LoadedProgram {
    GlShaderProgram program;
    std::string description;
};

core::Result<LoadedProgram> loadProgram(const std::filesystem::path& directory,
                                        const char* vertexFile, const char* fragmentFile)
{
    const auto vertexPath = directory / vertexFile;
    const auto fragmentPath = directory / fragmentFile;
    auto vertexCode = core::readTextFile(vertexPath);
    if (!vertexCode) {
        return vertexCode.error();
    }
    auto fragmentCode = core::readTextFile(fragmentPath);
    if (!fragmentCode) {
        return fragmentCode.error();
    }
    const std::string vertexName = core::toUtf8(vertexPath);
    const std::string fragmentName = core::toUtf8(fragmentPath);
    auto program = GlShaderProgram::create({vertexName, vertexCode.value()},
                                           {fragmentName, fragmentCode.value()});
    if (!program) {
        return program.error();
    }
    return LoadedProgram{std::move(program).value(),
                         "'" + vertexName + "' + '" + fragmentName + "'"};
}

// The mesh program must expose the interface GlMesh and drawMesh rely on.
// Catches renamed or unused (optimized away) variables with a clear message
// instead of a silently black or empty frame.
std::optional<core::Error> checkMeshInterface(const LoadedProgram& loaded, GLint& mvpLocation)
{
    const GLuint program = loaded.program.name();
    const auto requireAttribute = [&](const char* name,
                                      GLuint location) -> std::optional<core::Error> {
        const GLint actual = glGetAttribLocation(program, name);
        if (actual != static_cast<GLint>(location)) {
            return core::Error{"shader program " + loaded.description + ": vertex input '" + name +
                               "' must be active at location " + std::to_string(location) +
                               (actual < 0 ? " (missing or unused)"
                                           : " (found at " + std::to_string(actual) + ")")};
        }
        return std::nullopt;
    };
    if (auto error = requireAttribute(kPositionAttribute, opengl::kPositionLocation)) {
        return error;
    }
    if (auto error = requireAttribute(kColorAttribute, opengl::kColorLocation)) {
        return error;
    }
    mvpLocation = glGetUniformLocation(program, kMvpUniform);
    if (mvpLocation < 0) {
        return core::Error{"shader program " + loaded.description + ": uniform mat4 '" +
                           kMvpUniform + "' is missing or unused"};
    }
    return std::nullopt;
}

detail::PoolHandle toPoolHandle(MeshHandle handle) noexcept
{
    return detail::PoolHandle{handle.index, handle.generation};
}

std::string glString(GLenum name)
{
    // glGetString returns null on error; the bytes are ASCII/UTF-8 text.
    const GLubyte* text = glGetString(name);
    return text != nullptr ? std::string(reinterpret_cast<const char*>(text)) : "(unknown)";
}

RendererInfo queryInfo()
{
    return RendererInfo{
        .api = "OpenGL " + std::to_string(kRequiredGlMajor) + "." +
               std::to_string(kRequiredGlMinor) + " Core",
        .driverVersion = glString(GL_VERSION),
        .device = glString(GL_RENDERER),
        .vendor = glString(GL_VENDOR),
    };
}

} // namespace

struct Renderer::Impl {
    Impl(GlShaderProgram program, GLint mvp, bool debugOutput)
        : meshProgram(std::move(program)), mvpLocation(mvp), debugOutputEnabled(debugOutput)
    {
    }

    ~Impl()
    {
        if (debugOutputEnabled) {
            opengl::disableDebugOutput();
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    GlShaderProgram meshProgram;
    GLint mvpLocation = -1;
    RendererInfo info = queryInfo();
    detail::HandlePool<GlMesh> meshes;
    bool debugOutputEnabled = false;
    std::uint32_t errorCount = 0;
    bool invalidDrawLogged = false;
    // Size passed to the last beginFrame(); 0 before the first frame.
    int frameWidth = 0;
    int frameHeight = 0;
    math::Mat4 viewProjection{1.0F};
    RenderStats currentFrame;
    RenderStats lastFrame;
};

core::Result<Renderer> Renderer::create(const RendererDesc& desc)
{
    if (desc.glProcLoader == nullptr) {
        return core::Error{"Renderer::create: glProcLoader is null"};
    }

    // GLAD stores the function pointers globally; with one context on one
    // thread (the current design) that is safe.
    const int version = gladLoadGL(desc.glProcLoader);
    if (version == 0) {
        return core::Error{"failed to load OpenGL functions (is a context current?)"};
    }
    const int major = GLAD_VERSION_MAJOR(version);
    const int minor = GLAD_VERSION_MINOR(version);
    if (major < kRequiredGlMajor || (major == kRequiredGlMajor && minor < kRequiredGlMinor)) {
        return core::Error{"OpenGL " + std::to_string(kRequiredGlMajor) + "." +
                           std::to_string(kRequiredGlMinor) + " required, context provides " +
                           std::to_string(major) + "." + std::to_string(minor)};
    }

    const bool debugOutput = desc.debugOutput && opengl::enableDebugOutput();
    if (desc.debugOutput && !debugOutput) {
        core::logWarning("render",
                         "debug output requested, but the context is not a debug context");
    }
    const auto failSetup = [debugOutput](core::Error error) {
        if (debugOutput) {
            opengl::disableDebugOutput();
        }
        return error;
    };

    auto loaded = loadProgram(desc.shaderDirectory, kMeshVertexShader, kMeshFragmentShader);
    if (!loaded) {
        return failSetup(loaded.error());
    }
    GLint mvpLocation = -1;
    if (auto error = checkMeshInterface(loaded.value(), mvpLocation)) {
        return failSetup(std::move(*error));
    }

    // Fixed pipeline state for the forward renderer. Front faces are
    // counter-clockwise (MeshData convention); back faces are never visible on
    // closed low-poly meshes and are skipped.
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glDepthFunc(GL_LESS);

    auto impl = std::make_unique<Impl>(std::move(loaded).value().program, mvpLocation, debugOutput);
    impl->errorCount += opengl::checkGlErrors("renderer setup");
    return Renderer{std::move(impl)};
}

Renderer::Renderer(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

Renderer::~Renderer() = default;
Renderer::Renderer(Renderer&& other) noexcept = default;
Renderer& Renderer::operator=(Renderer&& other) noexcept = default;

core::Result<MeshHandle> Renderer::createMesh(const geometry::MeshData& mesh)
{
    if (auto error = geometry::validate(mesh)) {
        return core::Error{"createMesh: " + error->message};
    }
    auto glMesh = GlMesh::create(mesh);
    if (!glMesh) {
        return core::Error{"createMesh: " + glMesh.error().message};
    }
    const detail::PoolHandle handle = impl_->meshes.insert(std::move(glMesh).value());
    return MeshHandle{handle.index, handle.generation};
}

std::optional<core::Error> Renderer::updateMesh(MeshHandle handle, const geometry::MeshData& mesh)
{
    GlMesh* glMesh = impl_->meshes.find(toPoolHandle(handle));
    if (glMesh == nullptr) {
        return core::Error{"updateMesh: invalid or destroyed mesh handle"};
    }
    if (auto error = geometry::validate(mesh)) {
        return core::Error{"updateMesh: " + error->message};
    }
    if (auto error = glMesh->update(mesh)) {
        return core::Error{"updateMesh: " + error->message};
    }
    return std::nullopt;
}

void Renderer::destroyMesh(MeshHandle handle) noexcept
{
    (void)impl_->meshes.erase(toPoolHandle(handle));
}

bool Renderer::isValid(MeshHandle handle) const noexcept
{
    return impl_->meshes.find(toPoolHandle(handle)) != nullptr;
}

void Renderer::beginFrame(int framebufferWidth, int framebufferHeight, const Color& clearColor)
{
    impl_->frameWidth = framebufferWidth;
    impl_->frameHeight = framebufferHeight;
    impl_->currentFrame = RenderStats{};
    glViewport(0, 0, framebufferWidth, framebufferHeight);

    // glClear respects the depth write mask, so restore it before clearing.
    setView(ViewDesc{});
    glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    impl_->meshProgram.use();
}

void Renderer::setView(const ViewDesc& view)
{
    impl_->viewProjection = view.projection * view.view;

    if (view.depth == DepthMode::TestAndWrite) {
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
    } else {
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
    }

    if (view.blend == BlendMode::Alpha) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_BLEND);
    }
}

void Renderer::drawMesh(MeshHandle handle, const math::Mat4& model)
{
    const GlMesh* glMesh = impl_->meshes.find(toPoolHandle(handle));
    if (glMesh == nullptr) {
        ++impl_->errorCount;
        // Usually happens every frame once it happens; one message is enough.
        if (!impl_->invalidDrawLogged) {
            impl_->invalidDrawLogged = true;
            core::logError("render",
                           "drawMesh: invalid or destroyed mesh handle (index {}, "
                           "generation {}); further invalid draws are only counted",
                           handle.index, handle.generation);
        }
        return;
    }

    const math::Mat4 modelViewProjection = impl_->viewProjection * model;
    glProgramUniformMatrix4fv(impl_->meshProgram.name(), impl_->mvpLocation, 1, GL_FALSE,
                              glm::value_ptr(modelViewProjection));
    glMesh->draw();

    ++impl_->currentFrame.drawCalls;
    impl_->currentFrame.triangles += glMesh->indexCount() / 3;
}

void Renderer::endFrame()
{
    glBindVertexArray(0);
    impl_->errorCount += opengl::checkGlErrors("frame");
    impl_->lastFrame = impl_->currentFrame;
}

core::Result<core::Image> Renderer::captureFrame()
{
    const int width = impl_->frameWidth;
    const int height = impl_->frameHeight;
    if (width <= 0 || height <= 0) {
        return core::Error{"captureFrame: no frame has been rendered yet"};
    }

    constexpr std::size_t kChannels = 3;
    const auto rowBytes = static_cast<std::size_t>(width) * kChannels;
    core::Image image{width, height, {}};
    image.pixels.resize(rowBytes * static_cast<std::size_t>(height));

    // Rows are tightly packed in core::Image; the default pack alignment is 4.
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, image.pixels.data());
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    if (const auto errors = opengl::checkGlErrors("captureFrame"); errors > 0) {
        impl_->errorCount += errors;
        return core::Error{"captureFrame: glReadPixels failed"};
    }

    // OpenGL returns the bottom row first; core::Image stores the top row first.
    for (std::size_t top = 0, bottom = static_cast<std::size_t>(height) - 1; top < bottom;
         ++top, --bottom) {
        std::swap_ranges(image.pixels.begin() + static_cast<std::ptrdiff_t>(top * rowBytes),
                         image.pixels.begin() + static_cast<std::ptrdiff_t>((top + 1) * rowBytes),
                         image.pixels.begin() + static_cast<std::ptrdiff_t>(bottom * rowBytes));
    }
    return image;
}

std::uint32_t Renderer::errorCount() const noexcept
{
    return impl_->errorCount;
}

const RenderStats& Renderer::lastFrameStats() const noexcept
{
    return impl_->lastFrame;
}

const RendererInfo& Renderer::info() const noexcept
{
    return impl_->info;
}

} // namespace engine::render
