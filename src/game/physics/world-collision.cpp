#include "lc1/game/physics/world-collision.hpp"

#include "lc1/error.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lc1 {
namespace {

bool finite(glm::vec2 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y);
}

bool finite(glm::vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool overlaps(glm::vec2 position, float half_width, GroundBounds obstacle)
{
    return position.x + half_width > obstacle.min.x && position.x - half_width < obstacle.max.x &&
           position.y + half_width > obstacle.min.y && position.y - half_width < obstacle.max.y;
}

struct Contact {
    float time = 1.0F;
    glm::vec2 normal{0.0F};
};

// Slab intersection against an expanded obstacle. Parallel motion exactly on an
// edge is allowed, so sliding does not stick. A point-sized corner touch is harmless.
std::optional<Contact> sweep(glm::vec2 position, glm::vec2 delta, GroundBounds obstacle)
{
    float enter = -std::numeric_limits<float>::infinity();
    float leave = std::numeric_limits<float>::infinity();
    glm::vec2 normal{0.0F};
    for (int axis = 0; axis < 2; ++axis) {
        if (delta[axis] == 0.0F) {
            if (position[axis] <= obstacle.min[axis] || position[axis] >= obstacle.max[axis])
                return std::nullopt;
            continue;
        }
        float const first = (obstacle.min[axis] - position[axis]) / delta[axis];
        float const second = (obstacle.max[axis] - position[axis]) / delta[axis];
        float const near = std::min(first, second);
        if (near > enter) {
            enter = near;
            normal = glm::vec2{0.0F};
            normal[axis] = delta[axis] > 0.0F ? -1.0F : 1.0F;
        }
        leave = std::min(leave, std::max(first, second));
    }
    if (enter < 0.0F || enter > 1.0F || enter >= leave || leave <= 0.0F)
        return std::nullopt;
    return Contact{enter, normal};
}

template <typename Visit>
void visit_terrain_obstacles(Continent const &continent, GroundBounds query, Visit visit)
{
    auto const columns = continent.columns();
    auto const rows = continent.rows();
    glm::vec2 const grid_max{static_cast<float>(columns - 1), static_cast<float>(rows - 1)};
    // Include neighbouring cells: subtraction at a large world origin can round a
    // grazing body onto a grid edge. The exact overlap/sweep still rejects misses.
    auto const first =
        glm::clamp(glm::floor((query.min - continent.bounds().min) / continent.tile_size()) - 1.0F,
                   glm::vec2{0.0F}, grid_max);
    auto const last =
        glm::clamp(glm::floor((query.max - continent.bounds().min) / continent.tile_size()) + 1.0F,
                   glm::vec2{0.0F}, grid_max);
    for (auto z = static_cast<std::size_t>(first.y); z <= static_cast<std::size_t>(last.y); ++z) {
        for (auto x = static_cast<std::size_t>(first.x); x <= static_cast<std::size_t>(last.x);
             ++x) {
            if (continent.terrain_walkable(x, z))
                continue;
            auto const min =
                continent.bounds().min +
                glm::vec2{static_cast<float>(x), static_cast<float>(z)} * continent.tile_size();
            visit(GroundBounds{min, min + continent.tile_size()});
        }
    }
}

} // namespace

WorldCollision::WorldCollision(Continent const &continent) : continent_(&continent)
{
}

std::optional<float> WorldCollision::ground_height(glm::vec2 position) const
{
    return continent_->ground_height(position);
}

std::optional<GroundSample> WorldCollision::support_below(glm::vec3 feet, float max_distance,
                                                          float footprint_radius) const
{
    if (!finite(feet) || !std::isfinite(max_distance) || max_distance < 0.0F ||
        !std::isfinite(footprint_radius) || footprint_radius < 0.0F)
        fail("invalid ground support query");

    constexpr float tolerance = 0.001F;
    float const lowest = feet.y - max_distance - tolerance;
    float const highest = feet.y + tolerance;

    std::optional<GroundSample> best;
    auto consider = [&](GroundSample sample) {
        if (sample.height < lowest || sample.height > highest)
            return;
        if (!best || sample.height > best->height)
            best = sample;
    };

    if (auto const sample = continent_->terrain_sample({feet.x, feet.z})) {
        auto const *area = continent_->region_at({feet.x, feet.z});
        if (area && area->walkable())
            consider(*sample);
    }
    for (auto const &block : continent_->blocks()) {
        if (!block.walkable_top)
            continue;
        if (feet.x + footprint_radius <= block.min.x || feet.x - footprint_radius >= block.max.x ||
            feet.z + footprint_radius <= block.min.z || feet.z - footprint_radius >= block.max.z)
            continue;
        consider({.height = block.max.y, .normal = {0.0F, 1.0F, 0.0F}});
    }
    return best;
}

std::optional<float> WorldCollision::ceiling_above(glm::vec3 head, float max_distance,
                                                   float footprint_radius) const
{
    if (!finite(head) || !std::isfinite(max_distance) || max_distance < 0.0F ||
        !std::isfinite(footprint_radius) || footprint_radius < 0.0F)
        fail("invalid ceiling query");
    std::optional<float> lowest;
    for (auto const &block : continent_->blocks()) {
        if (!block.solid || block.min.y < head.y || block.min.y - head.y > max_distance ||
            head.x + footprint_radius <= block.min.x || head.x - footprint_radius >= block.max.x ||
            head.z + footprint_radius <= block.min.z || head.z - footprint_radius >= block.max.z)
            continue;
        if (!lowest || block.min.y < *lowest)
            lowest = block.min.y;
    }
    return lowest;
}

bool WorldCollision::can_stand(glm::vec3 feet, float half_width, float height) const
{
    if (!finite(feet) || !std::isfinite(half_width) || !std::isfinite(height) ||
        half_width <= 0.0F || height <= 0.0F)
        return false;
    glm::vec2 const position{feet.x, feet.z};
    auto const area = continent_->bounds();
    if (glm::any(glm::lessThan(position - half_width, area.min)) ||
        glm::any(glm::greaterThan(position + half_width, area.max)))
        return false;
    bool blocked = false;
    visit_terrain_obstacles(*continent_, {position - half_width, position + half_width},
                            [&](GroundBounds obstacle) {
                                blocked = blocked || overlaps(position, half_width, obstacle);
                            });
    if (blocked)
        return false;
    for (auto const &block : continent_->blocks()) {
        if (block.solid && block.max.y > feet.y && block.min.y < feet.y + height &&
            overlaps(position, half_width,
                     {{block.min.x, block.min.z}, {block.max.x, block.max.z}}))
            return false;
    }
    return true;
}

glm::vec3 WorldCollision::move(glm::vec3 feet, glm::vec2 displacement, float half_width,
                               float height) const
{
    if (!finite(displacement) || !can_stand(feet, half_width, height))
        fail("invalid starting position or displacement for continent movement");
    glm::vec2 position{feet.x, feet.z};
    glm::vec2 remaining = displacement;
    GroundBounds const limits{continent_->bounds().min + half_width,
                              continent_->bounds().max - half_width};
    for (int iteration = 0; iteration < 4 && glm::dot(remaining, remaining) > 1e-12F; ++iteration) {
        Contact contact;
        for (int axis = 0; axis < 2; ++axis) {
            if (remaining[axis] == 0.0F)
                continue;
            float const boundary = remaining[axis] > 0.0F ? limits.max[axis] : limits.min[axis];
            float const time = (boundary - position[axis]) / remaining[axis];
            if (time >= 0.0F && time < contact.time) {
                contact.time = time;
                contact.normal = glm::vec2{0.0F};
                contact.normal[axis] = remaining[axis] > 0.0F ? -1.0F : 1.0F;
            }
        }
        auto consider = [&](GroundBounds obstacle) {
            auto const hit =
                sweep(position, remaining, {obstacle.min - half_width, obstacle.max + half_width});
            if (hit && hit->time < contact.time)
                contact = *hit;
        };
        visit_terrain_obstacles(*continent_,
                                {glm::min(position, position + remaining) - half_width,
                                 glm::max(position, position + remaining) + half_width},
                                consider);
        for (auto const &block : continent_->blocks()) {
            if (block.solid && block.max.y > feet.y && block.min.y < feet.y + height)
                consider({{block.min.x, block.min.z}, {block.max.x, block.max.z}});
        }
        if (contact.time == 1.0F) {
            position += remaining;
            break;
        }
        // Leave a tenth of a millimetre to keep floating point roundoff outside solids.
        float const safe_time = std::max(0.0F, contact.time - 0.0001F / glm::length(remaining));
        position += remaining * safe_time;
        remaining *= 1.0F - safe_time;
        remaining -= contact.normal * glm::dot(remaining, contact.normal);
    }
    position = glm::clamp(position, limits.min, limits.max);
    return {position.x, feet.y, position.y};
}

} // namespace lc1
