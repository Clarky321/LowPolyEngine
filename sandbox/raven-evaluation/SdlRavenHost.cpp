#include "SdlRavenHost.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>

namespace raven_eval {
namespace {

// Wake-ups may come from Raven's worker threads; the prototype's loop never
// sleeps, so they are only counted.
std::atomic<std::uint64_t> g_wakeUps{0};

RKey keyByPosition(SDL_Scancode scancode) noexcept
{
    if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z) {
        return static_cast<RKey>(R_KEY_A + (scancode - SDL_SCANCODE_A));
    }
    if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9) {
        return static_cast<RKey>(R_KEY_1 + (scancode - SDL_SCANCODE_1));
    }
    if (scancode >= SDL_SCANCODE_F1 && scancode <= SDL_SCANCODE_F12) {
        return static_cast<RKey>(R_KEY_F1 + (scancode - SDL_SCANCODE_F1));
    }
    if (scancode >= SDL_SCANCODE_KP_1 && scancode <= SDL_SCANCODE_KP_9) {
        return static_cast<RKey>(R_KEY_KP_1 + (scancode - SDL_SCANCODE_KP_1));
    }
    switch (scancode) {
    case SDL_SCANCODE_0:
        return R_KEY_0;
    case SDL_SCANCODE_SPACE:
        return R_KEY_SPACE;
    case SDL_SCANCODE_APOSTROPHE:
        return R_KEY_APOSTROPHE;
    case SDL_SCANCODE_COMMA:
        return R_KEY_COMMA;
    case SDL_SCANCODE_MINUS:
        return R_KEY_MINUS;
    case SDL_SCANCODE_PERIOD:
        return R_KEY_PERIOD;
    case SDL_SCANCODE_SLASH:
        return R_KEY_SLASH;
    case SDL_SCANCODE_SEMICOLON:
        return R_KEY_SEMICOLON;
    case SDL_SCANCODE_EQUALS:
        return R_KEY_EQUAL;
    case SDL_SCANCODE_LEFTBRACKET:
        return R_KEY_LEFT_BRACKET;
    case SDL_SCANCODE_BACKSLASH:
        return R_KEY_BACKSLASH;
    case SDL_SCANCODE_RIGHTBRACKET:
        return R_KEY_RIGHT_BRACKET;
    case SDL_SCANCODE_GRAVE:
        return R_KEY_GRAVE;
    case SDL_SCANCODE_ESCAPE:
        return R_KEY_ESCAPE;
    case SDL_SCANCODE_RETURN:
        return R_KEY_ENTER;
    case SDL_SCANCODE_TAB:
        return R_KEY_TAB;
    case SDL_SCANCODE_BACKSPACE:
        return R_KEY_BACKSPACE;
    case SDL_SCANCODE_INSERT:
        return R_KEY_INSERT;
    case SDL_SCANCODE_DELETE:
        return R_KEY_DELETE;
    case SDL_SCANCODE_RIGHT:
        return R_KEY_RIGHT;
    case SDL_SCANCODE_LEFT:
        return R_KEY_LEFT;
    case SDL_SCANCODE_DOWN:
        return R_KEY_DOWN;
    case SDL_SCANCODE_UP:
        return R_KEY_UP;
    case SDL_SCANCODE_PAGEUP:
        return R_KEY_PAGE_UP;
    case SDL_SCANCODE_PAGEDOWN:
        return R_KEY_PAGE_DOWN;
    case SDL_SCANCODE_HOME:
        return R_KEY_HOME;
    case SDL_SCANCODE_END:
        return R_KEY_END;
    case SDL_SCANCODE_CAPSLOCK:
        return R_KEY_CAPS_LOCK;
    case SDL_SCANCODE_PRINTSCREEN:
        return R_KEY_PRINT_SCREEN;
    case SDL_SCANCODE_PAUSE:
        return R_KEY_PAUSE;
    case SDL_SCANCODE_KP_0:
        return R_KEY_KP_0;
    case SDL_SCANCODE_KP_PERIOD:
        return R_KEY_KP_DECIMAL;
    case SDL_SCANCODE_KP_DIVIDE:
        return R_KEY_KP_DIVIDE;
    case SDL_SCANCODE_KP_MULTIPLY:
        return R_KEY_KP_MULTIPLY;
    case SDL_SCANCODE_KP_MINUS:
        return R_KEY_KP_SUBTRACT;
    case SDL_SCANCODE_KP_PLUS:
        return R_KEY_KP_ADD;
    case SDL_SCANCODE_KP_ENTER:
        return R_KEY_KP_ENTER;
    case SDL_SCANCODE_LSHIFT:
        return R_KEY_LEFT_SHIFT;
    case SDL_SCANCODE_LCTRL:
        return R_KEY_LEFT_CTRL;
    case SDL_SCANCODE_LALT:
        return R_KEY_LEFT_ALT;
    case SDL_SCANCODE_LGUI:
        return R_KEY_LEFT_SUPER;
    case SDL_SCANCODE_RSHIFT:
        return R_KEY_RIGHT_SHIFT;
    case SDL_SCANCODE_RCTRL:
        return R_KEY_RIGHT_CTRL;
    case SDL_SCANCODE_RALT:
        return R_KEY_RIGHT_ALT;
    case SDL_SCANCODE_RGUI:
        return R_KEY_RIGHT_SUPER;
    case SDL_SCANCODE_APPLICATION:
        return R_KEY_MENU;
    default:
        return R_KEY_UNKNOWN;
    }
}

RMouseButton toRavenButton(Uint8 button) noexcept
{
    switch (button) {
    case SDL_BUTTON_RIGHT:
        return R_MOUSE_RIGHT;
    case SDL_BUTTON_MIDDLE:
        return R_MOUSE_MIDDLE;
    case SDL_BUTTON_X1:
        return R_MOUSE_X1;
    case SDL_BUTTON_X2:
        return R_MOUSE_X2;
    default:
        return R_MOUSE_LEFT;
    }
}

SDL_SystemCursor toSdlCursor(RCursor cursor) noexcept
{
    switch (cursor) {
    case R_CURSOR_IBEAM:
        return SDL_SYSTEM_CURSOR_TEXT;
    case R_CURSOR_HAND:
        return SDL_SYSTEM_CURSOR_POINTER;
    case R_CURSOR_CROSSHAIR:
        return SDL_SYSTEM_CURSOR_CROSSHAIR;
    case R_CURSOR_RESIZE_H:
        return SDL_SYSTEM_CURSOR_EW_RESIZE;
    case R_CURSOR_RESIZE_V:
        return SDL_SYSTEM_CURSOR_NS_RESIZE;
    case R_CURSOR_RESIZE_NWSE:
        return SDL_SYSTEM_CURSOR_NWSE_RESIZE;
    case R_CURSOR_RESIZE_NESW:
        return SDL_SYSTEM_CURSOR_NESW_RESIZE;
    case R_CURSOR_MOVE:
        return SDL_SYSTEM_CURSOR_MOVE;
    case R_CURSOR_NOT_ALLOWED:
        return SDL_SYSTEM_CURSOR_NOT_ALLOWED;
    default:
        return SDL_SYSTEM_CURSOR_DEFAULT;
    }
}

} // namespace

RKey toRavenKey(SDL_Keycode key, SDL_Scancode scancode) noexcept
{
    // SDL keycodes of printable keys are their (lower-case) characters.
    if (key >= SDLK_A && key <= SDLK_Z) {
        return static_cast<RKey>(R_KEY_A + static_cast<int>(key - SDLK_A));
    }
    if (key >= SDLK_0 && key <= SDLK_9) {
        return static_cast<RKey>(R_KEY_0 + static_cast<int>(key - SDLK_0));
    }
    switch (key) {
    case SDLK_SPACE:
        return R_KEY_SPACE;
    case SDLK_APOSTROPHE:
        return R_KEY_APOSTROPHE;
    case SDLK_COMMA:
        return R_KEY_COMMA;
    case SDLK_MINUS:
        return R_KEY_MINUS;
    case SDLK_PERIOD:
        return R_KEY_PERIOD;
    case SDLK_SLASH:
        return R_KEY_SLASH;
    case SDLK_SEMICOLON:
        return R_KEY_SEMICOLON;
    case SDLK_EQUALS:
        return R_KEY_EQUAL;
    case SDLK_LEFTBRACKET:
        return R_KEY_LEFT_BRACKET;
    case SDLK_BACKSLASH:
        return R_KEY_BACKSLASH;
    case SDLK_RIGHTBRACKET:
        return R_KEY_RIGHT_BRACKET;
    case SDLK_GRAVE:
        return R_KEY_GRAVE;
    default:
        return keyByPosition(scancode);
    }
}

Ruint toRavenModifiers(SDL_Keymod modifiers) noexcept
{
    Ruint result = 0;
    if ((modifiers & SDL_KMOD_SHIFT) != 0) {
        result |= R_MOD_SHIFT;
    }
    if ((modifiers & SDL_KMOD_CTRL) != 0) {
        result |= R_MOD_CTRL;
    }
    if ((modifiers & SDL_KMOD_ALT) != 0) {
        result |= R_MOD_ALT;
    }
    if ((modifiers & SDL_KMOD_GUI) != 0) {
        result |= R_MOD_SUPER;
    }
    return result;
}

std::vector<std::uint32_t> decodeUtf8(std::string_view text)
{
    constexpr std::uint32_t kReplacement = 0xFFFD;
    std::vector<std::uint32_t> result;
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        std::size_t length = 0;
        std::uint32_t value = 0;
        std::uint32_t minimum = 0;
        if (lead < 0x80U) {
            length = 1;
            value = lead;
        } else if ((lead & 0xE0U) == 0xC0U) {
            length = 2;
            value = lead & 0x1FU;
            minimum = 0x80;
        } else if ((lead & 0xF0U) == 0xE0U) {
            length = 3;
            value = lead & 0x0FU;
            minimum = 0x800;
        } else if ((lead & 0xF8U) == 0xF0U) {
            length = 4;
            value = lead & 0x07U;
            minimum = 0x10000;
        } else {
            result.push_back(kReplacement);
            ++i;
            continue;
        }
        bool valid = i + length <= text.size();
        for (std::size_t k = 1; valid && k < length; ++k) {
            const auto next = static_cast<unsigned char>(text[i + k]);
            valid = (next & 0xC0U) == 0x80U;
            value = (value << 6U) | (next & 0x3FU);
        }
        if (!valid || value < minimum || value > 0x10FFFFU ||
            (value >= 0xD800U && value <= 0xDFFFU)) {
            result.push_back(kReplacement);
            ++i;
            continue;
        }
        result.push_back(value);
        i += length;
    }
    return result;
}

SdlRavenHost::SdlRavenHost(SDL_Window* window) noexcept : window_(window) {}

SdlRavenHost::~SdlRavenHost()
{
    for (SDL_Cursor* cursor : cursors_) {
        if (cursor != nullptr) {
            SDL_DestroyCursor(cursor);
        }
    }
}

void SdlRavenHost::registerPlatform()
{
    RHostPlatformDesc desc{};
    desc.set_cursor = &SdlRavenHost::setCursor;
    desc.clipboard_set = &SdlRavenHost::setClipboard;
    desc.clipboard_get = &SdlRavenHost::clipboard;
    desc.wake = &SdlRavenHost::wake;
    desc.user_data = this;
    Rhost_platform_register(&desc);
}

void SdlRavenHost::attach(Rid ravenWindow)
{
    ravenWindow_ = ravenWindow;
    updateSize();
    Rhost_inject_focus(ravenWindow_, (SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) != 0
                                         ? R_TRUE
                                         : R_FALSE);
}

float SdlRavenHost::pixelsPerPoint() const noexcept
{
    const float density = SDL_GetWindowPixelDensity(window_);
    return density > 0.0F ? density : 1.0F;
}

void SdlRavenHost::updateSize()
{
    SDL_GetWindowSizeInPixels(window_, &pixelWidth_, &pixelHeight_);
    // Pixels per logical unit: the pixel density times the display's content
    // scale (125 % on a laptop gives 1.25 with a density of 1).
    const float displayScale = SDL_GetWindowDisplayScale(window_);
    scale_ = displayScale > 0.0F ? displayScale : 1.0F;
    if (ravenWindow_ != R_INVALID_ID) {
        Rhost_window_set_size(ravenWindow_, pixelWidth_, pixelHeight_, scale_);
        ++stats_.resizes;
    }
}

bool SdlRavenHost::handleEvent(const SDL_Event& event)
{
    if (ravenWindow_ == R_INVALID_ID) {
        return false;
    }
    const float toPixels = pixelsPerPoint();
    const auto pixel = [toPixels](float points) {
        return static_cast<int>(std::lround(points * toPixels));
    };
    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
        mouseX_ = pixel(event.motion.x);
        mouseY_ = pixel(event.motion.y);
        Rhost_inject_mouse_move(ravenWindow_, mouseX_, mouseY_,
                                toRavenModifiers(SDL_GetModState()));
        ++stats_.mouseMoves;
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        const RMouseButton button = toRavenButton(event.button.button);
        const bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        mouseX_ = pixel(event.button.x);
        mouseY_ = pixel(event.button.y);
        const auto held = std::find(heldButtons_.begin(), heldButtons_.end(), button);
        if (down && held == heldButtons_.end()) {
            heldButtons_.push_back(button);
        } else if (!down && held != heldButtons_.end()) {
            heldButtons_.erase(held);
        }
        Rhost_inject_mouse_button(ravenWindow_, mouseX_, mouseY_, button, down ? R_TRUE : R_FALSE,
                                  toRavenModifiers(SDL_GetModState()));
        ++stats_.mouseButtons;
        return true;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        // SDL: +y away from the user, +x to the right; Raven: positive = up / left.
        const float flip = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0F : 1.0F;
        Rhost_inject_mouse_wheel(ravenWindow_, -event.wheel.x * flip, event.wheel.y * flip,
                                 toRavenModifiers(SDL_GetModState()));
        ++stats_.wheels;
        return true;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        const RKey key = toRavenKey(event.key.key, event.key.scancode);
        if (key == R_KEY_UNKNOWN) {
            return false;
        }
        const bool down = event.type == SDL_EVENT_KEY_DOWN;
        const auto held = std::find(heldKeys_.begin(), heldKeys_.end(), key);
        if (down && held == heldKeys_.end()) {
            heldKeys_.push_back(key);
        } else if (!down && held != heldKeys_.end()) {
            heldKeys_.erase(held);
        }
        Rhost_inject_key(ravenWindow_, key, down ? R_TRUE : R_FALSE,
                         toRavenModifiers(event.key.mod), event.key.repeat ? R_TRUE : R_FALSE);
        ++stats_.keys;
        return true;
    }
    case SDL_EVENT_TEXT_INPUT:
        if (event.text.text == nullptr) {
            return false;
        }
        for (const std::uint32_t codepoint : decodeUtf8(event.text.text)) {
            Rhost_inject_text(ravenWindow_, codepoint);
            ++stats_.codepoints;
        }
        return true;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        Rhost_inject_focus(ravenWindow_, R_TRUE);
        ++stats_.focusChanges;
        return true;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        // The releases of keys and buttons held now go to whichever window
        // has the focus, so Raven (and the widget that has its keyboard)
        // would keep them down; release them here, while the widget still
        // has the focus, then tell Raven the window lost it.
        releaseHeldInput();
        Rhost_inject_focus(ravenWindow_, R_FALSE);
        ++stats_.focusChanges;
        return true;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        Rhost_inject_mouse_leave(ravenWindow_);
        ++stats_.mouseLeaves;
        return true;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        updateSize();
        return true;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    case SDL_EVENT_QUIT:
        Rhost_inject_close(ravenWindow_);
        ++stats_.closeRequests;
        return true;
    default:
        return false;
    }
}

void SdlRavenHost::releaseHeldInput()
{
    // Modifiers are released last, so the other keys' releases carry none.
    const auto isModifier = [](RKey key) {
        return key == R_KEY_LEFT_SHIFT || key == R_KEY_RIGHT_SHIFT || key == R_KEY_LEFT_CTRL ||
               key == R_KEY_RIGHT_CTRL || key == R_KEY_LEFT_ALT || key == R_KEY_RIGHT_ALT ||
               key == R_KEY_LEFT_SUPER || key == R_KEY_RIGHT_SUPER;
    };
    std::stable_partition(heldKeys_.begin(), heldKeys_.end(),
                          [&isModifier](RKey key) { return !isModifier(key); });
    for (const RKey key : heldKeys_) {
        Rhost_inject_key(ravenWindow_, key, R_FALSE, 0, R_FALSE);
        ++stats_.releasedKeys;
    }
    heldKeys_.clear();
    for (const RMouseButton button : heldButtons_) {
        Rhost_inject_mouse_button(ravenWindow_, mouseX_, mouseY_, button, R_FALSE, 0);
        ++stats_.releasedButtons;
    }
    heldButtons_.clear();
}

HostStats SdlRavenHost::stats() const noexcept
{
    HostStats result = stats_;
    result.wakeUps = g_wakeUps.load(std::memory_order_relaxed);
    return result;
}

void SdlRavenHost::setCursor(Rid /*window*/, RCursor cursor, void* user)
{
    auto* self = static_cast<SdlRavenHost*>(user);
    ++self->stats_.cursorChanges;
    if (cursor == R_CURSOR_NONE) {
        SDL_HideCursor();
        return;
    }
    const auto index = static_cast<std::size_t>(toSdlCursor(cursor));
    if (index >= self->cursors_.size()) {
        self->cursors_.resize(index + 1, nullptr);
    }
    if (self->cursors_[index] == nullptr) {
        self->cursors_[index] = SDL_CreateSystemCursor(toSdlCursor(cursor));
    }
    SDL_ShowCursor();
    SDL_SetCursor(self->cursors_[index]);
}

void SdlRavenHost::setClipboard(const char* text, void* user)
{
    auto* self = static_cast<SdlRavenHost*>(user);
    ++self->stats_.clipboardWrites;
    SDL_SetClipboardText(text != nullptr ? text : "");
}

const char* SdlRavenHost::clipboard(void* user)
{
    auto* self = static_cast<SdlRavenHost*>(user);
    ++self->stats_.clipboardReads;
    char* text = SDL_GetClipboardText();
    self->clipboard_ = text != nullptr ? text : "";
    SDL_free(text);
    return self->clipboard_.c_str();
}

void SdlRavenHost::wake(void* user)
{
    (void)user;
    g_wakeUps.fetch_add(1, std::memory_order_relaxed);
}

} // namespace raven_eval
