#include "lc1/game/character/model-config.hpp"
#include "lc1/error.hpp"
#include "lc1/game/character/character-body.hpp"
#include "lc1/game/character/character-model.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace lc1 {
namespace {

glm::vec3 vector(std::array<float, 3> const &value)
{
    return {value[0], value[1], value[2]};
}

std::vector<scene::ModelNode> model_nodes(ModelConfig const &config)
{
    std::vector<scene::ModelNode> nodes;
    for (auto const &node : config.nodes.value())
        nodes.push_back({.name = node.name,
                         .parent_index = node.parent_index.value(),
                         .transform = node.transform.value().transform(),
                         .mesh_index = node.mesh_index.value()});
    return nodes;
}

std::vector<scene::AnimationClip> model_animations(ModelConfig const &config)
{
    std::vector<scene::AnimationClip> clips;
    for (auto const &clip : config.animations.value()) {
        scene::AnimationClip result{.name = clip.name, .channels = {}};
        for (auto const &channel : clip.channels) {
            scene::AnimationChannel converted{.node = channel.node,
                                              .path = channel.path,
                                              .interpolation = channel.interpolation.value(),
                                              .times = channel.times,
                                              .values = {}};
            if (channel.path == scene::AnimationChannel::Path::Rotation) {
                std::vector<glm::quat> values;
                for (auto const &value : channel.values) {
                    if (!std::ranges::all_of(value, [](float v) { return std::isfinite(v); }))
                        fail("model animation rotation must be finite");
                    values.emplace_back(glm::radians(vector(value)));
                }
                converted.values = std::move(values);
            }
            else {
                std::vector<glm::vec3> values;
                for (auto const &value : channel.values)
                    values.push_back(vector(value));
                converted.values = std::move(values);
            }
            result.channels.push_back(std::move(converted));
        }
        clips.push_back(std::move(result));
    }
    return clips;
}

} // namespace

scene::NodeTransform ModelTransformConfig::transform() const
{
    if (!std::ranges::all_of(rotation_degrees.value(), [](float v) { return std::isfinite(v); }))
        fail("model rotation must be finite");
    return {.translation = vector(translation.value()),
            .rotation = glm::quat{glm::radians(vector(rotation_degrees.value()))},
            .scale = vector(scale.value())};
}

bool ModelTransformConfig::operator==(ModelTransformConfig const &other) const
{
    return translation.value() == other.translation.value() &&
           rotation_degrees.value() == other.rotation_degrees.value() &&
           scale.value() == other.scale.value();
}

bool ModelNodeConfig::operator==(ModelNodeConfig const &other) const
{
    return name == other.name && parent_index.value() == other.parent_index.value() &&
           mesh_index.value() == other.mesh_index.value() &&
           transform.value() == other.transform.value();
}

bool ModelAnimationChannelConfig::operator==(ModelAnimationChannelConfig const &other) const
{
    return node == other.node && path == other.path &&
           interpolation.value() == other.interpolation.value() && times == other.times &&
           values == other.values;
}

MaterialInfo ModelMaterialConfig::material() const
{
    auto const &color = base_color.value();
    return {.parameters = {.base_color_factor = {color[0], color[1], color[2], color[3]},
                           .emissive_factor = vector(emissive.value()),
                           .metallic_factor = metallic.value(),
                           .roughness_factor = roughness.value(),
                           .normal_scale = normal_scale.value()}};
}

bool ModelMaterialConfig::operator==(ModelMaterialConfig const &other) const
{
    return base_color.value() == other.base_color.value() &&
           metallic.value() == other.metallic.value() &&
           roughness.value() == other.roughness.value() &&
           emissive.value() == other.emissive.value() &&
           normal_scale.value() == other.normal_scale.value() &&
           base_color_texture.value() == other.base_color_texture.value() &&
           metallic_roughness_texture.value() == other.metallic_roughness_texture.value() &&
           normal_texture.value() == other.normal_texture.value() &&
           emissive_texture.value() == other.emissive_texture.value();
}

bool ModelConfig::operator==(ModelConfig const &other) const
{
    return path.value() == other.path.value() && transform.value() == other.transform.value() &&
           material.value() == other.material.value() && nodes.value() == other.nodes.value() &&
           animations.value() == other.animations.value() && eyes.value() == other.eyes.value() &&
           eyes_offset.value() == other.eyes_offset.value() &&
           first_person_hidden_nodes.value() == other.first_person_hidden_nodes.value() &&
           weapon_mount.value() == other.weapon_mount.value() &&
           idle_animation.value() == other.idle_animation.value() &&
           action_animation.value() == other.action_animation.value() &&
           move_animation.value() == other.move_animation.value() &&
           run_animation.value() == other.run_animation.value() &&
           attack_animation.value() == other.attack_animation.value() &&
           death_animation.value() == other.death_animation.value();
}

float configured_animation_duration(ModelConfig const &config, std::string const &name,
                                    std::filesystem::path const &asset_root)
{
    if (name.empty())
        return 0;
    if (!config.animations.value().empty()) {
        for (auto const &clip : config.animations.value())
            if (clip.name == name) {
                float duration = 0;
                for (auto const &channel : clip.channels)
                    if (!channel.times.empty())
                        duration = std::max(duration, channel.times.back());
                return duration;
            }
        fail("model is missing animation {}", name);
    }
    auto const model = load_configured_model(config, asset_root, CharacterBody{});
    auto const index = model.definition().find_animatinon(name);
    if (!index)
        fail("model is missing animation {}", name);
    return model.definition().animations()[*index].duration();
}

void validate_model_config(ModelConfig const &config)
{
    auto const &material = config.material.value();
    for (auto const &name : {config.path.value(), material.base_color_texture.value(),
                             material.metallic_roughness_texture.value(),
                             material.normal_texture.value(), material.emissive_texture.value()}) {
        auto const path = std::filesystem::path{name}.lexically_normal();
        if (path.is_absolute() || (!path.empty() && *path.begin() == ".."))
            fail("model asset path must stay relative to the asset root: {}", name);
    }
    config.transform.value().transform().matrix();
    config.eyes_offset.value().transform().matrix();
    validate_material(material.material());
    auto nodes = model_nodes(config);
    if (!nodes.empty()) {
        std::size_t mesh_count = 0;
        for (auto const &node : nodes)
            if (node.mesh_index) {
                if (*node.mesh_index == std::numeric_limits<std::size_t>::max())
                    fail("invalid model mesh index");
                mesh_count = std::max(mesh_count, *node.mesh_index + 1);
            }
        // The imported mesh count is checked when its geometry is loaded.
        scene::ModelDefinition{mesh_count, std::move(nodes), model_animations(config)};
    }
    else if (!config.animations.value().empty())
        fail("configured animations require explicit model nodes");
}

std::vector<bool> first_person_hidden_nodes(ModelConfig const &config,
                                            std::span<scene::ModelNode const> nodes)
{
    std::vector<bool> hidden(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i)
        hidden[i] = (nodes[i].parent_index && hidden[*nodes[i].parent_index]) ||
                    std::ranges::find(config.first_person_hidden_nodes.value(), nodes[i].name) !=
                        config.first_person_hidden_nodes.value().end();
    return hidden;
}

scene::Model load_configured_model(ModelConfig const &config,
                                   std::filesystem::path const &asset_root,
                                   CharacterBody const &body)
{
    auto const imported =
        config.path.value() == "builtin:flashlight" ? make_flashlight_model()
        : config.path.value() == "builtin:torch"    ? make_torch_model()
        : config.path.value().empty()
            ? make_character_model(body)
            : scene::Model::load_from_file(asset_root / config.path.value(),
                                           scene::GltfMaterialPolicy::UseExternal);
    std::vector<scene::Mesh> meshes;
    for (std::size_t i = 0; i < imported.mesh_count(); ++i) {
        auto const vertices = imported.vertices(i);
        auto const indices = imported.indices(i);
        meshes.emplace_back(std::vector<Vertex>{vertices.begin(), vertices.end()},
                            std::vector<std::uint32_t>{indices.begin(), indices.end()});
    }
    auto nodes = model_nodes(config);
    auto animations = model_animations(config);
    if (nodes.empty()) {
        auto const &definition = imported.definition();
        nodes.assign(definition.nodes().begin(), definition.nodes().end());
        animations.assign(definition.animations().begin(), definition.animations().end());
    }
    // A parent node applies the configured local transform to the complete imported hierarchy.
    for (auto &node : nodes)
        node.parent_index = node.parent_index ? *node.parent_index + 1 : 0;
    nodes.insert(nodes.begin(),
                 {.name = "Model", .transform = config.transform.value().transform()});
    for (auto &clip : animations)
        for (auto &channel : clip.channels)
            ++channel.node;
    std::vector<scene::SkinnedMesh> skins{imported.skins().begin(), imported.skins().end()};
    if (!skins.empty() && !config.nodes.value().empty())
        fail("configured nodes cannot replace a skinned hierarchy");
    for (auto &skin : skins) {
        ++skin.node;
        for (auto &joint : skin.skin.joints)
            ++joint;
    }
    auto const hidden = first_person_hidden_nodes(config, nodes);
    for (auto &skin : skins) {
        std::vector<std::uint32_t> body_indices;
        std::vector<std::uint32_t> head_indices;
        for (std::size_t triangle = 0; triangle < skin.indices.size(); triangle += 3) {
            bool head = false;
            for (std::size_t corner = 0; corner < 3; ++corner) {
                auto const &influence = skin.influences[skin.indices[triangle + corner]];
                float head_weight = 0;
                for (int joint = 0; joint < 4; ++joint)
                    if (hidden[skin.skin.joints[influence.joints[joint]]])
                        head_weight += influence.weights[joint];
                head = head || head_weight >= 0.5F;
            }
            auto &indices = head ? head_indices : body_indices;
            indices.insert(indices.end(), skin.indices.begin() + triangle,
                           skin.indices.begin() + triangle + 3);
        }
        skin.view_index_count = static_cast<std::uint32_t>(body_indices.size());
        body_indices.insert(body_indices.end(), head_indices.begin(), head_indices.end());
        skin.indices = std::move(body_indices);
        meshes[*nodes[skin.node].mesh_index] = scene::Mesh{skin.vertices, skin.indices};
    }
    scene::Model model{std::move(meshes), std::move(nodes), std::move(animations),
                       std::move(skins)};
    if (!config.material.value().normal_texture.value().empty())
        model.generate_tangents();
    return model;
}

} // namespace lc1
