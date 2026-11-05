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
                         GameSimulation &simulation, std::filesystem::path const &save_directory,
                         OutputSettings output, Window *window, PlayerId player)
    : window_(window), input_router_(default_game_bindings(), GameInputContext::Gameplay),
      simulation_(&simulation), connection_(simulation.connect(player)),
      snapshot_(*simulation_->snapshot()),
      world_presentation_(device, asset_root, simulation_->continent(), snapshot_, output, player),
      player_view_(simulation_->continent(),
                   snapshot_.player(connection_.player_id()).pose.position()),
      music_(world_presentation_.audio_context()),
      save_directory_(std::filesystem::weakly_canonical(save_directory)),
      saves_root_(save_directory_.parent_path())
{
    if (!window_)
        fail("game session requires a window");

    refresh_saves();
    window_->set_cursor_captured(should_captures_cursor());
    update_title();

    spdlog::info("[world] {} regions, {} locations, {} roads; fixed oblique view",
                 simulation_->continent().regions().size(),
                 simulation_->continent().locations().size(),
                 simulation_->continent().roads().size());
}

void GameSession::update(float delta_seconds, InputState const &input_state)
{
    if (!std::isfinite(delta_seconds) || delta_seconds < 0)
        fail("invalid session frame time");
    snapshot_ = simulation_->presentation_snapshot();
    input_router_.update(input_state);

    auto &imgui_io = ImGui::GetIO();
    if (window_->focused() && !imgui_io.WantCaptureKeyboard)
        quit_requested_ = input_router_.pressed(GameAction::QuitGame);

    bool const controls_enabled = ui_.gameplay_enabled() && window_->focused() &&
                                  input_router_.active_context() == GameInputContext::Gameplay;
    bool const keyboard_enabled = controls_enabled && !imgui_io.WantCaptureKeyboard;
    bool const mouse_enabled = controls_enabled && !imgui_io.WantCaptureMouse;

    player_view_.update_view_mode(input_router_, keyboard_enabled,
                                  snapshot_.player(connection_.player_id()).pose);
    bool const keyboard_save_requested =
        keyboard_enabled && input_router_.pressed(GameAction::SaveGame);

    player_view_.prepare_input(input_state.cursor_delta(), keyboard_enabled, mouse_enabled);
    connection_.submit(
        PlayerCharacterController::read_input(input_router_, player_view_.character_input()));
    if (mouse_enabled)
        player_view_.update_pointer(delta_seconds, input_state, window_->window_size(),
                                    window_->hovered() && !window_->cursor_captured());
    world_presentation_.update(
        delta_seconds,
        {
            .cycle_lighting = keyboard_enabled && input_router_.pressed(GameAction::CycleLighting),
            .toggle_showcase_motion =
                keyboard_enabled && input_router_.pressed(GameAction::ToggleShowcaseMotion),
            .exposure_steps =
                keyboard_enabled
                    ? static_cast<int>(input_router_.pressed(GameAction::IncreaseExposure)) -
                          static_cast<int>(input_router_.pressed(GameAction::DecreaseExposure))
                    : 0,
        });
    std::optional<glm::mat4> eye_transform;
    if (!player_view_.oblique())
        eye_transform = world_presentation_.observer_eye_transform();
    player_view_.update_camera(delta_seconds, snapshot_.player(connection_.player_id()).pose,
                               eye_transform);
    world_presentation_.update_listener(player_view_.camera());

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
            .bag = snapshot_.players.at(connection_.player_id()).bag,
            .saves = saves_,
            .current_save = slot_name(save_directory_),
            .extent = window_->framebuffer_extent(),
            .lighting_name = world_presentation_.lighting_name(),
            .exposure = world_presentation_.output().exposure,
            .showcase_animated = world_presentation_.showcase_animated(),
            .interaction_target = snapshot_.players.at(connection_.player_id()).interaction_target,
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
        request_save(*ui_actions.save);
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

void GameSession::request_save(std::string_view name)
{
    try {
        save_request_ = Save::slot_directory(saves_root_, name);
    }
    catch (Error const &error) {
        spdlog::error("[save] {}", error.diagnostic());
        save_failed(std::format("Save failed: {}", error.what()));
    }
}

void GameSession::save_completed(std::filesystem::path directory)
{
    save_directory_ = std::move(directory);
    ui_.set_save_status(std::format("Saved: {}", slot_name(save_directory_)));
    ui_.saved();
    refresh_saves();
}

void GameSession::save_failed(std::string message)
{
    ui_.set_save_status(std::move(message));
}

std::optional<std::filesystem::path> GameSession::take_save_request()
{
    return std::exchange(save_request_, std::nullopt);
}

} // namespace lc1

namespace lc1 {

void GameSession::release_controls()
{
    connection_.submit({});
}

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

WorldSnapshot const &GameSession::world() const
{
    return snapshot_;
}

WorldPresentation const &GameSession::world_presentation() const
{
    return world_presentation_;
}

PlayerId GameSession::player_id() const
{
    return connection_.player_id();
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
