// Platform layer against real SDL, using the "dummy" video driver that CTest
// selects through SDL_VIDEO_DRIVER (tests/CMakeLists.txt). No display or GPU.

#include "engine/platform/GlContext.hpp"
#include "engine/platform/Platform.hpp"
#include "engine/platform/Window.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>

using engine::platform::GlContext;
using engine::platform::GlContextDesc;
using engine::platform::Platform;
using engine::platform::Window;
using engine::platform::WindowDesc;

TEST_CASE("Platform initializes and reports no quit without input", "[platform][integration]")
{
    auto platform = Platform::initialize();
    REQUIRE(platform.hasValue());
    engine::platform::InputState input;
    CHECK_FALSE(platform.value().pollEvents(input).quitRequested);
}

TEST_CASE("A second Platform keeps working after the first one is destroyed",
          "[platform][integration]")
{
    auto second = Platform::initialize();
    REQUIRE(second.hasValue());
    {
        auto first = Platform::initialize();
        REQUIRE(first.hasValue());
    }
    // The video subsystem is reference counted, so it must still be up.
    auto window = Window::create(second.value(), WindowDesc{});
    CHECK(window.hasValue());
}

TEST_CASE("Moving a Platform transfers ownership", "[platform][integration]")
{
    auto platform = Platform::initialize();
    REQUIRE(platform.hasValue());
    Platform moved = std::move(platform).value();
    auto window = Window::create(moved, WindowDesc{});
    CHECK(window.hasValue());
}

TEST_CASE("Window::create opens a window with the requested pixel size", "[platform][integration]")
{
    auto platform = Platform::initialize();
    REQUIRE(platform.hasValue());

    auto window = Window::create(platform.value(), WindowDesc{.width = 640, .height = 360});
    REQUIRE(window.hasValue());
    const auto size = window.value().pixelSize();
    CHECK(size.width == 640);
    CHECK(size.height == 360);
}

TEST_CASE("Window::create rejects an invalid description", "[platform][integration]")
{
    auto platform = Platform::initialize();
    REQUIRE(platform.hasValue());

    auto window = Window::create(platform.value(), WindowDesc{.width = 0});
    REQUIRE_FALSE(window.hasValue());
    CHECK(window.error().message.find("width") != std::string::npos);
}

TEST_CASE("GlContext::create refuses a window created without OpenGL", "[platform][integration]")
{
    auto platform = Platform::initialize();
    REQUIRE(platform.hasValue());
    auto window = Window::create(platform.value(), WindowDesc{.openGl = false});
    REQUIRE(window.hasValue());

    auto context = GlContext::create(window.value(), GlContextDesc{});
    REQUIRE_FALSE(context.hasValue());
    CHECK(context.error().message.find("openGl") != std::string::npos);
}
