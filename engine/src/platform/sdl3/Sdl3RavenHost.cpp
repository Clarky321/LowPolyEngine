// Raven's host platform on the engine's SDL3 window (ADR 0019, ADR 0027),
// from the validated prototype (experiment/raven-final-validation,
// sandbox/raven-evaluation/SdlRavenHost.cpp); see Sdl3RavenHost.hpp.

#include "Sdl3RavenHost.hpp"

#include "Sdl3Window.hpp"

#include "engine/core/Log.hpp"
#include "engine/core/Utf8.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>

namespace engine::platform {
namespace {

// The host whose Raven window receives the window's events (one at a time:
// Raven allows one Rinit per process).
Sdl3RavenHost* g_attachedHost = nullptr;

// Raven may ask for a wake-up from its worker threads. The engine's loop never
// sleeps (it is paced by vsync), so wake-ups are only counted.
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

RMouseButton toRavenButton(std::uint8_t sdlButton) noexcept
{
    switch (sdlButton) {
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

std::vector<char32_t> decodeTextInput(std::string_view text)
{
    constexpr char32_t kReplacement = 0xFFFD;
    std::vector<char32_t> result;
    std::size_t offset = 0;
    while (offset < text.size()) {
        if (const auto codepoint = core::decodeUtf8At(text, offset)) {
            result.push_back(*codepoint);
        } else {
            result.push_back(kReplacement);
            ++offset;
        }
    }
    return result;
}

Sdl3RavenHost::Sdl3RavenHost(SDL_Window* window) noexcept : window_(window) {}

Sdl3RavenHost::~Sdl3RavenHost()
{
    stopTextInput();
    detach();
    for (SDL_Cursor* cursor : cursors_) {
        if (cursor != nullptr) {
            SDL_DestroyCursor(cursor);
        }
    }
}

void Sdl3RavenHost::registerPlatform()
{
    RHostPlatformDesc desc{};
    desc.set_cursor = &Sdl3RavenHost::setCursor;
    desc.clipboard_set = &Sdl3RavenHost::setClipboard;
    desc.clipboard_get = &Sdl3RavenHost::clipboard;
    desc.wake = &Sdl3RavenHost::wake;
    desc.user_data = this;
    Rhost_platform_register(&desc);
}

void Sdl3RavenHost::attach(Rid window)
{
    if (g_attachedHost != nullptr && g_attachedHost != this) {
        core::logError("ui", "a Raven window is already attached to the engine's window");
        return;
    }
    ravenWindow_ = window;
    g_attachedHost = this;
    updateSize();
    Rhost_inject_focus(ravenWindow_, (SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) != 0
                                         ? R_TRUE
                                         : R_FALSE);
}

void Sdl3RavenHost::detach() noexcept
{
    ravenWindow_ = R_INVALID_ID;
    heldKeys_.clear();
    heldButtons_.clear();
    lastWasCloseRequest_ = false;
    if (g_attachedHost == this) {
        g_attachedHost = nullptr;
    }
}

void Sdl3RavenHost::detachIfClosed() noexcept
{
    if (ravenWindow_ != R_INVALID_ID && Rwindow_is_valid(ravenWindow_) == R_FALSE) {
        detach();
    }
}

float Sdl3RavenHost::pixelsPerPoint() const noexcept
{
    const float density = SDL_GetWindowPixelDensity(window_);
    return density > 0.0F ? density : 1.0F;
}

void Sdl3RavenHost::updateSize()
{
    SDL_GetWindowSizeInPixels(window_, &pixelWidth_, &pixelHeight_);
    // Pixels per logical unit: SDL's display scale of the window is the pixel
    // density times the display's content scale (150 % gives 1.5 with a
    // density of 1).
    const float displayScale = SDL_GetWindowDisplayScale(window_);
    scale_ = displayScale > 0.0F ? displayScale : 1.0F;
    if (ravenWindow_ != R_INVALID_ID) {
        Rhost_window_set_size(ravenWindow_, pixelWidth_, pixelHeight_, scale_);
        ++stats_.resizes;
    }
}

bool Sdl3RavenHost::handleEvent(const SDL_Event& event)
{
    detachIfClosed();
    if (ravenWindow_ == R_INVALID_ID) {
        return false;
    }
    const bool afterCloseRequest = lastWasCloseRequest_;
    lastWasCloseRequest_ = event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    const float toPixels = pixelsPerPoint();
    const auto pixel = [toPixels](float points) {
        return static_cast<int>(std::lround(points * toPixels));
    };
    bool taken = true;
    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
        mouseX_ = pixel(event.motion.x);
        mouseY_ = pixel(event.motion.y);
        Rhost_inject_mouse_move(ravenWindow_, mouseX_, mouseY_,
                                toRavenModifiers(SDL_GetModState()));
        ++stats_.mouseMoves;
        break;
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
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        // SDL: +y away from the user, +x to the right; Raven: positive is up / left.
        const float flip = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0F : 1.0F;
        Rhost_inject_mouse_wheel(ravenWindow_, -event.wheel.x * flip, event.wheel.y * flip,
                                 toRavenModifiers(SDL_GetModState()));
        ++stats_.wheels;
        break;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        // Keys Raven does not know are still the UI's: they never reach the
        // application's input while a Raven window is attached.
        const RKey key = toRavenKey(event.key.key, event.key.scancode);
        if (key == R_KEY_UNKNOWN) {
            break;
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
        break;
    }
    case SDL_EVENT_TEXT_INPUT:
        if (event.text.text != nullptr) {
            for (const char32_t codepoint : decodeTextInput(event.text.text)) {
                Rhost_inject_text(ravenWindow_, static_cast<std::uint32_t>(codepoint));
                ++stats_.codepoints;
            }
        }
        break;
    case SDL_EVENT_TEXT_EDITING:
    case SDL_EVENT_TEXT_EDITING_CANDIDATES:
        // An IME composition in progress: Raven has no composition API; the
        // committed text arrives as SDL_EVENT_TEXT_INPUT.
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        Rhost_inject_focus(ravenWindow_, R_TRUE);
        ++stats_.focusChanges;
        taken = false;
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        // The releases of keys and buttons held now go to whichever window has
        // the focus, so Raven (and the widget that has its keyboard) would keep
        // them down: release them here, while the widget still has the focus,
        // then tell Raven the window lost it. The application's input gets the
        // event too (InputState::releaseAll).
        releaseHeldInput();
        Rhost_inject_focus(ravenWindow_, R_FALSE);
        ++stats_.focusChanges;
        taken = false;
        break;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        Rhost_inject_mouse_leave(ravenWindow_);
        break;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        updateSize();
        taken = false;
        break;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    case SDL_EVENT_QUIT:
        // A close request of the last window is followed at once by
        // SDL_EVENT_QUIT: that is the same request, and Raven may have closed
        // the window (or kept it open) already. A quit on its own (a signal,
        // the session ending) is a close request. Raven's answer decides; the
        // engine stops once the window is gone.
        if (!(event.type == SDL_EVENT_QUIT && afterCloseRequest)) {
            Rhost_inject_close(ravenWindow_);
            ++stats_.closeRequests;
        }
        break;
    default:
        taken = false;
        break;
    }
    // Raven destroys a window inside an accepted close request (or an action
    // such as "window.close"): its handle must not be used afterwards.
    detachIfClosed();
    return taken;
}

void Sdl3RavenHost::releaseHeldInput()
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

void Sdl3RavenHost::stopTextInput() noexcept
{
    if (textInputActive_) {
        SDL_StopTextInput(window_);
        textInputActive_ = false;
    }
    textInputArea_.reset();
}

void Sdl3RavenHost::syncTextInput()
{
    detachIfClosed();
    if (ravenWindow_ == R_INVALID_ID || Rwindow_wants_text_input(ravenWindow_) == R_FALSE) {
        stopTextInput();
        return;
    }
    if (!textInputActive_) {
        if (!SDL_StartTextInput(window_)) {
            core::logWarning("ui", "SDL_StartTextInput failed: {}", SDL_GetError());
            return;
        }
        textInputActive_ = true;
        textInputArea_.reset();
    }
    // The caret, in the window's pixels. None while the window does not have
    // the keyboard: the last area stays for when it comes back.
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    if (Rwindow_get_text_input_rect(ravenWindow_, &x, &y, &width, &height) == R_FALSE) {
        return;
    }
    // SDL takes window coordinates (points); one point is pixelsPerPoint pixels.
    const float toPoints = 1.0F / pixelsPerPoint();
    const auto left = static_cast<int>(std::floor(static_cast<float>(x) * toPoints));
    const auto top = static_cast<int>(std::floor(static_cast<float>(y) * toPoints));
    const auto right = static_cast<int>(std::ceil(static_cast<float>(x + width) * toPoints));
    const auto bottom = static_cast<int>(std::ceil(static_cast<float>(y + height) * toPoints));
    const SDL_Rect area{left, top, std::max(right - left, 1), std::max(bottom - top, 1)};
    if (textInputArea_ && textInputArea_->x == area.x && textInputArea_->y == area.y &&
        textInputArea_->w == area.w && textInputArea_->h == area.h) {
        return;
    }
    // The caret is the area; the cursor offset within it is 0.
    if (SDL_SetTextInputArea(window_, &area, 0)) {
        textInputArea_ = area;
        ++stats_.textInputAreas;
    }
}

void Sdl3RavenHost::setCursor(Rid /*window*/, RCursor cursor, void* user)
{
    auto* self = static_cast<Sdl3RavenHost*>(user);
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
    if (self->cursors_[index] != nullptr) {
        SDL_SetCursor(self->cursors_[index]);
    }
}

void Sdl3RavenHost::setClipboard(const char* text, void* /*user*/)
{
    SDL_SetClipboardText(text != nullptr ? text : "");
}

const char* Sdl3RavenHost::clipboard(void* user)
{
    auto* self = static_cast<Sdl3RavenHost*>(user);
    char* text = SDL_GetClipboardText();
    self->clipboard_ = text != nullptr ? text : "";
    SDL_free(text);
    return self->clipboard_.c_str();
}

void Sdl3RavenHost::wake(void* /*user*/)
{
    g_wakeUps.fetch_add(1, std::memory_order_relaxed);
}

bool forwardEventToUi(const SDL_Event& event)
{
    return g_attachedHost != nullptr && g_attachedHost->handleEvent(event);
}

} // namespace engine::platform

namespace engine::ui {

std::unique_ptr<RavenHost> createRavenHost(platform::Window& window)
{
    SDL_Window* handle = platform::detail::NativeWindowAccess::handle(window);
    if (handle == nullptr) {
        return nullptr;
    }
    return std::make_unique<platform::Sdl3RavenHost>(handle);
}

} // namespace engine::ui
