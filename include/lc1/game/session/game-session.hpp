#pragma once

#include "lc1/game/actions.hpp"
#include "lc1/game/player/player-view.hpp"
#include "lc1/game/session/game-simulation.hpp"
#include "lc1/game/ui/game-ui.hpp"
#include "lc1/game/ui/music-browser.hpp"
#include "lc1/game/world/world-presentation.hpp"
#include <utility>

namespace lc1 {

// Coordinates a local playable session, not a server GameMode or a network replica.
// Application owns window/input/backend lifetime. Device, Window and GameSimulation outlive this
// session; Application waits for submitted GPU work before destroying it.
class GameSession {
  public:
    GameSession(Device const &device, std::filesystem::path const &asset_root,
                GameSimulation &simulation, std::filesystem::path const &save_directory,
                OutputSettings output, Window *window, PlayerId player);

    GameSession(GameSession const &) = delete;
    GameSession &operator=(GameSession const &) = delete;
    GameSession(GameSession &&) = delete;
    GameSession &operator=(GameSession &&) = delete;

    ~GameSession() = default;

    // Resolves actions once, submits value input and presents the latest completed tick.
    // Applies cursor capture directly through the borrowed Window after UI actions.
    void update(float delta_seconds, InputState const &input_state);

    // Called when no drawable frame exists, so stale held controls cannot keep moving.
    void release_controls();

    void set_aspect_ratio(float aspect_ratio);

    std::optional<std::filesystem::path> take_load_request();
    std::optional<std::filesystem::path> take_save_request();
    void save_completed(std::filesystem::path directory);
    void save_failed(std::string message);

    void resume_loaded();

    void load_failed(std::string message);

    bool quit_requested() const;

    // Main-thread views. World snapshot contents are replaced on the next update().
    WorldSnapshot const &world() const;

    WorldPresentation const &world_presentation() const;

    PlayerView const &player_view() const;
    PlayerId player_id() const;

    GameUi const &ui() const;

  private:
    void request_save(std::string_view name);
    void refresh_saves();
    void update_title();

    bool should_captures_cursor() const;

    Window *window_; // Required non-null borrow; Application's Window outlives this.
    GameInputRouter input_router_;
    // Owners precede borrowers: presentation reads the local snapshot, music borrows its audio
    // context.
    GameSimulation *simulation_; // Non-null borrow; Application keeps it alive past this session.
    GameSimulation::Connection connection_;
    WorldSnapshot snapshot_;
    WorldPresentation world_presentation_;
    PlayerView player_view_;
    MusicBrowser music_;
    GameUi ui_;

    std::filesystem::path save_directory_;
    std::filesystem::path saves_root_;
    std::vector<std::string> saves_;
    std::optional<std::filesystem::path> load_request_;
    std::optional<std::filesystem::path> save_request_;

    bool quit_requested_ = false;
};

} // namespace lc1
