#include "lc1/game/debug/pbr-showcase.hpp"
#include "lc1/scene/lights/point-light.hpp"
#include "lc1/scene/lights/sphere-light.hpp"
#include "lc1/scene/lights/spot-light.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace lc1::demo {
namespace {

struct Geometry {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;

    void quad(glm::vec3 point, glm::vec3 u, glm::vec3 v, glm::vec3 color = glm::vec3{1})
    {
        auto const first = static_cast<std::uint32_t>(vertices.size());
        auto const normal = glm::normalize(glm::cross(u, v));
        vertices.push_back({point, normal, color, {0, 1}});
        vertices.push_back({point + u, normal, color, {1, 1}});
        vertices.push_back({point + u + v, normal, color, {1, 0}});
        vertices.push_back({point + v, normal, color, {0, 0}});
        for (auto const index : {0U, 1U, 2U, 0U, 2U, 3U})
            indices.push_back(first + index);
    }

    GpuMesh upload(Device const &device, bool with_tangents = false) const
    {
        if (with_tangents) {
            auto tangent_vertices = vertices;
            auto tangent_indices = indices;
            generate_tangents(tangent_vertices, tangent_indices);
            return {device, tangent_vertices, tangent_indices};
        }
        return {device, std::span<Vertex const>{vertices}, std::span<std::uint32_t const>{indices}};
    }
};

GpuMesh make_cube(Device const &device, bool colored)
{
    Geometry geometry;
    std::array const points{glm::vec3{-0.5F, -0.5F, -0.5F}, glm::vec3{0.5F, -0.5F, -0.5F},
                            glm::vec3{0.5F, 0.5F, -0.5F},   glm::vec3{-0.5F, 0.5F, -0.5F},
                            glm::vec3{-0.5F, -0.5F, 0.5F},  glm::vec3{0.5F, -0.5F, 0.5F},
                            glm::vec3{0.5F, 0.5F, 0.5F},    glm::vec3{-0.5F, 0.5F, 0.5F}};
    std::array const colors{glm::vec3{0.75F, 0.07F, 0.035F}, glm::vec3{0.08F, 0.3F, 0.8F},
                            glm::vec3{0.04F, 0.6F, 0.3F},    glm::vec3{0.9F, 0.4F, 0.03F},
                            glm::vec3{0.8F, 0.8F, 0.7F},     glm::vec3{0.5F, 0.07F, 0.55F}};
    std::size_t i = 0;
    for (auto const face : std::array{std::array{4U, 5U, 6U, 7U}, std::array{1U, 0U, 3U, 2U},
                                      std::array{0U, 4U, 7U, 3U}, std::array{5U, 1U, 2U, 6U},
                                      std::array{3U, 7U, 6U, 2U}, std::array{0U, 1U, 5U, 4U}}) {
        geometry.quad(points[face[0]], points[face[1]] - points[face[0]],
                      points[face[3]] - points[face[0]], colored ? colors[i] : glm::vec3{1});
        ++i;
    }
    return geometry.upload(device, true);
}

GpuMesh make_sphere(Device const &device)
{
    Geometry geometry;
    constexpr std::uint32_t slices = 64, stacks = 32;
    for (std::uint32_t row = 0; row <= stacks; ++row) {
        float const v = static_cast<float>(row) / stacks;
        float const theta = v * glm::pi<float>();
        for (std::uint32_t column = 0; column <= slices; ++column) {
            float const u = static_cast<float>(column) / slices;
            float const phi = u * glm::two_pi<float>();
            glm::vec3 const normal{std::sin(theta) * std::cos(phi), std::cos(theta),
                                   std::sin(theta) * std::sin(phi)};
            geometry.vertices.push_back({.position = normal, .normal = normal, .uv = {u, v}});
        }
    }
    for (std::uint32_t row = 0; row < stacks; ++row)
        for (std::uint32_t column = 0; column < slices; ++column) {
            auto const a = row * (slices + 1) + column;
            auto const b = a + slices + 1;
            if (row != 0)
                geometry.indices.insert(geometry.indices.end(), {a, a + 1, b});
            if (row + 1 != stacks)
                geometry.indices.insert(geometry.indices.end(), {a + 1, b + 1, b});
        }
    return geometry.upload(device, true);
}

} // namespace

std::vector<ContinentBlock> PbrShowcase::spawn_blocks()
{
    // Leave x in [-5.5, 5.5] and the original city/village road completely open.
    // Low display plinths have walkable tops; the tall back walls and dividers do not.
    auto block = [](glm::vec3 min, glm::vec3 max, bool walkable_top) {
        return ContinentBlock{min, max, ContinentSurface::Stone, true, walkable_top};
    };
    std::vector<ContinentBlock> blocks{block({-21, 0, -18}, {-5.5F, 0.8F, -14}, true),
                                       block({-21, 0, -19}, {-5.5F, 7.1F, -18.5F}, false),
                                       block({6, 0, -18}, {21, 0.8F, -14}, true),
                                       block({6, 0, -19}, {21, 4.4F, -18.5F}, false),
                                       block({6, 0, -11}, {21, 0.8F, -7}, true),
                                       block({-21, 0, -11}, {-5.5F, 0.8F, -7}, true)};
    for (float const x : {-13.0F, 13.0F}) {
        blocks.push_back(block({x - 6, 0, -4}, {x + 6, 0.75F, 6}, true));
        blocks.push_back(block({x - 7.5F, 0, 12}, {x + 7.5F, 0.8F, 18}, true));
        blocks.push_back(block({x - 7.5F, 0, 11.4F}, {x + 7.5F, 5.9F, 11.8F}, false));
    }
    return blocks;
}

MaterialInfo const &PbrShowcase::material(glm::vec3 color, float metallic, float roughness,
                                          glm::vec3 emission)
{
    MaterialInfo info;
    info.parameters = {.base_color_factor = glm::vec4{color, 1},
                       .emissive_factor = emission,
                       .metallic_factor = metallic,
                       .roughness_factor = roughness};
    materials_.push_back(info);
    return materials_.back();
}

std::size_t PbrShowcase::place(scene::GpuModel const &mesh, MaterialInfo const &material,
                               glm::vec3 position, glm::vec3 scale, float yaw)
{
    auto transform = glm::translate(glm::mat4{1}, position + origin_);
    transform = glm::rotate(transform, yaw, glm::vec3{0, 1, 0});
    draws_.push_back(
        {.model = &mesh, .material = &material, .world_transform = glm::scale(transform, scale)});
    return draws_.size() - 1;
}

GpuTexture const &PbrShowcase::texture(Device const &device, TexturePattern pattern,
                                       TextureColorSpace color_space)
{
    constexpr std::uint32_t size = 256;
    std::vector<std::uint8_t> pixels(size * size * 4);
    for (std::uint32_t y = 0; y < size; ++y)
        for (std::uint32_t x = 0; x < size; ++x) {
            std::array<std::uint8_t, 4> color{255, 255, 255, 255};
            bool const alternate = ((x / 32) + (y / 32)) % 2 == 0;
            if (pattern ==
                TexturePattern::Checks) // Base color: ivory and indigo checks, fine warm grid.
                color = x % 32 < 2 || y % 32 < 2 ? std::array<std::uint8_t, 4>{210, 150, 60, 255}
                        : alternate              ? std::array<std::uint8_t, 4>{220, 215, 198, 255}
                                                 : std::array<std::uint8_t, 4>{35, 65, 115, 255};
            else if (pattern == TexturePattern::MetallicRoughness) // G: roughness steps, B:
                                                                   // dielectric/metal halves.
                color = {255, static_cast<std::uint8_t>(20 + (y / 32) * 32),
                         static_cast<std::uint8_t>(x < size / 2 ? 0 : 255), 255};
            else if (pattern == TexturePattern::Emission) { // Emissive bars: black regions remain
                                                            // non-emitting.
                bool const lit = x % 64 < 40 && y % 64 < 40;
                color = lit ? std::array<std::uint8_t, 4>{35, 180, 255, 255}
                            : std::array<std::uint8_t, 4>{0, 0, 0, 255};
            }
            else if (pattern == TexturePattern::Normal) {
                float const u = (static_cast<float>(x) + 0.5F) / size * glm::two_pi<float>() * 4;
                float const v = (static_cast<float>(y) + 0.5F) / size * glm::two_pi<float>() * 4;
                auto const normal = glm::normalize(glm::vec3{
                    -0.45F * std::cos(u) * std::sin(v), -0.45F * std::sin(u) * std::cos(v), 1});
                for (int channel = 0; channel < 3; ++channel)
                    color[channel] = static_cast<std::uint8_t>(
                        std::lround((normal[channel] * 0.5F + 0.5F) * 255));
            }
            else if (pattern == TexturePattern::FineNormal) {
                // Small slopes from periodic height waves: surface grain, not deep folds.
                float const u = (static_cast<float>(x) + 0.5F) / size * glm::two_pi<float>();
                float const v = (static_cast<float>(y) + 0.5F) / size * glm::two_pi<float>();
                float const a = std::cos(13 * u + 11 * v);
                float const b = std::cos(23 * u - 17 * v);
                float const c = std::cos(7 * u + 31 * v);
                auto const normal =
                    glm::normalize(glm::vec3{-(0.052F * a + 0.0092F * b + 0.0021F * c),
                                             -(0.044F * a - 0.0068F * b + 0.0093F * c), 1});
                for (int channel = 0; channel < 3; ++channel)
                    color[channel] = static_cast<std::uint8_t>(
                        std::lround((normal[channel] * 0.5F + 0.5F) * 255));
            }
            else // Slate floor, visible fine-scale texture and generated mipmaps.
                color = x % 32 < 1 || y % 32 < 1 ? std::array<std::uint8_t, 4>{28, 36, 44, 255}
                        : alternate              ? std::array<std::uint8_t, 4>{100, 111, 118, 255}
                                                 : std::array<std::uint8_t, 4>{86, 97, 104, 255};
            std::copy(color.begin(), color.end(), pixels.begin() + (y * size + x) * 4);
        }
    textures_.push_back(std::make_unique<GpuTexture>(
        device, vk::Extent2D{.width = size, .height = size}, pixels, color_space));
    return *textures_.back();
}

PbrShowcase::PbrShowcase(Device const &device, glm::vec3 origin)
    : origin_(origin), sphere_(make_sphere(device)), cube_(make_cube(device, false)),
      vertex_colors_(make_cube(device, true))
{
    auto const &white = material({0.7F, 0.75F, 0.8F}, 0, 0.85F);
    auto const &dark = material({0.035F, 0.045F, 0.06F}, 0, 0.6F);
    auto const &trim = material({0, 0, 0}, 0, 1, {0.04F, 0.6F, 0.85F});
    // Panel lettering keeps the brightness the emissive material used to give it,
    // and the showcase origin every exhibit is placed from.
    auto label = [this](std::string text, glm::vec3 center, float line_height, float max_width) {
        labels_.push_back({.text = label_text_.emplace_back(std::move(text)),
                           .center = center + origin_,
                           .line_height = line_height,
                           .max_width = max_width,
                           .color = {1.8F, 1.8F, 1.8F}});
    };
    MaterialInfo floor_info;
    floor_info.parameters.metallic_factor = 0;
    floor_info.parameters.roughness_factor = 0.9F;
    floor_info.base_color.texture =
        &texture(device, TexturePattern::Floor, TextureColorSpace::Srgb);
    materials_.push_back(floor_info);
    for (float const x : {-13.5F, 13.5F}) {
        place(cube_, materials_.back(), {x, 0.03F, 0}, {17, 0.02F, 41});
        place(cube_, trim, {x > 0 ? 5.2F : -5.2F, 0.06F, 0}, {0.07F, 0.03F, 41});
    }

    // Full 3x5 parameter matrix, with numeric labels readable at ground level.
    label("METALLIC / ROUGHNESS", {-13.2F, 6.85F, -18.4F}, 0.72F, 14);
    label("ROUGHNESS  0     .25     .5     .75     1", {-13.2F, 0.72F, -13.94F}, 0.42F, 14.8F);
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 5; ++column) {
            auto const &sample = material({0.8F, 0.32F, 0.08F}, row / 2.0F, column / 4.0F);
            place(sphere_, sample, {-18.5F + column * 2.6F, 5.6F - row * 1.8F, -16.2F},
                  glm::vec3{0.72F});
        }
    label("M 0", {-20.2F, 5.8F, -18.3F}, 0.4F, 1.2F);
    label("M .5", {-20.2F, 4.0F, -18.3F}, 0.4F, 1.2F);
    label("M 1", {-20.2F, 2.2F, -18.3F}, 0.4F, 1.2F);

    // Identical flat geometry and material parameters; only normal mapping differs.
    auto const &normal_texture = texture(device, TexturePattern::Normal, TextureColorSpace::Linear);
    std::array const normal_labels{"NORMAL OFF", "NORMAL x1", "NORMAL x2"};
    for (int i = 0; i < 3; ++i) {
        MaterialInfo info;
        info.parameters.base_color_factor = {0.55F, 0.3F, 0.1F, 1};
        info.parameters.metallic_factor = 0.25F;
        info.parameters.roughness_factor = 0.3F;
        info.parameters.normal_scale = static_cast<float>(i);
        if (i != 0)
            info.normal.texture = &normal_texture;
        materials_.push_back(info);
        float const x = -18.5F + i * 5.0F;
        place(cube_, materials_.back(), {x, 1.0F, -9}, {3.5F, 0.3F, 3.5F});
        label(normal_labels[i], {x, 0.68F, -6.94F}, 0.5F, 4.5F);
    }

    // Shared base image, factor tint, and independent linear MR map.
    auto const &checker = texture(device, TexturePattern::Checks, TextureColorSpace::Srgb);
    auto const &mr = texture(device, TexturePattern::MetallicRoughness, TextureColorSpace::Linear);
    MaterialInfo textured;
    textured.parameters.metallic_factor = 0;
    textured.parameters.roughness_factor = 0.45F;
    textured.base_color.texture = &checker;
    materials_.push_back(textured);
    place(cube_, materials_.back(), {8.5F, 2, -16}, glm::vec3{2.0F}, 0.3F);
    textured.parameters.base_color_factor = {0.3F, 0.95F, 0.55F, 1};
    materials_.push_back(textured);
    place(sphere_, materials_.back(), {13.4F, 2, -16}, glm::vec3{1.05F});
    MaterialInfo mapped;
    mapped.parameters.base_color_factor = {0.85F, 0.42F, 0.1F, 1};
    mapped.metallic_roughness.texture = &mr;
    materials_.push_back(mapped);
    place(sphere_, materials_.back(), {18.2F, 2, -16}, glm::vec3{1.05F}, 1.1F);
    label("TEXTURE CHANNELS", {13.5F, 4.1F, -18.4F}, 0.7F, 14);
    label("BASE COLOR", {8.5F, 0.68F, -13.94F}, 0.45F, 4);
    label("COLOR TINT", {13.4F, 0.68F, -13.94F}, 0.45F, 4);
    label("MR MAP", {18.2F, 0.68F, -13.94F}, 0.45F, 4);

    std::array const metal_colors{glm::vec3{1, 0.766F, 0.336F}, glm::vec3{0.955F, 0.638F, 0.538F},
                                  glm::vec3{0.91F, 0.92F, 0.92F}, glm::vec3{0.56F, 0.57F, 0.58F},
                                  glm::vec3{0.06F, 0.2F, 0.7F}};
    std::array const names{"GOLD", "COPPER", "SILVER", "IRON", "PAINT"};
    for (std::size_t i = 0; i < metal_colors.size(); ++i) {
        auto const &sample = material(metal_colors[i], i == 4 ? 0.0F : 1.0F, 0.23F);
        float const x = 7.5F + static_cast<float>(i) * 2.75F;
        place(sphere_, sample, {x, 1.8F, -9}, glm::vec3{0.82F});
        label(names[i], {x, 0.68F, -6.94F}, 0.44F, 2.4F);
    }

    // Equal-energy point/sphere lights and matching occluders, seen on real receivers.
    for (float const x : {-13.0F, 13.0F}) {
        place(cube_, white, {x, 0.8F, 1}, {12, 0.08F, 10});
        for (float const dx : {-3.0F, -1.5F, 0.0F, 1.5F, 3.0F})
            place(cube_, dark, {x + dx, 1.9F, -0.7F}, {0.22F, 2.1F, 0.4F});
        place(cube_, dark, {x, 2.8F, -0.7F}, {6.4F, 0.22F, 0.4F});
        place(sphere_, dark, {x - 2.4F, 1.7F, 3}, glm::vec3{0.8F});
        label(x < 0 ? "POINT / HARD SHADOW" : "SPHERE / SOFT SHADOW", {x, 0.64F, 6.06F}, 0.62F,
              11.5F);
    }
    auto const &moving = material({0.6F, 0.08F, 0.04F}, 0, 0.4F);
    moving_shadow_draw_ = place(cube_, moving, {15, 2.1F, 2.5F}, {1.3F, 1.3F, 1.3F});
    lights_.push_back(
        scene::to_light(scene::PointLight{.base = {.color = {0.75F, 0.85F, 1}, .intensity = 110},
                                          .position = origin_ + glm::vec3{-15, 6, -2}}));
    lights_.push_back(
        scene::to_light(scene::SphereLight{.base = {.color = {0.75F, 0.85F, 1}, .intensity = 110},
                                           .position = origin_ + glm::vec3{11, 6, -2},
                                           .radius = 1.0F,
                                           .shadow_sample_count = 2}));
    lights_.push_back(
        scene::to_light(scene::PointLight{.base = {.color = {1, 0.92F, 0.8F}, .intensity = 150},
                                          .position = origin_ + glm::vec3{-13, 7, -12}}));
    lights_.push_back(
        scene::to_light(scene::SpotLight{.base = {.color = {0.85F, 0.93F, 1}, .intensity = 400},
                                         .position = origin_ + glm::vec3{13, 8, -10},
                                         .direction = glm::normalize(glm::vec3{0, -1, -0.6F}),
                                         .inner_angle = glm::radians(24.0F),
                                         .outer_angle = glm::radians(38.0F)}));
    lights_.push_back(
        scene::to_light(scene::PointLight{.base = {.color = {1, 0.9F, 0.78F}, .intensity = 110},
                                          .position = origin_ + glm::vec3{13, 6, 19}}));

    // Display fixtures are rings around the light, never closed meshes enclosing it.
    // A closed emissive bulb would otherwise shadow its own point/sphere source.
    auto const &fixture = material({0, 0, 0}, 0, 1, {3, 4, 6});
    for (auto const &light : lights_) {
        auto const p = light.position - origin_;
        float const radius = light.type == 3 ? 1.15F : 0.28F;
        place(cube_, fixture, p + glm::vec3{radius, 0, 0}, {0.04F, 0.08F, radius * 2});
        place(cube_, fixture, p + glm::vec3{-radius, 0, 0}, {0.04F, 0.08F, radius * 2});
        place(cube_, fixture, p + glm::vec3{0, 0, radius}, {radius * 2, 0.08F, 0.04F});
        place(cube_, fixture, p + glm::vec3{0, 0, -radius}, {radius * 2, 0.08F, 0.04F});
    }

    // Emission is visible without lights. Its surface does not illuminate neighbours.
    std::array const powers{0.25F, 1.0F, 4.0F, 16.0F, 64.0F};
    std::array const power_labels{".25", "1", "4", "16", "64"};
    for (std::size_t i = 0; i < powers.size(); ++i) {
        auto const &emissive = material({0, 0, 0}, 0, 1, glm::vec3{1, 0.22F, 0.025F} * powers[i]);
        float const x = -18.5F + static_cast<float>(i) * 2.7F;
        place(cube_, emissive, {x, 1.8F, 15}, {2.05F, 1.7F, 0.2F});
        label(power_labels[i], {x, 0.68F, 18.06F}, 0.6F, 2.2F);
    }
    MaterialInfo emission_map;
    emission_map.parameters.base_color_factor = {0, 0, 0, 1};
    emission_map.parameters.emissive_factor = {6, 6, 6};
    emission_map.parameters.metallic_factor = 0;
    emission_map.emissive.texture =
        &texture(device, TexturePattern::Emission, TextureColorSpace::Srgb);
    materials_.push_back(emission_map);
    place(cube_, materials_.back(), {-13, 4, 14}, {8, 1.4F, 0.2F});
    label("EMISSION / HDR", {-13, 5.45F, 11.86F}, 0.8F, 14);

    auto const &fine_normal =
        texture(device, TexturePattern::FineNormal, TextureColorSpace::Linear);
    MaterialInfo ceramic_info;
    ceramic_info.parameters = {.base_color_factor = {0.055F, 0.42F, 0.32F, 1},
                               .metallic_factor = 0,
                               .roughness_factor = 0.25F};
    ceramic_info.parameters.normal_scale = 0.65F;
    ceramic_info.normal.texture = &fine_normal;
    materials_.push_back(ceramic_info);
    auto const &ceramic = materials_.back();
    place(sphere_, ceramic, {13, 1.6F, 15}, {1.7F, 0.65F, 0.85F});
    MaterialInfo vertex_info;
    vertex_info.parameters.metallic_factor = 0;
    vertex_info.parameters.roughness_factor = 0.38F;
    vertex_info.parameters.normal_scale = 0.65F;
    vertex_info.normal.texture = &fine_normal;
    materials_.push_back(vertex_info);
    auto const &vertex_material = materials_.back();
    place(vertex_colors_, vertex_material, {8.2F, 2.1F, 15}, {-1.7F, 1.7F, 1.7F}, -0.3F);
    rotating_draw_ = place(vertex_colors_, vertex_material, {18, 2.1F, 15}, glm::vec3{1.9F});
    label("MIRRORED", {8.2F, 0.68F, 18.06F}, 0.46F, 4);
    label("SCALED", {13, 0.68F, 18.06F}, 0.46F, 4);
    label("VERTEX COLOR", {18, 0.68F, 18.06F}, 0.46F, 4.5F);
    label("TRANSFORMS + NORMALS", {13, 5.45F, 11.86F}, 0.8F, 14);
    label("F4 LIGHTS  /  F5 MOTION", {13, 4.55F, 11.86F}, 0.5F, 14);
    label("PGUP / PGDN EXPOSURE", {-13, 4.65F, 11.86F}, 0.5F, 14);
    label("PBR  /  MATERIAL WALK", {0, 0.4F, 20.5F}, 0.7F, 10);
    update(0);
}

void PbrShowcase::update(float seconds)
{
    auto transform = glm::translate(glm::mat4{1}, origin_ + glm::vec3{18, 2.1F, 15});
    transform = glm::rotate(transform, seconds * 0.45F, glm::vec3{0, 1, 0});
    transform = glm::rotate(transform, 0.22F, glm::vec3{1, 0, 0});
    draws_[rotating_draw_].world_transform = glm::scale(transform, glm::vec3{1.9F});
    auto moving = glm::translate(glm::mat4{1},
                                 origin_ + glm::vec3{15 + 1.1F * std::sin(seconds), 2.1F, 2.5F});
    moving = glm::rotate(moving, seconds * 0.7F, glm::vec3{0, 1, 0});
    draws_[moving_shadow_draw_].world_transform = glm::scale(moving, glm::vec3{1.3F});
}

} // namespace lc1::demo

namespace lc1::demo {

std::span<scene::SceneModel const> PbrShowcase::draws() const
{
    return draws_;
}

std::span<WorldTextItem const> PbrShowcase::labels() const
{
    return labels_;
}

std::span<scene::Light const> PbrShowcase::lights() const
{
    return lights_;
}

} // namespace lc1::demo
