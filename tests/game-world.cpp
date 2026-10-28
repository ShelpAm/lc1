#include "lc1/game/world/game-world.hpp"

#include "lc1/error.hpp"
#include "lc1/game/continent/patrol-house.hpp"
#include "lc1/game/party/party.hpp"
#include "lc1/game/player/player-view.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
void check(bool condition, std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string{message});
}

class Checkpoint {
  public:
    Checkpoint()
        : directory_(std::filesystem::temp_directory_path() /
                     std::format("lc1-world-test-{:p}", static_cast<void const *>(this)))
    {
        auto data = lc1::Save{"saves/01"}.data();
        std::ranges::sort(data.characters, {}, &lc1::SerializedCharacterConfig::keyid);
        for (auto const &character : data.characters) {
            auto const index = data.states.size();
            data.states.push_back({.keyid = character.keyid,
                                   .x = static_cast<float>(index) * 10.0F,
                                   .y = 0,
                                   .z = 64,
                                   .body_yaw = 0,
                                   .look_yaw = 0,
                                   .look_pitch = 0,
                                   .patrol = false});
        }
        for (auto &state : data.states) {
            if (state.keyid == data.session.player) {
                state.x = 0;
                state.z = 64;
            }
            if (state.keyid == 100025) {
                state.x = 0;
                state.z = 62;
            }
        }
        lc1::Save::write(directory_, data);
    }

    ~Checkpoint()
    {
        std::error_code ignored;
        std::filesystem::remove_all(directory_, ignored);
    }

    std::filesystem::path const &directory() const { return directory_; }

  private:
    std::filesystem::path directory_;
};

lc1::Character const &find(lc1::GameWorld const &world, std::uint32_t id)
{
    auto const characters = world.characters();
    auto const result = std::ranges::find_if(
        characters, [id](auto const *character) { return character->config().keyid == id; });
    check(result != characters.end(), "character exists in runtime roster");
    return **result;
}

void world_and_checkpoint()
{
    Checkpoint checkpoint;
    // Save is destroyed immediately after construction. The world retains no borrow to it.
    lc1::GameWorld world{lc1::Save{checkpoint.directory()}, "dialogs/catalog.yaml"};
    check(world.characters().size() == 7, "headless world builds the entire fixture");
    check(std::ranges::is_sorted(world.characters(), {},
                                 [](auto const *character) { return character->config().keyid; }),
          "runtime roster has stable identity order");
    check(world.player().config().party->leader() == &world.player(), "membership is linked");
    check(find(world, 100025).config().equipment.size() == 1,
          "equipment links survive world assembly");

    world.update(0, {.player_intent = {.interact = true}});
    check(world.interaction_target() == &find(world, 100025), "facing selects the nearby NPC");
    check(world.dialogs().dialog_id(world.player()) == "recruit", "world starts recruitment");
    world.update(0, {.response_index = 0});
    // Long stalls use the same simulation cap for conversations and physics.
    world.update(10, {});
    check(world.dialogs().find(world.player())->displaying_response(),
          "long frame cannot skip response display while movement is capped");
    for (int i = 0; i < 16; ++i)
        world.update(0.1F, {});
    check(world.dialogs().find(world.player())->current_node_id() == "offer",
          "conversation advances on simulation time");
    world.update(0, {.response_index = 0});
    check(find(world, 100025).config().party == world.player().config().party,
          "dialog action transfers both sides of membership");
    check(find(world, 100025).companion_order() == lc1::CompanionOrder::Follow,
          "recruited character follows the leader");
    world.update(0, {.player_intent = {.interact = true}});
    check(!world.dialogs().find(world.player()), "player can interrupt a conversation");
    world.player().move(world.continent(), {10, 0, 0});
    auto const before = find(world, 100025).position();
    world.update(0.1F, {});
    check(glm::distance(find(world, 100025).position(), world.player().position()) <
              glm::distance(before, world.player().position()),
          "follower uses updated leader pose");
    world.player().set_body_yaw(40);
    world.player().set_look_angles(20, 10);
    lc1::Save::write(checkpoint.directory(), world.snapshot());
    lc1::GameWorld restored{lc1::Save{checkpoint.directory()}, "dialogs/catalog.yaml"};
    check(restored.player().position() == world.player().position(),
          "checkpoint restores position");
    check(restored.player().body_yaw() == 40 && restored.player().look_yaw() == 20 &&
              restored.player().look_pitch() == 10,
          "checkpoint restores body and look angles");
    check(find(restored, 100025).config().party == restored.player().config().party &&
              find(restored, 100025).companion_order() == lc1::CompanionOrder::Follow,
          "checkpoint restores recruitment and companion commands");
    check(!restored.dialogs().find(restored.player()),
          "active dialogue is intentionally transient");

    for (auto elapsed : {-1.0F, std::numeric_limits<float>::quiet_NaN()}) {
        bool rejected = false;
        try {
            restored.update(elapsed, {});
        }
        catch (lc1::Error const &) {
            rejected = true;
        }
        check(rejected, "world rejects invalid simulation time");
    }
}

void reload_world()
{
    Checkpoint checkpoint;
    lc1::GameWorld world{lc1::Save{checkpoint.directory()}, "dialogs/catalog.yaml"};
    auto const initial_position = world.player().position();
    lc1::CharacterIntent interact;
    interact.interact = true;
    world.update(0, {.player_intent = interact});
    check(world.dialogs().find(world.player()), "reload begins with an active conversation");
    world.player().move(world.continent(), {10, 0, 0});
    world.player().set_look_angles(20, 10);

    for (int i = 0; i < 2; ++i) {
        world.load(lc1::Save{checkpoint.directory()});
        check(world.characters().size() == 7, "reload replaces rather than appends the roster");
        check(world.player().position() == initial_position && world.player().look_yaw() == 0,
              "reload discards the old character state");
        check(!world.interaction_target() && !world.dialogs().find(world.player()),
              "reload clears interactions and active conversations");
        check(world.player().config().party->leader() == &world.player(),
              "reload relinks party membership");
        world.update(0, {.player_intent = interact});
        check(world.dialogs().dialog_id(world.player()) == "recruit",
              "reloaded interactions borrow the live dialog system");
    }

    auto data = lc1::Save{checkpoint.directory()}.data();
    auto alternate = data;
    alternate.continent = {"prototype", 1};
    lc1::Save::write(checkpoint.directory(), alternate);
    bool rejected = false;
    try {
        world.load(lc1::Save{checkpoint.directory()});
    }
    catch (lc1::Error const &) {
        rejected = true;
    }
    check(rejected && world.player().position() == initial_position,
          "changing continent is rejected without clearing the existing world");
    check(world.snapshot().continent == data.continent,
          "world snapshots retain the actual continent identity");
    data.session.player = 100025;
    data.session.example = 0;
    lc1::Save::write(checkpoint.directory(), data);
    world.load(lc1::Save{checkpoint.directory()});
    check(world.player().config().keyid == 100025, "reload changes player identity");
    check(world.snapshot().session.example == 0, "reload clears the previous example identity");
    world.update(0.1F, {});
}

void restored_house_patrol()
{
    Checkpoint checkpoint;
    auto data = lc1::Save{checkpoint.directory()}.data();
    auto const example = data.session.example;
    for (auto &state : data.states) {
        if (state.keyid != example)
            continue;
        state.x = 36;
        state.y = 7.8F;
        state.z = 23;
        state.patrol = true;
    }
    lc1::Save::write(checkpoint.directory(), data);
    lc1::GameWorld world{lc1::Save{checkpoint.directory()}, "dialogs/catalog.yaml"};
    check(find(world, example).position().y == 7.8F,
          "house patrol restores the saved floor without teleporting");
    bool descended = false;
    bool climbed_again = false;
    for (int frame = 0; frame < 60 * 160; ++frame) {
        world.update(1.0F / 60.0F, {});
        auto const height = find(world, example).position().y;
        descended |= height < 0.7F;
        climbed_again |= descended && height > 7.7F;
    }
    check(descended && climbed_again,
          "world restores a multi-floor patrol and completes its return route");
    auto snapshot = world.snapshot();
    check(
        std::ranges::find(snapshot.states, example, &lc1::SerializedCharacterState::keyid)->patrol,
        "house patrol retains the existing save patrol flag");
}

void world_applies_control()
{
    Checkpoint checkpoint;
    lc1::GameWorld world{lc1::Save{checkpoint.directory()}, "dialogs/catalog.yaml"};
    auto const before = world.player().position();
    world.update(
        10,
        {.player_intent = {
             .move = {1, 0}, .body_yaw = 45, .look = glm::vec2{0, 10}, .face_movement = false}});
    check(std::abs(world.player().position().x - before.x -
                   world.player().body().walk_speed * 0.1F) < 0.001F,
          "world applies movement with the same capped delta as physics");
    check(world.player().body_yaw() == 45 && world.player().look_pitch() == 10,
          "world applies orientation without a view or controller update");
    auto const pose = world.player().position();
    bool rejected = false;
    try {
        world.update(0.1F,
                     {.player_intent = {.move = {0, 1},
                                        .body_yaw = std::numeric_limits<float>::quiet_NaN()}});
    }
    catch (lc1::Error const &) {
        rejected = true;
    }
    check(rejected && world.player().position() == pose,
          "invalid control is rejected before mutating the player");
}

void control_physics_camera_order()
{
    Checkpoint checkpoint;
    lc1::GameWorld world{lc1::Save{checkpoint.directory()}, "dialogs/catalog.yaml"};
    lc1::PlayerView view{world.continent(), world.player().position()};
    world.player().set_body_yaw(25);
    world.player().set_look_angles(10, 5);
    view.toggle(world.player());
    check(world.player().body_yaw() == 25 && world.player().look_yaw() == 10 &&
              world.player().look_pitch() == 5,
          "view switching leaves character pose unchanged until the world applies intent");
    auto const before_control = world.player().position();
    lc1::InputState devices;
    devices.on_key(lc1::Key::Space, true);
    lc1::GameInputRouter input{lc1::default_game_bindings(), lc1::GameInputContext::Gameplay};
    input.update(devices);
    view.update_view_mode(input, true, world.player());
    auto const command = view.resolve_input(world.player(), input, {}, true, true);
    check(world.player().position() == before_control && world.player().on_ground(),
          "resolving view input does not mutate world state");
    world.update(0.1F, {.player_intent = command});
    view.update_camera(0.1F, world.player());
    check(!world.player().on_ground() && world.player().position().y > 0,
          "jump input reaches physics in the same frame");
    check(view.camera().position() == world.player().eye_position(),
          "camera follows the resolved physics pose in the same frame");
    auto const before = world.player().position();
    devices.begin_frame();
    devices.on_key(lc1::Key::W, true);
    devices.on_key(lc1::Key::D, true);
    input.update(devices);
    view.update_view_mode(input, false, world.player());
    auto const disabled = view.resolve_input(world.player(), input, {500, 500}, false, false);
    world.update(0, {.player_intent = disabled});
    check(world.player().position() == before && world.player().body_yaw() == 0,
          "disabled controls cannot move or turn the player");
}
} // namespace

int main()
{
    try {
        world_and_checkpoint();
        reload_world();
        control_physics_camera_order();
        world_applies_control();
        restored_house_patrol();
        std::cout << "Game world tests passed\n";
        return EXIT_SUCCESS;
    }
    catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
