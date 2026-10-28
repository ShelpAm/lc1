#include "lc1/game/continent/continent.hpp"

#include "lc1/error.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>

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

glm::vec3 terrain_normal(float dx, float dz)
{
    return glm::normalize(glm::vec3{-dx, 1.0F, -dz});
}

bool valid(GroundBounds bounds)
{
    return finite(bounds.min) && finite(bounds.max) && bounds.min.x < bounds.max.x &&
           bounds.min.y < bounds.max.y;
}

bool contains(GroundBounds bounds, glm::vec2 point)
{
    return glm::all(glm::greaterThanEqual(point, bounds.min)) &&
           glm::all(glm::lessThan(point, bounds.max));
}

template <typename T, typename Id> T const *find_id(std::vector<T> const &items, Id id)
{
    auto const found = std::ranges::find(items, id, &T::id);
    return found == items.end() ? nullptr : &*found;
}

template <typename T> void validate_ids(std::vector<T> const &items)
{
    std::unordered_set<std::uint32_t> ids;
    for (auto const &item : items) {
        auto const id = static_cast<std::uint32_t>(item.id);
        if (id == 0 || item.name.empty() || !ids.insert(id).second)
            fail("world map IDs must be nonzero and unique within each category, with names");
    }
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
void visit_terrain_obstacles(ContinentData const &data, std::span<std::uint8_t const> blocked,
                             GroundBounds query, Visit visit)
{
    auto const columns = data.tiles.front().size();
    auto const rows = data.tiles.size();
    glm::vec2 const grid_max{static_cast<float>(columns - 1), static_cast<float>(rows - 1)};
    // Include neighbouring cells: subtraction at a large world origin can round a
    // grazing body onto a grid edge. The exact overlap/sweep still rejects misses.
    auto const first = glm::clamp(glm::floor((query.min - data.origin) / data.tile_size) - 1.0F,
                                  glm::vec2{0.0F}, grid_max);
    auto const last = glm::clamp(glm::floor((query.max - data.origin) / data.tile_size) + 1.0F,
                                 glm::vec2{0.0F}, grid_max);
    for (auto z = static_cast<std::size_t>(first.y); z <= static_cast<std::size_t>(last.y); ++z) {
        for (auto x = static_cast<std::size_t>(first.x); x <= static_cast<std::size_t>(last.x);
             ++x) {
            if (!blocked[z * columns + x])
                continue;
            auto const min = data.origin + glm::vec2{static_cast<float>(x), static_cast<float>(z)} *
                                               data.tile_size;
            visit(GroundBounds{min, min + data.tile_size});
        }
    }
}

} // namespace

float Road::length() const
{
    float result = 0.0F;
    for (std::size_t i = 1; i < points.size(); ++i)
        result += glm::distance(points[i - 1], points[i]);
    return result;
}

bool Road::contains(glm::vec2 position, float clearance) const
{
    if (!finite(position) || !std::isfinite(clearance) || clearance < 0.0F)
        return false;
    float const radius = width * 0.5F + clearance;
    for (std::size_t i = 1; i < points.size(); ++i) {
        glm::vec2 const start = points[i - 1];
        glm::vec2 const delta = points[i] - start;
        float const squared_length = glm::dot(delta, delta);
        if (squared_length <= 0.0F)
            continue;
        float const time =
            glm::clamp(glm::dot(position - start, delta) / squared_length, 0.0F, 1.0F);
        if (glm::distance(position, start + time * delta) <= radius)
            return true;
    }
    return false;
}

void Continent::validate_identity(ContinentIdentity const &identity)
{
    if (identity.id != "present" && identity.id != "prototype")
        fail("unknown continent id {}", identity.id);
    if (identity.version != 1)
        fail("unsupported continent {} version {}", identity.id, identity.version);
}

Continent Continent::load(ContinentIdentity const &identity,
                          std::span<ContinentBlock const> spawn_blocks)
{
    validate_identity(identity);
    return identity.id == "present" ? make_present(spawn_blocks) : make_prototype(spawn_blocks);
}

Continent::Continent(ContinentData data) : data_(std::move(data))
{
    if (!finite(data_.origin) || !std::isfinite(data_.tile_size) || data_.tile_size <= 0.0F ||
        data_.tiles.empty() || data_.tiles.front().empty())
        fail("invalid continent grid");
    validate_ids(data_.regions);
    validate_ids(data_.locations);
    validate_ids(data_.roads);
    for (auto const &area : data_.regions) {
        if (area.type < RegionType::Plain || area.type > RegionType::Sea)
            fail("unknown continent region type");
    }
    for (auto const &row : data_.tiles) {
        if (row.size() != columns())
            fail("continent tile rows must have equal widths");
        for (auto const &tile : row) {
            if (!region(tile.region))
                fail("continent tile references an unknown region");
        }
    }
    if (!data_.height_samples.empty()) {
        if (data_.height_samples.size() != rows() + 1)
            fail("continent height samples must have one more row than the tile grid");
        for (auto const &row : data_.height_samples) {
            if (row.size() != columns() + 1)
                fail("continent height samples must have one more column than the tile grid");
            for (float const height : row) {
                if (!std::isfinite(height))
                    fail("continent height samples must be finite");
            }
        }
    }
    if (!valid(bounds()) || !finite(data_.spawn))
        fail("invalid continent bounds or spawn");
    auto const spawn_height = ground_height({data_.spawn.x, data_.spawn.z});
    if (!spawn_height || std::abs(*spawn_height - data_.spawn.y) > 0.001F)
        fail("spawn is outside walkable terrain or not on the ground surface");
    blocked_tiles_.reserve(rows() * columns());
    for (auto const &row : data_.tiles)
        for (auto const &tile : row)
            blocked_tiles_.push_back(!region(tile.region)->walkable());
    for (auto const &patch : data_.patches) {
        if (!valid(patch.bounds))
            fail("invalid continent ground patch");
    }
    for (auto const &block : data_.blocks) {
        if (!finite(block.min) || !finite(block.max) ||
            glm::any(glm::greaterThanEqual(block.min, block.max)))
            fail("invalid continent block");
    }
    for (auto const &place : data_.locations) {
        if (!valid(place.bounds) || glm::any(glm::lessThan(place.bounds.min, bounds().min)) ||
            glm::any(glm::greaterThan(place.bounds.max, bounds().max)) || !finite(place.entrance) ||
            glm::any(glm::lessThan(place.entrance, place.bounds.min)) ||
            glm::any(glm::greaterThan(place.entrance, place.bounds.max)) || !region(place.region) ||
            region_at(place.entrance) != region(place.region) || place.type < LocationType::City ||
            place.type > LocationType::Wilderness)
            fail("invalid location layout or obstructed entrance: {}", place.name);
        auto const entrance_height = ground_height(place.entrance);
        if (!entrance_height ||
            !can_stand({place.entrance.x, *entrance_height, place.entrance.y}, 0.3F, 1.8F))
            fail("invalid location layout or obstructed entrance: {}", place.name);
    }
    for (auto const &path : data_.roads) {
        auto const *from = location(path.from);
        auto const *to = location(path.to);
        if (!from || !to || from == to || !std::isfinite(path.width) || path.width <= 0.0F ||
            path.points.size() < 2 || path.points.front() != from->entrance ||
            path.points.back() != to->entrance)
            fail("invalid road endpoints or width: {}", path.name);
        // Conservative square clearance guarantees the full road width is on passable
        // ground, including bends. There are no bridges or tunnels in this ground layer.
        float const half_width = path.width * 0.5F;
        for (std::size_t i = 0; i < path.points.size(); ++i) {
            auto const point = path.points[i];
            if (!can_stand({point.x, 0, point.y}, half_width, 1.8F))
                fail("road leaves passable ground: {}", path.name);
            if (i == 0)
                continue;
            glm::vec2 const delta = point - path.points[i - 1];
            if (!finite(delta) || glm::length(delta) < 0.001F)
                fail("road has a degenerate segment: {}", path.name);
            auto const previous = path.points[i - 1];
            auto const reached = move({previous.x, 0, previous.y}, delta, half_width, 1.8F);
            if (glm::distance(glm::vec2{reached.x, reached.z}, point) > 0.001F)
                fail("road crosses an obstacle: {}", path.name);
        }
    }
}

Region const *Continent::region(RegionId id) const
{
    return find_id(data_.regions, id);
}

Location const *Continent::location(LocationId id) const
{
    return find_id(data_.locations, id);
}

Road const *Continent::road(RoadId id) const
{
    return find_id(data_.roads, id);
}

Region const *Continent::region_at(glm::vec2 position) const
{
    auto const tile = tile_at(position);
    return tile ? region(tile->region) : nullptr;
}

Location const *Continent::location_at(glm::vec2 position) const
{
    // Overlapping sites select the most local footprint; equal sizes keep author order.
    Location const *result = nullptr;
    float smallest_area = std::numeric_limits<float>::infinity();
    for (auto const &place : data_.locations) {
        auto const size = place.bounds.max - place.bounds.min;
        float const area = size.x * size.y;
        if (contains(place.bounds, position) && area < smallest_area) {
            result = &place;
            smallest_area = area;
        }
    }
    return result;
}

Road const *Continent::road_at(glm::vec2 position) const
{
    // Intersections return the first authored road. Use roads_from for graph adjacency.
    for (auto const &path : data_.roads) {
        if (path.contains(position))
            return &path;
    }
    return nullptr;
}

std::vector<RoadId> Continent::roads_from(LocationId id) const
{
    std::vector<RoadId> result;
    for (auto const &path : data_.roads) {
        if (path.from == id || path.to == id)
            result.push_back(path.id);
    }
    return result;
}

GroundBounds Continent::bounds() const
{
    return {data_.origin,
            data_.origin +
                glm::vec2{static_cast<float>(columns()), static_cast<float>(rows())} * tile_size()};
}

std::optional<TileData> Continent::tile_at(glm::vec2 position) const
{
    auto const area = bounds();
    if (!finite(position) || glm::any(glm::lessThan(position, area.min)) ||
        glm::any(glm::greaterThanEqual(position, area.max)))
        return std::nullopt;
    glm::vec2 const grid = (position - area.min) / tile_size();
    // Subtraction can round a point just below max up to the grid extent.
    auto const x = std::min(static_cast<std::size_t>(grid.x), columns() - 1);
    auto const z = std::min(static_cast<std::size_t>(grid.y), rows() - 1);
    return data_.tiles[z][x];
}

std::optional<GroundSample> Continent::terrain_sample(glm::vec2 position) const
{
    GroundBounds const area = bounds();
    if (!finite(position) || glm::any(glm::lessThan(position, area.min)) ||
        glm::any(glm::greaterThan(position, area.max)))
        return std::nullopt;

    if (data_.height_samples.empty())
        return GroundSample{};

    glm::vec2 const grid = (position - area.min) / tile_size();
    auto const x = std::min(static_cast<std::size_t>(grid.x), columns() - 1);
    auto const z = std::min(static_cast<std::size_t>(grid.y), rows() - 1);
    float const fx = grid.x - static_cast<float>(x);
    float const fz = grid.y - static_cast<float>(z);

    float const h00 = data_.height_samples[z][x];
    float const h10 = data_.height_samples[z][x + 1];
    float const h01 = data_.height_samples[z + 1][x];
    float const h11 = data_.height_samples[z + 1][x + 1];
    float const height = glm::mix(glm::mix(h00, h10, fx), glm::mix(h01, h11, fx), fz);

    // Heights are stored at cell corners, so the slope is constant inside a cell.
    float const dx = ((h10 - h00) * (1.0F - fz) + (h11 - h01) * fz) / tile_size();
    float const dz = ((h01 - h00) * (1.0F - fx) + (h11 - h10) * fx) / tile_size();
    return GroundSample{.height = height, .normal = terrain_normal(dx, dz)};
}

std::optional<float> Continent::ground_height(glm::vec2 position) const
{
    auto const area = region_at(position);
    if (!area || !area->walkable())
        return std::nullopt;
    auto const sample = terrain_sample(position);
    return sample ? std::optional<float>{sample->height} : std::nullopt;
}

std::optional<GroundSample> Continent::support_below(glm::vec3 feet, float max_distance,
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

    if (auto const sample = terrain_sample({feet.x, feet.z})) {
        auto const *area = region_at({feet.x, feet.z});
        if (area && area->walkable())
            consider(*sample);
    }
    for (auto const &block : data_.blocks) {
        if (!block.walkable_top)
            continue;
        if (feet.x + footprint_radius <= block.min.x || feet.x - footprint_radius >= block.max.x ||
            feet.z + footprint_radius <= block.min.z || feet.z - footprint_radius >= block.max.z)
            continue;
        consider({.height = block.max.y, .normal = {0.0F, 1.0F, 0.0F}});
    }
    return best;
}

bool Continent::can_stand(glm::vec3 feet, float half_width, float height) const
{
    if (!finite(feet) || !std::isfinite(half_width) || !std::isfinite(height) ||
        half_width <= 0.0F || height <= 0.0F)
        return false;
    glm::vec2 const position{feet.x, feet.z};
    auto const area = bounds();
    if (glm::any(glm::lessThan(position - half_width, area.min)) ||
        glm::any(glm::greaterThan(position + half_width, area.max)))
        return false;
    bool blocked = false;
    visit_terrain_obstacles(data_, blocked_tiles_, {position - half_width, position + half_width},
                            [&](GroundBounds obstacle) {
                                blocked = blocked || overlaps(position, half_width, obstacle);
                            });
    if (blocked)
        return false;
    for (auto const &block : data_.blocks) {
        if (block.solid && block.max.y > feet.y && block.min.y < feet.y + height &&
            overlaps(position, half_width,
                     {{block.min.x, block.min.z}, {block.max.x, block.max.z}}))
            return false;
    }
    return true;
}

glm::vec3 Continent::move(glm::vec3 feet, glm::vec2 displacement, float half_width,
                          float height) const
{
    if (!finite(displacement) || !can_stand(feet, half_width, height))
        fail("invalid starting position or displacement for continent movement");
    glm::vec2 position{feet.x, feet.z};
    glm::vec2 remaining = displacement;
    GroundBounds const limits{bounds().min + half_width, bounds().max - half_width};
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
        visit_terrain_obstacles(data_, blocked_tiles_,
                                {glm::min(position, position + remaining) - half_width,
                                 glm::max(position, position + remaining) + half_width},
                                consider);
        for (auto const &block : data_.blocks) {
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

namespace lc1 {

bool Region::walkable() const
{
    return type != RegionType::Sea && type != RegionType::Mountain;
}

ContinentIdentity const &Continent::identity() const
{
    return data_.identity;
}

float Continent::tile_size() const
{
    return data_.tile_size;
}

std::size_t Continent::columns() const
{
    return data_.tiles.front().size();
}

std::size_t Continent::rows() const
{
    return data_.tiles.size();
}

glm::vec3 Continent::spawn() const
{
    return data_.spawn;
}

std::span<GroundPatch const> Continent::patches() const
{
    return data_.patches;
}

std::span<ContinentBlock const> Continent::blocks() const
{
    return data_.blocks;
}

std::span<Region const> Continent::regions() const
{
    return data_.regions;
}

std::span<Location const> Continent::locations() const
{
    return data_.locations;
}

std::span<Road const> Continent::roads() const
{
    return data_.roads;
}

} // namespace lc1
