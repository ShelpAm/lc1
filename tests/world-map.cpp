#include "lc1/error.hpp"
#include "lc1/game/continent/continent.hpp"
#include "lc1/game/physics/world-collision.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void check(bool condition, std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string{message});
}

lc1::ContinentData connected_world()
{
    return {.origin = {-32, -32},
            .tile_size = 16,
            .tiles = std::vector(4, std::vector<lc1::TileData>(4)),
            .spawn = {0, 0, -24},
            .patches = {},
            .blocks = {},
            .regions = {{lc1::RegionId{1}, "Plain", lc1::RegionType::Plain},
                        {lc1::RegionId{9}, "Sea", lc1::RegionType::Sea}},
            .locations = {{lc1::LocationId{10},
                           "Village",
                           lc1::LocationType::Village,
                           lc1::RegionId{1},
                           {{-28, 0}, {-16, 12}},
                           {-20, 4}},
                          {lc1::LocationId{20},
                           "Ruins",
                           lc1::LocationType::Ruins,
                           lc1::RegionId{1},
                           {{16, 0}, {28, 12}},
                           {20, 4}}},
            .roads = {{lc1::RoadId{7},
                       "Road",
                       lc1::LocationId{10},
                       lc1::LocationId{20},
                       4,
                       {{-20, 4}, {20, 4}}}},
            .height_samples = {}};
}

template <typename Edit> void rejects(Edit edit, std::string_view reason)
{
    auto data = connected_world();
    edit(data);
    try {
        lc1::Continent const invalid{std::move(data)};
    }
    catch (lc1::Error const &error) {
        check(std::string_view{error.what()}.find(reason) != std::string_view::npos,
              "map rejected for an unexpected reason");
        return;
    }
    throw std::runtime_error("invalid world map was accepted");
}

void identity_and_queries()
{
    auto data = connected_world();
    std::ranges::reverse(data.regions);
    std::ranges::reverse(data.locations);
    lc1::Continent const world{std::move(data)};
    check(world.region_at({0, 0})->id == lc1::RegionId{1}, "tile identity survives region reorder");
    check(world.location(lc1::LocationId{10})->name == "Village",
          "location IDs are not array indices");
    check(world.road(lc1::RoadId{7})->to == lc1::LocationId{20}, "road endpoints retain identity");
    check(!world.region(lc1::RegionId{99}) && !world.location(lc1::LocationId{99}) &&
              !world.road(lc1::RoadId{99}),
          "unknown IDs return no result");
    check(world.location_at({-28, 0})->id == lc1::LocationId{10} && !world.location_at({-16, 4}),
          "location queries use half-open footprint bounds");
    check(!world.location_at({0, 0}), "countryside is not a location");
    check(!world.region_at({32, 0}) && !world.road_at({32, 4}), "outside map queries are empty");
    float const nan = std::numeric_limits<float>::quiet_NaN();
    check(!world.region_at({nan, 0}) && !world.location_at({nan, 0}) && !world.road_at({nan, 0}),
          "nonfinite coordinates are never map hits");
    check(world.roads_from(lc1::LocationId{10}) == std::vector{lc1::RoadId{7}} &&
              world.roads_from(lc1::LocationId{20}) == std::vector{lc1::RoadId{7}},
          "road adjacency works at both ends");
    check(world.roads_from(lc1::LocationId{99}).empty(), "unknown locations have no connections");

    data = connected_world();
    data.locations.push_back({lc1::LocationId{30},
                              "Village Camp",
                              lc1::LocationType::Wilderness,
                              lc1::RegionId{1},
                              {{-24, 2}, {-18, 8}},
                              {-20, 4}});
    lc1::Continent const nested{std::move(data)};
    check(nested.location_at({-20, 4})->id == lc1::LocationId{30},
          "nested locations select the smaller footprint");
}

void polyline_queries()
{
    auto data = connected_world();
    data.roads.front().points = {{-20, 4}, {-4, 16}, {4, 16}, {20, 4}};
    lc1::Continent const world{std::move(data)};
    auto const &road = world.roads().front();
    check(std::abs(road.length() - 48.0F) < 0.001F, "length sums diagonal and straight segments");
    check(world.road_at({0, 16}) == &road && world.road_at({-12, 10}) == &road,
          "all polyline segments are queryable");
    check(world.road_at({0, 18}) == &road && !world.road_at({0, 18.01F}),
          "road query respects half width");
    check(road.contains({-22, 4}) && !road.contains({-22.01F, 4}), "round end caps stop at radius");
    check(road.contains({0, 20}, 2) && !road.contains({0, 20.01F}, 2),
          "decoration clearance expands the road footprint");
    check(!world.road_at({0, 4}), "polyline does not fill its bounding box");
}

void invalid_maps()
{
    rejects([](auto &d) { d.regions.push_back(d.regions.front()); }, "IDs");
    rejects([](auto &d) { d.locations.push_back(d.locations.front()); }, "IDs");
    rejects([](auto &d) { d.roads.push_back(d.roads.front()); }, "IDs");
    rejects([](auto &d) { d.regions.front().id = lc1::RegionId{0}; }, "IDs");
    rejects([](auto &d) { d.tiles[0][0].region = lc1::RegionId{99}; }, "unknown region");
    rejects([](auto &d) { d.locations.front().region = lc1::RegionId{9}; }, "location");
    rejects([](auto &d) { d.locations.front().bounds.max = {40, 12}; }, "location");
    rejects([](auto &d) { d.locations.front().entrance = {0, 0}; }, "location");
    rejects([](auto &d) { d.roads.front().to = lc1::LocationId{99}; }, "endpoints");
    rejects([](auto &d) { d.roads.front().to = d.roads.front().from; }, "endpoints");
    rejects([](auto &d) { d.roads.front().width = 0; }, "width");
    rejects([](auto &d) { d.roads.front().width = std::numeric_limits<float>::infinity(); },
            "width");
    rejects([](auto &d) { d.roads.front().points.clear(); }, "endpoints");
    rejects([](auto &d) { d.roads.front().points.back().x = 19; }, "endpoints");
    rejects(
        [](auto &d) { d.roads.front().points.insert(d.roads.front().points.begin(), {-20, 4}); },
        "degenerate");
    rejects(
        [](auto &d) {
            d.roads.front().points.insert(d.roads.front().points.begin() + 1,
                                          {0, std::numeric_limits<float>::quiet_NaN()});
        },
        "passable");
    rejects([](auto &d) { d.tiles[2][2].region = lc1::RegionId{9}; }, "obstacle");
    rejects([](auto &d) { d.blocks.push_back({{5, 0, -8}, {5.1F, 4, 16}}); }, "obstacle");
    rejects([](auto &d) { d.blocks.push_back({{5, 0, 5.5F}, {6, 4, 16}}); }, "obstacle");
}

void prototype_network()
{
    lc1::Continent const world = lc1::Continent::make_prototype();
    for (float z = world.bounds().min.y; z < world.bounds().max.y; z += world.tile_size()) {
        for (float x = world.bounds().min.x; x < world.bounds().max.x; x += world.tile_size()) {
            auto const *region = world.region_at({x, z});
            check(region != nullptr, "every terrain tile belongs to a named region");
            check(world.ground_height({x, z}).has_value() == region->walkable(),
                  "terrain movement agrees with region type");
        }
    }
    for (auto const &place : world.locations()) {
        check(world.region_at(place.entrance)->id == place.region,
              "location entrance belongs to region");
    }

    auto const spawn = world.spawn();
    auto const *start = world.road_at({spawn.x, spawn.z});
    check(start != nullptr, "player starts on a connecting road");
    std::vector<lc1::LocationId> reached{start->from};
    for (std::size_t i = 0; i < reached.size(); ++i) {
        for (auto const id : world.roads_from(reached[i])) {
            auto const *road = world.road(id);
            auto const next = road->from == reached[i] ? road->to : road->from;
            if (std::ranges::find(reached, next) == reached.end())
                reached.push_back(next);
        }
    }
    check(reached.size() == world.locations().size(), "all prototype locations connect to spawn");

    for (auto const &road : world.roads()) {
        for (std::size_t i = 1; i < road.points.size(); ++i) {
            for (bool reverse : {false, true}) {
                auto const from = road.points[reverse ? i : i - 1];
                auto const to = road.points[reverse ? i - 1 : i];
                auto const result =
                    lc1::WorldCollision{world}.move({from.x, 0, from.y}, to - from, 0.3F, 1.8F);
                check(glm::distance(result, glm::vec3{to.x, 0, to.y}) < 0.001F,
                      "every road segment is walkable in both directions");
            }
        }
    }
}

} // namespace

int main()
{
    try {
        identity_and_queries();
        polyline_queries();
        invalid_maps();
        prototype_network();
        std::cout << "world map: identities, queries, validation and connected traversal passed\n";
    }
    catch (std::exception const &error) {
        std::cerr << "world map test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
