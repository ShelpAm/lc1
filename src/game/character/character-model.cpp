#include "lc1/game/character/character-model.hpp"
#include "lc1/game/character/character.hpp"
#include "lc1/scene/model-instance.hpp"

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

void apply_remiel_leg_height(scene::ModelInstance &pose, float scale)
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
    auto const extension = matrices[*lower][3].y * (scale - 1);
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
