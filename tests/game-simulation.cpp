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
            if (character.keyid == data.session.player)
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

void fixed_rate_and_messages()
{
    Checkpoint checkpoint;
    lc1::Save save{checkpoint.directory};
    lc1::GameSimulation simulation{save, "dialogs/catalog.yaml"};
    auto const initial = simulation.snapshot();
    auto const initial_position = initial->player().pose.position();
    lc1::SimulationInput input;
    input.player.view.keyboard_enabled = true;
    input.player.movement = {0, 1};
    auto const started = simulation.submit(input);
    // No render update or further input is needed for simulation progress.
    auto const moving =
        wait_for(simulation, [&](auto const &state) { return state.tick >= started.tick + 8; });
    check(moving->input_sequence == started.sequence && moving->input_tick == started.tick,
          "published state acknowledges the input sequence and its simulation tick");
    lc1::GameWorld reference{save, "dialogs/catalog.yaml"};
    lc1::PlayerControls controls;
    reference.player().set_controller(std::make_unique<lc1::PlayerCharacterController>(controls));
    for (std::uint64_t tick = 1; tick <= moving->tick; ++tick) {
        if (tick == started.tick)
            controls = input.player;
        reference.update(lc1::GameSimulation::step_seconds, {});
    }
    check(glm::distance(reference.player().position(), moving->player().pose.position()) < 0.0001F,
          "worker state equals the same number of fixed simulation steps without rendering");
    check(initial->player().pose.position() == initial_position,
          "previously acquired snapshots remain immutable while the worker advances");

    auto const stopped = simulation.submit({});
    auto const released = wait_for(
        simulation, [&](auto const &state) { return state.input_sequence >= stopped.sequence; });
    auto const later =
        wait_for(simulation, [&](auto const &state) { return state.tick >= released->tick + 5; });
    check(later->player().pose.position() == released->player().pose.position(),
          "released held movement stays stopped across later ticks");
    auto saved = simulation.checkpoint();
    check(saved.wait_for(std::chrono::seconds{5}) == std::future_status::ready,
          "checkpoint request completes on the worker");
    auto const data = saved.get();
    auto const player =
        std::ranges::find(data.states, data.session.player, &lc1::SerializedCharacterState::keyid);
    check(player != data.states.end() &&
              glm::vec3{player->x, player->y, player->z} == later->player().pose.position(),
          "checkpoint contains a coherent worker-owned world state");
}

void one_shot_inputs()
{
    Checkpoint checkpoint;
    lc1::GameSimulation simulation{lc1::Save{checkpoint.directory}, "dialogs/catalog.yaml"};
    lc1::SimulationInput input;
    input.player.view.keyboard_enabled = true;
    input.player.view.mouse_enabled = true;
    input.player.view.oblique = false;
    input.player.view.look_delta = {20, -10};
    input.player.jump = true;
    input.player.swing_weapon = true;
    auto const pressed = simulation.submit(input);
    auto const applied = wait_for(
        simulation, [&](auto const &state) { return state.input_sequence == pressed.sequence; });
    check(applied->player().attack_sequence == 1 && applied->player().pose.position().y > 0,
          "headless jump and weapon requests are accepted at the tick boundary");
    check(std::abs(applied->player().pose.body_yaw() + 1) < 0.001F,
          "mouse displacement is applied once, independently of the tick rate");
    auto const landed =
        wait_for(simulation, [&](auto const &state) { return state.tick >= applied->tick + 130; });
    check(landed->player().attack_sequence == 1 &&
              std::abs(landed->player().pose.position().y) < 0.001F &&
              landed->player().pose.body_yaw() == applied->player().pose.body_yaw(),
          "jump, attack and mouse deltas do not repeat while the last input is held");

    // A burst of render frames can release a key before the worker consumes its press.
    input.player.view.look_delta = {};
    input.player.jump = false;
    input.player.swing_weapon = true;
    simulation.submit(input);
    auto const release = simulation.submit({});
    auto const after = wait_for(
        simulation, [&](auto const &state) { return state.input_sequence >= release.sequence; });
    check(after->player().attack_sequence == 2,
          "a press survives a release frame before the next simulation tick");
}

void failure_and_shutdown()
{
    Checkpoint checkpoint;
    {
        lc1::GameSimulation simulation{lc1::Save{checkpoint.directory}, "dialogs/catalog.yaml"};
        lc1::SimulationInput invalid;
        invalid.player.view.keyboard_enabled = true;
        invalid.player.movement.x = std::numeric_limits<float>::quiet_NaN();
        simulation.submit(invalid);
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
        fixed_rate_and_messages();
        one_shot_inputs();
        failure_and_shutdown();
        std::cout << "game simulation tests passed\n";
    }
    catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
