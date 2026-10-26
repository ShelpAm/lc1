#pragma once

#include <chrono>

namespace lc1 {

class Stopwatch {
  public:
    Stopwatch();

    void restart();

    /// @return Delta time since last tick.
    float tick();

    // In seconds
    float delta_time() const;
    // In seconds
    float total_time() const;
    int frame_count() const;
    float fps() const;

  private:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Duration = Clock::duration;

    TimePoint last_tick_;
    Duration delta_time_{};
    Duration total_time_{};
    Duration fps_accumulator_{};
    int frame_count_ = 0;
    int fps_frames_ = 0;
    float fps_ = 0.F;
    static constexpr Duration fps_update_interval = std::chrono::milliseconds{500};
};

} // namespace lc1
