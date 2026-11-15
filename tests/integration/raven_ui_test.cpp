// The engine's Raven UI without a GPU (M5.2, ADR 0027): the SDL3 host bridge
// and the UI's lifetime, on a real SDL window of SDL's "dummy" video driver
// (set by CTest) with Raven's null backend. SDL events are pushed into SDL's
// real queue and read by Platform::pollEvents, so the event order of ADR 0027
// is what is tested: the debug UI, then Raven's host, then InputState.
//
// Raven's state is per process: one RavenUi at a time, in sequence.

#include "platform/sdl3/Sdl3RavenHost.hpp"
#include "platform/sdl3/Sdl3Window.hpp"
#include "ui/RavenUi.hpp"

#include "engine/platform/Input.hpp"
#include "engine/platform/Platform.hpp"
#include "engine/platform/Window.hpp"

#include <Raven.h>
#include <SDL3/SDL.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using engine::platform::InputState;
using engine::platform::Key;
using engine::platform::MouseButton;
using engine::platform::Platform;
using engine::platform::Sdl3RavenHost;
using engine::platform::Window;
using engine::ui::RavenBackendKind;
using engine::ui::RavenUi;
using engine::ui::UiDesc;

namespace {

// A text field, a button and a label.
constexpr std::string_view kFormLayout = R"(<Window Title="host test" Width="320" Height="200">
  <VBox Spacing="4">
    <TextInput Name="name"/>
    <Button Name="apply" Text="Apply"/>
    <Label Name="status" Text="idle"/>
  </VBox>
</Window>)";

UiDesc formDesc()
{
    return UiDesc{.layoutXml = std::string(kFormLayout), .layoutName = "form"};
}

// The platform, a window of the dummy video driver, and optionally the UI.
struct Headless {
    std::optional<Platform> platform;
    std::optional<Window> window;
    std::unique_ptr<RavenUi> ui;
    SDL_Window* sdl = nullptr;
    InputState input;

    Sdl3RavenHost& host() const
    {
        auto* bridge = dynamic_cast<Sdl3RavenHost*>(ui->host());
        REQUIRE(bridge != nullptr);
        return *bridge;
    }
    [[nodiscard]] Rid main() const { return ui->mainWindow(); }

    // Drains SDL's queue through the platform layer.
    bool poll() { return platform->pollEvents(input).quitRequested; }
};

Headless startHeadless(bool withUi, const UiDesc& desc = formDesc())
{
    Headless h;
    auto platform = Platform::initialize();
    REQUIRE(platform.hasValue());
    h.platform.emplace(std::move(platform).value());
    auto window = Window::create(
        *h.platform,
        {.title = "lpe raven host test", .width = 320, .height = 200, .resizable = true});
    REQUIRE(window.hasValue());
    h.window.emplace(std::move(window).value());
    h.sdl = engine::platform::detail::NativeWindowAccess::handle(*h.window);
    REQUIRE(h.sdl != nullptr);
    if (withUi) {
        auto ui = RavenUi::create(*h.window, desc, RavenBackendKind::Null);
        REQUIRE(ui.hasValue());
        h.ui = std::move(ui).value();
        // Animations off: widget state is read right after an input.
        Rset_animations_enabled(R_FALSE);
        h.ui->frame();
    }
    return h;
}

SDL_WindowID windowId(const Headless& h)
{
    return SDL_GetWindowID(h.sdl);
}

void pushWindowEvent(const Headless& h, SDL_EventType type)
{
    SDL_Event event{};
    event.type = type;
    event.window.windowID = windowId(h);
    REQUIRE(SDL_PushEvent(&event));
}

void pushKey(const Headless& h, SDL_Keycode key, SDL_Scancode scancode, bool down,
             SDL_Keymod modifiers = SDL_KMOD_NONE)
{
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.windowID = windowId(h);
    event.key.key = key;
    event.key.scancode = scancode;
    event.key.mod = modifiers;
    event.key.down = down;
    REQUIRE(SDL_PushEvent(&event));
}

void pushButton(const Headless& h, std::uint8_t button, bool down, float x, float y)
{
    SDL_Event event{};
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.windowID = windowId(h);
    event.button.button = button;
    event.button.down = down;
    event.button.x = x;
    event.button.y = y;
    REQUIRE(SDL_PushEvent(&event));
}

void pushText(const Headless& h, const char* text)
{
    SDL_Event event{};
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.windowID = windowId(h);
    // A literal: valid for as long as SDL keeps the event.
    event.text.text = text;
    REQUIRE(SDL_PushEvent(&event));
}

void pushQuit()
{
    SDL_Event event{};
    event.type = SDL_EVENT_QUIT;
    REQUIRE(SDL_PushEvent(&event));
}

// The middle of a widget, in the window's points (pixels / pixel density).
struct Point {
    float x = 0.0F;
    float y = 0.0F;
};

Point centerOf(const Headless& h, Rid widget)
{
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    Rwidget_get_rect(widget, &x, &y, &width, &height);
    const float toPoints = h.ui->scale() / SDL_GetWindowPixelDensity(h.sdl);
    return {(x + width * 0.5F) * toPoints, (y + height * 0.5F) * toPoints};
}

void click(Headless& h, Rid widget)
{
    const Point at = centerOf(h, widget);
    pushButton(h, SDL_BUTTON_LEFT, true, at.x, at.y);
    pushButton(h, SDL_BUTTON_LEFT, false, at.x, at.y);
    (void)h.poll();
}

std::size_t ravenAllocations()
{
    RMemoryStats memory{};
    Rmemory_get_stats(&memory);
    return memory.allocation_count;
}

// Raven's key-ups per key, seen by the window (Rwindow_set_event_hook).
struct KeyUps {
    std::vector<RKey> keys;

    static int observe(Rid /*window*/, const REvent* event, void* user)
    {
        if (event->type == R_EVENT_KEY_UP) {
            static_cast<KeyUps*>(user)->keys.push_back(event->key);
        }
        return R_FALSE;
    }
    [[nodiscard]] std::size_t count(RKey key) const
    {
        std::size_t n = 0;
        for (const RKey k : keys) {
            n += k == key ? 1U : 0U;
        }
        return n;
    }
};

struct CloseCounter {
    int calls = 0;
    int answer = R_TRUE;

    static int onClose(Rid /*window*/, void* user)
    {
        auto* self = static_cast<CloseCounter*>(user);
        ++self->calls;
        return self->answer;
    }
};

} // namespace

TEST_CASE("SDL keys map to Raven keys by meaning and by position", "[ui][raven][integration]")
{
    using engine::platform::toRavenKey;
    // Latin letters, digits and punctuation by meaning in the layout.
    CHECK(toRavenKey(SDLK_A, SDL_SCANCODE_A) == R_KEY_A);
    CHECK(toRavenKey(SDLK_7, SDL_SCANCODE_7) == R_KEY_7);
    CHECK(toRavenKey(SDLK_SLASH, SDL_SCANCODE_SLASH) == R_KEY_SLASH);
    // AZERTY: the key at QWERTY's Q types A, and is A.
    CHECK(toRavenKey(SDLK_A, SDL_SCANCODE_Q) == R_KEY_A);
    // Cyrillic layout: the key that types U+0444 is A by position, so Ctrl+A works.
    CHECK(toRavenKey(static_cast<SDL_Keycode>(0x0444), SDL_SCANCODE_A) == R_KEY_A);
    // Everything else by position.
    CHECK(toRavenKey(SDLK_F5, SDL_SCANCODE_F5) == R_KEY_F5);
    CHECK(toRavenKey(SDLK_LEFT, SDL_SCANCODE_LEFT) == R_KEY_LEFT);
    CHECK(toRavenKey(SDLK_BACKSPACE, SDL_SCANCODE_BACKSPACE) == R_KEY_BACKSPACE);
    CHECK(toRavenKey(SDLK_LSHIFT, SDL_SCANCODE_LSHIFT) == R_KEY_LEFT_SHIFT);
    CHECK(toRavenKey(SDLK_RCTRL, SDL_SCANCODE_RCTRL) == R_KEY_RIGHT_CTRL);
    CHECK(toRavenKey(SDLK_KP_ENTER, SDL_SCANCODE_KP_ENTER) == R_KEY_KP_ENTER);
    CHECK(toRavenKey(SDLK_UNKNOWN, SDL_SCANCODE_UNKNOWN) == R_KEY_UNKNOWN);

    using engine::platform::toRavenModifiers;
    CHECK(toRavenModifiers(SDL_KMOD_NONE) == 0U);
    CHECK(toRavenModifiers(SDL_KMOD_LSHIFT) == R_MOD_SHIFT);
    CHECK(toRavenModifiers(static_cast<SDL_Keymod>(SDL_KMOD_RCTRL | SDL_KMOD_LALT)) ==
          (R_MOD_CTRL | R_MOD_ALT));
    CHECK(toRavenModifiers(SDL_KMOD_GUI) == R_MOD_SUPER);

    using engine::platform::toRavenButton;
    CHECK(toRavenButton(SDL_BUTTON_LEFT) == R_MOUSE_LEFT);
    CHECK(toRavenButton(SDL_BUTTON_MIDDLE) == R_MOUSE_MIDDLE);
    CHECK(toRavenButton(SDL_BUTTON_RIGHT) == R_MOUSE_RIGHT);
    CHECK(toRavenButton(SDL_BUTTON_X1) == R_MOUSE_X1);
    CHECK(toRavenButton(SDL_BUTTON_X2) == R_MOUSE_X2);
}

TEST_CASE("SDL text becomes code points; invalid bytes become U+FFFD", "[ui][raven][integration]")
{
    using engine::platform::decodeTextInput;
    CHECK(decodeTextInput("Ab1") == std::vector<char32_t>{U'A', U'b', U'1'});
    // Cyrillic (2 bytes), a CJK character (3 bytes), an emoji (4 bytes).
    CHECK(decodeTextInput("\xD0\x9F\xE4\xB8\xAD\xF0\x9F\x98\x80") ==
          std::vector<char32_t>{U'П', U'中', U'\U0001F600'});
    // A stray continuation byte, a truncated sequence, an overlong form.
    CHECK(decodeTextInput("a\x80"
                          "b") == std::vector<char32_t>{U'a', U'�', U'b'});
    CHECK(decodeTextInput("\xD0") == std::vector<char32_t>{U'�'});
    CHECK(decodeTextInput("\xC0\xAF") == std::vector<char32_t>{U'�', U'�'});
    CHECK(decodeTextInput("").empty());
}

TEST_CASE("With the Raven UI, the window's input goes to Raven and not to InputState",
          "[ui][raven][integration]")
{
    {
        Headless h = startHeadless(true);
        const Rid input = Rwidget_find(h.main(), "name");
        REQUIRE(input != R_INVALID_ID);
        pushWindowEvent(h, SDL_EVENT_WINDOW_FOCUS_GAINED);
        (void)h.poll();

        // A click gives the field the keyboard; the next frame turns SDL text input on.
        click(h, input);
        CHECK(Rwidget_is_focused(input) == R_TRUE);
        CHECK_FALSE(h.input.isDown(MouseButton::Left));
        CHECK_FALSE(h.input.wasPressed(MouseButton::Left));
        h.ui->frame();
        CHECK(SDL_TextInputActive(h.sdl));

        // UTF-8 text, then an editing key: Raven's field changes, InputState does not.
        pushText(h, "Raven \xD0\x9F\xF0\x9F\x98\x80");
        (void)h.poll();
        CHECK(std::string_view{Rtext_input_get_text(input)} == "Raven \xD0\x9F\xF0\x9F\x98\x80");
        pushKey(h, SDLK_BACKSPACE, SDL_SCANCODE_BACKSPACE, true);
        pushKey(h, SDLK_BACKSPACE, SDL_SCANCODE_BACKSPACE, false);
        (void)h.poll();
        CHECK(std::string_view{Rtext_input_get_text(input)} == "Raven \xD0\x9F");
        CHECK_FALSE(h.input.isDown(Key::Backspace));
        CHECK_FALSE(h.input.wasPressed(Key::Backspace));

        // A key Raven does not know is still the UI's.
        pushKey(h, SDLK_F13, SDL_SCANCODE_F13, true);
        (void)h.poll();
        CHECK(h.host().stats().codepoints == 8);
        CHECK(h.ui->stats().ravenErrors == 0);
    }
    CHECK_FALSE(RavenUi::exists());
    CHECK(ravenAllocations() == 0);
}

TEST_CASE("Without a UI, the same events reach InputState", "[ui][raven][integration]")
{
    Headless h = startHeadless(false);
    pushKey(h, SDLK_W, SDL_SCANCODE_W, true);
    pushButton(h, SDL_BUTTON_LEFT, true, 10.0F, 10.0F);
    (void)h.poll();
    CHECK(h.input.isDown(Key::W));
    CHECK(h.input.isDown(MouseButton::Left));
    CHECK_FALSE(RavenUi::exists());
}

TEST_CASE("Focus loss releases held keys and buttons once, modifiers last",
          "[ui][raven][integration]")
{
    Headless h = startHeadless(true);
    KeyUps ups;
    Rwindow_set_event_hook(h.main(), &KeyUps::observe, &ups);
    pushWindowEvent(h, SDL_EVENT_WINDOW_FOCUS_GAINED);
    pushKey(h, SDLK_W, SDL_SCANCODE_W, true);
    pushKey(h, SDLK_LSHIFT, SDL_SCANCODE_LSHIFT, true, SDL_KMOD_LSHIFT);
    pushKey(h, SDLK_D, SDL_SCANCODE_D, true, SDL_KMOD_LSHIFT);
    pushButton(h, SDL_BUTTON_RIGHT, true, 20.0F, 20.0F);
    (void)h.poll();
    CHECK(h.host().heldKeys() == 3);
    CHECK(h.host().heldButtons() == 1);
    CHECK(Rwindow_is_key_down(h.main(), R_KEY_W) == R_TRUE);
    CHECK(Rwindow_is_mouse_button_down(h.main(), R_MOUSE_RIGHT) == R_TRUE);

    SECTION("nothing released by SDL: the host releases everything")
    {
        pushWindowEvent(h, SDL_EVENT_WINDOW_FOCUS_LOST);
        (void)h.poll();
        CHECK(h.host().stats().releasedKeys == 3);
        CHECK(h.host().stats().releasedButtons == 1);
        // The modifier came last.
        REQUIRE(ups.keys.size() == 3);
        CHECK(ups.keys.back() == R_KEY_LEFT_SHIFT);
    }
    SECTION("SDL released some keys itself: those are not released twice")
    {
        // On a real focus loss SDL sends key-ups for held keys, but no button-up.
        pushKey(h, SDLK_W, SDL_SCANCODE_W, false, SDL_KMOD_LSHIFT);
        pushKey(h, SDLK_LSHIFT, SDL_SCANCODE_LSHIFT, false);
        pushWindowEvent(h, SDL_EVENT_WINDOW_FOCUS_LOST);
        (void)h.poll();
        CHECK(h.host().stats().releasedKeys == 1);
        CHECK(h.host().stats().releasedButtons == 1);
        CHECK(ups.count(R_KEY_W) == 1);
        CHECK(ups.count(R_KEY_LEFT_SHIFT) == 1);
        CHECK(ups.count(R_KEY_D) == 1);
    }

    // Nothing is held anywhere afterwards: Raven, the host, InputState.
    CHECK(h.host().heldKeys() == 0);
    CHECK(h.host().heldButtons() == 0);
    CHECK(Rwindow_is_key_down(h.main(), R_KEY_W) == R_FALSE);
    CHECK(Rwindow_is_key_down(h.main(), R_KEY_LEFT_SHIFT) == R_FALSE);
    CHECK(Rwindow_is_key_down(h.main(), R_KEY_D) == R_FALSE);
    CHECK(Rwindow_is_mouse_button_down(h.main(), R_MOUSE_RIGHT) == R_FALSE);
    CHECK_FALSE(h.input.isDown(Key::W));
    CHECK_FALSE(h.input.isDown(MouseButton::Right));
    CHECK(h.ui->stats().ravenErrors == 0);
}

TEST_CASE("A close request reaches Raven once; the quit after it is the same request",
          "[ui][raven][integration]")
{
    Headless h = startHeadless(true);
    CloseCounter close;

    SECTION("accepted: the window closes, the handles are not used again, the loop ends")
    {
        Rwindow_set_close_callback(h.main(), &CloseCounter::onClose, &close);
        pushWindowEvent(h, SDL_EVENT_WINDOW_CLOSE_REQUESTED);
        pushQuit();
        const bool quit = h.poll();
        CHECK(close.calls == 1);
        CHECK(h.host().stats().closeRequests == 1);
        CHECK_FALSE(h.ui->isOpen());
        CHECK(h.host().attachedWindow() == R_INVALID_ID);
        CHECK(h.ui->mainWindow() == engine::ui::kInvalidRavenId);
        // The quit was not given to a window Raven no longer has.
        CHECK(quit);

        // Events after the close go to InputState; nothing uses the dead handles.
        pushKey(h, SDLK_W, SDL_SCANCODE_W, true);
        pushButton(h, SDL_BUTTON_LEFT, true, 5.0F, 5.0F);
        (void)h.poll();
        CHECK(h.input.isDown(Key::W));
        h.ui->frame();
        CHECK(h.ui->stats().ravenErrors == 0);
    }
    SECTION("refused: the window stays, and the quit after the request is not a second one")
    {
        close.answer = R_FALSE;
        Rwindow_set_close_callback(h.main(), &CloseCounter::onClose, &close);
        pushWindowEvent(h, SDL_EVENT_WINDOW_CLOSE_REQUESTED);
        pushQuit();
        CHECK_FALSE(h.poll());
        CHECK(close.calls == 1);
        CHECK(h.ui->isOpen());
        // A quit on its own (a signal, the session ending) is a new request.
        pushQuit();
        CHECK_FALSE(h.poll());
        CHECK(close.calls == 2);
        CHECK(h.ui->isOpen());
        CHECK(h.ui->stats().ravenErrors == 0);
    }
    SECTION("a quit on its own closes the window")
    {
        Rwindow_set_close_callback(h.main(), &CloseCounter::onClose, &close);
        pushQuit();
        (void)h.poll();
        CHECK(close.calls == 1);
        CHECK_FALSE(h.ui->isOpen());
    }
}

TEST_CASE("The host gives Raven the window's pixel size and display scale",
          "[ui][raven][integration]")
{
    Headless h = startHeadless(true);
    int sdlWidth = 0;
    int sdlHeight = 0;
    REQUIRE(SDL_GetWindowSizeInPixels(h.sdl, &sdlWidth, &sdlHeight));
    int width = 0;
    int height = 0;
    Rwindow_get_framebuffer_size(h.main(), &width, &height);
    CHECK(width == sdlWidth);
    CHECK(height == sdlHeight);
    CHECK(Rwindow_get_scale(h.main()) == SDL_GetWindowDisplayScale(h.sdl));
    CHECK(h.ui->scale() == SDL_GetWindowDisplayScale(h.sdl));

    // A new size reaches Raven through SDL's size event.
    REQUIRE(SDL_SetWindowSize(h.sdl, 500, 300));
    REQUIRE(SDL_SyncWindow(h.sdl));
    pushWindowEvent(h, SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED);
    (void)h.poll();
    REQUIRE(SDL_GetWindowSizeInPixels(h.sdl, &sdlWidth, &sdlHeight));
    Rwindow_get_framebuffer_size(h.main(), &width, &height);
    CHECK(sdlWidth == 500);
    CHECK(width == sdlWidth);
    CHECK(height == sdlHeight);
    // A display scale change is read from SDL again.
    const auto resizes = h.host().stats().resizes;
    pushWindowEvent(h, SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED);
    (void)h.poll();
    CHECK(h.host().stats().resizes == resizes + 1);
}

TEST_CASE("The IME area follows Raven's caret; text input is on only in a text field",
          "[ui][raven][integration]")
{
    Headless h = startHeadless(true);
    const Rid input = Rwidget_find(h.main(), "name");
    const Rid apply = Rwidget_find(h.main(), "apply");
    pushWindowEvent(h, SDL_EVENT_WINDOW_FOCUS_GAINED);
    (void)h.poll();
    h.ui->frame();
    CHECK_FALSE(SDL_TextInputActive(h.sdl));
    CHECK_FALSE(h.host().textInputArea().has_value());

    click(h, input);
    h.ui->frame();
    REQUIRE(SDL_TextInputActive(h.sdl));
    REQUIRE(h.host().textInputArea().has_value());
    // What SDL holds is Raven's caret, in the window's points.
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    REQUIRE(Rwindow_get_text_input_rect(h.main(), &x, &y, &width, &height) == R_TRUE);
    SDL_Rect area{};
    int cursor = -1;
    REQUIRE(SDL_GetTextInputArea(h.sdl, &area, &cursor));
    const float density = SDL_GetWindowPixelDensity(h.sdl);
    CHECK(area.x == static_cast<int>(std::floor(static_cast<float>(x) / density)));
    CHECK(area.y == static_cast<int>(std::floor(static_cast<float>(y) / density)));
    CHECK(area.h >= 1);
    CHECK(cursor == 0);

    // Typing moves the caret, and the area with it.
    const int firstX = area.x;
    pushText(h, "WWWW");
    (void)h.poll();
    h.ui->frame();
    REQUIRE(SDL_GetTextInputArea(h.sdl, &area, &cursor));
    CHECK(area.x > firstX);
    CHECK(h.host().stats().textInputAreas >= 2);

    // Another widget takes the keyboard: SDL text input is off again.
    click(h, apply);
    h.ui->frame();
    CHECK_FALSE(SDL_TextInputActive(h.sdl));
    CHECK_FALSE(h.host().textInputArea().has_value());
    CHECK(h.ui->stats().ravenErrors == 0);
}

TEST_CASE("The UI's lifetime: one at a time, switches off, nothing left after shutdown",
          "[ui][raven][integration]")
{
    auto platform = Platform::initialize();
    REQUIRE(platform.hasValue());
    auto window = Window::create(platform.value(), {.title = "lpe raven lifetime test"});
    REQUIRE(window.hasValue());

    SECTION("started: live reload and auto-loading are off before the first document")
    {
        auto ui = RavenUi::create(window.value(), formDesc(), RavenBackendKind::Null);
        REQUIRE(ui.hasValue());
        CHECK(RavenUi::exists());
        CHECK(Ris_initialized() == R_TRUE);
        CHECK(Rhost_platform_is_active() == R_TRUE);
        CHECK(Rxml_get_live_reload() == R_FALSE);
        CHECK(Rxml_get_auto_load() == R_FALSE);
        CHECK(ui.value()->isOpen());

        // A second UI is refused, and the first is untouched.
        auto second = RavenUi::create(window.value(), formDesc(), RavenBackendKind::Null);
        REQUIRE_FALSE(second.hasValue());
        CHECK(second.error().message.find("already running") != std::string::npos);
        CHECK(ui.value()->isOpen());

        ui.value()->frame();
        const engine::ui::UiRunStats stats = ui.value()->shutdown();
        CHECK(stats.enabled);
        CHECK(stats.frames == 1);
        CHECK(stats.ravenErrors == 0);
        CHECK(stats.blocksAfterShutdown == 0);
        CHECK_FALSE(RavenUi::exists());
        CHECK(Ris_initialized() == R_FALSE);
        CHECK(Rhost_platform_is_active() == R_FALSE);
        // Shutting down again changes nothing.
        CHECK(ui.value()->shutdown().blocksAfterShutdown == 0);
        CHECK_FALSE(ui.value()->isOpen());
    }
    SECTION("a layout Raven cannot build: an error naming it, nothing left running")
    {
        const UiDesc broken{.layoutXml = "<Window><NoSuchWidget/></Window>",
                            .layoutName = "broken.xml"};
        auto ui = RavenUi::create(window.value(), broken, RavenBackendKind::Null);
        REQUIRE_FALSE(ui.hasValue());
        CHECK(ui.error().message.find("broken.xml") != std::string::npos);
        CHECK(ui.error().message.find("NoSuchWidget") != std::string::npos);
        CHECK_FALSE(RavenUi::exists());
        CHECK(Ris_initialized() == R_FALSE);
        CHECK(ravenAllocations() == 0);
    }
    SECTION("a layout whose root is not a window")
    {
        const UiDesc notWindow{.layoutXml = "<VBox/>", .layoutName = "panel.xml"};
        auto ui = RavenUi::create(window.value(), notWindow, RavenBackendKind::Null);
        REQUIRE_FALSE(ui.hasValue());
        CHECK(ui.error().message.find("panel.xml") != std::string::npos);
        CHECK(ravenAllocations() == 0);
    }
    SECTION("start and stop again and again")
    {
        for (int cycle = 0; cycle < 4; ++cycle) {
            INFO("cycle " << cycle);
            auto ui = RavenUi::create(window.value(), formDesc(), RavenBackendKind::Null);
            REQUIRE(ui.hasValue());
            ui.value()->frame();
            ui.value()->frame();
            CHECK(ui.value()->stats().frames == 2);
            ui.value().reset();
            CHECK_FALSE(RavenUi::exists());
            CHECK(Ris_initialized() == R_FALSE);
            CHECK(ravenAllocations() == 0);
        }
    }
    // After each section a new UI starts as usual.
    auto again = RavenUi::create(window.value(), formDesc(), RavenBackendKind::Null);
    REQUIRE(again.hasValue());
    CHECK(again.value()->isOpen());
}
