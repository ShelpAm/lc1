#include "lc1/game-clock.hpp"

namespace lc1 {

Stopwatch::Stopwatch()
{
    last_tick_ = Clock::now();
}

void Stopwatch::restart()
{
    last_tick_ = Clock::now();
    delta_time_ = Duration::zero();
    total_time_ = Duration::zero();
    fps_accumulator_ = Duration::zero();
    frame_count_ = 0;
}

float Stopwatch::tick()
{
    auto now = Clock::now();
    delta_time_ = now - last_tick_;
    last_tick_ = now;

    total_time_ += delta_time_;
    ++frame_count_;

    fps_accumulator_ += delta_time_;
    ++fps_frames_;
    if (fps_accumulator_ >= fps_update_interval) {
        fps_ = static_cast<float>(fps_frames_) /
               std::chrono::duration<float>(fps_accumulator_).count();
        fps_accumulator_ = Duration::zero();
        fps_frames_ = 0;
    }

    return std::chrono::duration<float>(delta_time_).count();
}

} // namespace lc1

namespace lc1 {

float Stopwatch::delta_time() const
{
    return std::chrono::duration<float>(delta_time_).count();
}

float Stopwatch::total_time() const
{
    return std::chrono::duration<float>(total_time_).count();
}

int Stopwatch::frame_count() const
{
    return frame_count_;
}

float Stopwatch::fps() const
{
    return fps_;
}

} // namespace lc1
