#include "lc1/input.hpp"

namespace lc1 {
namespace {

constexpr std::size_t index_of(Key key)
{
    return static_cast<std::size_t>(key);
}

} // namespace

bool InputState::flag(std::array<bool, key_count> const &flags, Key key)
{
    auto const index = index_of(key);
    // A Key built by a cast is not something the enum rules out, and an out-of-range index
    // would be a silent out-of-bounds write in on_key. Treat it as never pressed.
    return index < key_count && flags[index];
}

void InputState::begin_frame()
{
    pressed_.fill(false);
    released_.fill(false);
    cursor_delta_ = glm::vec2{0.0F};
    scroll_ = glm::vec2{0.0F};
}

void InputState::on_key(Key key, bool is_down)
{
    auto const index = index_of(key);
    if (index >= key_count)
        return;

    if (is_down) {
        // GLFW cannot deliver two presses without a release between them, but the guard is
        // what keeps "pressed means this frame's transition" true if that ever changes.
        if (!down_[index])
            pressed_[index] = true;
        down_[index] = true;
    }
    else {
        if (down_[index])
            released_[index] = true;
        down_[index] = false;
    }
}

void InputState::on_cursor_position(glm::vec2 position)
{
    // The first position after construction, capture or focus is a baseline: the pointer
    // travelled while nothing was tracking it, and that distance is not camera motion.
    if (cursor_valid_)
        cursor_delta_ += position - cursor_;

    cursor_ = position;
    cursor_valid_ = true;
}

void InputState::on_scroll(glm::vec2 offset)
{
    scroll_ += offset;
}

void InputState::invalidate_cursor()
{
    cursor_valid_ = false;
    // Also drop what the pointer accumulated before the discontinuity: the caller has just
    // declared that this motion was not tracked, and keeping it would apply exactly the
    // jump the baseline exists to prevent -- on this frame rather than the next.
    cursor_delta_ = glm::vec2{0.0F};
}

void InputState::release_all()
{
    for (std::size_t index = 0; index < key_count; ++index) {
        if (!down_[index])
            continue;

        down_[index] = false;
        released_[index] = true;
    }
}

} // namespace lc1

namespace lc1 {

bool InputState::down(Key key) const
{
    return flag(down_, key);
}

bool InputState::pressed(Key key) const
{
    return flag(pressed_, key);
}

bool InputState::released(Key key) const
{
    return flag(released_, key);
}

glm::vec2 InputState::cursor() const
{
    return cursor_;
}

glm::vec2 InputState::cursor_delta() const
{
    return cursor_delta_;
}

glm::vec2 InputState::scroll() const
{
    return scroll_;
}

} // namespace lc1
