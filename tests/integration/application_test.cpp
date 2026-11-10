// Application startup against SDL's "dummy" video driver (set by CTest). The
// dummy driver has no OpenGL, so run() must fail cleanly before the first frame.

#include "engine/app/Application.hpp"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

namespace {

class CountingApp final : public engine::app::Application {
public:
    std::optional<engine::core::Error> start(engine::app::AppContext& /*context*/) override
    {
        ++starts;
        return std::nullopt;
    }
    void update(engine::app::AppContext& /*context*/,
                const engine::core::FrameTime& /*time*/) override
    {
        ++updates;
    }
    void render(engine::app::AppContext& /*context*/) override { ++renders; }
    void stop(engine::app::AppContext& /*context*/) override { ++stops; }

    int starts = 0;
    int updates = 0;
    int renders = 0;
    int stops = 0;
};

} // namespace

TEST_CASE("run() reports a startup failure without calling the application", "[app][integration]")
{
    CountingApp app;
    const auto result = engine::app::run(app, engine::app::ApplicationDesc{.maxFrames = 1});
    REQUIRE_FALSE(result.hasValue());
    // The message names the step that failed.
    const std::string& message = result.error().message;
    CHECK((message.find("window") != std::string::npos ||
           message.find("OpenGL") != std::string::npos));
    CHECK(app.starts == 0);
    CHECK(app.updates == 0);
    CHECK(app.renders == 0);
    CHECK(app.stops == 0);
}

TEST_CASE("run() rejects an invalid window description", "[app][integration]")
{
    CountingApp app;
    engine::app::ApplicationDesc desc;
    desc.window.width = 0;
    const auto result = engine::app::run(app, desc);
    REQUIRE_FALSE(result.hasValue());
    CHECK(result.error().message.find("width") != std::string::npos);
    CHECK(app.starts == 0);
}
