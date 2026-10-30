#include "lc1/scene/model-definition.hpp"
#include "lc1/error.hpp"

#include <algorithm>
#include <ranges>
#include <utility>

namespace lc1::scene {

ModelDefinition::ModelDefinition(std::size_t mesh_count, std::vector<ModelNode> nodes,
                                 std::vector<AnimationClip> animations)
    : nodes_(std::move(nodes)), animations_(std::move(animations))
{
    for (auto const &[index, node] : std::views::zip(std::views::iota(0UZ), nodes_)) {
        if (node.parent_index && *node.parent_index >= index)
            fail("model parent must precede its child");
        if (node.mesh_index && *node.mesh_index >= mesh_count)
            fail("model node mesh index is out of range");
        node.transform.matrix();
    }
    for (auto const &animation : animations_)
        animation.validate(nodes_.size());
}

std::span<ModelNode const> ModelDefinition::nodes() const
{
    return nodes_;
}

std::span<AnimationClip const> ModelDefinition::animations() const
{
    return animations_;
}

std::size_t ModelDefinition::draw_count() const
{
    return static_cast<std::size_t>(std::ranges::count_if(
        nodes_, [](auto const &node) { return node.mesh_index.has_value(); }));
}

std::optional<std::size_t> ModelDefinition::find_node(std::string_view name) const
{
    auto const found = std::ranges::find(nodes_, name, &ModelNode::name);
    if (found == nodes_.end())
        return std::nullopt;
    return static_cast<std::size_t>(found - nodes_.begin());
}

} // namespace lc1::scene
