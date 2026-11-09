#pragma once

#include "engine/core/Result.hpp"
#include "engine/platform/Input.hpp"

namespace engine::platform {

// What happened since the last pollEvents() call.
struct PlatformEvents {
    // The user closed the window or the OS asked the application to quit.
    bool quitRequested = false;
};

// Owns the OS platform layer (window system and event queue).
//
// Create it on the main thread before any Window and keep it alive until every
// Window is destroyed. Events must be polled on the same thread. Several
// instances may exist; the underlying platform shuts down with the last one.
class Platform {
public:
    [[nodiscard]] static core::Result<Platform> initialize();

    ~Platform();
    Platform(Platform&& other) noexcept;
    Platform& operator=(Platform&& other) noexcept;
    Platform(const Platform&) = delete;
    Platform& operator=(const Platform&) = delete;

    // Drains all pending OS events. Call once per frame: starts a new input
    // frame (InputState::beginFrame) and applies keyboard, mouse and focus events.
    [[nodiscard]] PlatformEvents pollEvents(InputState& input);

private:
    Platform() = default;
    void shutdown() noexcept;

    // False for moved-from instances, which must not shut the platform down.
    bool active_ = false;
};

} // namespace engine::platform
