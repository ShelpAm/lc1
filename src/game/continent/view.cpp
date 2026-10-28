#include "lc1/game/continent/view.hpp"

#include "lc1/game/continent/continent.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace lc1 {
namespace {

struct Geometry {
    glm::vec3 origin;
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;

    // u cross v points outwards: every generated face uses the same winding convention.
    void quad(glm::vec3 point, glm::vec3 u, glm::vec3 v, glm::vec3 color)
    {
        auto const first = static_cast<std::uint32_t>(vertices.size());
        auto const normal = glm::normalize(glm::cross(u, v));
        point -= origin;
        vertices.push_back({point, normal, color, {0.0F, 0.0F}});
        vertices.push_back({point + u, normal, color, {1.0F, 0.0F}});
        vertices.push_back({point + u + v, normal, color, {1.0F, 1.0F}});
        vertices.push_back({point + v, normal, color, {0.0F, 1.0F}});
        for (std::uint32_t offset : {0U, 1U, 2U, 0U, 2U, 3U})
            indices.push_back(first + offset);
    }

    void box(glm::vec3 min, glm::vec3 max, glm::vec3 color)
    {
        glm::vec3 const size = max - min;
        quad({max.x, min.y, max.z}, {0, 0, -size.z}, {0, size.y, 0}, color);
        quad({min.x, min.y, min.z}, {0, 0, size.z}, {0, size.y, 0}, color);
        quad({min.x, min.y, max.z}, {size.x, 0, 0}, {0, size.y, 0}, color);
        quad({max.x, min.y, min.z}, {-size.x, 0, 0}, {0, size.y, 0}, color);
        quad({min.x, max.y, max.z}, {size.x, 0, 0}, {0, 0, -size.z}, color);
        quad({min.x, min.y, min.z}, {size.x, 0, 0}, {0, 0, size.z}, color);
    }

    // Four terrain corners in winding order: (x0,z1), (x1,z1), (x1,z0), (x0,z0).
    void terrain_quad(std::array<glm::vec3, 4> const &positions,
                      std::array<glm::vec3, 4> const &normals, glm::vec3 color)
    {
        auto const first = static_cast<std::uint32_t>(vertices.size());
        constexpr std::array uvs{glm::vec2{0.0F, 0.0F}, glm::vec2{1.0F, 0.0F},
                                 glm::vec2{1.0F, 1.0F}, glm::vec2{0.0F, 1.0F}};
        for (std::size_t i = 0; i < positions.size(); ++i) {
            vertices.push_back({.position = positions[i] - origin,
                                .normal = normals[i],
                                .color = color,
                                .uv = uvs[i]});
        }
        for (std::uint32_t offset : {0U, 1U, 2U, 0U, 2U, 3U})
            indices.push_back(first + offset);
    }

    void road_polygon(Continent const &continent, std::vector<glm::vec2> points, GroundBounds chunk)
    {
        // Clip before triangulating so a road crossing chunks is drawn exactly once.
        for (int axis = 0; axis < 2; ++axis) {
            for (int side = 0; side < 2; ++side) {
                if (points.empty())
                    return;
                float const edge = side == 0 ? chunk.min[axis] : chunk.max[axis];
                auto inside = [&](glm::vec2 p) {
                    return side == 0 ? p[axis] >= edge : p[axis] <= edge;
                };
                std::vector<glm::vec2> clipped;
                glm::vec2 previous = points.back();
                for (auto const current : points) {
                    if (inside(previous) != inside(current)) {
                        float const t = (edge - previous[axis]) / (current[axis] - previous[axis]);
                        clipped.push_back(glm::mix(previous, current, t));
                    }
                    if (inside(current))
                        clipped.push_back(current);
                    previous = current;
                }
                points = std::move(clipped);
            }
        }
        if (points.size() < 3)
            return;
        auto const first = static_cast<std::uint32_t>(vertices.size());
        // The lift only keeps the decal stable up close; the renderer's negative depth
        // bias handles distant coplanarity.
        for (auto const point : points) {
            auto const sample = continent.terrain_sample(point);
            float const y = (sample ? sample->height : 0.0F) + 0.02F;
            vertices.push_back({.position = glm::vec3{point.x, y, point.y} - origin,
                                .color = {0.48F, 0.35F, 0.20F},
                                .uv = {0.5F, 0.5F}});
        }
        for (std::uint32_t i = 1; i + 1 < points.size(); ++i) {
            indices.push_back(first);
            indices.push_back(first + i);
            indices.push_back(first + i + 1);
        }
    }
};

glm::vec3 color_of(ContinentSurface surface)
{
    switch (surface) {
    case ContinentSurface::Grass:
        return {0.20F, 0.32F, 0.13F};
    case ContinentSurface::Road:
        return {0.48F, 0.35F, 0.20F};
    case ContinentSurface::Stone:
        return {0.42F, 0.44F, 0.43F};
    case ContinentSurface::Plaster:
        return {0.72F, 0.58F, 0.38F};
    case ContinentSurface::Roof:
        return {0.32F, 0.10F, 0.055F};
    case ContinentSurface::Wood:
        return {0.14F, 0.065F, 0.025F};
    case ContinentSurface::Window:
        return {0.035F, 0.065F, 0.085F};
    case ContinentSurface::Foliage:
        return {0.13F, 0.29F, 0.07F};
    case ContinentSurface::Canvas:
        return {0.63F, 0.55F, 0.30F};
    }
    return {1.0F, 0.0F, 1.0F};
}

glm::vec3 terrain_color(RegionType type)
{
    switch (type) {
    case RegionType::Plain:
        return color_of(ContinentSurface::Grass);
    case RegionType::Forest:
        return {0.10F, 0.22F, 0.08F};
    case RegionType::Mountain:
        return {0.35F, 0.34F, 0.31F};
    case RegionType::Wasteland:
        return {0.62F, 0.45F, 0.23F};
    case RegionType::Sea:
        return {0.06F, 0.22F, 0.34F};
    }
    return {1.0F, 0.0F, 1.0F};
}

void ground(Geometry &geometry, Continent const &continent, GroundBounds chunk)
{
    // Include patch and tile edges in the tessellation. Vertex heights and normals come
    // from the same bilinear terrain sample used by the character controller.
    std::array<std::vector<float>, 2> edges;
    for (int axis = 0; axis < 2; ++axis) {
        auto &values = edges[axis];
        values = {chunk.min[axis], chunk.max[axis]};
        auto add = [&](float value) {
            if (value > chunk.min[axis] && value < chunk.max[axis])
                values.push_back(value);
        };
        for (float value = continent.bounds().min[axis]; value < chunk.max[axis];
             value += continent.tile_size())
            add(value);
        for (auto const &patch : continent.patches()) {
            if (glm::any(glm::lessThanEqual(patch.bounds.max, chunk.min)) ||
                glm::any(glm::greaterThanEqual(patch.bounds.min, chunk.max)))
                continue;
            add(patch.bounds.min[axis]);
            add(patch.bounds.max[axis]);
        }
        std::ranges::sort(values);
        values.erase(std::unique(values.begin(), values.end()), values.end());
    }
    for (std::size_t z = 1; z < edges[1].size(); ++z) {
        for (std::size_t x = 1; x < edges[0].size(); ++x) {
            glm::vec2 const min{edges[0][x - 1], edges[1][z - 1]};
            glm::vec2 const max{edges[0][x], edges[1][z]};
            glm::vec2 const center = (min + max) * 0.5F;
            glm::vec3 color = terrain_color(continent.region_at(center)->type);
            for (auto const &patch : continent.patches()) {
                if (glm::all(glm::greaterThanEqual(center, patch.bounds.min)) &&
                    glm::all(glm::lessThan(center, patch.bounds.max)))
                    color = color_of(patch.surface);
            }
            int const checker = static_cast<int>(std::floor(center.x / continent.tile_size())) +
                                static_cast<int>(std::floor(center.y / continent.tile_size()));
            color *= checker % 2 == 0 ? 1.0F : 0.94F;

            auto const center_sample = continent.terrain_sample(center);
            float const elevation = center_sample ? center_sample->height : 0.0F;
            float const rock = std::clamp((elevation - 2.0F) / 12.0F, 0.0F, 0.65F);
            color = glm::mix(color, glm::vec3{0.38F, 0.36F, 0.31F}, rock);

            auto const p00 = continent.terrain_sample({min.x, max.y});
            auto const p10 = continent.terrain_sample({max.x, max.y});
            auto const p11 = continent.terrain_sample({max.x, min.y});
            auto const p01 = continent.terrain_sample({min.x, min.y});
            auto const sample_or_flat = [](std::optional<GroundSample> const &sample) {
                return sample ? *sample : GroundSample{};
            };
            auto const s00 = sample_or_flat(p00);
            auto const s10 = sample_or_flat(p10);
            auto const s11 = sample_or_flat(p11);
            auto const s01 = sample_or_flat(p01);
            geometry.terrain_quad(
                {glm::vec3{min.x, s00.height, max.y}, glm::vec3{max.x, s10.height, max.y},
                 glm::vec3{max.x, s11.height, min.y}, glm::vec3{min.x, s01.height, min.y}},
                {s00.normal, s10.normal, s11.normal, s01.normal}, color);
        }
    }
}

void roads(Geometry &geometry, Continent const &continent, GroundBounds chunk)
{
    for (auto const &road : continent.roads()) {
        float const radius = road.width * 0.5F;
        for (std::size_t i = 1; i < road.points.size(); ++i) {
            auto const start = road.points[i - 1];
            auto const end = road.points[i];
            auto const direction = glm::normalize(end - start);
            glm::vec2 const normal{-direction.y * radius, direction.x * radius};
            geometry.road_polygon(
                continent, {start + normal, end + normal, end - normal, start - normal}, chunk);
        }
        // Rounded ends also fill the outside of every bend, matching Road::contains.
        for (auto const point : road.points) {
            std::vector<glm::vec2> cap;
            constexpr int sides = 24;
            for (int side = 0; side < sides; ++side) {
                float const angle =
                    -2.0F * std::numbers::pi_v<float> * static_cast<float>(side) / sides;
                cap.push_back(point + radius * glm::vec2{std::cos(angle), std::sin(angle)});
            }
            geometry.road_polygon(continent, std::move(cap), chunk);
        }
    }
}

} // namespace

ContinentView::ContinentView(Device const &device, GpuTexture const &texture,
                             Continent const &continent)
    : material_(MaterialInfo{.parameters = {.metallic_factor = 0.0F, .roughness_factor = 0.8F},
                             .base_color = {.texture = &texture}})
{
    // 16 x 16 tiles per mesh keeps the full first continent at 384 draws, without
    // creating per-object UBOs/BLASes for thousands of tiny chunks. All are resident.
    float const chunk_size = continent.tile_size() * 16.0F;
    auto const bounds = continent.bounds();

    struct PendingDraw {
        std::size_t mesh_index;
        glm::vec3 origin;
        bool depth_bias;
    };

    std::vector<PendingDraw> pending;
    auto append_geometry = [&](Geometry &geometry) {
        if (geometry.vertices.empty())
            return;
        auto const mesh_index = models_.size();
        models_.push_back(std::make_unique<scene::GpuModel>(
            device, scene::Model{geometry.vertices, geometry.indices}));
        pending.push_back(
            {.mesh_index = mesh_index, .origin = geometry.origin, .depth_bias = false});
    };
    for (float z = bounds.min.y; z < bounds.max.y; z += chunk_size) {
        for (float x = bounds.min.x; x < bounds.max.x; x += chunk_size) {
            GroundBounds const chunk{{x, z}, glm::min(glm::vec2{x, z} + chunk_size, bounds.max)};
            Geometry terrain{.origin = {x, 0.0F, z}, .vertices = {}, .indices = {}};
            Geometry road{.origin = {x, 0.0F, z}, .vertices = {}, .indices = {}};
            ground(terrain, continent, chunk);
            roads(road, continent, chunk);
            for (auto const &block : continent.blocks()) {
                glm::vec3 const min{std::max(block.min.x, chunk.min.x), block.min.y,
                                    std::max(block.min.z, chunk.min.y)};
                glm::vec3 const max{std::min(block.max.x, chunk.max.x), block.max.y,
                                    std::min(block.max.z, chunk.max.y)};
                if (min.x < max.x && min.z < max.z)
                    terrain.box(min, max, color_of(block.surface));
            }
            append_geometry(terrain);
            if (road.vertices.empty())
                continue;
            auto const mesh_index = models_.size();
            models_.push_back(std::make_unique<scene::GpuModel>(
                device, scene::Model{road.vertices, road.indices}));
            pending.push_back(
                {.mesh_index = mesh_index, .origin = road.origin, .depth_bias = true});
        }
    }
    draws_.reserve(pending.size());
    for (auto const &entry : pending) {
        draws_.push_back({.model = models_[entry.mesh_index].get(),
                          .material = &material_,
                          .world_transform = glm::translate(glm::mat4{1.0F}, entry.origin),
                          .casts_shadow = !entry.depth_bias,
                          .depth_bias = entry.depth_bias});
    }
}

} // namespace lc1

namespace lc1 {

std::span<scene::SceneModel const> ContinentView::draw_items() const
{
    return draws_;
}

} // namespace lc1
