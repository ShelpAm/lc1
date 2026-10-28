#include "lc1/scene/animation.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace lc1::scene {
namespace {
template <typename T> bool finite(T const &value)
{
    for (glm::length_t i = 0; i < value.length(); ++i)
        if (!std::isfinite(value[i]))
            return false;
    return true;
}
} // namespace

float AnimationClip::duration() const
{
    float end = 0.0F;
    for (auto const &channel : channels)
        if (!channel.times.empty())
            end = std::max(end, channel.times.back());
    return end;
}

void AnimationClip::validate(std::size_t node_count) const
{
    if (channels.empty())
        fail("animation clip must contain a channel");
    std::set<std::pair<std::size_t, AnimationChannel::Path>> targets;
    for (auto const &channel : channels) {
        if (channel.node >= node_count)
            fail("animation target node is out of range");
        if (channel.path != AnimationChannel::Path::Translation &&
            channel.path != AnimationChannel::Path::Rotation &&
            channel.path != AnimationChannel::Path::Scale)
            fail("unsupported animation channel path");
        if (channel.interpolation != AnimationChannel::Interpolation::Step &&
            channel.interpolation != AnimationChannel::Interpolation::Linear)
            fail("unsupported animation interpolation");
        if (!targets.emplace(channel.node, channel.path).second)
            fail("animation channels must not write the same node property");
        if (channel.times.empty())
            fail("animation channel must contain a keyframe");
        float previous = -1.0F;
        for (auto const time : channel.times) {
            if (!std::isfinite(time) || time < 0.0F || time <= previous)
                fail(
                    "animation keyframe times must be finite, nonnegative and strictly increasing");
            previous = time;
        }
        bool const rotation = channel.path == AnimationChannel::Path::Rotation;
        if (rotation != std::holds_alternative<std::vector<glm::quat>>(channel.values))
            fail("animation channel value type does not match its path");
        std::visit(
            [&](auto const &values) {
                if (values.size() != channel.times.size())
                    fail("animation keyframe time and value counts differ");
                for (auto const &value : values)
                    if (!rotation && !finite(value))
                        fail("animation keyframe values must be finite");
            },
            channel.values);
        if (rotation) {
            for (auto const value : std::get<std::vector<glm::quat>>(channel.values)) {
                float const length = glm::length(value);
                if (!std::isfinite(length) || length == 0.0F)
                    fail("animation rotation keyframe must have a finite nonzero length");
            }
        }
        else if (channel.path == AnimationChannel::Path::Scale) {
            for (auto const value : std::get<std::vector<glm::vec3>>(channel.values))
                if (value.x == 0.0F || value.y == 0.0F || value.z == 0.0F)
                    fail("animation scale keyframe must be invertible");
        }
    }
}

void AnimationClip::sample(float time, std::span<NodeTransform> pose) const
{
    for (auto const &channel : channels) {
        auto const next = std::upper_bound(channel.times.begin(), channel.times.end(), time);
        auto const right = static_cast<std::size_t>(next - channel.times.begin());
        std::size_t const left = right == 0 ? 0 : right - 1;
        std::size_t const end = std::min(right, channel.times.size() - 1);
        float const factor =
            left == end || channel.interpolation == AnimationChannel::Interpolation::Step
                ? 0.0F
                : (time - channel.times[left]) / (channel.times[end] - channel.times[left]);
        auto &transform = pose[channel.node];
        if (channel.path == AnimationChannel::Path::Rotation) {
            auto const &values = std::get<std::vector<glm::quat>>(channel.values);
            auto const first = glm::normalize(values[left]);
            auto const last = glm::normalize(values[end]);
            transform.rotation = glm::normalize(glm::slerp(first, last, factor));
        }
        else {
            auto const &values = std::get<std::vector<glm::vec3>>(channel.values);
            auto const value = glm::mix(values[left], values[end], factor);
            if (channel.path == AnimationChannel::Path::Translation)
                transform.translation = value;
            else
                transform.scale = value;
        }
    }
}

} // namespace lc1::scene
