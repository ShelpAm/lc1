#include "lc1/game/debug/world-setup.hpp"
#include "lc1/game/continent/patrol-house.hpp"
#include "lc1/game/resources/catalog.hpp"

namespace lc1::demo {
std::vector<ContinentBlock> showcase_blocks()
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

WorldSetup world_setup(ContinentIdentity const &identity)
{
    WorldSetup setup{.continent = Continent::load(identity, showcase_blocks()),
                     .example_patrol = PatrolSetup{},
                     .initial_player_resources = {{game::moneta.keyid, 10},
                                                  {game::wood.keyid, 2},
                                                  {game::pig_iron.keyid, 3}}};
    auto &patrol = *setup.example_patrol;
    if (identity.id == "present") {
        patrol.spawn = patrol_house_spawn;
        patrol.arrival_radius = 0.4F;
        patrol.resume_nearest = true;
        for (auto const position : patrol_house_route())
            patrol.route.push_back(
                {.position = position,
                 .wait_seconds = position == glm::vec3{36, 7.8F, 23} ? 2.0F : 0.0F});
    }
    else {
        auto const spawn = setup.continent.spawn();
        patrol.route = {{.position = spawn + glm::vec3{3.27F, 0, -1.84F}, .wait_seconds = 1.83F},
                        {.position = spawn + glm::vec3{-4.12F, 0, 2.65F}, .wait_seconds = 2.47F},
                        {.position = spawn + glm::vec3{1.58F, 0, 4.03F}, .wait_seconds = 2.92F},
                        {.position = spawn + glm::vec3{-2.96F, 0, -3.41F}, .wait_seconds = 1.61F}};
    }
    return setup;
}
} // namespace lc1::demo
