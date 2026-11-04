#pragma once

#include "lc1/game/player/player-character-controller.hpp"
#include "lc1/game/world/game-world.hpp"
#include "lc1/game/world/world-snapshot.hpp"

#include <condition_variable>
#include <exception>
#include <future>
#include <mutex>
#include <thread>

namespace lc1 {
struct SimulationInput {
    PlayerControls player;
    WorldInteraction interaction;
};

struct InputStamp {
    std::uint64_t sequence;
    std::uint64_t tick; // Earliest simulation tick that can consume this input.
};

// Owns the world and its fixed-rate worker. Main-thread access uses value messages only,
// except for the immutable continent, which lives until the joined worker is destroyed.
class GameSimulation {
  public:
    static constexpr std::uint32_t tick_rate = 60;
    static constexpr float step_seconds = 1.0F / tick_rate;

    explicit GameSimulation(Save const &save, std::filesystem::path const &dialog_catalog);

    GameSimulation(GameSimulation const &) = delete;
    GameSimulation &operator=(GameSimulation const &) = delete;
    GameSimulation(GameSimulation &&) = delete;
    GameSimulation &operator=(GameSimulation &&) = delete;

    ~GameSimulation();

    InputStamp submit(SimulationInput input);
    std::shared_ptr<WorldSnapshot const> snapshot() const;
    std::future<SerializedWorld> checkpoint();
    Continent const &continent() const;

  private:
    void run(std::stop_token stop);

    GameWorld world_;
    PlayerControls controls_;
    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    std::optional<SimulationInput> pending_input_;
    InputStamp pending_stamp_{};
    std::uint64_t sequence_ = 0;
    std::uint64_t tick_ = 0;
    std::vector<std::promise<SerializedWorld>> checkpoint_requests_;
    std::shared_ptr<WorldSnapshot const> snapshot_;
    std::exception_ptr failure_;
    // Last: all state is initialized before the worker starts, and joined before teardown.
    std::jthread thread_;
};
} // namespace lc1
