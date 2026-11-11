#pragma once

#include "lc1/game/player/player-character-controller.hpp"
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
    double time_of_day;
    float time_rate;
    int grass_density;
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
    std::optional<float> time_rate;
    std::optional<int> grass_density;
    std::optional<EquipmentRequest> equipment;
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
    void player_info(GameUiInfo const &info, GameUiActions &actions);

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
    int selected_slot_ = -1;
    float slot_position_ = 0;
    float slot_target_ = 0;
    std::vector<std::uint32_t> inventory_order_;
};

} // namespace lc1
