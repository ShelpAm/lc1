#pragma once

#include "lc1/game/actions.hpp"
#include "lc1/game/player/player-view.hpp"
#include "lc1/game/ui/game-ui.hpp"
#include "lc1/game/ui/music-browser.hpp"
#include "lc1/game/world/game-world.hpp"
#include "lc1/game/world/world-presentation.hpp"
#include <utility>

namespace lc1 {

// Coordinates a local playable session, not a server GameMode or a network replica.
// Application owns window/input/backend lifetime. Device and Window outlive this session;
// Application waits for submitted GPU work before destroying it.
class GameSession {
  public:
    GameSession(Device const &device, std::filesystem::path const &asset_root, Save const &save,
                OutputSettings output, Window *window);
    GameSession(GameSession const &) = delete;
    GameSession &operator=(GameSession const &) = delete;
    GameSession(GameSession &&) = delete;
    GameSession &operator=(GameSession &&) = delete;
    ~GameSession() = default;

    // Resolves actions once, then updates view mode, simulation and presentation.
    // Application applies captures_cursor() after this returns.
    void update(InputState const &input_state, float delta_seconds);

    void set_aspect_ratio(float aspect_ratio);

    std::optional<std::filesystem::path> take_load_request();

    void resume_loaded();

    void load_failed(std::string message);

    bool quit_requested() const;

    // Read-only borrows; valid for this session's lifetime. Mutations use session operations.
    GameWorld const &world() const;

    WorldPresentation const &world_presentation() const;

    PlayerView const &player_view() const;

    GameUi const &ui() const;

  private:
    void save(std::string_view name);
    void refresh_saves();
    void update_title();

    bool should_captures_cursor() const;

    Window *window_; // Required non-null borrow; Application's Window outlives this.
    GameInputRouter input_router_;
    // Owners precede borrowers: view/player view borrow world, music borrows view's audio context.
    GameWorld world_;
    WorldPresentation world_presentation_;
    PlayerView player_view_;
    MusicBrowser music_;
    GameUi ui_;

    std::filesystem::path save_directory_;
    std::filesystem::path saves_root_;
    std::vector<std::string> saves_;
    std::optional<std::filesystem::path> load_request_;

    bool quit_requested_ = false;
};

} // namespace lc1
