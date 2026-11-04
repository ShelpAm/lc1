#include "lc1/game/session/game-simulation.hpp"

#include <chrono>
#include <utility>

namespace lc1 {
GameSimulation::GameSimulation(Save const &save, std::filesystem::path const &dialog_catalog)
    : world_(save, dialog_catalog),
      snapshot_(std::make_shared<WorldSnapshot const>(capture_world(world_)))
{
    world_.player().set_controller(std::make_unique<PlayerCharacterController>(controls_));
    thread_ = std::jthread([this](std::stop_token stop) { run(stop); });
}

GameSimulation::~GameSimulation()
{
    thread_.request_stop();
    wake_.notify_all();
    thread_.join();
}

InputStamp GameSimulation::submit(SimulationInput input)
{
    std::scoped_lock lock{mutex_};
    if (failure_)
        std::rethrow_exception(failure_);
    // Several render frames may arrive before the next tick. Keep the newest held state,
    // accumulate mouse motion, and retain edges across a subsequent release/UI-gated frame.
    if (pending_input_) {
        auto const &previous = *pending_input_;
        input.player.jump |= previous.player.jump;
        input.player.swing_weapon |= previous.player.swing_weapon;
        input.interaction.interact |= previous.interaction.interact;
        if (input.interaction.response_index < 0)
            input.interaction.response_index = previous.interaction.response_index;
        if (!input.player.view.view_changed) {
            input.player.view.look_delta += previous.player.view.look_delta;
            input.player.view.view_changed = previous.player.view.view_changed;
            input.player.view.restored_first_person_angles =
                previous.player.view.restored_first_person_angles;
        }
    }
    pending_input_ = std::move(input);
    pending_stamp_ = {.sequence = ++sequence_, .tick = tick_ + 1};
    return pending_stamp_;
}

std::shared_ptr<WorldSnapshot const> GameSimulation::snapshot() const
{
    std::scoped_lock lock{mutex_};
    if (failure_)
        std::rethrow_exception(failure_);
    return snapshot_;
}

std::future<SerializedWorld> GameSimulation::checkpoint()
{
    std::lock_guard lock{mutex_};
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
        WorldInteraction interaction;
        InputStamp applied{};
        while (!stop.stop_requested()) {
            std::uint64_t tick;
            {
                std::unique_lock lock{mutex_};
                wake_.wait_until(lock, stop, deadline, [] { return false; });
                if (stop.stop_requested())
                    break;
                tick = ++tick_;
                if (pending_input_) {
                    controls_ = std::move(pending_input_->player);
                    interaction = pending_input_->interaction;
                    applied = pending_stamp_;
                    pending_input_.reset();
                }
                requests.swap(checkpoint_requests_);
            }
            world_.update(step_seconds, interaction);
            auto state = capture_world(world_);
            state.tick = tick;
            state.input_sequence = applied.sequence;
            state.input_tick = applied.tick;
            auto published = std::make_shared<WorldSnapshot const>(std::move(state));
            {
                std::lock_guard lock{mutex_};
                snapshot_ = std::move(published);
            }
            if (!requests.empty()) {
                auto const saved = world_.snapshot();
                for (auto &request : requests)
                    request.set_value(saved);
                requests.clear();
            }
            controls_.jump = false;
            controls_.swing_weapon = false;
            controls_.view.look_delta = {};
            controls_.view.view_changed = false;
            interaction.interact = false;
            interaction.response_index = -1;
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
