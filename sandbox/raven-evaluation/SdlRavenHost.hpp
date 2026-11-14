#pragma once

// Raven's host platform (raven_host.h) on top of the one SDL3 window of the
// prototype: SDL events are translated into Rhost_inject_* calls, the window's
// pixel size and scale into Rhost_window_set_size, and Raven's requests for
// the cursor and the clipboard are served with SDL. Raven opens no window and
// reads no OS event queue of its own. Everything runs on the UI thread.

#include <SDL3/SDL.h>
#include <raven/raven_host.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace raven_eval {

// Raven key for an SDL key event: Latin letters, digits and punctuation by
// the key's meaning in the current layout; everything else, and letters of
// non-Latin layouts (so Ctrl+S works with a Cyrillic layout), by position.
[[nodiscard]] RKey toRavenKey(SDL_Keycode key, SDL_Scancode scancode) noexcept;
[[nodiscard]] Ruint toRavenModifiers(SDL_Keymod modifiers) noexcept;
// The code points of UTF-8 text; malformed bytes become U+FFFD.
[[nodiscard]] std::vector<std::uint32_t> decodeUtf8(std::string_view text);

struct HostStats {
    std::uint64_t mouseMoves = 0;
    std::uint64_t mouseButtons = 0;
    std::uint64_t wheels = 0;
    std::uint64_t keys = 0;
    std::uint64_t codepoints = 0;
    std::uint64_t focusChanges = 0;
    std::uint64_t mouseLeaves = 0;
    std::uint64_t resizes = 0;
    std::uint64_t closeRequests = 0;
    std::uint64_t cursorChanges = 0;
    std::uint64_t clipboardWrites = 0;
    std::uint64_t clipboardReads = 0;
    std::uint64_t wakeUps = 0;
};

class SdlRavenHost {
public:
    explicit SdlRavenHost(SDL_Window* window) noexcept;
    ~SdlRavenHost();
    SdlRavenHost(const SdlRavenHost&) = delete;
    SdlRavenHost& operator=(const SdlRavenHost&) = delete;

    // Rhost_platform_register with this object's callbacks; before Rinit.
    void registerPlatform();
    // The Raven window that stands for the SDL window (after Rwindow_create):
    // gives it the size, the scale and the keyboard.
    void attach(Rid ravenWindow);
    void detach() noexcept { ravenWindow_ = R_INVALID_ID; }

    // Translates one SDL event for Raven. Returns true if Raven got it.
    bool handleEvent(const SDL_Event& event);

    // The window's size in pixels and its pixels per logical unit.
    void updateSize();
    [[nodiscard]] int pixelWidth() const noexcept { return pixelWidth_; }
    [[nodiscard]] int pixelHeight() const noexcept { return pixelHeight_; }
    [[nodiscard]] float scale() const noexcept { return scale_; }

    [[nodiscard]] HostStats stats() const noexcept;

private:
    static void setCursor(Rid window, RCursor cursor, void* user);
    static void setClipboard(const char* text, void* user);
    static const char* clipboard(void* user);
    static void wake(void* user);

    [[nodiscard]] float pixelsPerPoint() const noexcept;

    SDL_Window* window_ = nullptr;
    Rid ravenWindow_ = R_INVALID_ID;
    int pixelWidth_ = 0;
    int pixelHeight_ = 0;
    float scale_ = 1.0F;
    std::vector<SDL_Cursor*> cursors_;
    std::string clipboard_;
    HostStats stats_;
};

} // namespace raven_eval
