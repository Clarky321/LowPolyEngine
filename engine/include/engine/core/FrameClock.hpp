#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace engine::core {

// Longest simulation step. A longer frame (debugger break, window drag, loading
// hitch) is shortened to this, so movement does not jump afterwards.
inline constexpr double kDefaultMaxFrameDeltaSeconds = 0.1;

struct FrameTime {
    // Time since the previous frame, clamped to the clock's maximum. Use it for
    // simulation and movement.
    double deltaSeconds = 0.0;
    // Measured time since the previous frame, not clamped. Use it for statistics.
    double rawDeltaSeconds = 0.0;
    // Sum of deltaSeconds: simulation time, which does not jump after a pause.
    double elapsedSeconds = 0.0;
    // 0 for the first frame.
    std::uint64_t frameIndex = 0;
};

// Turns time points into per-frame timing. Time is passed in instead of read
// inside, so the clock is deterministic in tests.
class FrameClock {
public:
    using TimePoint = std::chrono::steady_clock::time_point;

    // A non-positive or non-finite maximum falls back to kDefaultMaxFrameDeltaSeconds.
    explicit FrameClock(double maxDeltaSeconds = kDefaultMaxFrameDeltaSeconds) noexcept;

    // Starts the next frame at `now`. The first frame has a zero delta. A time
    // point earlier than the previous one also gives a zero delta.
    FrameTime tick(TimePoint now) noexcept;

    [[nodiscard]] double maxDeltaSeconds() const noexcept { return maxDeltaSeconds_; }

private:
    double maxDeltaSeconds_;
    std::optional<TimePoint> previous_;
    FrameTime last_;
};

struct FrameStatsSnapshot {
    double framesPerSecond = 0.0;
    double averageFrameMs = 0.0;
    // Slowest frame of the window: shows hitches that the average hides.
    double maxFrameMs = 0.0;
};

// Frame rate and frame time averaged over a time window (default 0.5 s), so
// the numbers are stable enough to read on screen.
class FrameStats {
public:
    // A non-positive or non-finite window falls back to 0.5 s.
    explicit FrameStats(double windowSeconds = 0.5) noexcept;

    // Adds one frame's unclamped duration. Zero, negative and non-finite
    // durations (e.g. the first frame) are ignored. Returns true when a window
    // completed and latest() changed.
    bool addFrame(double rawDeltaSeconds) noexcept;

    // All zero until the first window completes.
    [[nodiscard]] const FrameStatsSnapshot& latest() const noexcept { return latest_; }

private:
    double windowSeconds_;
    double accumulatedSeconds_ = 0.0;
    double maxFrameSeconds_ = 0.0;
    std::uint32_t frameCount_ = 0;
    FrameStatsSnapshot latest_;
};

} // namespace engine::core
