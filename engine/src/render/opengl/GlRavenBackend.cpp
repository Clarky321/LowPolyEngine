// Raven's OpenGL 4.5 backend (ADR 0019, ADR 0027), from the validated prototype
// (experiment/raven-final-validation, sandbox/raven-evaluation/GlRavenBackend.cpp).

#include "GlRavenBackend.hpp"

#include "RavenShaderSources.hpp"

#include "engine/core/Log.hpp"

#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <exception>
#include <functional>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace engine::render::opengl {
namespace {

// Texture units of the UI program: the command's texture, the backdrop and
// its blurred copy (the bindings ui.frag declares, renumbered for OpenGL).
constexpr GLuint kTextureUnit = 0;
constexpr GLuint kBackdropUnit = 1;
constexpr GLuint kBackdropBlurUnit = 2;
// Texture units whose bindings are saved, restored and compared: the three
// the backend uses and the next ones the engine might use.
constexpr int kSavedUnits = 8;
// Generic vertex attributes whose current values are compared (the engine
// sets them for meshes without normals or UVs).
constexpr int kCheckedAttributes = 4;
// Indexed uniform buffer bindings that are compared.
constexpr int kCheckedUniformBuffers = 4;

// The OpenGL state the backend changes: saved before its work and restored
// after it.
struct RestoredState {
    GLint program = 0;
    GLint vertexArray = 0;
    GLint arrayBuffer = 0;
    GLint activeTexture = GL_TEXTURE0;
    std::array<GLint, kSavedUnits> textures{};
    std::array<GLint, kSavedUnits> samplers{};
    GLint drawFramebuffer = 0;
    GLint readFramebuffer = 0;
    std::array<GLint, 4> viewport{};
    std::array<GLint, 4> scissorBox{};
    GLboolean scissorTest = GL_FALSE;
    GLboolean blend = GL_FALSE;
    GLboolean depthTest = GL_FALSE;
    GLboolean depthMask = GL_TRUE;
    GLboolean cullFace = GL_FALSE;
    GLboolean stencilTest = GL_FALSE;
    std::array<GLboolean, 4> colorMask{};
    GLint blendSrcRgb = 0;
    GLint blendDstRgb = 0;
    GLint blendSrcAlpha = 0;
    GLint blendDstAlpha = 0;
    GLint blendEquationRgb = 0;
    GLint blendEquationAlpha = 0;
    GLint unpackAlignment = 4;
    GLint unpackRowLength = 0;
    GLint unpackBuffer = 0;
    GLint packAlignment = 4;
    GLint packBuffer = 0;
    // RenderFrame clears with Raven's colour.
    std::array<GLfloat, 4> clearColor{};

    friend bool operator==(const RestoredState&, const RestoredState&) = default;
};

// State the backend never changes. It is only read, so that a change there
// is counted as a state mismatch too.
struct CheckedState {
    GLint elementArrayBuffer = 0;
    GLboolean framebufferSrgb = GL_FALSE;
    GLint depthFunc = 0;
    GLint cullFaceMode = 0;
    GLint frontFace = 0;
    std::array<GLint, 2> polygonMode{};
    GLint stencilFunc = 0;
    GLint stencilRef = 0;
    GLint stencilValueMask = 0;
    GLint stencilWriteMask = 0;
    GLint stencilFail = 0;
    GLint stencilPassDepthFail = 0;
    GLint stencilPassDepthPass = 0;
    std::array<GLfloat, 4> blendColor{};
    GLboolean primitiveRestart = GL_FALSE;
    GLboolean sampleAlphaToCoverage = GL_FALSE;
    GLint uniformBuffer = 0;
    std::array<GLint, kCheckedUniformBuffers> uniformBuffers{};
    std::array<std::array<GLfloat, 4>, kCheckedAttributes> attributes{};

    friend bool operator==(const CheckedState&, const CheckedState&) = default;
};

struct GlState {
    RestoredState restored;
    CheckedState checked;

    friend bool operator==(const GlState&, const GlState&) = default;
};

GlState captureState()
{
    GlState result;
    RestoredState& state = result.restored;
    CheckedState& checked = result.checked;
    glGetIntegerv(GL_CURRENT_PROGRAM, &state.program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &state.vertexArray);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &state.arrayBuffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &state.activeTexture);
    for (int unit = 0; unit < kSavedUnits; ++unit) {
        glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.textures[static_cast<std::size_t>(unit)]);
        glGetIntegerv(GL_SAMPLER_BINDING, &state.samplers[static_cast<std::size_t>(unit)]);
    }
    glActiveTexture(static_cast<GLenum>(state.activeTexture));
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &state.drawFramebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &state.readFramebuffer);
    glGetIntegerv(GL_VIEWPORT, state.viewport.data());
    glGetIntegerv(GL_SCISSOR_BOX, state.scissorBox.data());
    state.scissorTest = glIsEnabled(GL_SCISSOR_TEST);
    state.blend = glIsEnabled(GL_BLEND);
    state.depthTest = glIsEnabled(GL_DEPTH_TEST);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &state.depthMask);
    state.cullFace = glIsEnabled(GL_CULL_FACE);
    state.stencilTest = glIsEnabled(GL_STENCIL_TEST);
    glGetBooleanv(GL_COLOR_WRITEMASK, state.colorMask.data());
    glGetIntegerv(GL_BLEND_SRC_RGB, &state.blendSrcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &state.blendDstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &state.blendSrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &state.blendDstAlpha);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &state.blendEquationRgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &state.blendEquationAlpha);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &state.unpackAlignment);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &state.unpackRowLength);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &state.unpackBuffer);
    glGetIntegerv(GL_PACK_ALIGNMENT, &state.packAlignment);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &state.packBuffer);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, state.clearColor.data());
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &checked.elementArrayBuffer);
    checked.framebufferSrgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);
    glGetIntegerv(GL_DEPTH_FUNC, &checked.depthFunc);
    glGetIntegerv(GL_CULL_FACE_MODE, &checked.cullFaceMode);
    glGetIntegerv(GL_FRONT_FACE, &checked.frontFace);
    glGetIntegerv(GL_POLYGON_MODE, checked.polygonMode.data());
    glGetIntegerv(GL_STENCIL_FUNC, &checked.stencilFunc);
    glGetIntegerv(GL_STENCIL_REF, &checked.stencilRef);
    glGetIntegerv(GL_STENCIL_VALUE_MASK, &checked.stencilValueMask);
    glGetIntegerv(GL_STENCIL_WRITEMASK, &checked.stencilWriteMask);
    glGetIntegerv(GL_STENCIL_FAIL, &checked.stencilFail);
    glGetIntegerv(GL_STENCIL_PASS_DEPTH_FAIL, &checked.stencilPassDepthFail);
    glGetIntegerv(GL_STENCIL_PASS_DEPTH_PASS, &checked.stencilPassDepthPass);
    glGetFloatv(GL_BLEND_COLOR, checked.blendColor.data());
    checked.primitiveRestart = glIsEnabled(GL_PRIMITIVE_RESTART);
    checked.sampleAlphaToCoverage = glIsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glGetIntegerv(GL_UNIFORM_BUFFER_BINDING, &checked.uniformBuffer);
    for (int index = 0; index < kCheckedUniformBuffers; ++index) {
        glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, static_cast<GLuint>(index),
                        &checked.uniformBuffers[static_cast<std::size_t>(index)]);
    }
    for (int index = 0; index < kCheckedAttributes; ++index) {
        glGetVertexAttribfv(static_cast<GLuint>(index), GL_CURRENT_VERTEX_ATTRIB,
                            checked.attributes[static_cast<std::size_t>(index)].data());
    }
    return result;
}

void enableIf(GLenum capability, GLboolean enabled)
{
    if (enabled == GL_TRUE) {
        glEnable(capability);
    } else {
        glDisable(capability);
    }
}

void restoreState(const GlState& saved)
{
    const RestoredState& state = saved.restored;
    glUseProgram(static_cast<GLuint>(state.program));
    glBindVertexArray(static_cast<GLuint>(state.vertexArray));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(state.arrayBuffer));
    for (int unit = 0; unit < kSavedUnits; ++unit) {
        glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
        glBindTexture(GL_TEXTURE_2D,
                      static_cast<GLuint>(state.textures[static_cast<std::size_t>(unit)]));
        glBindSampler(static_cast<GLuint>(unit),
                      static_cast<GLuint>(state.samplers[static_cast<std::size_t>(unit)]));
    }
    glActiveTexture(static_cast<GLenum>(state.activeTexture));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(state.drawFramebuffer));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(state.readFramebuffer));
    glViewport(state.viewport[0], state.viewport[1], state.viewport[2], state.viewport[3]);
    glScissor(state.scissorBox[0], state.scissorBox[1], state.scissorBox[2], state.scissorBox[3]);
    enableIf(GL_SCISSOR_TEST, state.scissorTest);
    enableIf(GL_BLEND, state.blend);
    enableIf(GL_DEPTH_TEST, state.depthTest);
    glDepthMask(state.depthMask);
    enableIf(GL_CULL_FACE, state.cullFace);
    enableIf(GL_STENCIL_TEST, state.stencilTest);
    glColorMask(state.colorMask[0], state.colorMask[1], state.colorMask[2], state.colorMask[3]);
    glBlendFuncSeparate(
        static_cast<GLenum>(state.blendSrcRgb), static_cast<GLenum>(state.blendDstRgb),
        static_cast<GLenum>(state.blendSrcAlpha), static_cast<GLenum>(state.blendDstAlpha));
    glBlendEquationSeparate(static_cast<GLenum>(state.blendEquationRgb),
                            static_cast<GLenum>(state.blendEquationAlpha));
    glPixelStorei(GL_UNPACK_ALIGNMENT, state.unpackAlignment);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, state.unpackRowLength);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(state.unpackBuffer));
    glPixelStorei(GL_PACK_ALIGNMENT, state.packAlignment);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(state.packBuffer));
    glClearColor(state.clearColor[0], state.clearColor[1], state.clearColor[2],
                 state.clearColor[3]);
    // The checked group is not written: the backend does not change it, and
    // binding an element buffer here would change the bound vertex array.
}

// Unpack state for texture uploads (DSA calls do not touch bindings, but
// pixel storage is global state), restored afterwards.
class UnpackScope {
public:
    UnpackScope()
    {
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment_);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rowLength_);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &buffer_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    }
    ~UnpackScope()
    {
        glPixelStorei(GL_UNPACK_ALIGNMENT, alignment_);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, rowLength_);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(buffer_));
    }
    UnpackScope(const UnpackScope&) = delete;
    UnpackScope& operator=(const UnpackScope&) = delete;

private:
    GLint alignment_ = 4;
    GLint rowLength_ = 0;
    GLint buffer_ = 0;
};

struct Texture {
    GLuint name = 0;
    RTextureFormat format = R_TEXTURE_RGBA8;
    RFilterType filter = R_FILTER_LINEAR;
    GLsizei levels = 1;
};

struct Backend {
    Rid presentedWindow = R_INVALID_ID;
    std::thread::id thread;
    bool initialized = false;
    GLuint program = 0;
    GLint scaleLocation = -1;
    GLint translateLocation = -1;
    GLint pointerLocation = -1;
    GLuint vertexArray = 0;
    GLuint instanceBuffer = 0;
    GLuint white = 0;
    GLuint blurProgram = 0;
    GLint blurStepLocation = -1;
    GLuint blurVertexArray = 0;
    GLuint blurFramebuffer = 0;
    // Backdrop of glass: the frame, and two blur passes over it.
    std::array<GLuint, 3> backdrop{};
    GLsizei backdropWidth = 0;
    GLsizei backdropHeight = 0;
    std::vector<Texture> textures;
    RavenBackendStats stats;
};

Backend& backend()
{
    static Backend instance;
    return instance;
}

void checkThread()
{
    if (backend().initialized && std::this_thread::get_id() != backend().thread) {
        ++backend().stats.wrongThreadCalls;
    }
}

void countGlErrors(std::string_view where)
{
    for (GLenum error = glGetError(); error != GL_NO_ERROR; error = glGetError()) {
        ++backend().stats.glErrors;
        backend().stats.lastError = std::string(where) + ": OpenGL error " + std::to_string(error);
        core::logError("raven", "{}: OpenGL error 0x{:04X}", where, error);
    }
}

bool replaceOnce(std::string& text, std::string_view from, std::string_view to)
{
    const std::size_t at = text.find(from);
    if (at == std::string::npos) {
        return false;
    }
    text.replace(at, from.size(), to);
    return true;
}

std::size_t replaceAll(std::string& text, std::string_view from, std::string_view to)
{
    std::size_t count = 0;
    for (std::size_t at = text.find(from); at != std::string::npos;
         at = text.find(from, at + to.size())) {
        text.replace(at, from.size(), to);
        ++count;
    }
    return count;
}

// Raven's reference shaders are Vulkan GLSL. The changes for OpenGL 4.50:
// push constants become a plain uniform struct, gl_VertexIndex is
// gl_VertexID, the Y axis of clip space points up (so the scale's sign is
// dropped where the shader needs a size), gl_FragCoord is redeclared with its
// origin at the top left like Vulkan's, descriptor sets become texture units,
// and the backdrop (a copy of the bottom-up framebuffer) is read with V
// flipped. A pattern that is not found means the shaders changed: Init fails.
std::string adaptVertexShader(std::string source, std::string& problem)
{
    const bool ok =
        replaceOnce(source, "#version 450", "#version 450 core") &&
        replaceOnce(source, "layout(push_constant) uniform PushConstants {",
                    "struct PushConstants {") &&
        replaceOnce(source, "} pc;", "};\nuniform PushConstants pc;") &&
        replaceAll(source, "gl_VertexIndex", "gl_VertexID") > 0 &&
        replaceOnce(source, "v_inv_size = pc.scale * 0.5;", "v_inv_size = abs(pc.scale) * 0.5;");
    if (!ok) {
        problem = "ui.vert does not have the expected Vulkan constructs";
    }
    return source;
}

std::string adaptFragmentShader(std::string source, std::string& problem)
{
    bool ok = replaceOnce(source, "#version 450",
                          "#version 450 core\nlayout(origin_upper_left) in vec4 gl_FragCoord;") &&
              replaceOnce(source, "layout(set = 0, binding = 0) uniform sampler2D u_texture;",
                          "layout(binding = 0) uniform sampler2D u_texture;") &&
              replaceOnce(source, "layout(set = 1, binding = 0) uniform sampler2D u_backdrop;",
                          "layout(binding = 1) uniform sampler2D u_backdrop;") &&
              replaceOnce(source, "layout(set = 1, binding = 1) uniform sampler2D u_backdrop_blur;",
                          "layout(binding = 2) uniform sampler2D u_backdrop_blur;");
    // Every textureLod of the reference shader reads a backdrop.
    ok = ok && replaceAll(source, "textureLod(", "backdropSample(") > 0;
    ok = ok && replaceOnce(source, "/* Rounded box",
                           "vec4 backdropSample(sampler2D image, vec2 uv, float lod) {\n"
                           "\treturn textureLod(image, vec2(uv.x, 1.0 - uv.y), lod);\n"
                           "}\n\n/* Rounded box");
    if (!ok) {
        problem = "ui.frag does not have the expected Vulkan constructs";
    }
    return source;
}

// A full-screen triangle that blurs a texture along one axis (a 9-tap
// Gaussian with linear sampling): the backdrop of frosted glass.
constexpr const char* kBlurVertexShader = R"(#version 450 core
out vec2 uv;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

constexpr const char* kBlurFragmentShader = R"(#version 450 core
in vec2 uv;
layout(binding = 0) uniform sampler2D image;
uniform vec2 stepSize;
out vec4 color;
void main() {
    color = texture(image, uv) * 0.227027;
    color += (texture(image, uv + stepSize * 1.384615) + texture(image, uv - stepSize * 1.384615)) * 0.316216;
    color += (texture(image, uv + stepSize * 3.230769) + texture(image, uv - stepSize * 3.230769)) * 0.070270;
}
)";

GLuint compile(GLenum type, const std::string& source, std::string_view name, std::string& problem)
{
    const GLuint shader = glCreateShader(type);
    const char* text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_TRUE) {
        return shader;
    }
    std::array<char, 4096> log{};
    glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
    problem = std::string(name) + ": " + log.data();
    glDeleteShader(shader);
    return 0;
}

GLuint link(const std::string& vertex, const std::string& fragment, std::string_view name,
            std::string& problem)
{
    const GLuint vs = compile(GL_VERTEX_SHADER, vertex, std::string(name) + " (vertex)", problem);
    const GLuint fs =
        compile(GL_FRAGMENT_SHADER, fragment, std::string(name) + " (fragment)", problem);
    if (vs == 0 || fs == 0) {
        glDeleteShader(vs);
        glDeleteShader(fs);
        return 0;
    }
    const GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok == GL_TRUE) {
        return program;
    }
    std::array<char, 4096> log{};
    glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
    problem = std::string(name) + " (link): " + log.data();
    glDeleteProgram(program);
    return 0;
}

void releaseObjects()
{
    Backend& b = backend();
    for (Texture& texture : b.textures) {
        if (texture.name != 0) {
            glDeleteTextures(1, &texture.name);
        }
    }
    b.textures.clear();
    b.stats.liveTextures = 0;
    glDeleteTextures(static_cast<GLsizei>(b.backdrop.size()), b.backdrop.data());
    b.backdrop = {};
    b.backdropWidth = 0;
    b.backdropHeight = 0;
    glDeleteFramebuffers(1, &b.blurFramebuffer);
    glDeleteVertexArrays(1, &b.blurVertexArray);
    glDeleteProgram(b.blurProgram);
    glDeleteTextures(1, &b.white);
    glDeleteBuffers(1, &b.instanceBuffer);
    glDeleteVertexArrays(1, &b.vertexArray);
    glDeleteProgram(b.program);
    b.blurFramebuffer = 0;
    b.blurVertexArray = 0;
    b.blurProgram = 0;
    b.white = 0;
    b.instanceBuffer = 0;
    b.vertexArray = 0;
    b.program = 0;
}

int init(const RBackendInitInfo* /*info*/)
{
    try {
        Backend& b = backend();
        // A new Raven session: its statistics start from zero.
        b.stats = RavenBackendStats{};
        b.thread = std::this_thread::get_id();
        ++b.stats.initCount;
        std::string problem;
        // Raven's reference shaders, embedded from third_party/raven at build time.
        const std::string vertex = adaptVertexShader(std::string(raven_shaders::uiVert()), problem);
        const std::string fragment =
            adaptFragmentShader(std::string(raven_shaders::uiFrag()), problem);
        if (problem.empty()) {
            b.program = link(vertex, fragment, "Raven ui", problem);
        }
        if (b.program != 0) {
            b.blurProgram = link(kBlurVertexShader, kBlurFragmentShader, "backdrop blur", problem);
        }
        if (b.program == 0 || b.blurProgram == 0) {
            b.stats.lastError = problem;
            core::logError("raven", "{}", problem);
            releaseObjects();
            return R_FALSE;
        }
        b.scaleLocation = glGetUniformLocation(b.program, "pc.scale");
        b.translateLocation = glGetUniformLocation(b.program, "pc.translate");
        b.pointerLocation = glGetUniformLocation(b.program, "pc.pointer");
        b.blurStepLocation = glGetUniformLocation(b.blurProgram, "stepSize");

        // One RDrawInstance per instance, the four corners from gl_VertexID.
        glCreateBuffers(1, &b.instanceBuffer);
        glCreateVertexArrays(1, &b.vertexArray);
        glVertexArrayVertexBuffer(b.vertexArray, 0, b.instanceBuffer, 0,
                                  static_cast<GLsizei>(sizeof(RDrawInstance)));
        glVertexArrayBindingDivisor(b.vertexArray, 0, 1);
        struct Attribute {
            GLuint location;
            GLint size;
            GLenum type;
            bool normalized;
            bool integer;
            std::size_t offset;
        };
        const std::array<Attribute, 8> attributes{{
            {0, 4, GL_FLOAT, false, false, offsetof(RDrawInstance, rect)},
            {1, 4, GL_FLOAT, false, false, offsetof(RDrawInstance, uv)},
            {2, 4, GL_FLOAT, false, false, offsetof(RDrawInstance, clip)},
            {3, 4, GL_FLOAT, false, false, offsetof(RDrawInstance, radii)},
            {4, 4, GL_UNSIGNED_BYTE, true, false, offsetof(RDrawInstance, color)},
            {5, 4, GL_UNSIGNED_BYTE, true, false, offsetof(RDrawInstance, border_color)},
            {6, 1, GL_FLOAT, false, false, offsetof(RDrawInstance, border_width)},
            {7, 1, GL_UNSIGNED_INT, false, true, offsetof(RDrawInstance, flags)},
        }};
        for (const Attribute& a : attributes) {
            glEnableVertexArrayAttrib(b.vertexArray, a.location);
            if (a.integer) {
                glVertexArrayAttribIFormat(b.vertexArray, a.location, a.size, a.type,
                                           static_cast<GLuint>(a.offset));
            } else {
                glVertexArrayAttribFormat(b.vertexArray, a.location, a.size, a.type,
                                          a.normalized ? GL_TRUE : GL_FALSE,
                                          static_cast<GLuint>(a.offset));
            }
            glVertexArrayAttribBinding(b.vertexArray, a.location, 0);
        }
        glCreateVertexArrays(1, &b.blurVertexArray);
        glCreateFramebuffers(1, &b.blurFramebuffer);

        const std::uint32_t white = 0xFFFFFFFFU;
        glCreateTextures(GL_TEXTURE_2D, 1, &b.white);
        glTextureStorage2D(b.white, 1, GL_RGBA8, 1, 1);
        {
            const UnpackScope unpack;
            glTextureSubImage2D(b.white, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &white);
        }
        glTextureParameteri(b.white, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(b.white, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        b.initialized = true;
        countGlErrors("Init");
        return R_TRUE;
    } catch (const std::exception& error) {
        backend().stats.lastError = error.what();
        return R_FALSE;
    } catch (...) {
        return R_FALSE;
    }
}

void shutdown()
{
    checkThread();
    releaseObjects();
    countGlErrors("Shutdown");
    backend().initialized = false;
    ++backend().stats.shutdownCount;
}

// The host owns the window and presents; a surface needs no OpenGL object.
int createSurface(Rid /*window*/, const RNativeWindow* /*native*/, Ruint /*width*/,
                  Ruint /*height*/)
{
    checkThread();
    ++backend().stats.surfaces;
    return R_TRUE;
}

void destroySurface(Rid /*window*/)
{
    checkThread();
    --backend().stats.surfaces;
}

Texture* findTexture(Rid id)
{
    const std::size_t index = R_ID_INDEX(id);
    std::vector<Texture>& textures = backend().textures;
    if (id == R_INVALID_ID || index >= textures.size() || textures[index].name == 0) {
        return nullptr;
    }
    return &textures[index];
}

void applyFilter(Texture& texture, RFilterType filter)
{
    texture.filter = filter;
    const bool mipmaps = filter != R_FILTER_LINEAR && filter != R_FILTER_NEAREST;
    const GLint mag = filter == R_FILTER_NEAREST ? GL_NEAREST : GL_LINEAR;
    const GLint min = mipmaps ? GL_LINEAR_MIPMAP_LINEAR : mag;
    glTextureParameteri(texture.name, GL_TEXTURE_MIN_FILTER, min);
    glTextureParameteri(texture.name, GL_TEXTURE_MAG_FILTER, mag);
    glTextureParameteri(texture.name, GL_TEXTURE_MAX_LEVEL, mipmaps ? texture.levels - 1 : 0);
    if (mipmaps && texture.levels > 1) {
        glGenerateTextureMipmap(texture.name);
    }
}

int createTexture(Rid id, const RTextureDesc* desc, const void* pixels)
{
    checkThread();
    try {
        if (id == R_INVALID_ID || desc == nullptr || desc->width == 0 || desc->height == 0) {
            return R_FALSE;
        }
        Backend& b = backend();
        const std::size_t index = R_ID_INDEX(id);
        if (index >= b.textures.size()) {
            b.textures.resize(index + 64);
        }
        Texture& texture = b.textures[index];
        if (texture.name != 0) {
            glDeleteTextures(1, &texture.name);
            --b.stats.liveTextures;
        }
        texture = Texture{};
        texture.format = desc->format;
        // Storage for a full mip chain, so the filter may switch to mipmaps later.
        GLsizei levels = 1;
        for (Ruint size = std::max(desc->width, desc->height); size > 1; size /= 2) {
            ++levels;
        }
        texture.levels = levels;
        const bool alpha = desc->format == R_TEXTURE_ALPHA8;
        glCreateTextures(GL_TEXTURE_2D, 1, &texture.name);
        glTextureStorage2D(texture.name, levels, alpha ? GL_R8 : GL_RGBA8,
                           static_cast<GLsizei>(desc->width), static_cast<GLsizei>(desc->height));
        if (pixels != nullptr) {
            const UnpackScope unpack;
            glTextureSubImage2D(texture.name, 0, 0, 0, static_cast<GLsizei>(desc->width),
                                static_cast<GLsizei>(desc->height), alpha ? GL_RED : GL_RGBA,
                                GL_UNSIGNED_BYTE, pixels);
        }
        if (alpha) {
            // Glyph masks are sampled as (1, 1, 1, value), as raven_backend.h asks.
            const std::array<GLint, 4> swizzle{GL_ONE, GL_ONE, GL_ONE, GL_RED};
            glTextureParameteriv(texture.name, GL_TEXTURE_SWIZZLE_RGBA, swizzle.data());
        }
        glTextureParameteri(texture.name, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(texture.name, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        applyFilter(texture, desc->filter);
        ++b.stats.liveTextures;
        countGlErrors("CreateTexture");
        return R_TRUE;
    } catch (...) {
        return R_FALSE;
    }
}

void updateTexture(Rid id, Ruint x, Ruint y, Ruint width, Ruint height, const void* pixels)
{
    checkThread();
    Texture* texture = findTexture(id);
    if (texture == nullptr || pixels == nullptr || width == 0 || height == 0) {
        return;
    }
    {
        const UnpackScope unpack;
        glTextureSubImage2D(texture->name, 0, static_cast<GLint>(x), static_cast<GLint>(y),
                            static_cast<GLsizei>(width), static_cast<GLsizei>(height),
                            texture->format == R_TEXTURE_ALPHA8 ? GL_RED : GL_RGBA,
                            GL_UNSIGNED_BYTE, pixels);
    }
    if (texture->filter != R_FILTER_LINEAR && texture->filter != R_FILTER_NEAREST &&
        texture->levels > 1) {
        glGenerateTextureMipmap(texture->name);
    }
    countGlErrors("UpdateTexture");
}

void setTextureFilter(Rid id, RFilterType filter)
{
    checkThread();
    if (Texture* texture = findTexture(id)) {
        applyFilter(*texture, filter);
    }
}

void destroyTexture(Rid id)
{
    checkThread();
    if (Texture* texture = findTexture(id)) {
        glDeleteTextures(1, &texture->name);
        *texture = Texture{};
        --backend().stats.liveTextures;
    }
}

void applyUiState(const RDrawData& data)
{
    const Backend& b = backend();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, static_cast<GLsizei>(data.framebuffer_width),
               static_cast<GLsizei>(data.framebuffer_height));
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    // ui.frag writes premultiplied colors.
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUseProgram(b.program);
    // Pixels (origin top left) to clip space (Y up).
    glUniform2f(b.scaleLocation, 2.0F / static_cast<float>(data.framebuffer_width),
                -2.0F / static_cast<float>(data.framebuffer_height));
    glUniform2f(b.translateLocation, -1.0F, 1.0F);
    glUniform2f(b.pointerLocation, data.pointer[0], data.pointer[1]);
    for (GLuint unit = 0; unit < static_cast<GLuint>(kSavedUnits); ++unit) {
        glBindSampler(unit, 0);
    }
    glActiveTexture(GL_TEXTURE0 + kBackdropUnit);
    glBindTexture(GL_TEXTURE_2D, b.backdrop[0] != 0 ? b.backdrop[0] : b.white);
    glActiveTexture(GL_TEXTURE0 + kBackdropBlurUnit);
    glBindTexture(GL_TEXTURE_2D, b.backdrop[2] != 0 ? b.backdrop[2] : b.white);
    glActiveTexture(GL_TEXTURE0 + kTextureUnit);
    glBindVertexArray(b.vertexArray);
}

void captureBackdrop(const RDrawData& data)
{
    Backend& b = backend();
    const auto width = static_cast<GLsizei>(data.framebuffer_width);
    const auto height = static_cast<GLsizei>(data.framebuffer_height);
    if (b.backdropWidth != width || b.backdropHeight != height) {
        glDeleteTextures(static_cast<GLsizei>(b.backdrop.size()), b.backdrop.data());
        glCreateTextures(GL_TEXTURE_2D, static_cast<GLsizei>(b.backdrop.size()), b.backdrop.data());
        for (GLuint texture : b.backdrop) {
            glTextureStorage2D(texture, 1, GL_RGBA8, width, height);
            glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTextureParameteri(texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTextureParameteri(texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        b.backdropWidth = width;
        b.backdropHeight = height;
    }
    // The whole frame (raven_backend.h allows it), then two blur passes.
    glCopyTextureSubImage2D(b.backdrop[0], 0, 0, 0, 0, 0, width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, b.blurFramebuffer);
    glDisable(GL_BLEND);
    glUseProgram(b.blurProgram);
    glBindVertexArray(b.blurVertexArray);
    for (std::size_t pass = 0; pass < 2; ++pass) {
        glNamedFramebufferTexture(b.blurFramebuffer, GL_COLOR_ATTACHMENT0, b.backdrop[pass + 1], 0);
        glBindTextureUnit(0, b.backdrop[pass]);
        glUniform2f(b.blurStepLocation, pass == 0 ? 4.0F / static_cast<float>(width) : 0.0F,
                    pass == 0 ? 0.0F : 4.0F / static_cast<float>(height));
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    ++b.stats.backdrops;
    applyUiState(data);
}

// The checked state the application left after its viewport callback: its
// own changes, which the backend keeps (it never writes that state). The
// element buffer belongs to the vertex array, which the backend restores.
void keepApplicationState(const GlState& after, GlState& expected)
{
    const GLint elementArrayBuffer = expected.checked.elementArrayBuffer;
    expected.checked = after.checked;
    expected.checked.elementArrayBuffer = elementArrayBuffer;
}

void callViewport(const RDrawData& data, const RDrawCommand& command, const GlState& host,
                  GlState& expected)
{
    if (command.callback == nullptr || command.rect[2] <= 0 || command.rect[3] <= 0) {
        return;
    }
    const int framebufferWidth = static_cast<int>(data.framebuffer_width);
    const int framebufferHeight = static_cast<int>(data.framebuffer_height);
    RViewportDrawInfo info{};
    info.backend = R_BACKEND_CUSTOM;
    info.viewport = command.viewport;
    info.window = data.window;
    info.x = command.rect[0];
    info.y = command.rect[1];
    info.width = command.rect[2];
    info.height = command.rect[3];
    info.clip_x = std::max(command.clip[0], 0);
    info.clip_y = std::max(command.clip[1], 0);
    info.clip_width = std::min(command.clip[0] + command.clip[2], framebufferWidth) - info.clip_x;
    info.clip_height = std::min(command.clip[1] + command.clip[3], framebufferHeight) - info.clip_y;
    if (info.clip_width <= 0 || info.clip_height <= 0) {
        return;
    }
    info.framebuffer_width = framebufferWidth;
    info.framebuffer_height = framebufferHeight;
    info.frame_index = data.frame_index;
    // The application draws with its own state, as it was before the frame,
    // inside the widget: viewport and scissor in OpenGL's bottom-up pixels.
    restoreState(host);
    glViewport(info.x, framebufferHeight - info.y - info.height, info.width, info.height);
    glEnable(GL_SCISSOR_TEST);
    glScissor(info.clip_x, framebufferHeight - info.clip_y - info.clip_height, info.clip_width,
              info.clip_height);
    ++backend().stats.viewportCallbacks;
    command.callback(&info, command.user_data);
    keepApplicationState(captureState(), expected);
    applyUiState(data);
}

void readCapture(const RDrawData& data, RCapture& capture)
{
    const std::size_t row = static_cast<std::size_t>(data.framebuffer_width) * 4;
    const std::size_t rows = data.framebuffer_height;
    capture.width = static_cast<int>(data.framebuffer_width);
    capture.height = static_cast<int>(data.framebuffer_height);
    capture.pixels = Rpixels_alloc(row * rows);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glReadPixels(0, 0, capture.width, capture.height, GL_RGBA, GL_UNSIGNED_BYTE, capture.pixels);
    std::vector<std::uint8_t> temp(row);
    for (std::size_t y = 0; y < rows / 2; ++y) {
        std::uint8_t* top = capture.pixels + y * row;
        std::uint8_t* bottom = capture.pixels + (rows - 1 - y) * row;
        std::memcpy(temp.data(), top, row);
        std::memcpy(top, bottom, row);
        std::memcpy(bottom, temp.data(), row);
    }
}

void renderFrame(const RDrawData* data, RCapture* capture)
{
    checkThread();
    if (data == nullptr || data->framebuffer_width == 0 || data->framebuffer_height == 0 ||
        !backend().initialized) {
        return;
    }
    if (backend().presentedWindow != R_INVALID_ID && data->window != backend().presentedWindow) {
        ++backend().stats.otherWindowFrames;
        return;
    }
    try {
        const auto started = std::chrono::steady_clock::now();
        Backend& b = backend();
        const GlState host = captureState();
        GlState expected = host;
        applyUiState(*data);
        glClearColor(data->clear_color[0], data->clear_color[1], data->clear_color[2],
                     data->clear_color[3]);
        glClear(GL_COLOR_BUFFER_BIT);
        glNamedBufferData(b.instanceBuffer,
                          static_cast<GLsizeiptr>(data->instance_count * sizeof(RDrawInstance)),
                          data->instances, GL_STREAM_DRAW);
        for (Ruint i = 0; i < data->command_count; ++i) {
            const RDrawCommand& command = data->commands[i];
            if (command.type == R_DRAW_CALLBACK) {
                callViewport(*data, command, host, expected);
                continue;
            }
            if (command.type == R_DRAW_BACKDROP) {
                captureBackdrop(*data);
                continue;
            }
            // R_DRAW_INSTANCES, R_DRAW_ERASE, and R_DRAW_SHADER without custom
            // shaders (CreateShader is not provided): plain UI instances.
            if (command.first_instance > data->instance_count ||
                command.instance_count > data->instance_count - command.first_instance ||
                command.instance_count == 0) {
                continue;
            }
            const Texture* texture = findTexture(command.texture);
            glBindTexture(GL_TEXTURE_2D, texture != nullptr ? texture->name : b.white);
            if (command.type == R_DRAW_ERASE) {
                glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
            } else {
                glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            }
            glDrawArraysInstancedBaseInstance(GL_TRIANGLE_STRIP, 0, 4,
                                              static_cast<GLsizei>(command.instance_count),
                                              command.first_instance);
            ++b.stats.drawCalls;
        }
        if (capture != nullptr) {
            readCapture(*data, *capture);
        }
        countGlErrors("RenderFrame");
        restoreState(host);
        if (!(captureState() == expected)) {
            ++b.stats.stateMismatches;
        }
        ++b.stats.frames;
        b.stats.lastFrameMilliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
                .count();
    } catch (const std::exception& error) {
        backend().stats.lastError = error.what();
    } catch (...) {
        backend().stats.lastError = "unknown exception in RenderFrame";
    }
}

void waitIdle()
{
    checkThread();
    glFinish();
}

} // namespace

void setRavenPresentedWindow(Rid window) noexcept
{
    backend().presentedWindow = window;
}

const RBackendVTable& ravenBackend() noexcept
{
    static const RBackendVTable table = [] {
        RBackendVTable t{};
        t.name = "OpenGL 4.5 Core (LowPolyEngine)";
        t.Init = init;
        t.Shutdown = shutdown;
        t.CreateSurface = createSurface;
        t.DestroySurface = destroySurface;
        t.CreateTexture = createTexture;
        t.UpdateTexture = updateTexture;
        t.SetTextureFilter = setTextureFilter;
        t.DestroyTexture = destroyTexture;
        t.RenderFrame = renderFrame;
        t.WaitIdle = waitIdle;
        // No custom shader effects: OpenGL 4.5 Core cannot load SPIR-V.
        t.CreateShader = nullptr;
        t.DestroyShader = nullptr;
        return t;
    }();
    return table;
}

const RavenBackendStats& ravenBackendStats() noexcept
{
    return backend().stats;
}

bool sameGlStateAround(const std::function<void()>& work, GlStateGroups groups)
{
    const GlState before = captureState();
    work();
    const GlState after = captureState();
    return groups == GlStateGroups::All ? after == before : after.restored == before.restored;
}

} // namespace engine::render::opengl
