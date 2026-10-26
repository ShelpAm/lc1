#include "lc1/game/session/game-session.hpp"
#include "lc1/error.hpp"
#include "lc1/game/actions.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <imgui.h>
#include <ranges>
#include <spdlog/spdlog.h>

namespace lc1 {
namespace {
std::string slot_name(std::filesystem::path const &directory)
{
    auto const utf8 = directory.filename().u8string();
    return {utf8.begin(), utf8.end()};
}
} // namespace

GameSession::GameSession(Device const &device, std::filesystem::path const &asset_root,
                         Save const &save, OutputSettings output, Window *window)
    : window_(window), input_router_(default_game_bindings(), GameInputContext::Gameplay),
      world_(save), world_presentation_(device, asset_root, world_, output),
      player_view_(world_.continent(), world_.player().position()),
      music_(world_presentation_.audio_context()),
      save_directory_(std::filesystem::weakly_canonical(save.directory())),
      saves_root_(save_directory_.parent_path())
{
    if (!window_)
        fail("game session requires a window");

    refresh_saves();
    window_->set_cursor_captured(should_captures_cursor());
    update_title();

    spdlog::info("[save] {}: {} factions, {} parties, {} characters, {} equipment, {} weapons",
                 save.directory().string(), save.factions().size(), save.parties().size(),
                 save.characters().size(), save.equipments().size(), save.weapons().size());
    spdlog::info("[world] {} regions, {} locations, {} roads; fixed oblique view",
                 world_.continent().regions().size(), world_.continent().locations().size(),
                 world_.continent().roads().size());
}

void GameSession::update(InputState const &input_state, float delta_seconds)
{
    if (!std::isfinite(delta_seconds) || delta_seconds < 0.0F)
        fail("invalid session update time");

    input_router_.update(input_state);

    float const elapsed = std::min(delta_seconds, 0.1F);
    auto const &ui = ImGui::GetIO();
    if (window_->focused() && !ui.WantCaptureKeyboard &&
        input_router_.pressed(GameAction::QuitGame))
        quit_requested_ = true;

    bool const controls_enabled = ui_.gameplay_enabled() && window_->focused() &&
                                  input_router_.active_context() == GameInputContext::Gameplay;
    bool const keyboard_enabled = controls_enabled && !ui.WantCaptureKeyboard;
    bool const mouse_enabled = controls_enabled && !ui.WantCaptureMouse;

    player_view_.update_view_mode(input_router_, keyboard_enabled, world_.player());
    bool const keyboard_save_requested =
        keyboard_enabled && input_router_.pressed(GameAction::SaveGame);

    auto const response_index = [&]() -> int {
        if (!keyboard_enabled)
            return -1;
        constexpr std::array actions{GameAction::DialogResponse0, GameAction::DialogResponse1,
                                     GameAction::DialogResponse2, GameAction::DialogResponse3};
        for (auto const [index, action] : std::views::zip(std::views::iota(0), actions)) {
            if (input_router_.pressed(action))
                return index;
        }
        return -1;
    };
    // The controller supplies intent; GameWorld applies it through Character and then
    // handles the world-level dialog interaction. Whether an idle player accepts ambient
    // requests is a Character rule, not an input rule.
    world_.update(elapsed, WorldInteraction{
                               .player_intent = player_view_.resolve_input(
                                   world_.player(), input_router_, input_state.cursor_delta(),
                                   keyboard_enabled, mouse_enabled),
                               .player_input_enabled = keyboard_enabled,
                               .response_index = response_index(),
                           });
    if (mouse_enabled)
        player_view_.update_pointer(input_state, window_->content_size(),
                                    window_->hovered() && !window_->cursor_captured(), elapsed);
    world_presentation_.update(
        {
            .nod = keyboard_enabled && input_router_.pressed(GameAction::Nod),
            .cycle_lighting = keyboard_enabled && input_router_.pressed(GameAction::CycleLighting),
            .toggle_showcase_motion =
                keyboard_enabled && input_router_.pressed(GameAction::ToggleShowcaseMotion),
            .exposure_steps =
                keyboard_enabled
                    ? static_cast<int>(input_router_.pressed(GameAction::IncreaseExposure)) -
                          static_cast<int>(input_router_.pressed(GameAction::DecreaseExposure))
                    : 0,
        },
        elapsed);
    std::optional<glm::mat4> eye_transform;
    if (!player_view_.oblique())
        eye_transform = world_presentation_.player_eye_transform();
    player_view_.update_camera(world_.player(), elapsed, eye_transform);
    world_presentation_.update_listener(player_view_.camera());

    auto const *target = world_.interaction_target();
    if (keyboard_save_requested) {
        refresh_saves();
        ui_.open_save(slot_name(save_directory_));
    }
    if (keyboard_enabled && input_router_.pressed(GameAction::LoadGame)) {
        refresh_saves();
        ui_.open_load();
    }
    auto const ui_actions = ui_.update(
        {
            .bag = world_.player().bag(),
            .saves = saves_,
            .current_save = slot_name(save_directory_),
            .extent = window_->framebuffer_extent(),
            .lighting_name = world_presentation_.lighting_name(),
            .exposure = world_presentation_.output().exposure,
            .showcase_animated = world_presentation_.showcase_animated(),
            .interaction_target =
                target ? std::string_view{target->config().name} : std::string_view{},
        },
        music_);
    if (ui_actions.start)
        ui_.start_game();
    quit_requested_ |= ui_actions.quit;
    if (ui_actions.volume)
        world_presentation_.set_volume(*ui_actions.volume);

    window_->set_cursor_captured(should_captures_cursor());

    if (ui_actions.refresh_saves)
        refresh_saves();
    if (ui_actions.save)
        save(*ui_actions.save);
    if (ui_actions.load) {
        try {
            load_request_ = Save::slot_directory(saves_root_, *ui_actions.load);
        }
        catch (Error const &error) {
            spdlog::error("[load] {}", error.diagnostic());
            ui_.set_save_status(error.what());
        }
    }

    update_title();
}

void GameSession::update_title()
{
    auto const location = world_presentation_.location();
    auto const title = std::format(
        "lc1 | {} | {} | Lighting: {} | Exposure: {:.2f}", location.region, location.place,
        world_presentation_.lighting_name(), world_presentation_.output().exposure);
    if (window_->set_title(title))
        spdlog::info("[world] {}", title);
}

void GameSession::refresh_saves()
{
    try {
        saves_ = Save::slots(saves_root_);
    }
    catch (std::exception const &error) {
        spdlog::error("[save] cannot list saves: {}", error.what());
        ui_.set_save_status("Cannot list saves. Please try Refresh.");
    }
}

void GameSession::save(std::string_view name)
{
    try {
        auto directory = Save::slot_directory(saves_root_, name);
        Save::write(directory, world_.snapshot());
        save_directory_ = std::move(directory);
        ui_.set_save_status(std::format("Saved: {}", name));
        ui_.saved();
        refresh_saves();
    }
    catch (Error const &error) {
        spdlog::error("[save] {}", error.diagnostic());
        ui_.set_save_status(std::format("Save failed: {}", error.what()));
    }
    catch (std::exception const &error) {
        spdlog::error("[save] {}", error.what());
        ui_.set_save_status(std::format("Save failed: {}", error.what()));
    }
}

} // namespace lc1

namespace lc1 {

void GameSession::set_aspect_ratio(float aspect_ratio)
{
    player_view_.set_aspect_ratio(aspect_ratio);
}

std::optional<std::filesystem::path> GameSession::take_load_request()
{
    return std::exchange(load_request_, std::nullopt);
}

void GameSession::resume_loaded()
{
    ui_.start_game();
}

void GameSession::load_failed(std::string message)
{
    ui_.set_save_status(std::move(message));
}

bool GameSession::quit_requested() const
{
    return quit_requested_;
}

GameWorld const &GameSession::world() const
{
    return world_;
}

WorldPresentation const &GameSession::world_presentation() const
{
    return world_presentation_;
}

PlayerView const &GameSession::player_view() const
{
    return player_view_;
}

GameUi const &GameSession::ui() const
{
    return ui_;
}

bool GameSession::should_captures_cursor() const
{
    return ui_.gameplay_enabled() && !player_view_.oblique();
}

} // namespace lc1
