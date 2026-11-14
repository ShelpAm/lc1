#include "lc1/game/continent/continent.hpp"

#include "layout.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace lc1 {

Continent Continent::make_prototype(std::span<ContinentBlock const> spawn_blocks)
{
    constexpr RegionId plain{1}, forest{2}, mountain{3}, wasteland{4}, sea{5};
    constexpr LocationId city{1}, village{2}, fortress{3}, port{4}, ruins{5}, camp{6};
    ContinentData data{
        .origin = {-384.0F, -576.0F},
        .tile_size = 16.0F,
        .tiles = std::vector(56, std::vector<TileData>(48)),
        .spawn = {0.0F, 0.0F, 32.0F},
        .patches = {},
        .blocks = {},
        .regions = {{plain, "Longwind Plain", RegionType::Plain},
                    {forest, "Eastern Forest", RegionType::Forest},
                    {mountain, "Northern Mountains", RegionType::Mountain},
                    {wasteland, "Redearth Wasteland", RegionType::Wasteland},
                    {sea, "West Sea", RegionType::Sea}},
        .locations =
            {
                {city,
                 "Longwind City",
                 LocationType::City,
                 plain,
                 {{-160, -384}, {160, 0}},
                 {0, 0}},
                {village,
                 "Southfield Village",
                 LocationType::Village,
                 plain,
                 {{-64, 80}, {64, 196}},
                 {0, 112}},
                {fortress,
                 "Northmount Fortress",
                 LocationType::Fortress,
                 plain,
                 {{-240, -448}, {-176, -400}},
                 {-208, -400}},
                {port,
                 "Westshore Port",
                 LocationType::Port,
                 plain,
                 {{-288, 64}, {-224, 128}},
                 {-240, 96}},
                {ruins,
                 "Redearth Ruins",
                 LocationType::Ruins,
                 wasteland,
                 {{80, 208}, {144, 272}},
                 {112, 224}},
                {camp,
                 "Woodland Camp",
                 LocationType::Wilderness,
                 forest,
                 {{208, 32}, {272, 96}},
                 {224, 64}},
            },
        .roads =
            {
                {RoadId{1},
                 "City South Road",
                 city,
                 village,
                 8,
                 {{0, 0}, {0, 64}, {5, 78}, {0, 96}, {0, 112}}},
                {RoadId{2},
                 "Westport Trade Road",
                 village,
                 port,
                 6,
                 {{0, 112}, {-6, 123}, {-12, 134}, {-42, 134}, {-112, 112}, {-240, 96}}},
                {RoadId{3},
                 "Northmount Military Road",
                 port,
                 fortress,
                 6,
                 {{-240, 96}, {-208, 48}, {-208, -400}}},
                {RoadId{4}, "Forest Trail", village, camp, 4, {{0, 112}, {96, 112}, {224, 64}}},
                {RoadId{5},
                 "Old Wasteland Road",
                 village,
                 ruins,
                 4,
                 {{0, 112}, {0, 208}, {64, 208}, {112, 224}}},
            },
        .height_samples = {},
    };
    for (std::size_t z = 0; z < data.tiles.size(); ++z) {
        for (std::size_t x = 0; x < data.tiles[z].size(); ++x) {
            glm::vec2 const position =
                data.origin +
                glm::vec2{static_cast<float>(x), static_cast<float>(z)} * data.tile_size;
            data.tiles[z][x].region = position.x < -288                       ? sea
                                      : position.y < -448                     ? mountain
                                      : position.x >= 192                     ? forest
                                      : position.y >= 192 && position.x >= 32 ? wasteland
                                                                              : plain;
        }
    }
    auto patch = [&](glm::vec2 min, glm::vec2 max, ContinentSurface surface) {
        data.patches.push_back({{min, max}, surface});
    };
    auto block = [&](glm::vec3 min, glm::vec3 max, ContinentSurface surface, bool solid = true) {
        data.blocks.push_back({min, max, surface, solid});
    };

    add_starter_buildings(data);

    // Fortress at the foot of the mountains, with a south-facing gate.
    patch({-240, -448}, {-176, -400}, ContinentSurface::Stone);
    block({-240, 0, -448}, {-176, 8, -444}, ContinentSurface::Stone);
    block({-240, 0, -444}, {-236, 8, -400}, ContinentSurface::Stone);
    block({-180, 0, -444}, {-176, 8, -400}, ContinentSurface::Stone);
    block({-240, 0, -404}, {-214, 8, -400}, ContinentSurface::Stone);
    block({-202, 0, -404}, {-176, 8, -400}, ContinentSurface::Stone);
    for (float x : {-230.0F, -186.0F})
        block({x - 5, 0, -441}, {x + 5, 13, -431}, ContinentSurface::Stone);
    // Port: warehouse, quay and land-side wooden jetties. Water remains impassable.
    patch({-288, 64}, {-278, 128}, ContinentSurface::Wood);
    for (float z : {72.0F, 116.0F}) {
        patch({-278, z - 3}, {-246, z + 3}, ContinentSurface::Wood);
        block({-286, 0, z - 1}, {-284, 7, z + 1}, ContinentSurface::Wood);
    }
    block({-272, 0, 68}, {-250, 7, 84}, ContinentSurface::Plaster);
    block({-274, 7, 66}, {-248, 9, 86}, ContinentSurface::Roof);
    for (float x : {-260.0F, -254.0F, -248.0F})
        block({x, 0, 120}, {x + 4, 3, 124}, ContinentSurface::Wood);
    // Ruined pillars and broken walls leave the entrance and middle open.
    patch({88, 232}, {136, 264}, ContinentSurface::Stone);
    for (float x : {92.0F, 132.0F}) {
        for (float z : {236.0F, 260.0F})
            block({x - 2, 0, z - 2}, {x + 2, x < 100 ? 9 : 4, z + 2}, ContinentSurface::Stone);
    }
    block({90, 0, 264}, {104, 4, 267}, ContinentSurface::Stone);
    block({120, 0, 264}, {134, 2, 267}, ContinentSurface::Stone);
    // Forest campsite: canvas shelters, a fire ring and a supply stack.
    patch({216, 48}, {264, 88}, ContinentSurface::Road);
    for (float z : {52.0F, 80.0F})
        block({244, 0, z - 4}, {256, 4, z + 4}, ContinentSurface::Canvas);
    block({238, 0, 64}, {242, 0.5F, 68}, ContinentSurface::Stone);
    block({258, 0, 68}, {263, 2, 72}, ContinentSurface::Wood);

    for (std::size_t z = 0; z < data.tiles.size(); ++z) {
        for (std::size_t x = 0; x < data.tiles[z].size(); ++x) {
            glm::vec2 const center =
                data.origin +
                (glm::vec2{static_cast<float>(x), static_cast<float>(z)} + 0.5F) * data.tile_size;
            if (data.tiles[z][x].region == mountain) {
                float const height = 12.0F + static_cast<float>((x * 7 + z * 11) % 5) * 5.0F;
                block({center.x - 7, 0, center.y - 7}, {center.x + 7, height, center.y + 7},
                      ContinentSurface::Stone);
            }
            if (data.tiles[z][x].region != forest || (x + z) % 2 != 0)
                continue;
            bool const near_road = std::ranges::any_of(
                data.roads, [&](Road const &road) { return road.contains(center, 6.0F); });
            bool const near_location =
                std::ranges::any_of(data.locations, [&](Location const &place) {
                    return glm::all(glm::greaterThanEqual(center, place.bounds.min - 6.0F)) &&
                           glm::all(glm::lessThanEqual(center, place.bounds.max + 6.0F));
                });
            if (near_road || near_location)
                continue;
            block({center.x - 0.7F, 0, center.y - 0.7F}, {center.x + 0.7F, 6, center.y + 0.7F},
                  ContinentSurface::Wood);
            block({center.x - 3, 4, center.y - 3}, {center.x + 3, 10, center.y + 3},
                  ContinentSurface::Foliage);
        }
    }
    for (auto const &extra : spawn_blocks)
        data.blocks.push_back(
            {extra.min + data.spawn, extra.max + data.spawn, extra.surface, extra.solid});
    data.identity = {"prototype", 1};
    return Continent{std::move(data)};
}

} // namespace lc1
