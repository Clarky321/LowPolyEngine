#pragma once

// Internal to the engine: Raven's OpenGL 4.5 Core backend (Rbackend_register
// with R_BACKEND_CUSTOM; ADR 0019, ADR 0027). Only engine/src/ui/ uses it;
// this header names no OpenGL type.
//
// Taken over from the validated prototype (experiment/raven-final-validation,
// sandbox/raven-evaluation/GlRavenBackend.cpp) with the final validation's
// fixes: the clear colour is restored, and the comparison covers state the
// backend never writes too.
//
// The backend draws on the OpenGL context that is current on the calling
// thread, the thread that created the renderer: Raven runs with
// RInitDesc::disable_render_thread, so every call comes from the main thread.
// Raven's reference shaders (third_party/raven/src/render/vulkan/shaders/
// ui.vert and ui.frag, embedded at build time) are adapted from Vulkan GLSL to
// OpenGL GLSL 4.50 at Init; a shader Raven changed fails Init.
//
// It never presents (the engine swaps the buffers). Every OpenGL state it
// touches is saved when a frame starts and restored when it ends, and
// restored around viewport callbacks. Raven's backend table has no user data,
// so the backend's state is one object per process; Raven allows one Rinit at
// a time.

#include <raven/raven_backend.h>

#include <cstdint>
#include <functional>
#include <string>

namespace engine::render::opengl {

struct RavenBackendStats {
    std::uint64_t frames = 0;
    std::uint64_t viewportCallbacks = 0;
    std::uint64_t backdrops = 0;
    std::uint64_t drawCalls = 0;
    // OpenGL errors seen at the end of a frame or of a texture call.
    std::uint32_t glErrors = 0;
    // Frames after which the OpenGL state differed from the state before.
    std::uint32_t stateMismatches = 0;
    // Backend calls from another thread than the one that called Init.
    std::uint32_t wrongThreadCalls = 0;
    std::uint32_t liveTextures = 0;
    // Frames Raven produced for other windows than the presented one: the
    // engine's window shows one Raven window, so such frames are not drawn.
    std::uint64_t otherWindowFrames = 0;
    std::uint32_t surfaces = 0;
    std::uint32_t initCount = 0;
    std::uint32_t shutdownCount = 0;
    double lastFrameMilliseconds = 0.0;
    std::string lastError;
};

// The Raven window shown in the engine's window; frames of other Raven
// windows are skipped. R_INVALID_ID before the window exists.
void setRavenPresentedWindow(Rid window) noexcept;

// The table for Rbackend_register. Init resets the statistics.
[[nodiscard]] const RBackendVTable& ravenBackend() noexcept;

[[nodiscard]] const RavenBackendStats& ravenBackendStats() noexcept;

// The parts of the OpenGL state the comparison covers: what the backend
// restores (program, bindings of vertex array, buffers, eight texture units
// and their samplers, framebuffers, viewport, scissor, blend, depth, cull,
// stencil test, masks, pixel storage, clear colour), and what it only reads
// (element buffer, sRGB, depth function, cull mode, front face, polygon mode,
// stencil functions and operations, blend colour, primitive restart, alpha to
// coverage, uniform buffer bindings, current vertex attributes).
enum class GlStateGroups { Restored, All };

// Runs `work` and tells whether the given part of the OpenGL state is the same
// afterwards. For checks of other code on the same context: Raven's whole
// frame (Restored: a viewport callback may change the rest, which is the
// engine's own state), Dear ImGui's renderer (All). Needs a current context.
[[nodiscard]] bool sameGlStateAround(const std::function<void()>& work, GlStateGroups groups);

} // namespace engine::render::opengl
