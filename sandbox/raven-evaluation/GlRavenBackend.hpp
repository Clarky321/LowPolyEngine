#pragma once

// An OpenGL 4.5 Core backend for Raven (Rbackend_register + R_BACKEND_CUSTOM),
// written for the Raven UI evaluation (docs/experiments/raven-ui-evaluation.md).
//
// It draws on the OpenGL context that is current on the calling thread, which
// must be the thread that created it: Raven runs with
// RInitDesc::disable_render_thread, so every backend call comes from the UI
// thread. Raven's reference shaders (ui.vert, ui.frag in the Raven checkout)
// are read at Init and adapted from Vulkan GLSL to OpenGL GLSL 4.50.
//
// The backend never presents: the host (the prototype) swaps the buffers.
// Every OpenGL state it touches is saved when a frame starts and restored
// when it ends, and restored around viewport callbacks, so the engine's
// renderer finds its own state.
//
// Raven's backend table has no user data, so the backend's state is one
// object per process; Raven allows one Rinit at a time.

#include <raven/raven_backend.h>

#include <cstdint>
#include <functional>
#include <string>

namespace raven_eval {

struct BackendStats {
    std::uint64_t frames = 0;
    std::uint64_t viewportCallbacks = 0;
    std::uint64_t backdrops = 0;
    std::uint64_t drawCalls = 0;
    // OpenGL errors seen at the end of a frame or a texture call.
    std::uint32_t glErrors = 0;
    // Frames after which the OpenGL state differed from the state before.
    std::uint32_t stateMismatches = 0;
    // Backend calls from another thread than the one that called Init.
    std::uint32_t wrongThreadCalls = 0;
    std::uint32_t liveTextures = 0;
    // Frames Raven produced for other windows than the presented one (the
    // host platform has one OS window; such frames are not drawn).
    std::uint64_t otherWindowFrames = 0;
    std::uint32_t surfaces = 0;
    std::uint32_t initCount = 0;
    std::uint32_t shutdownCount = 0;
    double lastFrameMilliseconds = 0.0;
    std::string lastError;
};

// Folder with Raven's ui.vert and ui.frag; set before Rinit.
void setShaderDirectory(std::string directory);

// The Raven window the host presents in its OS window; frames of other
// windows are skipped. R_INVALID_ID draws every window (not useful).
void setPresentedWindow(Rid window);

// The table for Rbackend_register.
[[nodiscard]] const RBackendVTable& openGlBackend();

[[nodiscard]] const BackendStats& backendStats();

// The parts of the OpenGL state RenderFrame compares: what the backend
// restores (program, bindings, framebuffers, viewport, scissor, blend, depth,
// cull, stencil test, masks, pixel storage, clear colour), and what it only
// reads (element buffer, sRGB, depth function, cull mode, front face,
// polygon mode, stencil functions and operations, blend colour, primitive
// restart, alpha to coverage, uniform buffer bindings, current attributes).
enum class GlStateGroups { Restored, All };

// Runs `work` and tells whether the given part of the state is the same
// afterwards. For checks of other code on the same context: Raven's whole
// frame (Restored: the viewport callback may legitimately change the rest,
// which is the engine's own state), Dear ImGui's renderer (All).
[[nodiscard]] bool sameGlStateAround(const std::function<void()>& work, GlStateGroups groups);

} // namespace raven_eval
