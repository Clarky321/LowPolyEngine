#include "engine/core/FrameClock.hpp"

#include <algorithm>
#include <cmath>

namespace engine::core {

namespace {

constexpr double kDefaultStatsWindowSeconds = 0.5;

double positiveOr(double value, double fallback) noexcept
{
    return (std::isfinite(value) && value > 0.0) ? value : fallback;
}

} // namespace

FrameClock::FrameClock(double maxDeltaSeconds) noexcept
    : maxDeltaSeconds_(positiveOr(maxDeltaSeconds, kDefaultMaxFrameDeltaSeconds))
{
}

FrameTime FrameClock::tick(TimePoint now) noexcept
{
    FrameTime time;
    if (previous_) {
        time.frameIndex = last_.frameIndex + 1;
        if (now > *previous_) {
            time.rawDeltaSeconds = std::chrono::duration<double>(now - *previous_).count();
        }
        time.deltaSeconds = std::min(time.rawDeltaSeconds, maxDeltaSeconds_);
        time.elapsedSeconds = last_.elapsedSeconds + time.deltaSeconds;
    }
    // Keep the latest time point even if it went backwards, so the next delta
    // is measured from it instead of growing from an older point.
    previous_ = now;
    last_ = time;
    return time;
}

FrameStats::FrameStats(double windowSeconds) noexcept
    : windowSeconds_(positiveOr(windowSeconds, kDefaultStatsWindowSeconds))
{
}

bool FrameStats::addFrame(double rawDeltaSeconds) noexcept
{
    if (!std::isfinite(rawDeltaSeconds) || rawDeltaSeconds <= 0.0) {
        return false;
    }
    accumulatedSeconds_ += rawDeltaSeconds;
    maxFrameSeconds_ = std::max(maxFrameSeconds_, rawDeltaSeconds);
    ++frameCount_;
    if (accumulatedSeconds_ < windowSeconds_) {
        return false;
    }

    const double frames = static_cast<double>(frameCount_);
    latest_.framesPerSecond = frames / accumulatedSeconds_;
    latest_.averageFrameMs = accumulatedSeconds_ * 1000.0 / frames;
    latest_.maxFrameMs = maxFrameSeconds_ * 1000.0;
    accumulatedSeconds_ = 0.0;
    maxFrameSeconds_ = 0.0;
    frameCount_ = 0;
    return true;
}

} // namespace engine::core
