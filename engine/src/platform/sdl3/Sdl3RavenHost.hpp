#pragma once

// Internal to the SDL3 platform layer: Raven's host platform (raven_host.h) on
// the engine's SDL3 window (ADR 0019, ADR 0027). SDL events become
// Rhost_inject_* calls, the window's pixel size and display scale become
// Rhost_window_set_size, and Raven's requests for the cursor, the clipboard
// and the text input area are served with SDL. Raven opens no window and
// reads no OS event queue of its own; everything runs on the main thread.
// Never include this outside engine/src/platform/sdl3/ (and the tests).
//
// Taken over from the validated prototype (experiment/raven-final-validation,
// sandbox/raven-evaluation/SdlRavenHost.cpp) with the final validation's
// fixes:
//   1. keys and buttons held when the window loses the keyboard focus are
//      released (modifiers last), once each;
//   2. a close request followed by SDL_EVENT_QUIT reaches Raven once; a quit
//      on its own is a close request;
//   3. after Raven accepted a close, the window's handle is not used again
//      (the host detaches as soon as the handle is invalid).
// New in M5.2: the IME area (Rwindow_get_text_input_rect to
// SDL_SetTextInputArea), and SDL text input on only while a Raven text field
// has the keyboard.

#include "../../ui/RavenHost.hpp"

#include <raven/raven_host.h>

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::platform {

// Raven key for an SDL key event: Latin letters, digits and punctuation by the
// key's meaning in the current layout; everything else, and letters of
// non-Latin layouts (so Ctrl+S works with a Cyrillic layout), by position.
[[nodiscard]] RKey toRavenKey(SDL_Keycode key, SDL_Scancode scancode) noexcept;
[[nodiscard]] Ruint toRavenModifiers(SDL_Keymod modifiers) noexcept;
[[nodiscard]] RMouseButton toRavenButton(std::uint8_t sdlButton) noexcept;

// The code points of an SDL text event; each invalid byte becomes U+FFFD.
[[nodiscard]] std::vector<char32_t> decodeTextInput(std::string_view text);

struct RavenHostStats {
    std::uint64_t mouseMoves = 0;
    std::uint64_t mouseButtons = 0;
    std::uint64_t wheels = 0;
    std::uint64_t keys = 0;
    std::uint64_t codepoints = 0;
    std::uint64_t focusChanges = 0;
    std::uint64_t resizes = 0;
    // Close requests given to Raven (Rhost_inject_close).
    std::uint64_t closeRequests = 0;
    // Key-ups and button-ups the host sent itself on focus loss.
    std::uint64_t releasedKeys = 0;
    std::uint64_t releasedButtons = 0;
    // SDL_SetTextInputArea calls (the caret moved, or text input started).
    std::uint64_t textInputAreas = 0;
};

class Sdl3RavenHost final : public ui::RavenHost {
public:
    explicit Sdl3RavenHost(SDL_Window* window) noexcept;
    ~Sdl3RavenHost() override;

    void registerPlatform() override;
    void attach(Rid window) override;
    void detach() noexcept override;
    [[nodiscard]] Rid attachedWindow() const noexcept override { return ravenWindow_; }
    void syncTextInput() override;
    [[nodiscard]] float scale() const noexcept override { return scale_; }

    // Translates one SDL event for Raven. True if the UI took it, so the
    // application's input (InputState) and the quit request do not get it:
    // every keyboard, text, mouse and wheel event while a window is attached,
    // and the close requests given to Raven or dropped as duplicates. Focus,
    // size and scale events are seen and passed on.
    bool handleEvent(const SDL_Event& event);

    // The window's size in pixels and its pixels per logical unit, given to
    // the attached Raven window.
    void updateSize();
    [[nodiscard]] int pixelWidth() const noexcept { return pixelWidth_; }
    [[nodiscard]] int pixelHeight() const noexcept { return pixelHeight_; }

    // Keys and mouse buttons Raven was told are down and not yet up.
    [[nodiscard]] std::size_t heldKeys() const noexcept { return heldKeys_.size(); }
    [[nodiscard]] std::size_t heldButtons() const noexcept { return heldButtons_.size(); }

    // What the host told SDL: text input on, and the area in window coordinates.
    [[nodiscard]] bool textInputActive() const noexcept { return textInputActive_; }
    [[nodiscard]] const std::optional<SDL_Rect>& textInputArea() const noexcept
    {
        return textInputArea_;
    }

    [[nodiscard]] const RavenHostStats& stats() const noexcept { return stats_; }

private:
    // Sends Raven the releases of everything still held (before focus loss).
    void releaseHeldInput();
    // Detaches once Raven destroyed the window (it does so inside an accepted
    // Rhost_inject_close, or through an action of its own).
    void detachIfClosed() noexcept;
    void stopTextInput() noexcept;
    [[nodiscard]] float pixelsPerPoint() const noexcept;

    static void setCursor(Rid window, RCursor cursor, void* user);
    static void setClipboard(const char* text, void* user);
    static const char* clipboard(void* user);
    static void wake(void* user);

    SDL_Window* window_ = nullptr;
    Rid ravenWindow_ = R_INVALID_ID;
    int pixelWidth_ = 0;
    int pixelHeight_ = 0;
    float scale_ = 1.0F;
    std::vector<SDL_Cursor*> cursors_;
    std::string clipboard_;
    std::vector<RKey> heldKeys_;
    std::vector<RMouseButton> heldButtons_;
    // The last mouse position given to Raven, in pixels.
    int mouseX_ = 0;
    int mouseY_ = 0;
    // The previous event was a window close request (SDL_EVENT_QUIT follows).
    bool lastWasCloseRequest_ = false;
    bool textInputActive_ = false;
    std::optional<SDL_Rect> textInputArea_;
    RavenHostStats stats_;
};

// Hands one event to the attached Raven host, if any. True if the UI took it
// (see Sdl3RavenHost::handleEvent). Called by Platform::pollEvents after the
// debug UI has seen the event.
bool forwardEventToUi(const SDL_Event& event);

} // namespace engine::platform
