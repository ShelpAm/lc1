#include "lc1/game/continent/continent.hpp"
#include "lc1/game/camera/map-camera-controller.hpp"
#include "lc1/game/character/ai-character-controller.hpp"
#include "lc1/game/character/character.hpp"
#include "lc1/game/continent/patrol-house.hpp"
#include "lc1/game/debug/pbr-showcase.hpp"
#include "lc1/scene/camera.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {

void check(bool condition, std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string{message});
}

bool near(float a, float b)
{
    return std::abs(a - b) < 0.001F;
}

lc1::ContinentData flat_world()
{
    return {.origin = {-32.0F, -32.0F},
            .tile_size = 16.0F,
            .tiles = std::vector(4, std::vector<lc1::TileData>(4)),
            .spawn = {0.0F, 0.0F, 0.0F},
            .patches = {},
            .blocks = {},
            .regions = {{lc1::RegionId{1}, "Plain", lc1::RegionType::Plain},
                        {lc1::RegionId{2}, "Sea", lc1::RegionType::Sea}},
            .locations = {},
            .roads = {},
            .height_samples = {}};
}

void grid_and_terrain()
{
    auto data = flat_world();
    data.tiles[2][3].region = lc1::RegionId{2};
    lc1::Continent const world{std::move(data)};
    check(world.rows() == 4 && world.columns() == 4, "grid dimensions");
    check(!world.tile_at({-32.01F, 0.0F}), "negative out-of-bounds query");
    check(!world.tile_at({32.0F, 0.0F}), "upper bound is exclusive");
    check(world.tile_at({std::nextafter(32.0F, 0.0F), 0.0F}).has_value(),
          "a point immediately inside the upper bound remains a valid query");
    check(world.region_at({16.0F, 0.0F})->type == lc1::RegionType::Sea, "tile/region lookup");
    check(!world.ground_height({17.0F, 1.0F}), "sea is not walkable");
    auto const shore = world.move({0, 0, 1}, {100, 0}, 0.3F, 1.8F);
    check(near(shore.x, 15.7F), "movement stops at shoreline with body clearance");
    auto const edge = world.move({0, 0, -10}, {-100, 0}, 0.3F, 1.8F);
    check(near(edge.x, -31.7F), "body stays within continent bounds");

    data = flat_world();
    data.tiles[1].pop_back();
    bool rejected = false;
    try {
        lc1::Continent invalid{std::move(data)};
    }
    catch (std::exception const &) {
        rejected = true;
    }
    check(rejected, "ragged terrain grid must be rejected");
}

void collision_and_sliding()
{
    auto data = flat_world();
    data.blocks.push_back({{5, 0, -20}, {5.1F, 4, 20}});
    lc1::Continent const world{std::move(data)};
    auto const hit = world.move({0, 0, 0}, {100, 0}, 0.3F, 1.8F);
    check(near(hit.x, 4.7F), "high-speed sweep must not tunnel through thin walls");
    auto const slide = world.move({0, 0, 0}, {10, 4}, 0.3F, 1.8F);
    check(near(slide.x, 4.7F) && near(slide.z, 4.0F), "diagonal movement slides along wall");
    auto const away = world.move(slide, {-2, 0}, 0.3F, 1.8F);
    check(near(away.x, 2.7F), "contact must allow movement away from the wall");
    auto const tangent = world.move(hit, {0, 8}, 0.3F, 1.8F);
    check(near(tangent.z, 8.0F), "contact must allow tangential movement");

    data = flat_world();
    data.blocks.push_back({{5, 0, -20}, {6, 4, 20}});
    data.blocks.push_back({{-20, 0, 5}, {20, 4, 6}});
    lc1::Continent const corner{std::move(data)};
    auto const stopped = corner.move({0, 0, 0}, {10, 10}, 0.3F, 1.8F);
    check(near(stopped.x, 4.7F) && near(stopped.z, 4.7F), "two contact planes stop at corner");
    check(corner.can_stand(stopped, 0.3F, 1.8F), "corner resolution leaves a valid body");

    data = flat_world();
    data.blocks.push_back({{-2, 2, -2}, {2, 5, -1}});
    lc1::Continent const lintel{std::move(data)};
    check(near(lintel.move({0, 0, 2}, {0, -6}, 0.3F, 1.8F).z, -4),
          "player passes below an overhead lintel");
    check(near(lintel.move({0, 0, 2}, {0, -6}, 0.3F, 2.5F).z, -0.7F),
          "a taller body collides with the same lintel");
}

void terrain_collision_candidates()
{
    auto grid_data = flat_world();
    grid_data.origin = {-512, -384};
    grid_data.tiles = std::vector(48, std::vector<lc1::TileData>(64));
    auto reference_data = grid_data;
    // The reference uses the original all-blocks sweep, so it cannot accidentally
    // share a bug in the new terrain candidate selection.
    for (int z = 0; z < 48; ++z) {
        for (int x = 0; x < 64; ++x) {
            if ((x * 7 + z * 11) % 13 != 0 || (x == 32 && z == 24))
                continue;
            grid_data.tiles[z][x].region = lc1::RegionId{2};
            auto const corner = grid_data.origin + glm::vec2{x, z} * 16.0F;
            reference_data.blocks.push_back(
                {{corner.x, 0, corner.y}, {corner.x + 16, 8, corner.y + 16}});
        }
    }
    lc1::Continent const grid{std::move(grid_data)};
    lc1::Continent const reference{std::move(reference_data)};
    std::mt19937 random{41};
    std::uniform_real_distribution<float> x_position{-512, 512};
    std::uniform_real_distribution<float> z_position{-384, 384};
    std::uniform_real_distribution<float> travel{-1200, 1200};
    int sweeps = 0;
    for (int trial = 0; trial < 1600; ++trial) {
        glm::vec3 const start{x_position(random), 0, z_position(random)};
        float const half_width = trial % 3 == 0 ? 8.0F : 0.3F;
        bool const valid = reference.can_stand(start, half_width, 1.8F);
        check(grid.can_stand(start, half_width, 1.8F) == valid,
              "nearby tile selection agrees with full body overlap tests");
        if (!valid)
            continue;
        glm::vec2 const delta{trial % 5 == 0 ? 0.0F : travel(random), travel(random)};
        auto const expected = reference.move(start, delta, half_width, 1.8F);
        auto const actual = grid.move(start, delta, half_width, 1.8F);
        if (glm::distance(expected, actual) >= 0.001F)
            std::cerr << "trial " << trial << " start " << start.x << ',' << start.z << " delta "
                      << delta.x << ',' << delta.y << " half " << half_width << " expected "
                      << expected.x << ',' << expected.z << " actual " << actual.x << ','
                      << actual.z << '\n';
        check(glm::distance(expected, actual) < 0.001F,
              "local terrain candidates preserve long, diagonal and sliding sweeps");
        ++sweeps;
    }
    check(sweeps > 500, "exercise enough valid terrain sweeps");
}

void player_and_city()
{
    lc1::Continent const flat{flat_world()};
    lc1::Character straight{flat, {.position = flat.spawn()}, {}};
    lc1::Character diagonal{flat, {.position = flat.spawn()}, {}};
    straight.walk(0.1F, flat, {1, 0, 0}, false);
    diagonal.walk(0.1F, flat, {1, 0, 1}, false);
    check(near(glm::length(straight.position()), glm::length(diagonal.position())),
          "diagonal movement must not be faster");
    check(near(straight.position().y, 0) && near(straight.eye_position().y, 1.65F),
          "feet and camera remain at ground level");
    lc1::Character resumed{flat, {.position = flat.spawn()}, {}};
    resumed.walk(10.0F, flat, {1, 0, 0}, false);
    check(near(resumed.position().x, 0.45F), "long stalls do not teleport the player");

    lc1::Continent const city = lc1::Continent::make_prototype();
    lc1::Character player{city, {.position = city.spawn()}, {}};
    for (int frame = 0; frame < 600; ++frame)
        player.walk(1.0F / 60.0F, city, {0, 0, -1}, false);
    check(player.position().z < -10.0F, "WASD walk crosses the actual city gate");
    check(near(player.eye_position().y, 1.65F), "entering city does not change camera height");
    for (int frame = 0; frame < 600; ++frame)
        player.walk(1.0F / 60.0F, city, {0, 0, 1}, false);
    check(near(player.position().z, city.spawn().z), "the gate also allows exiting the city");
    auto const wall = city.move({40, 0, 32}, {0, -100}, 0.3F, 1.8F);
    check(near(wall.z, 0.3F), "city wall blocks movement beside the gate");

    std::mt19937 random{29};
    std::uniform_real_distribution<float> direction{-1.0F, 1.0F};
    for (int step = 0; step < 4000; ++step) {
        player.walk(0.1F, city, {direction(random), 0, direction(random)}, true);
        check(city.can_stand(player.position(), player.half_width(), player.height()),
              "movement must preserve the nonpenetration invariant");
    }
}

void present_relief()
{
    lc1::Continent const world = lc1::Continent::make_present();
    auto const hill = world.terrain_sample({96.0F, 32.0F});
    check(hill && hill->height > 9.5F, "present scene contains a substantial terrain hill");
    check(hill->normal.y < 0.999F, "terrain hill has a non-flat surface normal");
    auto const spawn_height = world.ground_height({world.spawn().x, world.spawn().z});
    check(spawn_height && near(*spawn_height, 0.0F), "spawn remains on flat traversable ground");
    auto const showcase_edge = world.terrain_sample({22.0F, 32.0F});
    check(showcase_edge && near(showcase_edge->height, 0.0F),
          "terrain relief does not intrude into the PBR showcase footprint");

    auto const approach = world.ground_height({70.0F, 32.0F});
    check(approach.has_value(), "hill approach is walkable");
    lc1::Character walker{world, {.position = {70.0F, *approach, 32.0F}}, {}};
    for (int frame = 0; frame < 500; ++frame) {
        walker.walk(1.0F / 60.0F, world, {1.0F, 0.0F, 0.0F}, false);
        walker.update(1.0F / 60.0F, world, {});
    }
    check(walker.on_ground() && walker.position().y > 2.0F,
          "character climbs the present scene relief instead of staying at Y=0");
    auto const ground = world.ground_height({walker.position().x, walker.position().z});
    check(ground && std::abs(walker.position().y - *ground) < 0.01F,
          "character feet follow the sampled terrain height");

    float const takeoff_height = walker.position().y;
    walker.request_jump();
    for (int frame = 0; frame < 300; ++frame)
        walker.update(1.0F / 60.0F, world, {});
    check(walker.on_ground() && walker.position().y > takeoff_height - 1.0F,
          "jumping on a terrain slope lands back on the hill instead of falling through");

    // Match the game loop order: vertical update first, then controller-driven
    // horizontal movement. A walking/running jump must not outrun the support query.
    for (bool running : {false, true}) {
        auto const start = world.ground_height({78.0F, 32.0F});
        check(start.has_value(), "slope jump start is grounded");
        lc1::Character slope_jumper{world, {.position = {78.0F, *start, 32.0F}}, {}};
        slope_jumper.request_jump();
        for (int frame = 0; frame < 600; ++frame) {
            slope_jumper.update(1.0F / 60.0F, world, {});
            slope_jumper.walk(1.0F / 60.0F, world, {1.0F, 0.0F, 0.0F}, running);
        }
        slope_jumper.update(1.0F / 60.0F, world, {});
        auto const landed_ground =
            world.ground_height({slope_jumper.position().x, slope_jumper.position().z});
        check(landed_ground && slope_jumper.on_ground() &&
                  std::abs(slope_jumper.position().y - *landed_ground) < 0.01F,
              "a moving jump on a hill lands on terrain rather than falling through");
    }
}

void showcase_platform_jump()
{
    auto const blocks = lc1::demo::PbrShowcase::spawn_blocks();
    lc1::Continent const world = lc1::Continent::make_present(blocks);

    // The east front plinth is x in [6, 21], z in [14, 18], top at y=0.8.
    lc1::Character character{world, {.position = {13.5F, 0.0F, 18.8F}}, {}};
    character.request_jump();
    bool reached_plinth = false;
    for (int frame = 0; frame < 180; ++frame) {
        character.update(1.0F / 60.0F, world, {});
        if (character.on_ground() && character.position().y > 0.7F) {
            reached_plinth = true;
            break;
        }
        if (character.position().z > 17.0F)
            character.walk(1.0F / 60.0F, world, {0.0F, 0.0F, -1.0F}, false);
    }
    check(!reached_plinth && character.on_ground() && std::abs(character.position().y) < 0.01F &&
              character.position().z > 18.0F,
          "a half-meter jump cannot climb a 0.8-meter showcase plinth");

    // A 0.4-meter ledge exceeds step height but remains within jump reach.
    auto low_blocks = blocks;
    for (auto &block : low_blocks) {
        if (block.min.x == 6.0F && block.min.z == -18.0F && block.max.y == 0.8F)
            block.max.y = 0.4F;
    }
    lc1::Continent const low_world = lc1::Continent::make_present(low_blocks);
    lc1::Character low_jumper{low_world, {.position = {13.5F, 0.0F, 18.8F}}, {}};
    low_jumper.request_jump();
    bool reached_low_plinth = false;
    for (int frame = 0; frame < 180; ++frame) {
        low_jumper.update(1.0F / 60.0F, low_world, {});
        if (low_jumper.on_ground() && std::abs(low_jumper.position().y - 0.4F) < 0.01F) {
            reached_low_plinth = true;
            break;
        }
        if (low_jumper.position().z > 17.0F)
            low_jumper.walk(1.0F / 60.0F, low_world, {0.0F, 0.0F, -1.0F}, true);
    }
    check(reached_low_plinth, "a half-meter jump lands on a reachable walkable ledge");

    // Start on top to verify walking and jumping down independently of jump reach.
    character = lc1::Character{world, {.position = {13.5F, 0.8F, 16.0F}}, {}};
    check(character.position().x > 6.0F && character.position().x < 21.0F &&
              character.position().z > 14.0F && character.position().z < 18.0F,
          "showcase plinth landing stays within the platform footprint");

    // Moving on the plinth must not treat the solid block underneath as an invalid start.
    character.walk(1.0F / 60.0F, world, {1.0F, 0.0F, 0.0F}, false);
    character.update(1.0F / 60.0F, world, {});
    check(character.on_ground() && character.position().y > 0.7F,
          "walking on a showcase plinth remains valid");

    // Walk and jump off the east edge. The foot support must persist until the body
    // footprint has cleared the plinth, otherwise the falling body clips its side and
    // the next horizontal move starts from an invalid position.
    character.request_jump();
    bool reached_ground = false;
    for (int frame = 0; frame < 300; ++frame) {
        character.update(1.0F / 60.0F, world, {});
        character.walk(1.0F / 60.0F, world, {1.0F, 0.0F, 0.0F}, true);
        if (character.on_ground() && character.position().y < 0.1F) {
            reached_ground = true;
            break;
        }
    }
    check(reached_ground, "jumping down from a showcase plinth lands on the ground");
}

void switchback_stairs()
{
    lc1::Continent const world = lc1::Continent::make_present();

    // The southwest corner has open ground plus both stacked landings at one XZ.
    auto const ground_level = world.support_below({29.0F, 0.5F, 65.0F}, 0.6F);
    auto const middle_level = world.support_below({29.0F, 10.5F, 65.0F}, 1.0F);
    auto const top_level = world.support_below({29.0F, 20.0F, 65.0F}, 1.0F);
    check(ground_level && std::abs(ground_level->height) < 0.01F,
          "spiral stair has a ground level at the southwest corner");
    check(middle_level && std::abs(middle_level->height - 9.6F) < 0.01F,
          "spiral stair has a middle landing at the same XZ");
    check(top_level && std::abs(top_level->height - 19.2F) < 0.01F,
          "spiral stair has a top landing at the same XZ");

    // Flights from the two loops are also stacked above one another.
    auto const lower_flight = world.support_below({29.0F, 1.5F, 62.1F}, 1.0F);
    auto const upper_flight = world.support_below({29.0F, 11.0F, 62.1F}, 1.0F);
    check(lower_flight && std::abs(lower_flight->height - 1.2F) < 0.01F,
          "lower spiral flight is a walkable surface");
    check(upper_flight && std::abs(upper_flight->height - 10.8F) < 0.01F,
          "upper spiral flight is another walkable surface at the same XZ");

    lc1::Character character{world, {.position = {29.0F, 0.0F, 65.2F}}, {}};
    auto step_towards = [&](glm::vec3 direction, auto stop) {
        for (int frame = 0; frame < 1600; ++frame) {
            character.update(1.0F / 60.0F, world, {});
            character.walk(1.0F / 60.0F, world, direction, false);
            if (stop(character.position()))
                break;
        }
    };
    auto check_level = [&](float height, std::string_view message) {
        check(character.on_ground() && std::abs(character.position().y - height) < 0.01F, message);
    };

    step_towards({0.0F, 0.0F, -1.0F},
                 [](glm::vec3 const &position) { return position.z <= 59.2F; });
    check_level(2.4F, "first flight reaches the 2.4 m northwest landing");

    step_towards({1.0F, 0.0F, 0.0F}, [](glm::vec3 const &position) { return position.x >= 35.0F; });
    check_level(4.8F, "second flight reaches the 4.8 m northeast landing");

    step_towards({0.0F, 0.0F, 1.0F}, [](glm::vec3 const &position) { return position.z >= 65.0F; });
    check_level(7.2F, "third flight reaches the 7.2 m southeast landing");

    step_towards({-1.0F, 0.0F, 0.0F},
                 [](glm::vec3 const &position) { return position.x <= 29.0F; });
    check_level(9.6F, "fourth flight completes the 9.6 m lower loop");

    step_towards({0.0F, 0.0F, -1.0F},
                 [](glm::vec3 const &position) { return position.z <= 59.2F; });
    check_level(12.0F, "fifth flight climbs above the lower loop");

    step_towards({1.0F, 0.0F, 0.0F}, [](glm::vec3 const &position) { return position.x >= 35.0F; });
    check_level(14.4F, "sixth flight reaches the 14.4 m northeast landing");

    step_towards({0.0F, 0.0F, 1.0F}, [](glm::vec3 const &position) { return position.z >= 65.0F; });
    check_level(16.8F, "seventh flight reaches the 16.8 m southeast landing");

    step_towards({-1.0F, 0.0F, 0.0F},
                 [](glm::vec3 const &position) { return position.x <= 29.0F; });
    check_level(19.2F, "eighth flight completes the 19.2 m upper loop");
}

void camera_projection()
{
    lc1::scene::FpsCamera camera;
    camera.set_position({0, 0, 10});
    camera.look_at({0, 0, 0});
    camera.set_zfar(200.0F);
    camera.set_aspect_ratio(2.0F);
    camera.set_fov_y(90.0F);
    auto project = [&](glm::vec3 point) {
        glm::vec4 const clip = camera.view_projection_matrix() * glm::vec4{point, 1.0F};
        return glm::vec3{clip} / clip.w;
    };
    check(near(project({20, 10, 0}).x, 1.0F) && near(project({20, 10, 0}).y, 1.0F),
          "perspective field of view respects aspect ratio without a second Y flip");
    check(near(project({5, 0, 0}).x, 0.25F) && near(project({5, 0, -10}).x, 0.125F),
          "doubling depth halves the apparent size");
    check(near(project({0, 0, 9.9F}).z, 0.0F) && near(project({0, 0, -190}).z, 1.0F),
          "perspective depth maps the near/far planes to Vulkan's zero-to-one range");
}

void patrol_house_floors()
{
    auto const world = lc1::Continent::make_present();
    lc1::Character npc{world, {.position = lc1::patrol_house_spawn}, {}};
    std::vector<lc1::Waypoint> points;
    auto const route = lc1::patrol_house_route();
    // Start indoors and exercise both stairs, the platform, and the exterior return.
    auto const start = std::ranges::find(route, lc1::patrol_house_spawn);
    auto const index = static_cast<std::size_t>(start - route.begin());
    for (std::size_t i = 0; i < route.size(); ++i)
        points.push_back({.position = route[(index + i) % route.size()]});
    lc1::AiCharacterController controller{points, 0.4F};
    bool upper = false;
    bool middle = false;
    bool platform = false;
    bool returned = false;
    bool outside = false;
    for (int frame = 0; frame < 120 * 240; ++frame) {
        auto const intent = controller.update(1.0F / 120.0F, npc);
        npc.update(1.0F / 120.0F, world, intent);
        check(world.can_stand(npc.position(), npc.half_width(), npc.height()),
              "house patrol never penetrates walls or ceilings");
        upper |= npc.position().y > 7.7F;
        middle |= npc.position().y > 4.1F && npc.position().y < 4.3F;
        platform |= npc.position().x > 40 && near(npc.position().y, 1.2F);
        returned |= upper && npc.position().x < 31 && npc.position().y < 0.7F;
        outside |= returned && npc.position().x < 1 && npc.position().z > 55;
    }
    if (!(upper && middle && platform && returned && outside))
        std::cerr << "House patrol stopped at " << npc.position().x << ", " << npc.position().y
                  << ", " << npc.position().z << "\n";
    check(upper && middle && platform && returned && outside,
          "NPC visits the split level, both upper floors, descends, and exits the house");
    check(!world.can_stand({39.2F, 0.6F, 20}, 0.3F, 1.8F),
          "ground-floor partition blocks characters");
    check(world.can_stand({39.2F, 0.6F, 25.5F}, 0.3F, 1.8F), "room doorway has body clearance");
    check(world.can_stand({36, 0.6F, 23}, 0.3F, 1.8F) &&
              world.can_stand({36, 4.2F, 23}, 0.3F, 1.8F) &&
              world.can_stand({36, 7.8F, 23}, 0.3F, 1.8F),
          "stacked rooms retain independent walkable levels");
}

void perspective_camera_zoom()
{
    glm::vec3 const player{0, 0, 0};
    lc1::scene::FpsCamera camera;
    camera.set_zfar(800.0F);
    lc1::MapCameraController controller{player};
    controller.apply(camera);
    auto project = [&](glm::vec3 point) {
        glm::vec4 const clip = camera.view_projection_matrix() * glm::vec4{point, 1.0F};
        return glm::vec3{clip} / clip.w;
    };
    auto const center = controller.center();
    auto const forward = camera.forward();
    auto const sample = center + camera.right() * 5.0F;
    float const initial_size = project(sample).x;
    float const initial_distance = glm::distance(camera.position(), center);
    check(initial_size > project(sample + forward * 20.0F).x,
          "the active oblique map camera has perspective foreshortening");
    check(near(project(center + camera.up() * 24.0F).y, 1.0F),
          "default framing covers 48 metres at the target depth");

    controller.zoom(1.0F);
    controller.apply(camera);
    check(near(glm::distance(camera.position(), center), initial_distance),
          "wheel input changes the target without snapping the camera");
    controller.update(0.0F, camera, player);
    check(near(glm::distance(camera.position(), center), initial_distance),
          "zero elapsed time does not advance zoom smoothing");
    controller.update(1.0F / 60.0F, camera, player);
    float const partial_distance = glm::distance(camera.position(), center);
    float const target_distance = initial_distance * std::exp(-0.12F);
    check(partial_distance < initial_distance && partial_distance > target_distance,
          "zoom moves partway toward its target like recenter smoothing");
    controller.update(1.0F / 60.0F, camera, player);
    check(glm::distance(camera.position(), center) < partial_distance,
          "zoom keeps settling after wheel input ends");
    controller.zoom(1.0F);
    check(near(controller.view_height(), 48.0F * std::exp(-0.24F)),
          "successive wheel events accumulate on the target framing");
    controller.zoom(-1.0F);
    controller.update(0.1F, camera, player);
    check(glm::distance(camera.position(), center) < initial_distance &&
              project(sample).x > initial_size,
          "wheel zoom approaches the target and increases apparent size");
    check(glm::length(project(center) * glm::vec3{1, 1, 0}) < 0.001F &&
              glm::length(camera.forward() - forward) < 0.001F,
          "zoom preserves the target at screen center and the fixed viewing angle");
    controller.zoom(-1.0F);
    controller.update(0.1F, camera, player);
    check(near(glm::distance(camera.position(), center), initial_distance),
          "reverse wheel input restores the camera distance");

    controller.zoom(20.0F);
    controller.update(0.1F, camera, player);
    check(near(controller.view_height(), 16.0F), "zoom respects the minimum framing limit");
    check(project(center).z > 0.0F && project(center).z < 1.0F && camera.position().y > player.y,
          "minimum zoom keeps the target visible and camera above the ground");
}

void camera_lock_and_edge_pan()
{
    lc1::GroundBounds const bounds{{-100, -100}, {100, 100}};
    glm::vec3 const player{0, 0, 0};
    lc1::scene::FpsCamera camera;
    lc1::MapCameraController controller{player};
    auto same = [](glm::vec3 a, glm::vec3 b) { return glm::length(a - b) < 0.001F; };
    controller.pan(0.1F, {-1, 0}, bounds);
    controller.update(0.1F, camera, {3, 0, 4});
    check(controller.locked() && same(controller.center(), {3, 0.9F, 4}),
          "locked camera follows the player and ignores pan");
    auto const before_unlock = controller.center();
    controller.toggle_lock();
    controller.update(0.1F, camera, {10, 0, 10});
    check(!controller.locked() && same(controller.center(), before_unlock),
          "unlocking preserves the center instead of following movement");
    controller.pan(0.1F, {-1, 0}, bounds);
    controller.update(0.1F, camera, player);
    auto const pan = controller.center() - before_unlock;
    check(glm::dot(pan, camera.right()) < -1 && std::abs(glm::dot(pan, camera.heading())) < 0.001F,
          "pan follows the fixed camera basis");
    controller.recenter({10, 0, 10});
    controller.update(0.1F, camera, {10, 0, 10});
    check(!controller.locked() && same(controller.center(), {10, 0.9F, 10}),
          "recenter does not change the follow lock");
    controller.update(0.1F, camera, {12, 0, 10});
    check(same(controller.center(), {10, 0.9F, 10}), "recenter does not enable later following");
    controller.toggle_lock();
    controller.update(0.1F, camera, {12, 0, 10});
    check(controller.locked() && same(controller.center(), {12, 0.9F, 10}),
          "locking again immediately follows the player");
    lc1::MapCameraController side{player};
    lc1::MapCameraController corner{player};
    side.toggle_lock();
    corner.toggle_lock();
    auto const start = side.center();
    side.pan(0.1F, {-1, 0}, bounds);
    corner.pan(0.1F, {-1, 1}, bounds);
    check(near(glm::length(side.center() - start), glm::length(corner.center() - start)),
          "corner pan does not exceed the maximum side speed");
    for (int frame = 0; frame < 100; ++frame)
        corner.pan(0.1F, {-1, 1}, {{-2, -2}, {2, 2}});
    check(std::abs(corner.center().x) <= 2 && std::abs(corner.center().z) <= 2,
          "free camera center stays within the continent");
}

} // namespace

int main()
{
    try {
        grid_and_terrain();
        collision_and_sliding();
        terrain_collision_candidates();
        player_and_city();
        present_relief();
        showcase_platform_jump();
        switchback_stairs();
        camera_projection();
        perspective_camera_zoom();
        patrol_house_floors();
        camera_lock_and_edge_pan();
        std::cout
            << "continent: terrain, continuous collision, sliding and city traversal passed\n";
    }
    catch (std::exception const &error) {
        std::cerr << "continent test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
