#include "lc1/game/continent/view.hpp"

#include "lc1/game/continent/continent.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <set>

namespace lc1 {
namespace {

// Smooth value noise; wrapping the lattice also makes the generated maps seamless.
float ground_noise(glm::vec2 point, int period)
{
    auto const cell = glm::ivec2{glm::floor(point)};
    auto const f = glm::fract(point);
    auto const t = f * f * (3.0F - 2.0F * f);
    auto hash = [period](int x, int y) {
        x = (x % period + period) % period;
        y = (y % period + period) % period;
        auto value =
            static_cast<std::uint32_t>(x) * 374761393U + static_cast<std::uint32_t>(y) * 668265263U;
        value = (value ^ (value >> 13)) * 1274126177U;
        return static_cast<float>((value ^ (value >> 16)) & 0xffffU) / 65535.0F;
    };
    return glm::mix(glm::mix(hash(cell.x, cell.y), hash(cell.x + 1, cell.y), t.x),
                    glm::mix(hash(cell.x, cell.y + 1), hash(cell.x + 1, cell.y + 1), t.x), t.y);
}

constexpr float ground_texture_metres = 4.0F;

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

    void triangle(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 color, bool two_sided = false)
    {
        auto const first = static_cast<std::uint32_t>(vertices.size());
        auto const normal = glm::normalize(glm::cross(b - a, c - a));
        vertices.push_back(
            {.position = a - origin, .normal = normal, .color = color, .uv = {0, 0}});
        vertices.push_back(
            {.position = b - origin, .normal = normal, .color = color, .uv = {0, 0}});
        vertices.push_back(
            {.position = c - origin, .normal = normal, .color = color, .uv = {0, 0}});
        indices.insert(indices.end(), {first, first + 1, first + 2});
        if (two_sided) {
            vertices.push_back(
                {.position = c - origin, .normal = -normal, .color = color, .uv = {0, 0}});
            vertices.push_back(
                {.position = b - origin, .normal = -normal, .color = color, .uv = {0, 0}});
            vertices.push_back(
                {.position = a - origin, .normal = -normal, .color = color, .uv = {0, 0}});
            indices.insert(indices.end(), {first + 3, first + 4, first + 5});
        }
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
                      std::array<glm::vec3, 4> const &normals,
                      std::array<glm::vec3, 4> const &colors)
    {
        auto const first = static_cast<std::uint32_t>(vertices.size());
        for (std::size_t i = 0; i < positions.size(); ++i) {
            vertices.push_back(
                {.position = positions[i] - origin,
                 .normal = normals[i],
                 .color = colors[i],
                 .uv = glm::vec2{positions[i].x, -positions[i].z} / ground_texture_metres});
        }
        for (std::uint32_t offset : {0U, 1U, 2U, 0U, 2U, 3U})
            indices.push_back(first + offset);
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

float road_distance(Road const &road, glm::vec2 point)
{
    float distance = std::numeric_limits<float>::max();
    for (std::size_t i = 1; i < road.points.size(); ++i) {
        auto const start = road.points[i - 1];
        auto const delta = road.points[i] - start;
        float const t =
            glm::clamp(glm::dot(point - start, delta) / glm::dot(delta, delta), 0.0F, 1.0F);
        distance = std::min(distance, glm::distance(point, start + t * delta));
    }
    return distance;
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

            auto const center_sample = continent.terrain_sample(center);
            float const elevation = center_sample ? center_sample->height : 0.0F;
            float const rock = std::clamp((elevation - 2.0F) / 12.0F, 0.0F, 0.65F);
            color = glm::mix(color, glm::vec3{0.38F, 0.36F, 0.31F}, rock);

            // Only refine near road edges. Interiors keep the coarse heightfield mesh.
            auto emit = [&](this auto const &self, glm::vec2 lo, glm::vec2 hi) -> void {
                auto const middle = (lo + hi) * 0.5F;
                float const reach = glm::distance(lo, hi) * 0.5F + 1.2F;
                bool const edge = std::ranges::any_of(continent.roads(), [&](auto const &road) {
                    return std::abs(road_distance(road, middle) - road.width * 0.5F) < reach;
                });
                if (edge && std::max(hi.x - lo.x, hi.y - lo.y) > 1.0F) {
                    self(lo, middle);
                    self({middle.x, lo.y}, {hi.x, middle.y});
                    self(middle, hi);
                    self({lo.x, middle.y}, {middle.x, hi.y});
                    return;
                }
                std::array points{glm::vec2{lo.x, hi.y}, hi, glm::vec2{hi.x, lo.y}, lo};
                std::array<glm::vec3, 4> positions, normals, colors;
                for (std::size_t i = 0; i < points.size(); ++i) {
                    auto const point = points[i];
                    auto const sample = continent.terrain_sample(point).value_or(GroundSample{});
                    positions[i] = {point.x, sample.height, point.y};
                    normals[i] = sample.normal;
                    float coverage = 0.0F;
                    for (auto const &road : continent.roads()) {
                        float const width = std::min(0.9F, road.width * 0.15F);
                        float const noise = (ground_noise(point / 2.7F, 4096) - 0.5F) * 0.8F +
                                            (ground_noise(point / 0.65F, 4096) - 0.5F) * 0.25F;
                        float const distance = road_distance(road, point) - road.width * 0.5F;
                        coverage =
                            std::max(coverage, 1.0F - glm::smoothstep(-width * 0.65F, width * 0.35F,
                                                                      distance + noise));
                    }
                    colors[i] = glm::mix(color, color_of(ContinentSurface::Road), coverage) *
                                (0.88F + 0.24F * ground_noise(point / 7.0F, 4096));
                }
                geometry.terrain_quad(positions, normals, colors);
            };
            emit(min, max);
        }
    }
}

std::optional<GroundSample> decoration_ground(Continent const &continent, glm::vec2 point,
                                              float building_margin)
{
    auto const *region = continent.region_at(point);
    auto const sample = continent.terrain_sample(point);
    if (!region || (region->type != RegionType::Plain && region->type != RegionType::Forest) ||
        !sample || sample->normal.y < 0.85F || continent.location_at(point))
        return std::nullopt;
    bool const occupied = std::ranges::any_of(continent.blocks(),
                                              [&](auto const &block) {
                                                  return point.x >= block.min.x - building_margin &&
                                                         point.x <= block.max.x + building_margin &&
                                                         point.y >= block.min.z - building_margin &&
                                                         point.y <= block.max.z + building_margin;
                                              }) ||
                          std::ranges::any_of(continent.patches(), [&](auto const &patch) {
                              return patch.surface != ContinentSurface::Grass &&
                                     glm::all(glm::greaterThanEqual(point, patch.bounds.min)) &&
                                     glm::all(glm::lessThanEqual(point, patch.bounds.max));
                          });
    return occupied ? std::nullopt : sample;
}

void roadside_details(Geometry &geometry, Continent const &continent, GroundBounds chunk)
{
    for (auto const &road : continent.roads()) {
        for (std::size_t segment = 1; segment < road.points.size(); ++segment) {
            auto const start = road.points[segment - 1];
            auto const end = road.points[segment];
            constexpr float verge_width = 7.0F;
            float const margin = road.width * 0.5F + verge_width;
            if (glm::any(glm::lessThan(glm::max(start, end) + margin, chunk.min)) ||
                glm::any(glm::greaterThan(glm::min(start, end) - margin, chunk.max)))
                continue;
            auto const direction = glm::normalize(end - start);
            glm::vec2 const side_direction{-direction.y, direction.x};
            int const count = static_cast<int>(std::ceil(glm::distance(start, end) / 1.4F));
            for (int step = 0; step < count; ++step) {
                auto const center = glm::mix(start, end, (step + 0.5F) / count);
                for (float side : {-1.0F, 1.0F}) {
                    auto const seed = center + side_direction * (side * 17.0F);
                    float const scatter = ground_noise(seed * 3.17F, 4096);
                    float const variation = ground_noise(seed * 7.31F, 4096);
                    bool const stone = variation < 0.3F;
                    if (!stone || step % 2 != 0)
                        continue;
                    float const offset = 1.5F + scatter * 4.0F;
                    auto const point = center + direction * ((scatter - 0.5F) * 1.8F) +
                                       side_direction * side * (road.width * 0.5F + offset);
                    if (glm::any(glm::lessThan(point, chunk.min)) ||
                        glm::any(glm::greaterThanEqual(point, chunk.max)))
                        continue;
                    auto const sample = decoration_ground(continent, point, 0.6F);
                    if (!sample || std::ranges::any_of(continent.roads(), [&](auto const &r) {
                            return road_distance(r, point) < r.width * 0.5F + 0.9F;
                        }))
                        continue;
                    glm::vec3 const base{point.x, sample->height - 0.025F, point.y};
                    if (stone) {
                        // A partly buried, asymmetric six-sided stone; no extra collider.
                        float const radius = 0.07F + scatter * 0.15F;
                        float const height = radius * (0.35F + variation);
                        glm::vec3 const top = base + glm::vec3{radius * 0.15F, height, 0};
                        glm::vec3 const bottom = base - glm::vec3{0, 0.06F, 0};
                        for (int face = 0; face < 6; ++face) {
                            float const a = (face / 6.0F + scatter) * 2 * std::numbers::pi_v<float>;
                            float const b =
                                ((face + 1) / 6.0F + scatter) * 2 * std::numbers::pi_v<float>;
                            auto lower = [&](float angle) {
                                return base + glm::vec3{std::cos(angle) * radius, height * 0.2F,
                                                        std::sin(angle) * radius * 0.75F};
                            };
                            auto upper = [&](float angle) {
                                return base +
                                       glm::vec3{std::cos(angle) * radius * 0.58F,
                                                 height *
                                                     (0.72F + 0.12F * std::sin(angle + scatter)),
                                                 std::sin(angle) * radius * 0.48F};
                            };
                            auto const color = glm::mix(glm::vec3{0.21F, 0.23F, 0.20F},
                                                        glm::vec3{0.42F, 0.36F, 0.27F}, scatter);
                            geometry.triangle(top, upper(b), upper(a), color);
                            geometry.triangle(lower(a), upper(a), upper(b), color);
                            geometry.triangle(lower(a), upper(b), lower(b), color);
                            geometry.triangle(bottom, lower(a), lower(b), color * 0.8F);
                        }
                    }
                }
            }
        }
    }
}

void roadside_grass(std::array<Geometry, 8> &grass, Continent const &continent, GroundBounds chunk)
{
    // Sample area, not one tuft per road-side step. Global cells make density
    // independent of segment lengths and avoid duplicates at bends/intersections.
    constexpr float spacing = 0.6F;
    constexpr float verge_width = 10.0F;
    std::set<std::pair<int, int>> cells;
    for (auto const &road : continent.roads()) {
        float const margin = road.width * 0.5F + verge_width;
        for (std::size_t i = 1; i < road.points.size(); ++i) {
            auto const lo =
                glm::max(chunk.min, glm::min(road.points[i - 1], road.points[i]) - margin);
            auto const hi =
                glm::min(chunk.max, glm::max(road.points[i - 1], road.points[i]) + margin);
            for (int z = static_cast<int>(std::floor(lo.y / spacing));
                 z < std::ceil(hi.y / spacing); ++z)
                for (int x = static_cast<int>(std::floor(lo.x / spacing));
                     x < std::ceil(hi.x / spacing); ++x)
                    cells.emplace(x, z);
        }
    }
    for (auto const [x, z] : cells) {
        glm::vec2 const cell{static_cast<float>(x), static_cast<float>(z)};
        float const random = ground_noise(cell, 4096);
        float const variation = ground_noise(cell + glm::vec2{71, 197}, 4096);
        glm::vec2 const point =
            (cell + glm::vec2{0.15F + random * 0.7F, 0.15F + variation * 0.7F}) * spacing;
        if (glm::any(glm::lessThan(point, chunk.min)) ||
            glm::any(glm::greaterThanEqual(point, chunk.max)))
            continue;
        float edge_distance = std::numeric_limits<float>::max();
        for (auto const &road : continent.roads())
            edge_distance = std::min(edge_distance, road_distance(road, point) - road.width * 0.5F);
        if (edge_distance < -0.35F || edge_distance > verge_width ||
            random > glm::clamp((verge_width - edge_distance) / 3.0F, 0.0F, 1.0F) ||
            ground_noise(point / 4.0F, 4096) < 0.25F)
            continue;
        auto const sample = decoration_ground(continent, point, 0.4F);
        if (!sample)
            continue;
        int const group =
            std::min(7, static_cast<int>(ground_noise(cell + glm::vec2{317, 691}, 4096) * 8.0F));
        auto &tuft = grass[group];
        bool const edge = edge_distance < 0.65F;
        glm::vec3 const base{point.x, sample->height - 0.025F, point.y};
        for (int blade = 0; blade < 7; ++blade) {
            float const angle = (blade * 0.382F + random) * 2 * std::numbers::pi_v<float>;
            glm::vec3 const across{std::cos(angle), 0, std::sin(angle)};
            glm::vec3 const lean{-across.z, 0, across.x};
            float const height =
                (edge ? 0.08F : 0.20F) +
                (edge ? 0.10F : 0.25F) *
                    ground_noise(cell + glm::vec2{blade * 3.1F, blade * 7.7F}, 4096);
            auto const root = base + across * ((blade - 3) * 0.04F);
            auto const middle = root + glm::vec3{0, height * 0.55F, 0} + lean * 0.035F;
            auto const tip = root + glm::vec3{0, height, 0} + lean * (height * 0.4F);
            auto const color = glm::mix(glm::vec3{0.09F, 0.19F, 0.03F},
                                        glm::vec3{0.29F, 0.35F, 0.075F}, variation);
            tuft.triangle(root - across * 0.025F, root + across * 0.025F, middle + across * 0.016F,
                          color * 0.75F, true);
            tuft.triangle(root - across * 0.025F, middle + across * 0.016F,
                          middle - across * 0.016F, color * 0.85F, true);
            tuft.triangle(middle - across * 0.016F, middle + across * 0.016F, tip, color, true);
        }
    }
}

} // namespace

ContinentView::ContinentView(Device const &device, GpuTexture const &texture,
                             Continent const &continent)
    : material_(MaterialInfo{.parameters = {.metallic_factor = 0.0F, .roughness_factor = 0.8F},
                             .base_color = {.texture = &texture}})
{
    // A four-metre patch combines broad soil flecks, aggregate and fine grains.
    // Derive normals from millimetre-scale heights using the same noise as the color.
    constexpr int size = 256;
    std::array<std::vector<std::uint8_t>, 3> pixels;
    for (auto &map : pixels)
        map.resize(size * size * 4);
    auto height = [](glm::vec2 uv) {
        return 0.004F * ground_noise(uv * 16.0F, 16) + 0.002F * ground_noise(uv * 64.0F, 64) +
               0.0008F * ground_noise(uv * 128.0F, 128);
    };
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            glm::vec2 const uv{static_cast<float>(x) / size, static_cast<float>(y) / size};
            float const soil = glm::smoothstep(0.35F, 0.7F, ground_noise(uv * 4.0F, 4));
            float const grain = ground_noise(uv * 128.0F, 128);
            float const aggregate = ground_noise(uv * 64.0F, 64);
            auto const tint =
                glm::mix(glm::vec3{0.82F, 0.86F, 0.75F}, glm::vec3{0.90F, 0.82F, 0.70F}, soil);
            auto const color = tint * (0.80F + 0.12F * grain + 0.08F * aggregate);
            constexpr float step = 1.0F / size;
            auto const normal = glm::normalize(
                glm::vec3{-(height(uv + glm::vec2{step, 0}) - height(uv - glm::vec2{step, 0})) /
                              (2.0F * step * ground_texture_metres),
                          -(height(uv + glm::vec2{0, step}) - height(uv - glm::vec2{0, step})) /
                              (2.0F * step * ground_texture_metres),
                          1.0F});
            auto const offset = (y * size + x) * 4;
            for (int c = 0; c < 3; ++c) {
                pixels[0][offset + c] = static_cast<std::uint8_t>(
                    std::lround(glm::clamp(color[c], 0.0F, 1.0F) * 255.0F));
                pixels[2][offset + c] =
                    static_cast<std::uint8_t>(std::lround((normal[c] * 0.5F + 0.5F) * 255.0F));
            }
            pixels[1][offset] = 255;
            pixels[1][offset + 1] =
                static_cast<std::uint8_t>(std::lround((0.84F + 0.14F * aggregate) * 255.0F));
            pixels[1][offset + 2] = 0;
            for (auto &map : pixels)
                map[offset + 3] = 255;
        }
    }
    for (std::size_t i = 0; i < pixels.size(); ++i)
        ground_textures_[i] = std::make_unique<GpuTexture>(
            device, vk::Extent2D{.width = size, .height = size}, pixels[i],
            i == 0 ? TextureColorSpace::Srgb : TextureColorSpace::Linear);
    ground_material_.parameters = {.metallic_factor = 0.0F, .roughness_factor = 1.0F};
    ground_material_.base_color.texture = ground_textures_[0].get();
    ground_material_.metallic_roughness.texture = ground_textures_[1].get();
    ground_material_.normal.texture = ground_textures_[2].get();

    // 16 x 16 tiles per chunk avoids thousands of tiny draws. Ground and
    // buildings use separate meshes so surface detail stays off the buildings.
    float const chunk_size = continent.tile_size() * 16.0F;
    auto const bounds = continent.bounds();

    struct PendingDraw {
        std::size_t mesh_index;
        glm::vec3 origin;
        int grass_level;
        MaterialInfo const *material;
    };

    std::vector<PendingDraw> pending;
    auto append_geometry = [&](Geometry &geometry, MaterialInfo const &material,
                               int grass_level = 0) {
        if (geometry.vertices.empty())
            return;
        auto const mesh_index = models_.size();
        scene::Model model{geometry.vertices, geometry.indices};
        if (material.normal.texture)
            model.generate_tangents();
        models_.push_back(std::make_unique<scene::GpuModel>(device, std::move(model)));
        pending.push_back({.mesh_index = mesh_index,
                           .origin = geometry.origin,
                           .grass_level = grass_level,
                           .material = &material});
    };
    // Grass shares eight density batches across the resident world, rather than
    // multiplying draw count by the number of chunks along every road.
    std::array<Geometry, 8> grass;
    for (auto &batch : grass)
        batch.origin = glm::vec3{0};
    for (float z = bounds.min.y; z < bounds.max.y; z += chunk_size) {
        for (float x = bounds.min.x; x < bounds.max.x; x += chunk_size) {
            GroundBounds const chunk{{x, z}, glm::min(glm::vec2{x, z} + chunk_size, bounds.max)};
            Geometry terrain{.origin = {x, 0.0F, z}, .vertices = {}, .indices = {}};
            ground(terrain, continent, chunk);
            Geometry buildings{.origin = terrain.origin, .vertices = {}, .indices = {}};
            for (auto const &block : continent.blocks()) {
                glm::vec3 const min{std::max(block.min.x, chunk.min.x), block.min.y,
                                    std::max(block.min.z, chunk.min.y)};
                glm::vec3 const max{std::min(block.max.x, chunk.max.x), block.max.y,
                                    std::min(block.max.z, chunk.max.y)};
                if (min.x < max.x && min.z < max.z)
                    buildings.box(min, max, color_of(block.surface));
            }
            roadside_details(buildings, continent, chunk);
            roadside_grass(grass, continent, chunk);
            append_geometry(terrain, ground_material_);
            append_geometry(buildings, material_);
        }
    }
    for (int group = 0; group < 8; ++group)
        append_geometry(grass[group], material_, group + 1);
    draws_.reserve(pending.size());
    for (auto const &entry : pending) {
        if (entry.grass_level != 0)
            grass_batches_.push_back({.draw_index = draws_.size(), .level = entry.grass_level});
        draws_.push_back({.model = models_[entry.mesh_index].get(),
                          .material = entry.material,
                          .world_transform = glm::translate(glm::mat4{1.0F}, entry.origin),
                          .visible = entry.grass_level <= 4});
    }
}

} // namespace lc1

namespace lc1 {

std::span<ContinentView::GrassBatch const> ContinentView::grass_batches() const
{
    return grass_batches_;
}

std::span<scene::SceneModel const> ContinentView::draw_items() const
{
    return draws_;
}

} // namespace lc1
