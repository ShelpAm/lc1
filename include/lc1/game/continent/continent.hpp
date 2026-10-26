#pragma once

#include "lc1/game/continent/identity.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace lc1 {

enum class RegionType : std::uint8_t {
    Plain,
    Forest,
    Mountain,
    Wasteland,
    Sea,
};

// Stable identities, independent of vector order. Zero is reserved for an invalid ID.
enum class RegionId : std::uint32_t {};
enum class LocationId : std::uint32_t {};
enum class RoadId : std::uint32_t {};

struct Region {
    RegionId id;
    std::string name;
    RegionType type = RegionType::Plain;

    bool walkable() const;
};

struct TileData {
    RegionId region{1};
};

enum class ContinentSurface { Grass, Road, Stone, Plaster, Roof, Wood, Window, Foliage, Canvas };

// XZ coordinates, in metres. Geometry, movement and city placement share these bounds.
struct GroundBounds {
    glm::vec2 min;
    glm::vec2 max;
};

struct GroundPatch {
    GroundBounds bounds;
    ContinentSurface surface = ContinentSurface::Grass;
};

struct ContinentBlock {
    glm::vec3 min;
    glm::vec3 max;
    ContinentSurface surface = ContinentSurface::Stone;
    bool solid = true;
    // Marks the top face as a walkable support surface. Walls and building shells keep
    // this false; bridges, terraces and roofs that characters may stand on set it true.
    bool walkable_top = false;
};

// A point on a terrain or walkable surface. `normal` is the surface normal used for
// slope checks and rendering; `height` is the world-space Y of the surface.
struct GroundSample {
    float height = 0.0F;
    glm::vec3 normal{0.0F, 1.0F, 0.0F};
};

enum class LocationType : std::uint8_t { City, Village, Fortress, Port, Ruins, Wilderness };

// Spatial layout only. A location may span tiles/regions; region identifies its entrance.
// Ownership, resources and garrison belong to persistent game state, keyed by id.
struct Location {
    LocationId id;
    std::string name;
    LocationType type;
    RegionId region;
    GroundBounds bounds;
    glm::vec2 entrance;
};

// Undirected connection between location entrances. Points are an XZ centreline in metres.
// Roads paint the existing ground; they do not override terrain or building collision.
struct Road {
    RoadId id;
    std::string name;
    LocationId from;
    LocationId to;
    float width;
    std::vector<glm::vec2> points;

    float length() const;
    bool contains(glm::vec2 position, float clearance = 0.0F) const;
};

struct ContinentData {
    glm::vec2 origin;
    float tile_size;
    std::vector<std::vector<TileData>> tiles; // tiles[z][x]
    glm::vec3 spawn;
    std::vector<GroundPatch> patches;
    std::vector<ContinentBlock> blocks;
    std::vector<Region> regions = {{RegionId{1}, "Plain", RegionType::Plain}};
    std::vector<Location> locations;
    std::vector<Road> roads;
    // Terrain heights at tile corners, indexed [z][x]. Expected dimensions are
    // (rows + 1) x (columns + 1). Empty means a flat world at Y=0.
    std::vector<std::vector<float>> height_samples;
    ContinentIdentity identity{.id = "custom", .version = 1};
};

class Continent {
  public:
    explicit Continent(ContinentData data);
    // Optional application scenery, relative to spawn. Blocks participate in
    // both the continent's visible geometry and its static collision.
    static Continent make_prototype(std::span<ContinentBlock const> spawn_blocks = {});
    // The playable continent follows continent-present.svg, the source of the PNG.
    static Continent make_present(std::span<ContinentBlock const> spawn_blocks = {});

    static void validate_identity(ContinentIdentity const &identity);
    static Continent load(ContinentIdentity const &identity,
                          std::span<ContinentBlock const> spawn_blocks = {});

    ContinentIdentity const &identity() const;

    GroundBounds bounds() const;

    float tile_size() const;

    std::size_t columns() const;

    std::size_t rows() const;

    std::optional<TileData> tile_at(glm::vec2 position) const;

    glm::vec3 spawn() const;

    std::span<GroundPatch const> patches() const;

    std::span<ContinentBlock const> blocks() const;

    std::span<Region const> regions() const;

    std::span<Location const> locations() const;

    std::span<Road const> roads() const;

    // Nullable, non-owning queries. Results borrow this immutable continent's storage.
    Region const *region(RegionId id) const;
    Location const *location(LocationId id) const;
    Road const *road(RoadId id) const;
    Region const *region_at(glm::vec2 position) const;
    Location const *location_at(glm::vec2 position) const;
    Road const *road_at(glm::vec2 position) const;
    std::vector<RoadId> roads_from(LocationId id) const;

    // Terrain surface at any XZ inside the continent, including non-walkable sea and
    // mountain cells. The normal follows the interpolated height field.
    std::optional<GroundSample> terrain_sample(glm::vec2 position) const;

    // Samples walkable terrain only. This remains the cheap region query; use
    // support_below() when the character needs the surface under its feet.
    std::optional<float> ground_height(glm::vec2 position) const;

    // Highest walkable surface at XZ that is not above `feet.y`, and no farther than
    // `max_distance` below it. Terrain and walkable block tops participate. A nonzero
    // footprint radius keeps a body supported while any part of its footprint overlaps
    // a block top, which prevents falling through a plinth edge and into its side.
    std::optional<GroundSample> support_below(glm::vec3 feet, float max_distance,
                                              float footprint_radius = 0.0F) const;

    // Validates the upright body against static obstacles at its current feet height.
    bool can_stand(glm::vec3 feet, float half_width, float height) const;

    // Sweeps an upright box, then slides along contact planes. The result preserves the
    // input feet height; vertical support is queried separately by the character.
    glm::vec3 move(glm::vec3 feet, glm::vec2 displacement, float half_width, float height) const;

  private:
    ContinentData data_;
    // Cached passability in row-major order. Collision visits only the swept tile range.
    std::vector<std::uint8_t> blocked_tiles_;
};

} // namespace lc1
