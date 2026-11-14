#include "layout.hpp"

#include "lc1/game/continent/continent.hpp"
#include "lc1/game/continent/village.hpp"

#include <algorithm>

#include <cmath>

namespace lc1 {

void add_starter_buildings(ContinentData &data)
{
    auto patch = [&](glm::vec2 min, glm::vec2 max, ContinentSurface surface) {
        data.patches.push_back({{min, max}, surface});
    };
    auto block = [&](glm::vec3 min, glm::vec3 max, ContinentSurface surface, bool solid = true,
                     bool walkable_top = false, BlockShape shape = BlockShape::Box) {
        data.blocks.push_back({min, max, surface, solid, walkable_top, shape});
    };

    // A square spiral stair tower southeast of the spawn/showcase clearing. The route
    // wraps around a central stone core; each of the four sides rises 2.4 m, so one
    // full loop adds 9.6 m. Two loops give ground, 9.6 m and 19.2 m levels at the
    // same XZ columns, with upper flights stacked above lower ones.
    {
        constexpr float stair_x = 28.0F;
        constexpr float stair_z = 58.0F;
        constexpr float step_rise = 0.3F;
        constexpr float step_tread = 0.5F;
        constexpr int steps_per_flight = 8;
        constexpr int flight_count = 8;
        constexpr int flights_per_loop = 4;
        constexpr float flight_height = step_rise * static_cast<float>(steps_per_flight);
        constexpr float loop_height = flight_height * static_cast<float>(flights_per_loop);
        constexpr float landing_size = 2.0F;
        constexpr float walkway_width = 2.0F;
        constexpr float slab_thickness = 0.2F;

        patch({stair_x - 1.0F, stair_z - 1.0F}, {stair_x + 9.0F, stair_z + 9.0F},
              ContinentSurface::Stone);

        // Central core: 4 x 4 m, rising through both loops. It is not walkable and
        // keeps the route a ring rather than allowing shortcuts through the middle.
        block({stair_x + walkway_width, 0.0F, stair_z + landing_size},
              {stair_x + 6.0F, loop_height * 2.0F, stair_z + 6.0F}, ContinentSurface::Stone, true,
              false);

        for (int flight = 0; flight < flight_count; ++flight) {
            int const loop = flight / flights_per_loop;
            int const side = flight % flights_per_loop;
            float const flight_base =
                static_cast<float>(loop) * loop_height + static_cast<float>(side) * flight_height;
            float const first_level_base = static_cast<float>(side) * flight_height;

            for (int step = 0; step < steps_per_flight; ++step) {
                float const top = flight_base + static_cast<float>(step + 1) * step_rise;
                float const bottom = loop == 0 && side == 0 ? 0.0F : top - slab_thickness;
                float const offset = static_cast<float>(step) * step_tread;
                if (side == 0) {
                    // West side, walking north.
                    float const z = stair_z + 6.0F - offset - step_tread;
                    block({stair_x, bottom, z}, {stair_x + walkway_width, top, z + step_tread},
                          ContinentSurface::Stone, true, true);
                }
                else if (side == 1) {
                    // North side, walking east.
                    float const x = stair_x + walkway_width + offset;
                    block({x, bottom, stair_z}, {x + step_tread, top, stair_z + walkway_width},
                          ContinentSurface::Stone, true, true);
                }
                else if (side == 2) {
                    // East side, walking south.
                    float const z = stair_z + walkway_width + offset;
                    block({stair_x + 6.0F, bottom, z},
                          {stair_x + 6.0F + walkway_width, top, z + step_tread},
                          ContinentSurface::Stone, true, true);
                }
                else {
                    // South side, walking west.
                    float const x = stair_x + 6.0F - offset - step_tread;
                    block({x, bottom, stair_z + 6.0F},
                          {x + step_tread, top, stair_z + 6.0F + walkway_width},
                          ContinentSurface::Stone, true, true);
                }
            }

            // Corner landing after this flight. Its height completes the side's rise.
            float const top =
                first_level_base + static_cast<float>(loop) * loop_height + flight_height;
            float const bottom = loop == 0 && side == 0 ? 0.0F : top - slab_thickness;
            if (side == 0) {
                block({stair_x, bottom, stair_z},
                      {stair_x + landing_size, top, stair_z + landing_size},
                      ContinentSurface::Stone, true, true);
            }
            else if (side == 1) {
                block({stair_x + 6.0F, bottom, stair_z},
                      {stair_x + 6.0F + landing_size, top, stair_z + landing_size},
                      ContinentSurface::Stone, true, true);
            }
            else if (side == 2) {
                block({stair_x + 6.0F, bottom, stair_z + 6.0F},
                      {stair_x + 6.0F + landing_size, top, stair_z + 6.0F + landing_size},
                      ContinentSurface::Stone, true, true);
            }
            else {
                block({stair_x, bottom, stair_z + 6.0F},
                      {stair_x + landing_size, top, stair_z + 6.0F + landing_size},
                      ContinentSurface::Stone, true, true);
            }
        }
    }

    // A 320 x 384 metre city, with an open twelve-metre gate and a continuous main road.
    patch({-160.0F, -384.0F}, {160.0F, 0.0F}, ContinentSurface::Stone);
    patch({-8.0F, -380.0F}, {8.0F, 0.0F}, ContinentSurface::Road);
    patch({-30.0F, -220.0F}, {30.0F, -164.0F}, ContinentSurface::Road);
    for (float z = -64.0F; z > -384.0F; z -= 64.0F)
        patch({-156.0F, z - 6.0F}, {156.0F, z + 6.0F}, ContinentSurface::Road);
    for (float x : {-104.0F, -56.0F, 56.0F, 104.0F})
        patch({x - 5.0F, -380.0F}, {x + 5.0F, -4.0F}, ContinentSurface::Road);

    block({-160.0F, 0.0F, -4.0F}, {-6.0F, 10.0F, 0.0F}, ContinentSurface::Stone);
    block({6.0F, 0.0F, -4.0F}, {160.0F, 10.0F, 0.0F}, ContinentSurface::Stone);
    block({-6.0F, 6.0F, -4.0F}, {6.0F, 12.0F, 0.0F}, ContinentSurface::Stone);
    block({-160.0F, 0.0F, -384.0F}, {160.0F, 10.0F, -380.0F}, ContinentSurface::Stone);
    block({-160.0F, 0.0F, -380.0F}, {-156.0F, 10.0F, -4.0F}, ContinentSurface::Stone);
    block({156.0F, 0.0F, -380.0F}, {160.0F, 10.0F, -4.0F}, ContinentSurface::Stone);
    for (float x : {-14.0F, 14.0F}) {
        block({x - 5.0F, 0.0F, -8.0F}, {x + 5.0F, 16.0F, 4.0F}, ContinentSurface::Stone);
        block({x - 5.5F, 16.0F, -8.5F}, {x + 5.5F, 17.0F, 4.5F}, ContinentSurface::Roof);
    }
    for (float x = -156.0F; x < 160.0F; x += 8.0F) {
        if (std::abs(x) >= 22.0F)
            block({x - 1.5F, 10.0F, -4.0F}, {x + 1.5F, 12.0F, 0.0F}, ContinentSurface::Stone);
    }
    // Facade markers are decorative; building interiors are not implemented yet.
    for (int row = 0; row < 6; ++row) {
        float const z = -32.0F - static_cast<float>(row) * 64.0F;
        for (float x : {-132.0F, -80.0F, -32.0F, 32.0F, 80.0F, 132.0F}) {
            if ((row == 2 || row == 3) && std::abs(x) < 40.0F)
                continue;
            float const height = 7.0F + static_cast<float>(row % 3) * 2.0F;
            block({x - 12.0F, 0.0F, z - 17.0F}, {x + 12.0F, height, z + 17.0F},
                  ContinentSurface::Plaster);
            block({x - 13.0F, height, z - 18.0F}, {x + 13.0F, height + 2.0F, z + 18.0F},
                  ContinentSurface::Roof);
            block({x - 1.2F, 0.0F, z + 17.0F}, {x + 1.2F, 2.5F, z + 17.08F}, ContinentSurface::Wood,
                  false);
            for (float offset : {-8.0F, -4.0F, 4.0F, 8.0F})
                block({x + offset - 0.7F, 3.5F, z + 17.0F}, {x + offset + 0.7F, 5.2F, z + 17.08F},
                      ContinentSurface::Window, false);
        }
    }
    block({-22.0F, 0.0F, -366.0F}, {22.0F, 18.0F, -340.0F}, ContinentSurface::Stone);
    block({-24.0F, 18.0F, -368.0F}, {24.0F, 21.0F, -338.0F}, ContinentSurface::Roof);

    // Southfield: the main through-road stays open; offset yards and narrow spurs
    // reveal the workshop, market and distant bell tower in successive views.
    auto house = [&](float x, float z, float width, float depth, float eaves,
                     bool ridge_x = false) {
        block({x, 0, z}, {x + width, 0.65F, z + depth}, ContinentSurface::Stone);
        block({x + 0.15F, 0.65F, z + 0.15F}, {x + width - 0.15F, eaves, z + depth - 0.15F},
              ContinentSurface::Plaster);
        data.blocks.push_back(
            {{x - 0.55F, eaves, z - 0.6F},
             {x + width + 0.55F, eaves + (ridge_x ? depth : width) * 0.65F, z + depth + 0.6F},
             ContinentSurface::Roof,
             true,
             false,
             ridge_x ? BlockShape::GableX : BlockShape::GableZ});
        // Timber bays wrap all four elevations, with a sill and an upper tie beam.
        for (float dx : {0.0F, width * 0.5F, width - 0.22F}) {
            for (float dz : {0.0F, depth - 0.22F})
                block({x + dx, 0.65F, z + dz}, {x + dx + 0.22F, eaves, z + dz + 0.22F},
                      ContinentSurface::Wood, false);
        }
        for (float y : {0.8F, eaves - 0.25F}) {
            block({x, y, z}, {x + width, y + 0.2F, z + 0.18F}, ContinentSurface::Wood, false);
            block({x, y, z + depth - 0.18F}, {x + width, y + 0.2F, z + depth},
                  ContinentSurface::Wood, false);
            block({x, y, z}, {x + 0.18F, y + 0.2F, z + depth}, ContinentSurface::Wood, false);
            block({x + width - 0.18F, y, z}, {x + width, y + 0.2F, z + depth},
                  ContinentSurface::Wood, false);
        }
        block({x + width * 0.5F - 0.65F, 0.65F, z - 0.03F},
              {x + width * 0.5F + 0.65F, 2.9F, z + 0.04F}, ContinentSurface::Wood, false);
        for (float dx : {width * 0.23F, width * 0.77F}) {
            block({x + dx - 0.4F, 2.0F, z - 0.06F}, {x + dx + 0.4F, 3.0F, z + 0.01F},
                  ContinentSurface::Window, false);
            block({x + dx - 0.52F, 1.85F, z - 0.18F}, {x + dx + 0.52F, 2.0F, z + 0.1F},
                  ContinentSurface::Wood, false);
        }
        // The long elevations face the lane. Give them real timber bays and
        // recessed-looking shuttered windows, rather than blank plaster walls.
        for (bool east : {false, true}) {
            float const face = east ? x + width - 0.15F : x + 0.15F;
            for (float fraction : {0.0F, 0.33F, 0.67F, 1.0F}) {
                float const z0 = z + 0.15F + (depth - 0.5F) * fraction;
                block({face - 0.13F, 0.65F, z0}, {face + 0.13F, eaves, z0 + 0.2F},
                      ContinentSurface::Wood, false);
            }
            for (float fraction : {0.23F, 0.77F}) {
                float const z0 = z + depth * fraction;
                block({face - 0.18F, 1.8F, z0 - 0.5F}, {face + 0.18F, 2.9F, z0 + 0.5F},
                      ContinentSurface::Window, false);
                for (float offset : {-0.68F, 0.5F})
                    block({face - 0.22F, 1.75F, z0 + offset},
                          {face + 0.22F, 3.0F, z0 + offset + 0.18F}, ContinentSurface::Wood, false);
                block({face - 0.25F, 1.65F, z0 - 0.72F}, {face + 0.25F, 1.8F, z0 + 0.72F},
                      ContinentSurface::Wood, false);
                if (eaves > 5) {
                    block({face - 0.18F, 4.0F, z0 - 0.4F}, {face + 0.18F, 4.9F, z0 + 0.4F},
                          ContinentSurface::Window, false);
                }
            }
            if (eaves > 5)
                block({face - 0.18F, 3.45F, z}, {face + 0.18F, 3.65F, z + depth},
                      ContinentSurface::Wood, false);
        }
        float const door_x = x < 0 ? x + width - 0.05F : x + 0.05F;
        float const door_z = z + depth * 0.5F;
        block({door_x - 0.15F, 0.65F, door_z - 0.7F}, {door_x + 0.15F, 2.8F, door_z + 0.7F},
              ContinentSurface::Wood, false);
        block({door_x - 0.18F, 2.75F, door_z - 0.9F}, {door_x + 0.18F, 3.05F, door_z + 0.9F},
              ContinentSurface::Wood, false);
        // Split the foundation into exposed stone courses, with staggered joints.
        for (int course = 0; course < 2; ++course) {
            float const y = static_cast<float>(course) * 0.3F;
            for (float offset = 0.15F; offset < depth - 0.5F; offset += 1.4F) {
                float const z0 = z + offset + static_cast<float>(course) * 0.25F;
                for (float side : {x - 0.06F, x + width - 0.06F})
                    block({side, y, z0}, {side + 0.12F, y + 0.26F, z0 + 1.1F},
                          ContinentSurface::Stone, false);
            }
        }
        block(
            {x + width - 2.0F, eaves - 0.5F, z + depth - 2.8F},
            {x + width - 0.8F, eaves + (ridge_x ? depth : width) * 0.65F + 1.2F, z + depth - 1.6F},
            ContinentSurface::Stone);
    };
    house(-17, 82, 10, 13, 5.3F);      // Tall gate-side dwelling.
    house(10, 91, 8, 10, 3.6F, true);  // Lower cottage exposes the forge beyond it.
    house(-29, 119, 12, 9, 4.4F);      // Smith's home behind the working yard.
    house(26, 136, 9, 15, 6.6F);       // Narrow two-storey inn at the market bend.
    house(-23, 153, 7, 9, 3.2F, true); // Small rear cottage, not a repeated housing row.

    // Open-front smithy: a masonry hearth, rear walls and a canopy over
    // the workbench. The yard opens east towards the lane, rather than facing a wall.
    block({-29, 0, 105}, {-26, 3.4F, 114}, ContinentSurface::Stone);
    block({-29, 0, 113}, {-16, 3.4F, 114}, ContinentSurface::Stone);
    for (float z : {105.0F, 113.5F})
        block({-16.5F, 0, z}, {-16, 3.4F, z + 0.5F}, ContinentSurface::Wood);
    data.blocks.push_back({{-30, 3.4F, 104},
                           {-15, 7.4F, 115},
                           ContinentSurface::Roof,
                           true,
                           false,
                           BlockShape::GableZ});
    // The hearth bed rests on masonry down to ground level, not on a floating slab.
    block(village::hearth + glm::vec3{-2.5F, -1.5F, -1.5F},
          village::hearth + glm::vec3{1, -0.5F, 1.5F}, ContinentSurface::Stone);
    block(village::hearth + glm::vec3{-2.5F, -0.5F, -1.5F},
          village::hearth + glm::vec3{1, -0.05F, 1.5F}, ContinentSurface::Stone);
    block(village::chimney + glm::vec3{-1, -6, -1}, village::chimney + glm::vec3{1, 0, 1},
          ContinentSurface::Stone);
    // Anvil: broad foot, narrow waist, overhanging striking face and horn.
    block({-20.4F, 0, 109}, {-19.2F, 0.75F, 110.2F}, ContinentSurface::Wood);
    block({-20.1F, 0.75F, 109.3F}, {-19.5F, 1.05F, 109.9F}, ContinentSurface::Stone);
    block({-20.5F, 1.05F, 109.1F}, {-18.9F, 1.25F, 110.1F}, ContinentSurface::Stone);
    block({-19, 1.08F, 109.35F}, {-18.4F, 1.2F, 109.85F}, ContinentSurface::Stone);
    block({-24, 0, 112}, {-20.5F, 1.05F, 112.8F}, ContinentSurface::Wood);
    for (int i = 0; i < 7; ++i) {
        float const offset = static_cast<float>(i) * 0.34F;
        block({-25 + offset, 0, 115.5F}, {-24.8F + offset, 0.3F, 117}, ContinentSurface::Stone,
              false);
    }

    // Household supplies stay against walls, leaving doorways and desire lines free.
    for (auto const base : {glm::vec2{-17, 97}, glm::vec2{19, 94}, glm::vec2{-23, 164}}) {
        for (int layer = 0; layer < 3; ++layer) {
            for (int log = 0; log < 4 - layer; ++log) {
                float const x = base.x + static_cast<float>(log) * 0.35F;
                float const y = static_cast<float>(layer) * 0.28F;
                block({x, y, base.y}, {x + 0.3F, y + 0.25F, base.y + 1.5F}, ContinentSurface::Wood,
                      false, false, BlockShape::CylinderZ);
            }
        }
        block({base.x + 2, 0, base.y}, {base.x + 2.6F, 0.7F, base.y + 0.6F}, ContinentSurface::Wood,
              true, false, BlockShape::CylinderY);
    }
    // Two unequal stalls form a loose market edge, rather than a regular grid.
    for (auto const base : {glm::vec2{14, 121}, glm::vec2{21, 128}}) {
        for (float dx : {0.0F, 4.0F})
            for (float dz : {0.0F, 2.6F})
                block({base.x + dx, 0, base.y + dz},
                      {base.x + dx + 0.12F, 2.7F, base.y + dz + 0.12F}, ContinentSurface::Wood);
        data.blocks.push_back({{base.x - 0.3F, 2.7F, base.y - 0.35F},
                               {base.x + 4.4F, 3.4F, base.y + 3.1F},
                               ContinentSurface::Canvas,
                               false,
                               false,
                               BlockShape::GableZ});
        block({base.x, 0.8F, base.y + 1.5F}, {base.x + 4, 1, base.y + 2.5F},
              ContinentSurface::Wood);
        for (float dx : {0.4F, 1.6F, 3.1F})
            block({base.x + dx, 1, base.y + 1.7F}, {base.x + dx + 0.6F, 1.5F, base.y + 2.3F},
                  ContinentSurface::Canvas, false);
    }
    // A handcart parked by the inn: bed, two wheels and long handles.
    block({23, 0.55F, 133}, {25, 0.8F, 135.5F}, ContinentSurface::Wood);
    for (float x : {22.8F, 24.9F}) {
        block({x, 0, 133.8F}, {x + 0.3F, 0.9F, 134.7F}, ContinentSurface::Wood, true, false,
              BlockShape::CylinderX);
        block({x, 0.7F, 131}, {x + 0.15F, 0.85F, 133.8F}, ContinentSurface::Wood, false);
    }
    // Kitchen gardens: individual low beds and stick fencing, never Road patches.
    for (int row = 0; row < 4; ++row) {
        float const z = 170 + static_cast<float>(row) * 2.6F;
        block({-33, 0, z}, {-22, 0.2F, z + 0.9F}, ContinentSurface::Road, false);
        for (int plant = 0; plant < 8; ++plant) {
            float const x = -32 + static_cast<float>(plant) * 1.3F;
            block({x, 0.2F, z + 0.1F}, {x + 0.5F, 0.5F, z + 0.7F}, ContinentSurface::Foliage,
                  false);
        }
    }
    // Short boundaries enclose working yards without fencing across the routes.
    auto fence = [&](glm::vec2 from, glm::vec2 to) {
        bool const along_x = from.y == to.y;
        float const length = glm::distance(from, to);
        int const bays = static_cast<int>(std::ceil(length / 2.2F));
        for (int i = 0; i <= bays; ++i) {
            auto const position = glm::mix(from, to, static_cast<float>(i) / bays);
            block({position.x - 0.09F, 0, position.y - 0.09F},
                  {position.x + 0.09F, 1.1F, position.y + 0.09F}, ContinentSurface::Wood);
        }
        for (float y : {0.35F, 0.85F}) {
            auto const min = glm::min(from, to);
            auto const max = glm::max(from, to);
            block({min.x - 0.05F, y, min.y - 0.05F},
                  {max.x + (along_x ? 0.05F : 0.08F), y + 0.12F, max.y + 0.08F},
                  ContinentSurface::Wood);
        }
    };
    fence({-23, 80}, {-23, 99});
    fence({-23, 99}, {-18, 99});
    fence({24, 88}, {24, 107});
    fence({18, 107}, {24, 107});
    fence({39, 134}, {39, 158});
    fence({27, 158}, {39, 158});
    fence({-35, 166}, {-35, 181});
    fence({-35, 181}, {-21, 181});

    auto barrel = [&](glm::vec2 position, float height) {
        float const radius = height * 0.34F;
        block({position.x - radius, 0, position.y - radius},
              {position.x + radius, height, position.y + radius}, ContinentSurface::Wood, true,
              false, BlockShape::CylinderY);
        for (float y : {height * 0.16F, height * 0.78F})
            block({position.x - radius - 0.025F, y, position.y - radius - 0.025F},
                  {position.x + radius + 0.025F, y + 0.08F, position.y + radius + 0.025F},
                  ContinentSurface::Stone, false, false, BlockShape::CylinderY);
    };
    barrel({-17.7F, 106.6F}, 1.1F); // Quenching water beside the anvil.
    barrel({19.7F, 123}, 1.2F);
    barrel({20.3F, 124.3F}, 0.85F);
    barrel({25, 140}, 1.25F);
    barrel({24.5F, 141.3F}, 1.0F);

    // Split firewood under a low rain shelter beside the smith's home.
    for (float x : {-32.0F, -29.7F})
        for (float z : {120.0F, 125.0F})
            block({x, 0, z}, {x + 0.15F, 2.0F, z + 0.15F}, ContinentSurface::Wood);
    block({-32.4F, 2, 119.6F}, {-29.2F, 2.25F, 125.5F}, ContinentSurface::Roof);
    for (int row = 0; row < 3; ++row)
        for (int log = 0; log < 6 - row; ++log) {
            float const z = 120.2F + static_cast<float>(log) * 0.55F;
            float const y = static_cast<float>(row) * 0.4F;
            block({-32, y, z}, {-30, y + 0.38F, z + 0.5F}, ContinentSurface::Wood, false, false,
                  BlockShape::CylinderX);
        }
    // A trestle carries worked bars; rough stock stays against the back wall.
    for (float x : {-23.5F, -21.0F})
        block({x, 0, 111.9F}, {x + 0.25F, 1, 112.8F}, ContinentSurface::Wood);
    for (int bar = 0; bar < 5; ++bar) {
        float const z = 112.0F + static_cast<float>(bar) * 0.14F;
        block({-23.8F, 1.06F, z}, {-21.4F, 1.13F, z + 0.07F}, ContinentSurface::Stone, false);
    }
    // A stone well marks the market edge, with a cap, winding drum and bucket.
    block({17, 0, 116}, {19.8F, 0.3F, 118.8F}, ContinentSurface::Stone);
    for (float x : {17.2F, 19.2F}) {
        block({x, 0.3F, 116.3F}, {x + 0.35F, 1.1F, 118.5F}, ContinentSurface::Stone);
        block({x, 1.1F, 117.2F}, {x + 0.2F, 3.1F, 117.4F}, ContinentSurface::Wood);
    }
    for (float z : {116.3F, 118.15F})
        block({17.5F, 0.3F, z}, {19.2F, 1.1F, z + 0.35F}, ContinentSurface::Stone);
    block({17.4F, 2, 117}, {19.5F, 2.35F, 117.6F}, ContinentSurface::Wood, false, false,
          BlockShape::CylinderX);
    block({18.35F, 0.5F, 117.3F}, {18.4F, 2.1F, 117.35F}, ContinentSurface::Wood, false);
    data.blocks.push_back({{16.8F, 3.1F, 116},
                           {20, 4.2F, 119},
                           ContinentSurface::Roof,
                           true,
                           false,
                           BlockShape::GableX});
    block({20.2F, 0, 117.8F}, {20.8F, 0.65F, 118.4F}, ContinentSurface::Wood, true, false,
          BlockShape::CylinderY);

    // Tall, narrow bell tower anchors the view beyond the workshop roofline.
    block({-17, 0, 184}, {-11, 15, 190}, ContinentSurface::Stone);
    for (float x : {-17.0F, -12.0F})
        for (float z : {184.0F, 189.0F})
            block({x, 15, z}, {x + 1, 19, z + 1}, ContinentSurface::Stone);
    block({-17, 19, 184}, {-11, 19.5F, 190}, ContinentSurface::Wood);
    data.blocks.push_back({{-18, 19.5F, 183},
                           {-10, 25, 191},
                           ContinentSurface::Roof,
                           true,
                           false,
                           BlockShape::GableZ});
    block({-14.3F, 16, 186.7F}, {-13.7F, 17.5F, 187.3F}, ContinentSurface::Wood, false);

    // These entrances also make local activity areas explicit in the existing road
    // graph; there is no second road-paint or collision representation.
    constexpr auto yard = village::smithy, market = village::market, tower = village::tower;
    auto const region = std::ranges::find(data.locations, LocationId{2}, &Location::id)->region;
    data.locations.push_back({yard,
                              "Smithy Yard",
                              LocationType::Wilderness,
                              region,
                              {{-30, 102}, {-8, 118}},
                              {-13, 110}});
    data.locations.push_back({market,
                              "Village Market",
                              LocationType::Wilderness,
                              region,
                              {{6, 117}, {26, 134}},
                              {10, 125}});
    data.locations.push_back({tower,
                              "Bell Tower",
                              LocationType::Wilderness,
                              region,
                              {{-20, 180}, {-7, 194}},
                              {-8, 183}});
    data.roads.push_back({RoadId{101},
                          "Smithy Approach",
                          LocationId{2},
                          yard,
                          5,
                          {{0, 112}, {-5, 115}, {-13, 110}}});
    data.roads.push_back(
        {RoadId{102}, "Market Lane", LocationId{2}, market, 3.2F, {{0, 112}, {5, 119}, {10, 125}}});
    data.roads.push_back({RoadId{103},
                          "Chapel Lane",
                          market,
                          tower,
                          2.4F,
                          {{10, 125}, {12, 145}, {6, 161}, {-8, 174}, {-8, 183}}});
}

} // namespace lc1
