#include "Sdl3Window.hpp"

#include "engine/platform/Platform.hpp"

#include <optional>
#include <string>
#include <utility>

namespace engine::platform {

namespace {

// Depth buffer precision for the default framebuffer. 24 bits is supported
// everywhere and enough for the planned view distances.
constexpr int kDepthBufferBits = 24;

// Pixel-format attributes must be set before the window exists; the context
// version/profile attributes are set later in GlContext::create.
std::optional<core::Error> setOpenGlPixelFormat()
{
    SDL_GL_ResetAttributes();
    if (!SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1) ||
        !SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, kDepthBufferBits)) {
        return core::Error{std::string("SDL_GL_SetAttribute failed: ") + SDL_GetError()};
    }
    return std::nullopt;
}

} // namespace

core::Result<Window> Window::create(const Platform& /*platform*/, const WindowDesc& desc)
{
    if (auto error = validate(desc)) {
        return std::move(*error);
    }

    SDL_WindowFlags flags = 0;
    if (desc.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (desc.openGl) {
        if (auto error = setOpenGlPixelFormat()) {
            return std::move(*error);
        }
        flags |= SDL_WINDOW_OPENGL;
    }

    SDL_Window* handle = SDL_CreateWindow(desc.title.c_str(), desc.width, desc.height, flags);
    if (handle == nullptr) {
        return core::Error{std::string("SDL_CreateWindow failed: ") + SDL_GetError()};
    }

    auto impl = std::make_unique<Impl>();
    impl->handle.reset(handle);
    impl->openGl = desc.openGl;
    return Window{std::move(impl)};
}

Window::Window(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

// Defined here, where Impl is complete, so unique_ptr<Impl> can be destroyed.
Window::~Window() = default;
Window::Window(Window&& other) noexcept = default;
Window& Window::operator=(Window&& other) noexcept = default;

PixelSize Window::pixelSize() const
{
    PixelSize size;
    if (!SDL_GetWindowSizeInPixels(impl_->handle.get(), &size.width, &size.height)) {
        // Only fails for an invalid handle (e.g. a moved-from Window).
        return PixelSize{};
    }
    return size;
}

bool Window::isMinimized() const
{
    return (SDL_GetWindowFlags(impl_->handle.get()) & SDL_WINDOW_MINIMIZED) != 0;
}

} // namespace engine::platform
