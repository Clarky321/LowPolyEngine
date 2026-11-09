#pragma once

#include "engine/core/Result.hpp"

#include <memory>
#include <optional>
#include <string>

namespace engine::platform {

class Platform;

// Largest accepted width/height in screen coordinates. Far above any current
// display; it only rejects garbage input before it reaches the OS.
inline constexpr int kMaxWindowExtent = 16384;

struct WindowDesc {
    std::string title = "LowPolyEngine";
    int width = 1280;
    int height = 720;
    bool resizable = true;
};

// Checks a description without touching the OS. Returns an error for
// non-positive or too large sizes.
[[nodiscard]] std::optional<core::Error> validate(const WindowDesc& desc);

// A native OS window. It must be destroyed before the Platform it was created with.
class Window {
public:
    // Requiring the Platform documents that the window system must be initialized.
    [[nodiscard]] static core::Result<Window> create(const Platform& platform,
                                                     const WindowDesc& desc);

    ~Window();
    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

private:
    // Hides the platform-specific handle so no SDL type appears in this header (ADR 0002).
    struct Impl;
    explicit Window(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

} // namespace engine::platform
