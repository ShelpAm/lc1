#pragma once

#include "lc1/scene/animation.hpp"
#include "lc1/scene/model.hpp"
#include "lc1/vk/render/material.hpp"

#include <rfl/DefaultVal.hpp>

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace lc1 {

struct CharacterBody;

// DefaultVal makes only these new presentation fields optional in otherwise strict save data.
struct ModelTransformConfig {
    rfl::DefaultVal<std::array<float, 3>> translation{{0, 0, 0}};
    rfl::DefaultVal<std::array<float, 3>> rotation_degrees{{0, 0, 0}};
    rfl::DefaultVal<std::array<float, 3>> scale{{1, 1, 1}};

    scene::NodeTransform transform() const;
    bool operator==(ModelTransformConfig const &other) const;
};

struct ModelNodeConfig {
    std::string name;
    rfl::DefaultVal<std::optional<std::size_t>> parent_index;
    rfl::DefaultVal<std::optional<std::size_t>> mesh_index;
    rfl::DefaultVal<ModelTransformConfig> transform;

    bool operator==(ModelNodeConfig const &other) const;
};

struct ModelAnimationChannelConfig {
    std::size_t node;
    scene::AnimationChannel::Path path;
    rfl::DefaultVal<scene::AnimationChannel::Interpolation> interpolation{
        scene::AnimationChannel::Interpolation::Linear};
    std::vector<float> times;
    // Rotation values are XYZ Euler degrees; translation and scale are XYZ values.
    std::vector<std::array<float, 3>> values;

    bool operator==(ModelAnimationChannelConfig const &other) const;
};

struct ModelAnimationConfig {
    std::string name;
    std::vector<ModelAnimationChannelConfig> channels;

    bool operator==(ModelAnimationConfig const &other) const = default;
};

struct ModelMaterialConfig {
    rfl::DefaultVal<std::array<float, 4>> base_color{{1, 1, 1, 1}};
    rfl::DefaultVal<float> metallic{0.0F};
    rfl::DefaultVal<float> roughness{0.8F};
    rfl::DefaultVal<std::array<float, 3>> emissive{{0, 0, 0}};
    rfl::DefaultVal<float> normal_scale{1.0F};
    rfl::DefaultVal<std::string> base_color_texture;
    rfl::DefaultVal<std::string> metallic_roughness_texture;
    rfl::DefaultVal<std::string> normal_texture;
    rfl::DefaultVal<std::string> emissive_texture;

    MaterialInfo material() const;
    bool operator==(ModelMaterialConfig const &other) const;
};

// Stored directly in each character/weapon record, not a separate content definition registry.
struct ModelConfig {
    rfl::DefaultVal<std::string> path;
    rfl::DefaultVal<ModelTransformConfig> transform;
    rfl::DefaultVal<ModelMaterialConfig> material;
    rfl::DefaultVal<std::vector<ModelNodeConfig>> nodes;
    rfl::DefaultVal<std::vector<ModelAnimationConfig>> animations;
    rfl::DefaultVal<std::string> eyes{"Eyes"};
    rfl::DefaultVal<ModelTransformConfig> eyes_offset;
    rfl::DefaultVal<std::vector<std::string>> first_person_hidden_nodes{
        std::vector<std::string>{"Head"}};
    rfl::DefaultVal<std::string> weapon_mount{"RightHandGrip"};
    rfl::DefaultVal<std::string> idle_animation;
    rfl::DefaultVal<std::string> action_animation;
    rfl::DefaultVal<std::string> move_animation;
    rfl::DefaultVal<std::string> run_animation;
    rfl::DefaultVal<std::string> attack_animation;
    rfl::DefaultVal<std::string> death_animation;

    bool operator==(ModelConfig const &other) const;
};

// Checks YAML content without loading external assets or creating a device.
void validate_model_config(ModelConfig const &config);
float configured_animation_duration(ModelConfig const &config, std::string const &name,
                                    std::filesystem::path const &asset_root);
std::vector<bool> first_person_hidden_nodes(ModelConfig const &config,
                                            std::span<scene::ModelNode const> nodes);
scene::Model load_configured_model(ModelConfig const &config,
                                   std::filesystem::path const &asset_root,
                                   CharacterBody const &body);

} // namespace lc1
