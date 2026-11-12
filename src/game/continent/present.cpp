#include "lc1/game/continent/continent.hpp"

#include "land.generated.hpp"
#include "layout.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace lc1 {
namespace {

// A compact, smooth hill. The quadratic falloff reaches zero at the radius, so the
// surrounding roads and city foundations can stay at their authored elevation.
float hill_height(glm::vec2 position, glm::vec2 center, float radius, float peak)
{
    float const offset = glm::distance(position, center);
    if (offset >= radius)
        return 0.0F;
    float const falloff = 1.0F - offset / radius;
    return peak * falloff * falloff;
}

void add_terrain_relief(ContinentData &data)
{
    std::size_t const rows = data.tiles.size();
    std::size_t const columns = data.tiles.front().size();
    data.height_samples.assign(rows + 1, std::vector<float>(columns + 1, 0.0F));

    // The first hill is visible immediately east of the spawn and is gentle enough for
    // the current controller to climb without stairs. The others shape the horizon.
    auto const height_at = [](glm::vec2 position) {
        float height = 0.0F;
        // Keep the spawn clearing flat; the nearest hill starts to its east.
        height += hill_height(position, {96.0F, 32.0F}, 48.0F, 10.0F);
        height += hill_height(position, {-64.0F, 64.0F}, 32.0F, 7.0F);
        height += hill_height(position, {-180.0F, 150.0F}, 65.0F, 14.0F);
        height += hill_height(position, {240.0F, 100.0F}, 80.0F, 16.0F);
        height += hill_height(position, {-260.0F, 300.0F}, 90.0F, 12.0F);
        return height;
    };

    for (std::size_t z = 0; z <= rows; ++z) {
        for (std::size_t x = 0; x <= columns; ++x) {
            glm::vec2 const position =
                data.origin +
                glm::vec2{static_cast<float>(x), static_cast<float>(z)} * data.tile_size;
            data.height_samples[z][x] = height_at(position);
        }
    }
}

} // namespace

Continent Continent::make_present(std::span<ContinentBlock const> spawn_blocks)
{
    constexpr RegionId plain{1}, sea{5};
    constexpr LocationId city{1}, village{2}, shore{7};
    ContinentData data{
        // The source canvas is 2400 x 1600. Scale by 2.56 m per source unit:
        // the south-bay city gate at (1250, 1050) becomes world (0, 0).
        .origin = {-3200.0F, -2688.0F},
        .tile_size = 16.0F,
        .tiles =
            std::vector(continent_land::rows, std::vector(continent_land::columns, TileData{sea})),
        .spawn = {0.0F, 0.0F, 32.0F},
        .patches = {},
        .blocks = {},
        .regions = {{plain, "Mainland and Islands", RegionType::Plain},
                    {sea, "Open Sea", RegionType::Sea}},
        .locations =
            {{city, "Longwind City", LocationType::City, plain, {{-160, -384}, {160, 0}}, {0, 0}},
             {village,
              "Southfield Village",
              LocationType::Village,
              plain,
              {{-64, 80}, {64, 160}},
              {0, 112}},
             {shore,
              "South Bay Coast",
              LocationType::Wilderness,
              plain,
              {{-24, 224}, {24, 256}},
              {0, 240}}},
        .roads = {{RoadId{1}, "City South Road", city, village, 8, {{0, 0}, {0, 112}}},
                  {RoadId{6}, "South Bay Trail", village, shore, 4, {{0, 112}, {0, 240}}}},
        .height_samples = {},
    };
    for (auto const &run : continent_land::runs)
        for (auto column = run[1]; column < run[2]; ++column)
            data.tiles[run[0]][column].region = plain;

    add_terrain_relief(data);
    add_starter_buildings(data);
    add_patrol_house(data);
    for (auto const &extra : spawn_blocks)
        data.blocks.push_back({extra.min + data.spawn, extra.max + data.spawn, extra.surface,
                               extra.solid, extra.walkable_top});
    data.identity = {"present", 1};
    return Continent{std::move(data)};
}

} // namespace lc1
