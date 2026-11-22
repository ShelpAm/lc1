#pragma once

#include "lc1/game/player/player-character-controller.hpp"
#include "lc1/game/world/world-snapshot.hpp"
#include "lc1/platform/window.hpp"
#include "lc1/vk/render/text-pass.hpp"
#include "lc1/vk/render/volumetric-clouds.hpp"
#include "lc1/vk/resources/gpu-font.hpp"
#include <array>
#include <filesystem>
#include <optional>
#include <span>

namespace lc1 {
class MusicBrowser;
class PlayerView;
class WorldPresentation;
struct GameLocation;

struct GameUiContext {
    WorldSnapshot const &world;
    PlayerId player_id;
    WorldPresentation const &presentation;
    PlayerView const &player_view;
    Window const &window;
    MusicBrowser &music;
    std::span<std::string const> saves;
    std::string_view current_save;
    // Live renderer configuration; null when the caller does not render clouds.
    CloudSettings *clouds = nullptr;
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
    std::optional<CharacterId> control_target;
};

class GameUi {
  public:
    explicit GameUi(std::filesystem::path settings_file);

    GameUiActions update(GameUiContext const &context);

    bool gameplay_enabled() const;

    void open_save(std::string_view name);

    void open_load();

    void saved();

    void start_game();

    void toggle_player_info();

    // Esc: opens the debug panel and menu over the running game, and closes them again.
    // While they are open gameplay input stops and the cursor is released in every view
    // mode, because gameplay_enabled() is false.
    void toggle_menu();

    void set_save_status(std::string status);

    std::vector<ScreenTextItem> screen_labels(WorldSnapshot const &world, PlayerId observer,
                                              GpuFont const &font, FrameExtent extent,
                                              GameLocation location) const;

  private:
    void player_info(GameUiContext const &info, GameUiActions &actions);
    void persist_ui_scale() const;

    enum class State { MainMenu, InSettings, InGame };
    std::filesystem::path settings_file_;
    State state_ = State::MainMenu;
    float ui_scale_ = 1.3F;
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
