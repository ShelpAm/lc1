#include "lc1/game/player/player-view.hpp"

#include "lc1/game/character/character.hpp"
#include "lc1/game/physics/world-collision.hpp"

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
    : continent_{&continent}, map_camera_{player_position}
{
    camera_.set_zfar(12800.0F);
    map_camera_.apply(camera_);
}

void PlayerView::follow_first_person(glm::vec3 eye_position, glm::vec3 look_direction)
{
    camera_.set_position(eye_position);
    camera_.look_at(eye_position + look_direction);
}

void PlayerView::toggle(CharacterPose const &player)
{
    if (!oblique_)
        first_person_angles_ = {player.body_yaw() + player.look_yaw(), player.look_pitch()};
    if (oblique_)
        oblique_ = false;
    else if (!third_person_) {
        third_person_ = true;
        follow_angles_ = {player.body_yaw() + player.look_yaw(), player.look_pitch()};
    }
    else {
        third_person_ = false;
        oblique_ = true;
    }
    view_changed_ = true;
    if (oblique_) {
        // Recenter while preserving zoom and the Y lock state.
        map_camera_.recenter(player.position());
        map_camera_.update(0.0F, camera_, player.position());
    }
    else {
        update_camera(0.0F, player);
    }
    spdlog::info("[camera] {}", oblique_        ? "oblique"
                                : third_person_ ? "third-person"
                                                : "first-person");
}

void PlayerView::update_view_mode(GameInputRouter const &input, bool keyboard_enabled,
                                  CharacterPose const &player)
{
    if (keyboard_enabled && input.pressed(GameAction::ToggleCameraView))
        toggle(player);

    recenter_ = oblique_ && !player.debug_flying && keyboard_enabled &&
                input.held(GameAction::RecenterCamera);
    if (oblique_ && keyboard_enabled && input.pressed(GameAction::ToggleCameraLock)) {
        map_camera_.toggle_lock();
        spdlog::info("[camera] {}", map_camera_.locked() ? "locked to player" : "free camera");
    }
    if (oblique_)
        map_camera_.apply(camera_);
}

void PlayerView::prepare_input(glm::vec2 look_delta, bool keyboard_enabled, bool mouse_enabled)
{
    if (third_person_ && mouse_enabled && !view_changed_) {
        follow_angles_.x -= look_delta.x * 0.05F;
        follow_angles_.y = std::clamp(follow_angles_.y - look_delta.y * 0.05F, -60.0F, 60.0F);
    }
    character_input_ = {
        .look_delta = view_changed_ ? glm::vec2{} : look_delta,
        .keyboard_enabled = keyboard_enabled,
        .mouse_enabled = mouse_enabled,
        .oblique = oblique_,
        .view_changed = view_changed_,
        .restored_first_person_angles = first_person_angles_,
        .view_heading = (oblique_ || third_person_) && (keyboard_enabled || mouse_enabled)
                            ? camera_.heading()
                            : glm::vec3{},
        .third_person_follow = third_person_,
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
    if (!oblique_)
        return;
    map_camera_.zoom(devices.scroll().y);
    if (pointer_active && !recenter_)
        map_camera_.pan(delta_seconds, edge_motion(devices.cursor(), viewport_size),
                        continent_->bounds());
}

void PlayerView::update_camera(float delta_seconds, CharacterPose const &player,
                               std::optional<glm::mat4> eye_transform)
{
    if (third_person_) {
        auto const yaw = glm::radians(follow_angles_.x);
        glm::vec3 const heading{-std::sin(yaw), 0.0F, -std::cos(yaw)};
        auto const target = player.position() + glm::vec3{0, player.body.height * 0.5F, 0};
        auto const pitch = glm::radians(std::clamp(25.0F - follow_angles_.y, -35.0F, 85.0F));
        auto const offset =
            -heading * (10.F * std::cos(pitch)) + glm::vec3{0, 10.F * std::sin(pitch), 0};
        auto const fraction =
            player.debug_flying
                ? 1.0F
                : WorldCollision{*continent_}.camera_fraction(target, target + offset, 0.25F);
        camera_.set_position(target + offset * fraction);
        camera_.look_at(target);
        return;
    }
    if (!oblique_) {
        auto direction = player.look_direction();
        auto position = player.eye_position();
        if (eye_transform) {
            position = glm::vec3{(*eye_transform)[3]};
            // Imported bone up/right axes can be flipped or stretched. Derive only
            // the mount's forward orientation; mouse angles use world-up semantics.
            auto const forward = glm::normalize(-glm::vec3{(*eye_transform)[2]});
            auto const yaw = std::atan2(-forward.x, -forward.z) + glm::radians(player.look_yaw());
            auto const pitch = glm::radians(std::clamp(
                glm::degrees(std::asin(std::clamp(forward.y, -1.0F, 1.0F))) + player.look_pitch(),
                -89.0F, 89.0F));
            direction = glm::vec3{-std::sin(yaw) * std::cos(pitch), std::sin(pitch),
                                  -std::cos(yaw) * std::cos(pitch)};
        }
        follow_first_person(position, direction);
        return;
    }
    if (recenter_)
        map_camera_.recenter(player.position());
    map_camera_.update(delta_seconds, camera_, player.position());
}

} // namespace lc1

namespace lc1 {

void PlayerView::set_aspect_ratio(float aspect_ratio)
{
    camera_.set_aspect_ratio(aspect_ratio);
}

bool PlayerView::oblique() const
{
    return oblique_;
}

bool PlayerView::first_person() const
{
    return !oblique_ && !third_person_;
}

scene::FpsCamera const &PlayerView::camera() const
{
    return camera_;
}

} // namespace lc1
