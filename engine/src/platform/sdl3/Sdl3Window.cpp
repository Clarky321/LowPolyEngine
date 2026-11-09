#include "engine/platform/Platform.hpp"
#include "engine/platform/Window.hpp"

#include <SDL3/SDL.h>

#include <string>
#include <utility>

namespace engine::platform {

namespace {

struct SdlWindowDeleter {
    void operator()(SDL_Window* window) const noexcept { SDL_DestroyWindow(window); }
};

} // namespace

struct Window::Impl {
    std::unique_ptr<SDL_Window, SdlWindowDeleter> handle;
};

core::Result<Window> Window::create(const Platform& /*platform*/, const WindowDesc& desc)
{
    if (auto error = validate(desc)) {
        return std::move(*error);
    }

    SDL_WindowFlags flags = 0;
    if (desc.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }

    SDL_Window* handle = SDL_CreateWindow(desc.title.c_str(), desc.width, desc.height, flags);
    if (handle == nullptr) {
        return core::Error{std::string("SDL_CreateWindow failed: ") + SDL_GetError()};
    }

    auto impl = std::make_unique<Impl>();
    impl->handle.reset(handle);
    return Window{std::move(impl)};
}

Window::Window(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

// Defined here, where Impl is complete, so unique_ptr<Impl> can be destroyed.
Window::~Window() = default;
Window::Window(Window&& other) noexcept = default;
Window& Window::operator=(Window&& other) noexcept = default;

} // namespace engine::platform
