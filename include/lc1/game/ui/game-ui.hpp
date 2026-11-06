#pragma once

#include "lc1/game/world/world-snapshot.hpp"
#include "lc1/platform/window.hpp"
#include "lc1/vk/render/text-pass.hpp"
#include "lc1/vk/resources/gpu-font.hpp"
#include <array>
#include <optional>
#include <span>

namespace lc1 {
class MusicBrowser;
struct GameLocation;

struct GameUiInfo {
    WorldSnapshot const &world;
    CharacterId player;
    std::vector<Party const *> player_parties; // Parties related to current player
    std::span<std::string const> saves;
    std::string_view current_save;
    FrameExtent extent;
    std::string_view lighting_name;
    float exposure;
    bool showcase_animated;
    std::string_view interaction_target;
};

struct GameUiActions {
    std::optional<std::string> save;
    std::optional<std::string> load;
    bool refresh_saves = false;
    bool start = false;
    bool quit = false;
    std::optional<float> volume;
};

class GameUi {
  public:
    GameUiActions update(GameUiInfo const &info, MusicBrowser &music);

    bool gameplay_enabled() const;

    void open_save(std::string_view name);

    void open_load();

    void saved();

    void start_game();

    void toggle_player_info();

    void set_save_status(std::string status);

    std::vector<ScreenTextItem> screen_labels(WorldSnapshot const &world, PlayerId observer,
                                              GpuFont const &font, FrameExtent extent,
                                              GameLocation location) const;

  private:
    void player_info(GameUiInfo const &info);

    enum class State { MainMenu, InSettings, InGame };
    State state_ = State::MainMenu;
    float volume_ = 50.0F;
    std::string save_status_ = "F6: Save game | F9: Load game";
    std::array<char, 129> save_name_{};
    bool save_open_ = false;
    bool load_open_ = false;
    bool player_info_opened_ = false;
    std::optional<std::uint32_t> selected_party_;
    std::optional<CharacterId> selected_character_;
    std::optional<std::uint32_t> selected_resource_;
    int inventory_category_ = 0;
    int selected_slot_ = 1;
};

} // namespace lc1
