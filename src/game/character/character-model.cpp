#include "lc1/game/character/character-model.hpp"
#include "lc1/game/character/character.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace lc1 {
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

scene::Model load_barrel_player_model(std::filesystem::path const &path, CharacterBody const &body)
{
    auto const placeholder = make_character_model(body);
    auto const imported = scene::Model::load_from_obj(path);
    glm::vec3 minimum{std::numeric_limits<float>::max()};
    glm::vec3 maximum{std::numeric_limits<float>::lowest()};
    for (auto const &vertex : imported.vertices()) {
        minimum = glm::min(minimum, vertex.position);
        maximum = glm::max(maximum, vertex.position);
    }
    auto const extent = maximum - minimum;
    if (extent.x <= 0 || extent.y <= 0 || extent.z <= 0)
        fail("player barrel model must have nonzero extent on every axis");
    float const width_scale = 2.0F * body.half_width / std::max(extent.x, extent.z);
    glm::vec3 const scale{width_scale, body.height / extent.y, width_scale};
    glm::vec3 const center{(minimum.x + maximum.x) * 0.5F, minimum.y,
                           (minimum.z + maximum.z) * 0.5F};
    auto const definition = placeholder.definition();
    std::vector<scene::ModelNode> nodes{definition.nodes().begin(), definition.nodes().end()};
    for (auto &node : nodes)
        node.mesh_index.reset();
    // Rock the barrel with the existing head animation; keep the Eyes mount in metres.
    nodes.push_back({.name = "Barrel",
                     .parent_index = 1,
                     .transform = {.translation = -center * scale - nodes[1].transform.translation,
                                   .scale = scale},
                     .mesh_index = 0});
    auto const vertices = imported.vertices();
    auto const indices = imported.indices();
    return scene::Model{
        {scene::Mesh{{vertices.begin(), vertices.end()}, {indices.begin(), indices.end()}}},
        std::move(nodes),
        {definition.animations().begin(), definition.animations().end()}};
}

scene::Model load_player_weapon_model(std::filesystem::path const &path)
{
    auto const imported = scene::Model::load_from_obj(path);
    auto const pitch = [](float degrees) {
        return glm::angleAxis(glm::radians(degrees), glm::vec3{1, 0, 0});
    };
    auto const rest = pitch(-45.0F);
    std::vector<scene::ModelNode> nodes{
        {.name = "Grip", .transform = {.translation = {0.5F, 1.05F, -0.4F}, .rotation = rest}},
        {.name = "KingdomLongsword",
         .parent_index = 0,
         // Authored Z-up geometry, with the grip centred near local Z = -1.3.
         .transform = {.translation = {0, 0.585F, 0},
                       .rotation = pitch(-90.0F),
                       .scale = glm::vec3{0.45F}},
         .mesh_index = 0}};
    scene::AnimationClip swing{
        .name = "Swing",
        .channels = {{.node = 0,
                      .path = scene::AnimationChannel::Path::Rotation,
                      .times = {0.0F, 0.12F, 0.28F, 0.55F},
                      .values = std::vector<glm::quat>{rest, pitch(25.0F), pitch(-120.0F), rest}}}};
    auto const vertices = imported.vertices();
    auto const indices = imported.indices();
    return scene::Model{
        {scene::Mesh{{vertices.begin(), vertices.end()}, {indices.begin(), indices.end()}}},
        std::move(nodes),
        {std::move(swing)}};
}

} // namespace lc1
