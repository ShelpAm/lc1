#include "lc1/scene/model-instance.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace lc1::scene {
namespace {
glm::mat4 compose_world_matrix(glm::mat4 const &world, glm::mat4 const &local)
{
    auto const matrix = world * local;
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (!std::isfinite(matrix[column][row]))
                fail("model node world transform must be finite");
    float const determinant = glm::determinant(glm::mat3{matrix});
    if (!std::isfinite(determinant) || determinant == 0.0F)
        fail("model node world transform must be invertible");
    return matrix;
}
} // namespace

ModelInstance::ModelInstance(ModelDefinition const &model) : model_(&model)
{
    for (auto const &node : model.nodes())
        local_transforms_.push_back(node.transform);
}

ModelDefinition const &ModelInstance::model() const
{
    return *model_;
}

std::span<NodeTransform const> ModelInstance::local_transforms() const
{
    return local_transforms_;
}

void ModelInstance::set_local_transform(std::size_t node_index, NodeTransform const &transform)
{
    if (node_index >= local_transforms_.size())
        fail("model instance node index is out of range");
    transform.matrix();
    local_transforms_[node_index] = transform;
}

void ModelInstance::reset_pose()
{
    for (std::size_t i = 0; i < local_transforms_.size(); ++i)
        local_transforms_[i] = model_->nodes()[i].transform;
    playback_.reset();
}

std::vector<glm::mat4> ModelInstance::model_matrices() const
{
    std::vector<glm::mat4> matrices;
    matrices.reserve(local_transforms_.size());
    for (std::size_t i = 0; i < local_transforms_.size(); ++i) {
        auto matrix = local_transforms_[i].matrix();
        if (auto const parent = model_->nodes()[i].parent)
            matrix = matrices[*parent] * matrix;
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row)
                if (!std::isfinite(matrix[column][row]))
                    fail("model hierarchy produced a non-finite matrix");
        matrices.push_back(matrix);
    }
    return matrices;
}

void ModelInstance::play_animation(std::size_t animation, bool looping)
{
    if (animation >= model_->animations().size())
        fail("model animation index is out of range");
    std::vector<NodeTransform> pose;
    pose.reserve(model_->nodes().size());
    for (auto const &node : model_->nodes())
        pose.push_back(node.transform);
    model_->animations()[animation].sample(0.0F, pose);
    for (auto const &transform : pose)
        transform.matrix();
    local_transforms_ = std::move(pose);
    playback_ = AnimationPlayback{.animation = animation, .looping = looping};
}

void ModelInstance::update_animation(float delta_seconds)
{
    if (!std::isfinite(delta_seconds) || delta_seconds < 0.0F)
        fail("animation delta must be finite and nonnegative");
    if (!playback_)
        return;
    auto const &clip = model_->animations()[playback_->animation];
    double const duration = clip.duration();
    double time = playback_->time + static_cast<double>(delta_seconds);
    if (duration == 0.0)
        time = 0.0;
    else if (playback_->looping)
        time = std::fmod(time, duration);
    else
        time = std::min(time, duration);
    auto pose = local_transforms_;
    clip.sample(static_cast<float>(time), pose);
    for (auto const &transform : pose)
        transform.matrix();
    local_transforms_ = std::move(pose);
    playback_->time = time;
}

std::optional<AnimationPlayback> ModelInstance::playback() const
{
    return playback_;
}

glm::mat4 ModelInstance::node_world_matrix(std::size_t node, glm::mat4 const &world_transform) const
{
    if (node >= local_transforms_.size())
        fail("model instance node index is out of range");
    return compose_world_matrix(world_transform, model_matrices()[node]);
}

std::vector<MeshInstance> ModelInstance::mesh_instances(glm::mat4 const &world_transform) const
{
    auto const matrices = model_matrices();
    std::vector<MeshInstance> instances;
    instances.reserve(model_->draw_count());
    for (std::size_t i = 0; i < model_->nodes().size(); ++i) {
        if (auto const mesh = model_->nodes()[i].mesh) {
            instances.push_back(MeshInstance{
                .mesh = *mesh, .model_matrix = compose_world_matrix(world_transform, matrices[i])});
        }
    }
    return instances;
}

} // namespace lc1::scene
