#include "engine/core/FrameClock.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <limits>

using Catch::Approx;
using engine::core::FrameClock;
using engine::core::FrameStats;
using namespace std::chrono_literals;

namespace {

// Any fixed origin works: the clock only looks at differences.
const FrameClock::TimePoint kStart = FrameClock::TimePoint{} + 1000s;

} // namespace

TEST_CASE("The first frame has zero delta and index 0", "[core][clock]")
{
    FrameClock clock;
    const auto time = clock.tick(kStart);
    CHECK(time.frameIndex == 0);
    CHECK(time.deltaSeconds == 0.0);
    CHECK(time.rawDeltaSeconds == 0.0);
    CHECK(time.elapsedSeconds == 0.0);
}

TEST_CASE("Frame delta and elapsed time follow the time points", "[core][clock]")
{
    FrameClock clock;
    (void)clock.tick(kStart);
    const auto second = clock.tick(kStart + 16ms);
    CHECK(second.frameIndex == 1);
    CHECK(second.deltaSeconds == Approx(0.016));
    CHECK(second.rawDeltaSeconds == Approx(0.016));
    CHECK(second.elapsedSeconds == Approx(0.016));

    const auto third = clock.tick(kStart + 50ms);
    CHECK(third.frameIndex == 2);
    CHECK(third.deltaSeconds == Approx(0.034));
    CHECK(third.elapsedSeconds == Approx(0.050));
}

TEST_CASE("A long pause is clamped to the maximum delta", "[core][clock]")
{
    // E.g. the process stopped in a debugger for 30 seconds.
    FrameClock clock(0.1);
    (void)clock.tick(kStart);
    (void)clock.tick(kStart + 10ms);
    const auto afterPause = clock.tick(kStart + 10ms + 30s);
    CHECK(afterPause.deltaSeconds == Approx(0.1));
    CHECK(afterPause.rawDeltaSeconds == Approx(30.0));
    // Simulation time grows by the clamped step only.
    CHECK(afterPause.elapsedSeconds == Approx(0.11));

    // The next frame is measured from the end of the pause, not clamped again.
    const auto next = clock.tick(kStart + 10ms + 30s + 20ms);
    CHECK(next.deltaSeconds == Approx(0.020));
}

TEST_CASE("A time point earlier than the previous one gives a zero delta", "[core][clock]")
{
    FrameClock clock;
    (void)clock.tick(kStart + 1s);
    const auto backwards = clock.tick(kStart);
    CHECK(backwards.deltaSeconds == 0.0);
    CHECK(backwards.rawDeltaSeconds == 0.0);
    CHECK(backwards.frameIndex == 1);
}

TEST_CASE("An invalid maximum delta falls back to the default", "[core][clock]")
{
    CHECK(FrameClock(0.0).maxDeltaSeconds() == engine::core::kDefaultMaxFrameDeltaSeconds);
    CHECK(FrameClock(-1.0).maxDeltaSeconds() == engine::core::kDefaultMaxFrameDeltaSeconds);
    CHECK(FrameClock(std::numeric_limits<double>::quiet_NaN()).maxDeltaSeconds() ==
          engine::core::kDefaultMaxFrameDeltaSeconds);
    CHECK(FrameClock(0.25).maxDeltaSeconds() == 0.25);
}

TEST_CASE("FrameStats publishes averages once per window", "[core][clock][stats]")
{
    FrameStats stats(0.5);
    // 60 frames at 120 Hz = 0.5 s: the window completes on the last frame.
    for (int i = 0; i < 59; ++i) {
        CHECK_FALSE(stats.addFrame(1.0 / 120.0));
    }
    CHECK(stats.latest().framesPerSecond == 0.0);
    CHECK(stats.addFrame(1.0 / 120.0));
    CHECK(stats.latest().framesPerSecond == Approx(120.0));
    CHECK(stats.latest().averageFrameMs == Approx(1000.0 / 120.0));
    CHECK(stats.latest().maxFrameMs == Approx(1000.0 / 120.0));
}

TEST_CASE("FrameStats reports the slowest frame of the window", "[core][clock][stats]")
{
    FrameStats stats(0.5);
    (void)stats.addFrame(0.010);
    (void)stats.addFrame(0.100);
    bool published = false;
    while (!published) {
        published = stats.addFrame(0.010);
    }
    CHECK(stats.latest().maxFrameMs == Approx(100.0));
    CHECK(stats.latest().averageFrameMs > 10.0);
}

TEST_CASE("FrameStats ignores zero, negative and non-finite durations", "[core][clock][stats]")
{
    FrameStats stats(0.5);
    CHECK_FALSE(stats.addFrame(0.0));
    CHECK_FALSE(stats.addFrame(-1.0));
    CHECK_FALSE(stats.addFrame(std::numeric_limits<double>::infinity()));
    CHECK_FALSE(stats.addFrame(std::numeric_limits<double>::quiet_NaN()));
    // One valid frame that fills the window on its own.
    CHECK(stats.addFrame(0.5));
    CHECK(stats.latest().framesPerSecond == Approx(2.0));
}
