#include "lc1/game/session/game-simulation.hpp"
#include "lc1/error.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <utility>

namespace lc1 {
GameSimulation::GameSimulation(Save const &save, std::filesystem::path const &dialog_catalog,
                               std::filesystem::path const &asset_root)
    : world_(save, dialog_catalog, asset_root),
      snapshot_(std::make_shared<WorldSnapshot const>(capture_world(world_))),
      previous_snapshot_(snapshot_)
{
    spdlog::info("[save] {}: {} factions, {} parties, {} characters, {} equipment, {} weapons",
                 save.directory().string(), save.factions().size(), save.parties().size(),
                 save.characters().size(), save.equipments().size(), save.weapons().size());
    for (auto const &[id, _] : world_.players())
        inputs_.emplace(id, PlayerInput{});
    thread_ = std::jthread([this](std::stop_token stop) { run(stop); });
}

GameSimulation::~GameSimulation()
{
    thread_.request_stop();
    wake_.notify_all();
    thread_.join();
}

GameSimulation::Connection::Connection(GameSimulation &simulation, PlayerId id)
    : simulation_(&simulation), id_(id)
{
}

InputStamp GameSimulation::Connection::submit(PlayerControls input) const
{
    return simulation_->submit(id_, std::move(input));
}

void GameSimulation::Connection::transfer_control(CharacterId target) const
{
    simulation_->transfer_control(id_, target);
}

PlayerId GameSimulation::Connection::player_id() const
{
    return id_;
}

GameSimulation::Connection GameSimulation::connect(PlayerId id)
{
    // Player roster and bindings are immutable for this simulation's lifetime.
    if (!inputs_.contains(id))
        fail("unknown player {}", id.value);
    return Connection{*this, id};
}

InputStamp GameSimulation::submit(PlayerId id, PlayerControls input)
{
    std::scoped_lock lock{mutex_};
    if (failure_)
        std::rethrow_exception(failure_);
    auto &slot = inputs_.at(id);
    // Several render frames may arrive before the next tick. Keep the newest held state,
    // accumulate mouse motion, and retain edges across a subsequent release/UI-gated frame.
    if (slot.pending) {
        auto const &previous = *slot.pending;
        input.jump |= previous.jump;
        input.swing_weapon |= previous.swing_weapon;
        input.nod |= previous.nod;
        input.interact |= previous.interact;
        if (!input.equipment)
            input.equipment = previous.equipment;
        if (input.response_index < 0)
            input.response_index = previous.response_index;
        if (!input.view.view_changed) {
            input.view.look_delta += previous.view.look_delta;
            input.view.view_changed = previous.view.view_changed;
            input.view.restored_first_person_angles = previous.view.restored_first_person_angles;
        }
    }
    slot.pending = std::move(input);
    slot.pending_stamp = {.sequence = ++slot.sequence, .tick = tick_ + 1};
    return slot.pending_stamp;
}

void GameSimulation::transfer_control(PlayerId id, CharacterId target)
{
    std::scoped_lock lock{mutex_};
    if (failure_)
        std::rethrow_exception(failure_);
    transfer_requests_.insert_or_assign(id, target);
    inputs_.at(id).pending = PlayerControls{};
}

std::shared_ptr<WorldSnapshot const> GameSimulation::snapshot() const
{
    std::scoped_lock lock{mutex_};
    if (failure_)
        std::rethrow_exception(failure_);
    return snapshot_;
}

WorldSnapshot GameSimulation::presentation_snapshot() const
{
    std::shared_ptr<WorldSnapshot const> previous;
    std::shared_ptr<WorldSnapshot const> current;
    std::chrono::steady_clock::time_point time;
    {
        std::lock_guard lock{mutex_};
        if (failure_)
            std::rethrow_exception(failure_);
        previous = previous_snapshot_;
        current = snapshot_;
        time = snapshot_time_;
    }
    auto const elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - time);
    float const alpha = std::clamp(elapsed.count() * tick_rate, 0.0F, 1.0F);
    return interpolate_world(*previous, *current, alpha);
}

std::future<SerializedWorld> GameSimulation::checkpoint()
{
    std::scoped_lock lock{mutex_};
    if (failure_)
        std::rethrow_exception(failure_);
    std::promise<SerializedWorld> request;
    auto result = request.get_future();
    checkpoint_requests_.push_back(std::move(request));
    return result;
}

Continent const &GameSimulation::continent() const
{
    return world_.continent();
}

void GameSimulation::run(std::stop_token stop)
{
    std::vector<std::promise<SerializedWorld>> requests;
    try {
        using Clock = std::chrono::steady_clock;
        constexpr std::chrono::duration<double> period{1.0 / tick_rate};
        auto deadline =
            std::chrono::time_point_cast<std::chrono::duration<double>>(Clock::now()) + period;
        std::map<PlayerId, PlayerControls> controls;
        std::map<PlayerId, CharacterId> transfers;
        while (!stop.stop_requested()) {
            std::uint64_t tick;
            {
                std::unique_lock lock{mutex_};
                wake_.wait_until(lock, stop, deadline, [] { return false; });
                if (stop.stop_requested())
                    break;
                tick = ++tick_;
                controls.clear();
                for (auto &[id, slot] : inputs_) {
                    if (slot.pending) {
                        controls.emplace(id, std::move(*slot.pending));
                        slot.applied = slot.pending_stamp;
                        slot.pending.reset();
                    }
                }
                requests.swap(checkpoint_requests_);
                transfers.clear();
                transfers.swap(transfer_requests_);
            }
            for (auto const &[id, target] : transfers)
                world_.transfer_control(id, target);
            world_.update(step_seconds, controls);
            auto state = capture_world(world_);
            state.tick = tick;
            for (auto const &[id, slot] : inputs_) {
                auto &player = state.players.at(id);
                player.input_sequence = slot.applied.sequence;
                player.input_tick = slot.applied.tick;
            }
            auto published = std::make_shared<WorldSnapshot const>(std::move(state));
            {
                std::lock_guard lock{mutex_};
                previous_snapshot_ = std::move(snapshot_);
                snapshot_ = std::move(published);
                snapshot_time_ = std::chrono::time_point_cast<Clock::duration>(deadline);
            }
            if (!requests.empty()) {
                auto const saved = world_.snapshot();
                for (auto &request : requests)
                    request.set_value(saved);
                requests.clear();
            }
            // Advance the original deadline: a delayed worker catches up without changing
            // the timestep or discarding simulation time. Rendering never waits for it.
            deadline += period;
        }
    }
    catch (...) {
        auto const failure = std::current_exception();
        std::lock_guard lock{mutex_};
        failure_ = failure;
        for (auto &request : requests)
            request.set_exception(failure);
        for (auto &request : checkpoint_requests_)
            request.set_exception(failure);
        checkpoint_requests_.clear();
    }
}
} // namespace lc1
