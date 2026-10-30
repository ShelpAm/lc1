#include "lc1/game/continent/continent.hpp"

#include "lc1/error.hpp"
#include "lc1/game/physics/world-collision.hpp"

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
    walkable_tiles_.reserve(rows() * columns());
    for (auto const &row : data_.tiles)
        for (auto const &tile : row)
            walkable_tiles_.push_back(region(tile.region)->walkable());
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
            !WorldCollision{*this}.can_stand({place.entrance.x, *entrance_height, place.entrance.y},
                                             0.3F, 1.8F))
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
            if (!WorldCollision{*this}.can_stand({point.x, 0, point.y}, half_width, 1.8F))
                fail("road leaves passable ground: {}", path.name);
            if (i == 0)
                continue;
            glm::vec2 const delta = point - path.points[i - 1];
            if (!finite(delta) || glm::length(delta) < 0.001F)
                fail("road has a degenerate segment: {}", path.name);
            auto const previous = path.points[i - 1];
            auto const reached =
                WorldCollision{*this}.move({previous.x, 0, previous.y}, delta, half_width, 1.8F);
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

bool Continent::terrain_walkable(std::size_t column, std::size_t row) const
{
    return column < columns() && row < rows() && walkable_tiles_[row * columns() + column];
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
