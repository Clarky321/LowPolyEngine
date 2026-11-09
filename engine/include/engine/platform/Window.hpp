#pragma once

#include "engine/core/Result.hpp"

#include <memory>
#include <optional>
#include <string>

namespace engine::platform {

class Platform;
class GlContext;

// Largest accepted width/height in screen coordinates. Far above any current
// display; it only rejects garbage input before it reaches the OS.
inline constexpr int kMaxWindowExtent = 16384;

struct WindowDesc {
    std::string title = "LowPolyEngine";
    int width = 1280;
    int height = 720;
    bool resizable = true;
    // Prepare the window for an OpenGL context (GlContext::create). The pixel
    // format is chosen when the window is created, so this cannot be added later.
    bool openGl = false;
};

// Size in pixels. Differs from the window size in screen coordinates on high-DPI displays.
struct PixelSize {
    int width = 0;
    int height = 0;
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

    // Current drawable size in pixels, e.g. for the render viewport.
    [[nodiscard]] PixelSize pixelSize() const;

    // True while the window is minimized; nothing it renders is visible then.
    [[nodiscard]] bool isMinimized() const;

private:
    // Hides the platform-specific handle so no SDL type appears in this header (ADR 0002).
    struct Impl;
    explicit Window(std::unique_ptr<Impl> impl) noexcept;

    // GlContext needs the native handle, which is otherwise private to the platform layer.
    friend class GlContext;

    std::unique_ptr<Impl> impl_;
};

} // namespace engine::platform
