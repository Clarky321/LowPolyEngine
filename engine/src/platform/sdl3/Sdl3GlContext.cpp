#include "Sdl3Window.hpp"

#include "engine/platform/GlContext.hpp"

#include <SDL3/SDL.h>

#include <string>
#include <utility>

namespace engine::platform {

namespace {

struct SdlGlContextDeleter {
    void operator()(SDL_GLContextState* context) const noexcept { SDL_GL_DestroyContext(context); }
};

core::Error sdlError(const char* what)
{
    return core::Error{std::string(what) + " failed: " + SDL_GetError()};
}

// SDL_GL_GetProcAddress returns SDL_FunctionPointer (void (*)(void)), which is
// the same type as GlProc; this wrapper pins the signature at compile time.
GlProc getProcAddress(const char* name)
{
    return SDL_GL_GetProcAddress(name);
}

} // namespace

struct GlContext::Impl {
    // Not owned: the Window outlives the context (see GlContext.hpp).
    SDL_Window* window = nullptr;
    std::unique_ptr<SDL_GLContextState, SdlGlContextDeleter> context;
    bool vsync = false;
};

core::Result<GlContext> GlContext::create(Window& window, const GlContextDesc& desc)
{
    if (!window.impl_->openGl) {
        return core::Error{"GlContext::create: the window was not created with openGl = true"};
    }

    const int flags = desc.debug ? SDL_GL_CONTEXT_DEBUG_FLAG : 0;
    if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, desc.majorVersion) ||
        !SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, desc.minorVersion) ||
        !SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE) ||
        !SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, flags)) {
        return sdlError("SDL_GL_SetAttribute");
    }

    auto impl = std::make_unique<Impl>();
    impl->window = window.impl_->handle.get();
    // Fails when the driver cannot provide the requested version/profile.
    impl->context.reset(SDL_GL_CreateContext(impl->window));
    if (!impl->context) {
        return core::Error{"OpenGL " + std::to_string(desc.majorVersion) + "." +
                           std::to_string(desc.minorVersion) +
                           " core context not available: " + SDL_GetError()};
    }

    // SDL_GL_CreateContext already makes it current; repeating it keeps the
    // thread binding explicit and reports a failure if there is one.
    if (!SDL_GL_MakeCurrent(impl->window, impl->context.get())) {
        return sdlError("SDL_GL_MakeCurrent");
    }

    // A refused swap interval is not fatal; callers can check vsyncEnabled().
    impl->vsync = desc.vsync && SDL_GL_SetSwapInterval(1);
    if (!desc.vsync) {
        SDL_GL_SetSwapInterval(0);
    }

    return GlContext{std::move(impl)};
}

GlContext::GlContext(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

GlContext::~GlContext() = default;
GlContext::GlContext(GlContext&& other) noexcept = default;
GlContext& GlContext::operator=(GlContext&& other) noexcept = default;

GlProcLoader GlContext::procLoader() const noexcept
{
    return &getProcAddress;
}

void GlContext::swapBuffers()
{
    SDL_GL_SwapWindow(impl_->window);
}

bool GlContext::vsyncEnabled() const noexcept
{
    return impl_->vsync;
}

} // namespace engine::platform
