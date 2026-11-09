#include "GlDiagnostics.hpp"
#include "GlHandle.hpp"
#include "GlShaderProgram.hpp"

#include "engine/core/FileSystem.hpp"
#include "engine/core/Log.hpp"
#include "engine/render/Renderer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <utility>

namespace engine::render {

namespace {

using opengl::GlBuffer;
using opengl::GlShaderProgram;
using opengl::GlVertexArray;

constexpr int kRequiredGlMajor = 4;
constexpr int kRequiredGlMinor = 5;

struct ColoredVertex {
    std::array<float, 3> position;
    std::array<float, 3> color;
};

// Attribute locations; must match layout(location = ...) in assets/shaders/triangle.vert.
constexpr GLuint kPositionLocation = 0;
constexpr GLuint kColorLocation = 1;
// Single vertex buffer binding point used by the triangle's vertex array.
constexpr GLuint kVertexBufferBinding = 0;

// Bring-up geometry in normalized device coordinates, counter-clockwise.
constexpr std::array<ColoredVertex, 3> kTestTriangle{{
    {{-0.6F, -0.5F, 0.0F}, {0.95F, 0.45F, 0.25F}},
    {{0.6F, -0.5F, 0.0F}, {0.30F, 0.75F, 0.45F}},
    {{0.0F, 0.6F, 0.0F}, {0.30F, 0.55F, 0.90F}},
}};

core::Result<GlShaderProgram> loadProgram(const std::filesystem::path& directory,
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
    return GlShaderProgram::create({vertexName, vertexCode.value()},
                                   {fragmentName, fragmentCode.value()});
}

// Uses direct state access (core in 4.5): objects are set up by name without
// binding them, so setup code cannot disturb unrelated GL state.
GlVertexArray createColoredVertexLayout(const GlBuffer& vertices)
{
    GLuint name = 0;
    glCreateVertexArrays(1, &name);
    GlVertexArray layout{name};

    glVertexArrayVertexBuffer(layout.get(), kVertexBufferBinding, vertices.get(), 0,
                              static_cast<GLsizei>(sizeof(ColoredVertex)));

    glEnableVertexArrayAttrib(layout.get(), kPositionLocation);
    glVertexArrayAttribFormat(layout.get(), kPositionLocation, 3, GL_FLOAT, GL_FALSE,
                              static_cast<GLuint>(offsetof(ColoredVertex, position)));
    glVertexArrayAttribBinding(layout.get(), kPositionLocation, kVertexBufferBinding);

    glEnableVertexArrayAttrib(layout.get(), kColorLocation);
    glVertexArrayAttribFormat(layout.get(), kColorLocation, 3, GL_FLOAT, GL_FALSE,
                              static_cast<GLuint>(offsetof(ColoredVertex, color)));
    glVertexArrayAttribBinding(layout.get(), kColorLocation, kVertexBufferBinding);
    return layout;
}

} // namespace

struct Renderer::Impl {
    Impl(GlShaderProgram program, GlBuffer vertices, GlVertexArray layout, bool debugOutput)
        : triangleProgram(std::move(program)), triangleVertices(std::move(vertices)),
          triangleLayout(std::move(layout)), debugOutputEnabled(debugOutput)
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

    GlShaderProgram triangleProgram;
    GlBuffer triangleVertices;
    GlVertexArray triangleLayout;
    bool debugOutputEnabled = false;
    std::uint32_t errorCount = 0;
    // Size passed to the last beginFrame(); 0 before the first frame.
    int frameWidth = 0;
    int frameHeight = 0;
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

    auto program = loadProgram(desc.shaderDirectory, "triangle.vert", "triangle.frag");
    if (!program) {
        if (debugOutput) {
            opengl::disableDebugOutput();
        }
        return program.error();
    }

    GLuint bufferName = 0;
    glCreateBuffers(1, &bufferName);
    GlBuffer vertices{bufferName};
    // Immutable storage: the triangle never changes after upload.
    glNamedBufferStorage(vertices.get(), static_cast<GLsizeiptr>(sizeof(kTestTriangle)),
                         kTestTriangle.data(), 0);
    GlVertexArray layout = createColoredVertexLayout(vertices);

    glEnable(GL_DEPTH_TEST);

    auto impl = std::make_unique<Impl>(std::move(program).value(), std::move(vertices),
                                       std::move(layout), debugOutput);
    impl->errorCount += opengl::checkGlErrors("renderer setup");
    return Renderer{std::move(impl)};
}

Renderer::Renderer(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

Renderer::~Renderer() = default;
Renderer::Renderer(Renderer&& other) noexcept = default;
Renderer& Renderer::operator=(Renderer&& other) noexcept = default;

void Renderer::beginFrame(int framebufferWidth, int framebufferHeight, const Color& clearColor)
{
    impl_->frameWidth = framebufferWidth;
    impl_->frameHeight = framebufferHeight;
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::drawTestTriangle()
{
    impl_->triangleProgram.use();
    glBindVertexArray(impl_->triangleLayout.get());
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(kTestTriangle.size()));
    glBindVertexArray(0);
}

void Renderer::endFrame()
{
    impl_->errorCount += opengl::checkGlErrors("frame");
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

} // namespace engine::render
