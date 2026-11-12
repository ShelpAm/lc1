#pragma once

#include "lc1/game/player/player-character-controller.hpp"
#include "lc1/game/world/game-world.hpp"
#include "lc1/game/world/world-snapshot.hpp"

#include <chrono>
#include <condition_variable>
#include <exception>
#include <future>
#include <mutex>
#include <thread>

namespace lc1 {
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

    explicit GameSimulation(Save const &save, std::filesystem::path const &dialog_catalog,
                            std::filesystem::path const &asset_root = "runtime/assets");

    GameSimulation(GameSimulation const &) = delete;
    GameSimulation &operator=(GameSimulation const &) = delete;
    GameSimulation(GameSimulation &&) = delete;
    GameSimulation &operator=(GameSimulation &&) = delete;

    ~GameSimulation();

    class Connection {
      public:
        InputStamp submit(PlayerControls input) const;
        void transfer_control(CharacterId target) const;
        PlayerId player_id() const;

      private:
        friend class GameSimulation;
        Connection(GameSimulation &simulation, PlayerId id);
        GameSimulation *simulation_; // Non-null; simulation outlives the bound handle.
        PlayerId id_;
    };

    // Bind an already identified player. No authentication or network transport here.
    Connection connect(PlayerId id);
    std::shared_ptr<WorldSnapshot const> snapshot() const;
    // One tick behind simulation; all presentation consumers share this sampled pose.
    WorldSnapshot presentation_snapshot() const;
    std::future<SerializedWorld> checkpoint();
    Continent const &continent() const;

  private:
    InputStamp submit(PlayerId id, PlayerControls input);
    void transfer_control(PlayerId id, CharacterId target);
    void run(std::stop_token stop);

    struct PlayerInput {
        std::optional<PlayerControls> pending;
        InputStamp pending_stamp{};
        InputStamp applied{};
        std::uint64_t sequence = 0;
    };

    GameWorld world_;
    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    std::map<PlayerId, PlayerInput> inputs_;
    std::map<PlayerId, CharacterId> transfer_requests_;
    std::uint64_t tick_ = 0;
    std::vector<std::promise<SerializedWorld>> checkpoint_requests_;
    std::shared_ptr<WorldSnapshot const> snapshot_;
    std::shared_ptr<WorldSnapshot const> previous_snapshot_;
    std::chrono::steady_clock::time_point snapshot_time_;
    std::exception_ptr failure_;
    // Last: all state is initialized before the worker starts, and joined before teardown.
    std::jthread thread_;
};
} // namespace lc1
