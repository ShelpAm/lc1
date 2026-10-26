#pragma once

#include "lc1/scene/model-node.hpp"

#include <span>
#include <string>
#include <variant>
#include <vector>

namespace lc1::scene {

struct AnimationChannel {
    enum class Path { Translation, Rotation, Scale };
    enum class Interpolation { Step, Linear };

    std::size_t node = 0;
    Path path = Path::Translation;
    Interpolation interpolation = Interpolation::Linear;
    std::vector<float> times; // Nonnegative seconds, strictly increasing.
    // Rotations use quaternions; translation and scale use vec3 values.
    std::variant<std::vector<glm::vec3>, std::vector<glm::quat>> values;
};

struct AnimationClip {
    std::string name;
    std::vector<AnimationChannel> channels;

    float duration() const;

  private:
    friend class ModelDefinition;
    friend class ModelInstance;
    void validate(std::size_t node_count) const;
    // ModelDefinition validates clips once; only instances sample the immutable data.
    void sample(float time, std::span<NodeTransform> pose) const;
};

} // namespace lc1::scene
