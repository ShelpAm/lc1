#include "lc1/game/player/player-view.hpp"

#include "lc1/platform/input.hpp"

#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace lc1 {

namespace {
glm::vec2 edge_motion(glm::vec2 cursor, glm::vec2 size)
{
    if (size.x <= 0.0F || size.y <= 0.0F || !std::isfinite(cursor.x) || !std::isfinite(cursor.y) ||
        cursor.x < 0.0F || cursor.y < 0.0F || cursor.x >= size.x || cursor.y >= size.y)
        return glm::vec2{0.0F};

    // A logical-point edge band keeps the feel consistent on HiDPI displays.
    float const band = std::min({24.0F, size.x * 0.5F, size.y * 0.5F});
    auto strength = [band](float distance) {
        return std::clamp((band - distance) / band, 0.0F, 1.0F);
    };
    glm::vec2 direction{strength(size.x - cursor.x) - strength(cursor.x),
                        strength(cursor.y) - strength(size.y - cursor.y)};
    // Corners must not move faster than the maximum speed along a single edge.
    return direction / std::max(glm::length(direction), 1.0F);
}

} // namespace

PlayerView::PlayerView(Continent const &continent, glm::vec3 player_position)
    : continent_{&continent}, oblique_camera_{player_position}
{
    camera_.set_zfar(12800.0F);
    oblique_camera_.apply(camera_);
}

void PlayerView::toggle(CharacterPose const &player)
{
    if (!oblique())
        first_person_camera_.remember_angles(player);
    view_changed_ = true;
    switch (mode_) {
    case ViewMode::Oblique:
        mode_ = ViewMode::FirstPerson;
        update_camera(0.0F, player);
        spdlog::info("[camera] first-person");
        break;
    case ViewMode::FirstPerson:
        mode_ = ViewMode::ThirdPerson;
        third_person_camera_.set_angles(first_person_camera_.remembered_angles());
        update_camera(0.0F, player);
        spdlog::info("[camera] third-person");
        break;
    case ViewMode::ThirdPerson:
        mode_ = ViewMode::Oblique;
        // Recenter while preserving zoom and the Y lock state.
        oblique_camera_.recenter(player.position());
        oblique_camera_.update(0.0F, camera_, player.position());
        spdlog::info("[camera] oblique");
        break;
    }
}

void PlayerView::update_view_mode(CameraActions actions, CharacterPose const &player)
{
    if (actions.toggle_view)
        toggle(player);

    oblique_camera_.set_recenter_held(oblique() && actions.recenter_held);
    if (oblique() && actions.toggle_lock) {
        oblique_camera_.toggle_lock();
        spdlog::info("[camera] {}", oblique_camera_.locked() ? "locked to player" : "free camera");
    }
    if (oblique())
        oblique_camera_.apply(camera_);
}

void PlayerView::prepare_input(glm::vec2 look_delta, bool keyboard_enabled, bool mouse_enabled)
{
    if (mode_ == ViewMode::ThirdPerson && mouse_enabled && !view_changed_)
        third_person_camera_.orbit(look_delta);
    character_input_ = {
        .look_delta = view_changed_ ? glm::vec2{} : look_delta,
        .keyboard_enabled = keyboard_enabled,
        .mouse_enabled = mouse_enabled,
        .mode = mode_,
        .view_changed = view_changed_,
        .restored_first_person_angles = first_person_camera_.remembered_angles(),
        .view_heading = !first_person() && (keyboard_enabled || mouse_enabled)
                            ? (oblique() ? camera_.heading() : third_person_camera_.heading())
                            : glm::vec3{},
    };
    view_changed_ = false;
}

PlayerCharacterInput const &PlayerView::character_input() const
{
    return character_input_;
}

void PlayerView::update_pointer(float delta_seconds, InputState const &devices,
                                glm::vec2 viewport_size, bool pointer_active)
{
    if (!oblique())
        return;
    oblique_camera_.zoom(devices.scroll().y);
    if (pointer_active)
        oblique_camera_.pan(delta_seconds, edge_motion(devices.cursor(), viewport_size),
                            continent_->bounds());
}

void PlayerView::update_camera(float delta_seconds, CharacterPose const &player,
                               std::optional<glm::mat4> eye_transform)
{
    switch (mode_) {
    case ViewMode::Oblique:
        oblique_camera_.update(delta_seconds, camera_, player.position());
        break;
    case ViewMode::FirstPerson:
        first_person_camera_.update(camera_, player, eye_transform);
        break;
    case ViewMode::ThirdPerson:
        third_person_camera_.update(camera_, player.eye_position());
        break;
    }
}

} // namespace lc1

namespace lc1 {

void PlayerView::set_aspect_ratio(float aspect_ratio)
{
    camera_.set_aspect_ratio(aspect_ratio);
}

bool PlayerView::oblique() const
{
    return mode_ == ViewMode::Oblique;
}

bool PlayerView::first_person() const
{
    return mode_ == ViewMode::FirstPerson;
}

scene::FpsCamera const &PlayerView::camera() const
{
    return camera_;
}

} // namespace lc1
