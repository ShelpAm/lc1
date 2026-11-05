#include "lc1/game/session/game-simulation.hpp"
#include "lc1/error.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
void check(bool condition, std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string{message});
}

class Checkpoint {
  public:
    Checkpoint()
        : directory(std::filesystem::temp_directory_path() /
                    std::format("lc1-simulation-test-{:p}", static_cast<void const *>(this)))
    {
        auto data = lc1::Save{"saves/01"}.data();
        std::ranges::sort(data.characters, {}, &lc1::SerializedCharacterConfig::keyid);
        data.states.clear();
        for (auto const &character : data.characters)
            data.states.push_back({.keyid = character.keyid,
                                   .x = static_cast<float>(data.states.size()) * 10,
                                   .y = 0,
                                   .z = 64,
                                   .body_yaw = 0,
                                   .look_yaw = 0,
                                   .look_pitch = 0,
                                   .patrol = false});
        for (auto &state : data.states)
            if (state.keyid == 100025) {
                state.x = 0;
                state.z = 62;
            }
        for (auto &character : data.characters)
            if (character.keyid == data.session.players.front().character)
                character.weapon[lc1::WeaponType::MeleeWeapon] = 1;
        lc1::Save::write(directory, data);
    }

    ~Checkpoint() { std::filesystem::remove_all(directory); }

    std::filesystem::path directory;
};

template <typename Predicate>
std::shared_ptr<lc1::WorldSnapshot const> wait_for(lc1::GameSimulation const &simulation,
                                                   Predicate predicate)
{
    auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (std::chrono::steady_clock::now() < deadline) {
        auto state = simulation.snapshot();
        if (predicate(*state))
            return state;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    throw std::runtime_error("simulation did not publish the expected state within five seconds");
}

void presentation_interpolation()
{
    lc1::GameWorld world{lc1::Save{"saves/01"}, "dialogs/catalog.yaml"};
    auto previous = lc1::capture_world(world);
    for (auto &character : previous.characters) {
        character.pose.world_transform.position = {0, 0, 0};
        character.pose.world_transform.rotation.y = 179;
        character.pose.relative_look_yaw = -20;
        character.pose.relative_look_pitch = -10;
    }
    auto current = previous;
    current.tick = 2;
    current.players.at(lc1::PlayerId{1}).input_sequence = 7;
    for (auto &character : current.characters) {
        character.pose.world_transform.position = {10, 2, -4};
        character.pose.world_transform.rotation.y = -179;
        character.pose.relative_look_yaw = 20;
        character.pose.relative_look_pitch = 10;
        character.attack_sequence = 3;
    }
    // Multiple render frames between the same ticks must keep advancing smoothly.
    for (float alpha : {0.0F, 0.25F, 0.5F, 0.75F, 1.0F}) {
        auto const rendered = lc1::interpolate_world(previous, current, alpha);
        check(rendered.tick == 2 && rendered.players.at(lc1::PlayerId{1}).input_sequence == 7,
              "presentation preserves latest simulation acknowledgements");
        for (auto const &character : rendered.characters) {
            auto const &pose = character.pose;
            check(glm::distance(pose.position(), glm::vec3{10, 2, -4} * alpha) < 0.0001F,
                  "all characters interpolate translation at render cadence");
            check(std::abs(pose.body_yaw() - (179 + 2 * alpha)) < 0.0001F,
                  "body yaw crosses the seam along the shortest arc");
            check(std::abs(pose.look_yaw() - (-20 + 40 * alpha)) < 0.0001F &&
                      std::abs(pose.look_pitch() - (-10 + 20 * alpha)) < 0.0001F,
                  "camera look angles use the same interpolation fraction");
            check(character.attack_sequence == 3,
                  "discrete attack events use the latest tick without interpolation");
        }
    }
    check(previous.player(lc1::PlayerId{1}).pose.position() == glm::vec3{} &&
              current.player(lc1::PlayerId{1}).pose.position() == glm::vec3{10, 2, -4},
          "render interpolation does not change authoritative snapshots");
}

void fixed_rate_and_messages()
{
    Checkpoint checkpoint;
    lc1::Save save{checkpoint.directory};
    lc1::GameSimulation simulation{save, "dialogs/catalog.yaml"};
    auto connection = simulation.connect(lc1::PlayerId{1});
    auto const initial = simulation.snapshot();
    auto const initial_position = initial->player(lc1::PlayerId{1}).pose.position();
    lc1::PlayerControls input;
    input.view.keyboard_enabled = true;
    input.movement = {0, 1};
    auto const started = connection.submit(input);
    // No render update or further input is needed for simulation progress.
    auto const moving =
        wait_for(simulation, [&](auto const &state) { return state.tick >= started.tick + 8; });
    check(moving->players.at(lc1::PlayerId{1}).input_sequence == started.sequence &&
              moving->players.at(lc1::PlayerId{1}).input_tick == started.tick,
          "published state acknowledges the input sequence and its simulation tick");
    lc1::GameWorld reference{save, "dialogs/catalog.yaml"};
    lc1::PlayerControls controls;
    reference.player(lc1::PlayerId{1})
        .set_controller(std::make_unique<lc1::PlayerCharacterController>(controls));
    for (std::uint64_t tick = 1; tick <= moving->tick; ++tick) {
        if (tick == started.tick)
            controls = input;
        reference.update(lc1::GameSimulation::step_seconds, {});
    }
    check(glm::distance(reference.player(lc1::PlayerId{1}).position(),
                        moving->player(lc1::PlayerId{1}).pose.position()) < 0.0001F,
          "worker state equals the same number of fixed simulation steps without rendering");
    check(initial->player(lc1::PlayerId{1}).pose.position() == initial_position,
          "previously acquired snapshots remain immutable while the worker advances");

    auto const stopped = connection.submit({});
    auto const released = wait_for(simulation, [&](auto const &state) {
        return state.players.at(lc1::PlayerId{1}).input_sequence >= stopped.sequence;
    });
    auto const later =
        wait_for(simulation, [&](auto const &state) { return state.tick >= released->tick + 5; });
    check(later->player(lc1::PlayerId{1}).pose.position() ==
              released->player(lc1::PlayerId{1}).pose.position(),
          "released held movement stays stopped across later ticks");
    auto saved = simulation.checkpoint();
    check(saved.wait_for(std::chrono::seconds{5}) == std::future_status::ready,
          "checkpoint request completes on the worker");
    auto const data = saved.get();
    auto const player = std::ranges::find(data.states, data.session.players.front().character,
                                          &lc1::SerializedCharacterState::keyid);
    check(player != data.states.end() && glm::vec3{player->x, player->y, player->z} ==
                                             later->player(lc1::PlayerId{1}).pose.position(),
          "checkpoint contains a coherent worker-owned world state");
}

void one_shot_inputs()
{
    Checkpoint checkpoint;
    lc1::GameSimulation simulation{lc1::Save{checkpoint.directory}, "dialogs/catalog.yaml"};
    auto connection = simulation.connect(lc1::PlayerId{1});
    lc1::PlayerControls input;
    input.view.keyboard_enabled = true;
    input.view.mouse_enabled = true;
    input.view.oblique = false;
    input.view.look_delta = {20, -10};
    input.jump = true;
    input.swing_weapon = true;
    auto const pressed = connection.submit(input);
    auto const applied = wait_for(simulation, [&](auto const &state) {
        return state.players.at(lc1::PlayerId{1}).input_sequence == pressed.sequence;
    });
    check(applied->player(lc1::PlayerId{1}).attack_sequence == 1 &&
              applied->player(lc1::PlayerId{1}).pose.position().y > 0,
          "headless jump and weapon requests are accepted at the tick boundary");
    check(std::abs(applied->player(lc1::PlayerId{1}).pose.body_yaw() + 1) < 0.001F,
          "mouse displacement is applied once, independently of the tick rate");
    auto const landed =
        wait_for(simulation, [&](auto const &state) { return state.tick >= applied->tick + 130; });
    check(landed->player(lc1::PlayerId{1}).attack_sequence == 1 &&
              std::abs(landed->player(lc1::PlayerId{1}).pose.position().y) < 0.001F &&
              landed->player(lc1::PlayerId{1}).pose.body_yaw() ==
                  applied->player(lc1::PlayerId{1}).pose.body_yaw(),
          "jump, attack and mouse deltas do not repeat while the last input is held");

    // A burst of render frames can release a key before the worker consumes its press.
    input.view.look_delta = {};
    input.jump = false;
    input.swing_weapon = true;
    connection.submit(input);
    auto const release = connection.submit({});
    auto const after = wait_for(simulation, [&](auto const &state) {
        return state.players.at(lc1::PlayerId{1}).input_sequence >= release.sequence;
    });
    check(after->player(lc1::PlayerId{1}).attack_sequence == 2,
          "a press survives a release frame before the next simulation tick");
}

void independent_players()
{
    Checkpoint checkpoint;
    auto data = lc1::Save{checkpoint.directory}.data();
    data.session.players = {{17, 1}, {29, 100024}};
    lc1::Save::write(checkpoint.directory, data);
    lc1::GameSimulation simulation{lc1::Save{checkpoint.directory}, "dialogs/catalog.yaml"};
    auto first = simulation.connect(lc1::PlayerId{17});
    auto second = simulation.connect(lc1::PlayerId{29});
    bool rejected = false;
    try {
        simulation.connect(lc1::PlayerId{1}); // Character 1 is not player 1.
    }
    catch (lc1::Error const &) {
        rejected = true;
    }
    check(rejected, "binding requires a player identity, never a character id");
    auto const initial = simulation.snapshot();
    lc1::PlayerControls forward;
    forward.view.keyboard_enabled = true;
    forward.movement = {0, 1};
    forward.nod = true;
    lc1::PlayerControls right = forward;
    right.movement = {1, 0};
    auto const a = first.submit(forward);
    auto const b = second.submit(right);
    check(a.sequence == 1 && b.sequence == 1, "each player has an independent input sequence");
    auto const moving = wait_for(
        simulation, [&](auto const &state) { return state.tick >= std::max(a.tick, b.tick) + 5; });
    auto const first_start = initial->player(first.player_id()).pose.position();
    auto const second_start = initial->player(second.player_id()).pose.position();
    auto const first_pose = moving->player(first.player_id()).pose.position();
    auto const second_pose = moving->player(second.player_id()).pose.position();
    check(first_pose.z < first_start.z && first_pose.x == first_start.x &&
              second_pose.x > second_start.x && second_pose.z == second_start.z,
          "two bound players move their own characters along independent axes");
    check(moving->player(first.player_id()).nod_sequence == 1 &&
              moving->player(second.player_id()).nod_sequence == 1,
          "one-shot actions are published for every player and consumed once");
    auto const stop = first.submit({});
    auto const released = wait_for(simulation, [&](auto const &state) {
        return state.players.at(first.player_id()).input_sequence == stop.sequence;
    });
    auto const later =
        wait_for(simulation, [&](auto const &state) { return state.tick >= released->tick + 4; });
    check(later->player(first.player_id()).pose.position() ==
                  released->player(first.player_id()).pose.position() &&
              later->player(second.player_id()).pose.position().x >
                  released->player(second.player_id()).pose.position().x &&
              later->players.at(second.player_id()).input_sequence == b.sequence,
          "releasing one connection preserves the other player's held input and acknowledgement");
    second.submit({});
    auto const saved = simulation.checkpoint().get();
    check(saved.session.players.size() == 2, "host checkpoint includes both players");
}

void failure_and_shutdown()
{
    Checkpoint checkpoint;
    {
        lc1::GameSimulation simulation{lc1::Save{checkpoint.directory}, "dialogs/catalog.yaml"};
        auto connection = simulation.connect(lc1::PlayerId{1});
        lc1::PlayerControls invalid;
        invalid.view.keyboard_enabled = true;
        invalid.movement.x = std::numeric_limits<float>::quiet_NaN();
        connection.submit(invalid);
        auto checkpoint_request = simulation.checkpoint();
        check(checkpoint_request.wait_for(std::chrono::seconds{5}) == std::future_status::ready,
              "worker failure releases a waiting checkpoint caller");
        bool rejected = false;
        try {
            checkpoint_request.get();
        }
        catch (lc1::Error const &) {
            rejected = true;
        }
        check(rejected, "simulation exceptions propagate through checkpoint requests");
        rejected = false;
        try {
            simulation.snapshot();
        }
        catch (lc1::Error const &) {
            rejected = true;
        }
        check(rejected, "simulation exceptions propagate when the main thread polls state");
    }
    // Exercise cancellation both while sleeping and immediately after construction.
    for (int i = 0; i < 5; ++i) {
        lc1::GameSimulation simulation{lc1::Save{checkpoint.directory}, "dialogs/catalog.yaml"};
    }
}
} // namespace

int main()
{
    try {
        presentation_interpolation();
        fixed_rate_and_messages();
        one_shot_inputs();
        independent_players();
        failure_and_shutdown();
        std::cout << "game simulation tests passed\n";
    }
    catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
