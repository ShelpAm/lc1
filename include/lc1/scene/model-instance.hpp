#pragma once

#include "lc1/scene/model-definition.hpp"

#include <optional>

namespace lc1::scene {

struct AnimationPlayback {
    std::size_t animation = 0;
    double time = 0.0;
    bool looping = true;
};

struct MeshInstance {
    std::size_t mesh;
    glm::mat4 model_matrix{1.0F};
};

class ModelInstance {
  public:
    // Required non-null borrow. ModelDefinition must remain alive, unmoved and unchanged while
    // borrowed.
    explicit ModelInstance(ModelDefinition const &model);

    ModelInstance(ModelDefinition &&) = delete;
    ModelInstance(ModelDefinition const &&) = delete;

    ModelDefinition const &model() const;

    std::span<NodeTransform const> local_transforms() const;

    void set_local_transform(std::size_t node_index, NodeTransform const &transform);

    // Resetting the pose also stops playback. Playing another clip starts from its default pose.
    void reset_pose();
    void play_animation(std::size_t animation, bool looping = true);
    void update_animation(float delta_seconds);
    std::optional<AnimationPlayback> playback() const;

    // Computed on demand: no stale matrix cache and no copy of the gameplay world transform.
    std::vector<glm::mat4> model_matrices() const;
    // Query any node, including mesh-less mounts, without retaining the world transform.
    glm::mat4 node_world_matrix(std::size_t node, glm::mat4 const &world_transform) const;
    // One entry per mesh-bearing node, including repeated uses of a shared mesh.
    // The external world transform is composed here, never retained by the instance.
    std::vector<MeshInstance> mesh_instances(glm::mat4 const &world_transform) const;

  private:
    ModelDefinition const *model_;
    std::vector<NodeTransform> local_transforms_;
    std::optional<AnimationPlayback> playback_;
};

} // namespace lc1::scene
