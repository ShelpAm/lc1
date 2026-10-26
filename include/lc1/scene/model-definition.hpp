#pragma once

#include "lc1/scene/animation.hpp"

#include <optional>
#include <string_view>

namespace lc1::scene {

// Shared runtime hierarchy and clips; owns no CPU geometry or GPU resources.
class ModelDefinition {
  public:
    ModelDefinition(std::size_t mesh_count, std::vector<ModelNode> nodes,
                    std::vector<AnimationClip> animations = {});

    std::span<ModelNode const> nodes() const;
    std::span<AnimationClip const> animations() const;
    std::size_t draw_count() const;
    // Returns the first matching node; names need not be unique.
    std::optional<std::size_t> find_node(std::string_view name) const;

  private:
    std::vector<ModelNode> nodes_;
    std::vector<AnimationClip> animations_;
};

} // namespace lc1::scene
