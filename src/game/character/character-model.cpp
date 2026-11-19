#include "lc1/game/character/character-model.hpp"
#include "lc1/game/character/character.hpp"
#include "lc1/scene/model-instance.hpp"

#include <glm/gtx/quaternion.hpp>

#include <array>
#include <cmath>
#include <utility>

namespace lc1 {
namespace {
// Closed surfaces of revolution, shared by the wooden shaft and flame silhouette.
scene::Model revolved_model(std::span<glm::vec2 const> profile, glm::vec3 color)
{
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    constexpr unsigned sides = 12;
    for (std::size_t ring = 1; ring < profile.size(); ++ring) {
        auto const bottom = profile[ring - 1];
        auto const top = profile[ring];
        for (unsigned side = 0; side < sides; ++side) {
            float const a = glm::two_pi<float>() * static_cast<float>(side) / sides;
            float const b = glm::two_pi<float>() * static_cast<float>(side + 1) / sides;
            auto const base = static_cast<std::uint32_t>(vertices.size());
            for (auto const sample :
                 std::array{glm::vec3{bottom.x, bottom.y, a}, glm::vec3{top.x, top.y, a},
                            glm::vec3{top.x, top.y, b}, glm::vec3{bottom.x, bottom.y, b}}) {
                auto const normal = glm::normalize(
                    glm::vec3{(top.y - bottom.y) * std::cos(sample.z), bottom.x - top.x,
                              (top.y - bottom.y) * std::sin(sample.z)});
                vertices.push_back({.position = {sample.x * std::cos(sample.z), sample.y,
                                                 sample.x * std::sin(sample.z)},
                                    .normal = normal,
                                    .color = color,
                                    .uv = {0.5F, 0.5F}});
            }
            for (auto const index : {0U, 1U, 2U, 0U, 2U, 3U})
                indices.push_back(base + index);
        }
    }
    return scene::Model{std::move(vertices), std::move(indices)};
}
} // namespace

RemielMotion::RemielMotion(scene::ModelDefinition const &model)
{
    // Called only for the explicitly selected Remiel asset.
    head_ = model.find_node("頭");
    torso_ = model.find_node("上半身2");
    for (auto const &[name, auxiliary] :
         std::array{std::pair{"左胸", "左胸補助"}, std::pair{"右胸", "右胸補助"}}) {
        auto const node = model.find_node(name);
        if (!node)
            fail("Remiel is missing secondary-motion joint {}", name);
        springs_.push_back({.node = *node, .auxiliary = model.find_node(auxiliary)});
    }
    // The index remains extended; other fingers curl independently around their local Z axes.
    for (auto const &[name, angles] : std::array{
             std::pair{"右腕", glm::vec3{-65, -15, 65}}, std::pair{"右ひじ", glm::vec3{-35, 0, 0}},
             std::pair{"右手首", glm::vec3{0, 0, -12}}, std::pair{"右人指１", glm::vec3{0}},
             std::pair{"右人指２", glm::vec3{0}}, std::pair{"右人指３", glm::vec3{0}},
             std::pair{"右中指１", glm::vec3{0, 0, 65}}, std::pair{"右中指２", glm::vec3{0, 0, 85}},
             std::pair{"右中指３", glm::vec3{0, 0, 55}}, std::pair{"右薬指１", glm::vec3{0, 0, 70}},
             std::pair{"右薬指２", glm::vec3{0, 0, 90}}, std::pair{"右薬指３", glm::vec3{0, 0, 55}},
             std::pair{"右小指１", glm::vec3{0, 0, 75}}, std::pair{"右小指２", glm::vec3{0, 0, 85}},
             std::pair{"右小指３", glm::vec3{0, 0, 55}},
             std::pair{"右親指１", glm::vec3{25, -15, 30}},
             std::pair{"右親指２", glm::vec3{0, 0, 35}}})
        if (auto const node = model.find_node(name))
            pointing_pose_.emplace_back(*node, angles);
    for (auto const &[joint, end] :
         std::array{std::pair{"右腕", "右ひじ"}, std::pair{"右ひじ", "右手首"},
                    std::pair{"右手首", "右人差指先"}}) {
        auto const node = model.find_node(joint);
        auto const tip = model.find_node(end);
        if (node && tip)
            pointing_chain_.emplace_back(*node, *tip);
    }
}

void RemielMotion::restore(scene::ModelInstance &pose)
{
    // Remove last frame's presentation layers before resampling sparse animation channels.
    for (std::size_t i = 0; i < base_pose_.size(); ++i)
        pose.set_local_transform(i, base_pose_[i]);
}

void RemielMotion::update(scene::ModelInstance &pose, glm::mat4 const &world, float delta_seconds,
                          bool idle, bool dead, bool pointing, glm::vec3 point_direction,
                          float prone_amount)
{
    base_pose_.assign(pose.local_transforms().begin(), pose.local_transforms().end());
    time_ = std::fmod(time_ + delta_seconds, 120.0F);
    if (!dead) {
        if (idle && torso_) {
            auto transform = pose.local_transforms()[*torso_];
            transform.translation.y += 0.018F * std::sin(time_ * glm::two_pi<float>() / 4);
            transform.rotation *=
                glm::angleAxis(glm::radians(0.65F * std::sin(time_ * glm::two_pi<float>() / 7.5F)),
                               glm::vec3{0, 0, 1});
            pose.set_local_transform(*torso_, transform);
        }
        if (idle && head_) {
            auto transform = pose.local_transforms()[*head_];
            transform.rotation *=
                glm::angleAxis(glm::radians(3.0F * std::sin(time_ * glm::two_pi<float>() / 10)),
                               glm::vec3{0, 1, 0});
            pose.set_local_transform(*head_, transform);
        }
        if (pointing)
            for (auto const &[node, angles] : pointing_pose_) {
                auto transform = pose.local_transforms()[node];
                transform.rotation =
                    pose.model().nodes()[node].transform.rotation * glm::quat{glm::radians(angles)};
                pose.set_local_transform(node, transform);
            }
        if (pointing) {
            // Aim each segment in camera-forward world space, rather than guessing Euler
            // angles in PMX's imported joint axes. Parent rotations and import correction
            // are included, so the arm and index remain aligned when the body turns.
            auto const forward = point_direction;
            for (auto const &[joint, tip] : pointing_chain_) {
                auto const matrices = pose.model_matrices();
                auto const parent = pose.model().nodes()[joint].parent_index;
                auto const inverse_parent =
                    glm::inverse(glm::mat3{world * (parent ? matrices[*parent] : glm::mat4{1})});
                auto const segment = glm::vec3{world * (matrices[tip][3] - matrices[joint][3])};
                auto transform = pose.local_transforms()[joint];
                transform.rotation =
                    glm::normalize(glm::rotation(glm::normalize(inverse_parent * segment),
                                                 glm::normalize(inverse_parent * forward)) *
                                   transform.rotation);
                pose.set_local_transform(joint, transform);
            }
        }
        // A short, frame-rate independent pose filter softens clip changes and crouch edges.
        // The unfiltered animation is restored next frame, so this never feeds back into clips.
        if (initialized_ && delta_seconds > 0 && delta_seconds <= 0.25F) {
            auto const blend = 1 - std::exp(-delta_seconds / 0.065F);
            for (std::size_t i = 0; i < displayed_pose_.size(); ++i) {
                auto target = pose.local_transforms()[i];
                target.translation =
                    glm::mix(displayed_pose_[i].translation, target.translation, blend);
                target.rotation =
                    glm::normalize(glm::slerp(displayed_pose_[i].rotation, target.rotation, blend));
                target.scale = glm::mix(displayed_pose_[i].scale, target.scale, blend);
                pose.set_local_transform(i, target);
            }
        }
    }
    displayed_pose_.assign(pose.local_transforms().begin(), pose.local_transforms().end());
    auto const matrices = pose.model_matrices();
    for (auto &spring : springs_) {
        auto const parent = pose.model().nodes()[spring.node].parent_index;
        auto const parent_world = world * (parent ? matrices[*parent] : glm::mat4{1});
        auto const anchor = glm::vec3{world * matrices[spring.node][3]};
        auto const unit = glm::length(glm::vec3{parent_world[1]});
        auto const travel = anchor - spring.previous_anchor;
        if (!initialized_ || dead || prone_amount > 0 || delta_seconds > 0.25F ||
            glm::length(travel) > unit * 3) {
            spring.offset = spring.velocity = spring.previous_velocity = glm::vec3{0};
        }
        else if (delta_seconds > 0) {
            auto const anchor_velocity = travel / delta_seconds;
            spring.velocity -= 1.5F * (anchor_velocity - spring.previous_velocity);
            spring.previous_velocity = anchor_velocity;
            auto const steps = static_cast<int>(std::ceil(delta_seconds * 120));
            auto const step = delta_seconds / static_cast<float>(steps);
            // Approximately 1.3 Hz with a slow, underdamped return. Integrate in world space so
            // turns, starts, stops and animated torso motion all produce inertia.
            for (int i = 0; i < steps; ++i) {
                spring.velocity += (-65.0F * spring.offset - 2.2F * spring.velocity) * step;
                spring.offset += spring.velocity * step;
                auto const limit = unit * 0.75F;
                auto const distance = glm::length(spring.offset);
                if (distance > limit) {
                    auto const normal = spring.offset / distance;
                    spring.offset = normal * limit;
                    spring.velocity -= normal * std::max(0.0F, glm::dot(spring.velocity, normal));
                }
            }
        }
        spring.previous_anchor = anchor;
        auto transform = pose.local_transforms()[spring.node];
        transform.translation +=
            glm::vec3{glm::inverse(parent_world) * glm::vec4{spring.offset, 0}};
        pose.set_local_transform(spring.node, transform);
        if (spring.auxiliary) {
            auto const auxiliary_parent = pose.model().nodes()[*spring.auxiliary].parent_index;
            auto const auxiliary_world =
                world * (auxiliary_parent ? matrices[*auxiliary_parent] : glm::mat4{1});
            auto auxiliary = pose.local_transforms()[*spring.auxiliary];
            auxiliary.translation +=
                glm::vec3{glm::inverse(auxiliary_world) * glm::vec4{spring.offset * 0.35F, 0}};
            pose.set_local_transform(*spring.auxiliary, auxiliary);
        }
    }
    initialized_ = true;
}

void apply_remiel_leg_height(scene::ModelInstance &pose, float scale, float prone_amount)
{
    auto const waist = pose.model().find_node("腰");
    auto const lower = pose.model().find_node("下半身");
    if (!waist || !lower)
        return;
    auto const matrices = pose.model_matrices();
    auto waist_transform = pose.local_transforms()[*waist];
    auto lower_transform = pose.local_transforms()[*lower];
    auto const parent = pose.model().nodes()[*waist].parent_index;
    auto const parent_matrix = parent ? matrices[*parent] : glm::mat4{1};
    // Stretch the lower body around the feet and translate its waist/upper body by
    // the same extension. Inverse parent converts world-up to the imported rig space.
    auto const extension = matrices[*lower][3].y * (scale - 1) * (1 - prone_amount);
    waist_transform.translation +=
        glm::vec3{glm::inverse(parent_matrix) * glm::vec4{0, extension, 0, 0}};
    lower_transform.scale.y *= scale;
    pose.set_local_transform(*waist, waist_transform);
    pose.set_local_transform(*lower, lower_transform);
}

scene::Model make_inventory_model(std::uint32_t resource)
{
    if (resource == 1) {
        std::array const profile{glm::vec2{0, -0.035F},    glm::vec2{0.38F, -0.035F},
                                 glm::vec2{0.4F, -0.02F},  glm::vec2{0.4F, 0.02F},
                                 glm::vec2{0.38F, 0.035F}, glm::vec2{0, 0.035F}};
        return revolved_model(profile, {0.8F, 0.5F, 0.12F});
    }
    if (resource == 2) {
        std::array const profile{glm::vec2{0, -0.5F},      glm::vec2{0.13F, -0.5F},
                                 glm::vec2{0.16F, -0.45F}, glm::vec2{0.16F, 0.45F},
                                 glm::vec2{0.13F, 0.5F},   glm::vec2{0, 0.5F}};
        return revolved_model(profile, {0.32F, 0.16F, 0.055F});
    }
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    auto box = [&](glm::vec3 low, glm::vec3 high, glm::vec3 color,
                   glm::mat4 transform = glm::mat4{1}) {
        std::array<glm::vec3, 8> points;
        for (unsigned i = 0; i < 8; ++i)
            points[i] =
                glm::vec3{transform * glm::vec4{(i & 1) ? high.x : low.x, (i & 2) ? high.y : low.y,
                                                (i & 4) ? high.z : low.z, 1}};
        for (auto const face : std::array{std::array{0U, 2U, 3U, 1U}, std::array{4U, 5U, 7U, 6U},
                                          std::array{0U, 4U, 6U, 2U}, std::array{1U, 3U, 7U, 5U},
                                          std::array{0U, 1U, 5U, 4U}, std::array{2U, 6U, 7U, 3U}}) {
            auto const base = static_cast<std::uint32_t>(vertices.size());
            auto const normal = glm::normalize(
                glm::cross(points[face[1]] - points[face[0]], points[face[2]] - points[face[0]]));
            for (auto const index : face)
                vertices.push_back({.position = points[index],
                                    .normal = normal,
                                    .color = color,
                                    .uv = {0.5F, 0.5F}});
            for (auto index : {0U, 1U, 2U, 0U, 2U, 3U})
                indices.push_back(base + index);
        }
    };
    if (resource == 3)
        box({-0.4F, -0.12F, -0.2F}, {0.4F, 0.12F, 0.2F}, {0.32F, 0.36F, 0.4F});
    else if (resource == 5) {
        box({-0.28F, -0.38F, -0.12F}, {0.28F, 0.28F, 0.12F}, {0.35F, 0.4F, 0.45F});
        for (float side : {-1.0F, 1.0F}) {
            auto const offset = glm::translate(glm::mat4{1}, glm::vec3{side * 0.29F, 0.22F, 0});
            box({-0.13F, -0.1F, -0.17F}, {0.13F, 0.1F, 0.17F}, {0.45F, 0.5F, 0.55F}, offset);
        }
        box({-0.28F, -0.25F, -0.14F}, {0.28F, -0.19F, 0.14F}, {0.22F, 0.1F, 0.04F});
    }
    else if (resource == 6) {
        for (float x : {-0.16F, 0.16F}) {
            box({x - 0.1F, -0.3F, -0.12F}, {x + 0.1F, 0.3F, 0.12F}, {0.22F, 0.1F, 0.035F});
            box({x - 0.1F, -0.3F, -0.3F}, {x + 0.1F, -0.12F, 0.12F}, {0.28F, 0.14F, 0.05F});
        }
    }
    else if (resource == 1004) {
        for (int i = 0; i < 16; ++i) {
            float const angle = -1.3F + (static_cast<float>(i) + 0.5F) * 2.6F / 16;
            auto transform = glm::translate(
                glm::mat4{1}, glm::vec3{0.5F * std::cos(angle), 0.5F * std::sin(angle), 0});
            transform = glm::rotate(transform, angle, glm::vec3{0, 0, 1});
            box({-0.025F, -0.045F, -0.025F}, {0.025F, 0.045F, 0.025F}, {0.3F, 0.15F, 0.05F},
                transform);
        }
        box({0.13F, -0.48F, -0.004F}, {0.14F, 0.48F, 0.004F}, {0.8F, 0.75F, 0.6F});
    }
    else
        fail("no inventory model for resource {}", resource);
    return scene::Model{std::move(vertices), std::move(indices)};
}

scene::Model make_torch_model()
{
    std::array const profile{glm::vec2{0, -0.18F},     glm::vec2{0.025F, -0.18F},
                             glm::vec2{0.035F, 0.32F}, glm::vec2{0.065F, 0.36F},
                             glm::vec2{0.065F, 0.46F}, glm::vec2{0, 0.46F}};
    return revolved_model(profile, {0.25F, 0.12F, 0.045F});
}

scene::Model make_flashlight_model()
{
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    constexpr unsigned sides = 16;
    constexpr float radius = 0.045F;
    constexpr float length = 0.24F;
    for (unsigned i = 0; i < sides; ++i) {
        float const a = glm::two_pi<float>() * static_cast<float>(i) / sides;
        float const b = glm::two_pi<float>() * static_cast<float>(i + 1) / sides;
        glm::vec3 const n0{std::cos(a), std::sin(a), 0};
        glm::vec3 const n1{std::cos(b), std::sin(b), 0};
        auto const base = static_cast<std::uint32_t>(vertices.size());
        for (auto const position :
             std::array{n0 * radius, n1 * radius, n1 * radius + glm::vec3{0, 0, -length},
                        n0 * radius + glm::vec3{0, 0, -length}})
            vertices.push_back({.position = position,
                                .normal = glm::normalize(glm::vec3{position.x, position.y, 0}),
                                .color = {0.12F, 0.14F, 0.16F},
                                .uv = {0.5F, 0.5F}});
        for (auto const index : {0U, 2U, 1U, 0U, 3U, 2U})
            indices.push_back(base + index);
        for (bool front : {false, true}) {
            auto const cap = static_cast<std::uint32_t>(vertices.size());
            glm::vec3 const offset{0, 0, front ? -length : 0};
            glm::vec3 const normal{0, 0, front ? -1 : 1};
            glm::vec3 const color = front ? glm::vec3{0.95F, 0.95F, 0.75F} : glm::vec3{0.12F};
            for (auto const position :
                 std::array{offset, offset + n0 * radius, offset + n1 * radius})
                vertices.push_back(
                    {.position = position, .normal = normal, .color = color, .uv = {0.5F, 0.5F}});
            indices.insert(indices.end(), {cap, cap + (front ? 2U : 1U), cap + (front ? 1U : 2U)});
        }
    }
    return scene::Model{std::move(vertices), std::move(indices)};
}

// Unit-radius iron helmet: flared brim, straight band and a low polygon dome.
scene::Model make_helmet_model()
{
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    constexpr unsigned sides = 24;
    constexpr std::array profile{glm::vec2{1.18F, 0.0F},  glm::vec2{1.18F, 0.06F},
                                 glm::vec2{1.0F, 0.10F},  glm::vec2{1.0F, 0.35F},
                                 glm::vec2{0.87F, 0.70F}, glm::vec2{0.5F, 0.98F},
                                 glm::vec2{0.0F, 1.08F}};
    for (std::size_t ring = 1; ring < profile.size(); ++ring) {
        for (unsigned side = 0; side < sides; ++side) {
            float const a = glm::two_pi<float>() * static_cast<float>(side) / sides;
            float const b = glm::two_pi<float>() * static_cast<float>(side + 1) / sides;
            auto const lower = profile[ring - 1];
            auto const upper = profile[ring];
            std::array const points{
                glm::vec3{lower.x * std::cos(a), lower.y, lower.x * std::sin(a)},
                glm::vec3{upper.x * std::cos(a), upper.y, upper.x * std::sin(a)},
                glm::vec3{upper.x * std::cos(b), upper.y, upper.x * std::sin(b)},
                glm::vec3{lower.x * std::cos(b), lower.y, lower.x * std::sin(b)}};
            std::array<glm::vec2, 2> profile_normals;
            for (unsigned endpoint = 0; endpoint < 2; ++endpoint) {
                auto const index = ring - 1 + endpoint;
                auto slope = upper - lower;
                // Smooth the dome across rings, but keep brim/band creases sharp.
                if (index >= 3 && ring >= 4) {
                    if (index == profile.size() - 1) {
                        profile_normals[endpoint] = {0, 1};
                        continue;
                    }
                    slope = profile[index + 1] - profile[index - 1];
                }
                profile_normals[endpoint] = glm::normalize(glm::vec2{slope.y, -slope.x});
            }
            auto const base = static_cast<std::uint32_t>(vertices.size());
            for (unsigned corner = 0; corner < points.size(); ++corner) {
                auto const n = profile_normals[corner == 1 || corner == 2 ? 1 : 0];
                auto const angle = corner < 2 ? a : b;
                vertices.push_back({.position = points[corner],
                                    .normal = {n.x * std::cos(angle), n.y, n.x * std::sin(angle)},
                                    .color = {0.42F, 0.46F, 0.5F},
                                    .uv = {0.5F, 0.5F}});
            }
            if (ring == profile.size() - 1)
                indices.insert(indices.end(), {base, base + 1, base + 3});
            else
                for (auto const index : {0U, 1U, 2U, 0U, 2U, 3U})
                    indices.push_back(base + index);
        }
    }
    return scene::Model{std::move(vertices), std::move(indices)};
}

// The placeholder mesh and collision body use the same dimensions.
scene::Model make_character_model(CharacterBody const &body)
{
    if (!std::isfinite(body.half_width) || body.half_width <= 0.0F || !std::isfinite(body.height) ||
        body.height <= 0.0F)
        fail("character model dimensions must be finite and positive");
    std::vector<scene::Mesh> meshes;
    auto box = [&](float radius, float bottom, float top, glm::vec3 color) {
        std::vector<Vertex> vertices;
        std::vector<std::uint32_t> indices;
        // Separate vertices at hard edges so each box face has its own normal.
        std::array const points{
            glm::vec3{-radius, bottom, -radius}, glm::vec3{radius, bottom, -radius},
            glm::vec3{radius, top, -radius},     glm::vec3{-radius, top, -radius},
            glm::vec3{-radius, bottom, radius},  glm::vec3{radius, bottom, radius},
            glm::vec3{radius, top, radius},      glm::vec3{-radius, top, radius}};
        for (auto const face : std::array{std::array{4U, 5U, 6U, 7U}, std::array{1U, 0U, 3U, 2U},
                                          std::array{0U, 4U, 7U, 3U}, std::array{5U, 1U, 2U, 6U},
                                          std::array{3U, 7U, 6U, 2U}, std::array{0U, 1U, 5U, 4U}}) {
            auto const base = static_cast<std::uint32_t>(vertices.size());
            auto const normal = glm::normalize(
                glm::cross(points[face[1]] - points[face[0]], points[face[2]] - points[face[0]]));
            for (auto const index : face)
                vertices.push_back({.position = points[index],
                                    .normal = normal,
                                    .color = color,
                                    .uv = {0.5F, 0.5F}});
            for (auto const index : {0U, 1U, 2U, 0U, 2U, 3U})
                indices.push_back(base + index);
        }
        meshes.emplace_back(std::move(vertices), std::move(indices));
    };
    float const neck_height = body.height * (2.0F / 3.0F);
    box(body.half_width, 0.0F, neck_height, {0.06F, 0.25F, 0.8F});
    box(body.half_width * (0.22F / 0.3F), 0.0F, body.height - neck_height, {0.95F, 0.65F, 0.15F});
    std::vector<scene::ModelNode> nodes{
        {.name = "Body", .mesh_index = 0},
        {.name = "Head",
         .parent_index = 0,
         .transform = {.translation = {0, neck_height, 0}},
         .mesh_index = 1},
        {.name = "Eyes",
         .parent_index = 1,
         .transform = {.translation = {0, body.eye_height - neck_height, 0}}}};
    auto const turn = glm::angleAxis(glm::radians(10.0F), glm::vec3{0, 1, 0});
    auto const opposite = glm::conjugate(turn);
    glm::quat const rest{1, 0, 0, 0};
    scene::AnimationClip idle{
        .name = "Idle",
        .channels = {{.node = 1,
                      .path = scene::AnimationChannel::Path::Rotation,
                      .times = {0, 1, 2, 3, 4},
                      .values = std::vector<glm::quat>{rest, turn, rest, opposite, rest}}}};
    auto const down = glm::angleAxis(glm::radians(-25.0F), glm::vec3{1, 0, 0});
    scene::AnimationClip nod{
        .name = "Nod",
        .channels = {{.node = 1,
                      .path = scene::AnimationChannel::Path::Rotation,
                      .times = {0.0F, 0.3F, 0.6F, 0.9F, 1.2F, 3.0F},
                      .values = std::vector<glm::quat>{rest, down, rest, down, rest, rest}}}};
    return scene::Model{std::move(meshes), std::move(nodes), {std::move(idle), std::move(nod)}};
}

} // namespace lc1

namespace lc1 {
glm::mat4 character_reaction_transform(bool dead, float elapsed, CharacterBody const &body)
{
    float const progress =
        glm::clamp(elapsed / (dead ? combat::death_duration : combat::hit_duration), 0.0F, 1.0F);
    float const amount = dead ? progress * progress * (3.0F - 2.0F * progress)
                              : std::sin(progress * glm::pi<float>());
    float const angle = glm::radians(dead ? -90.0F : 12.0F) * amount;
    auto const lift =
        glm::translate(glm::mat4{1}, glm::vec3{0, body.half_width * std::abs(std::sin(angle)), 0});
    return lift * glm::rotate(glm::mat4{1}, angle, glm::vec3{1, 0, 0});
}
} // namespace lc1
