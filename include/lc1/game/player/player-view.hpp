#pragma once

#include "lc1/game/actions.hpp"
#include "lc1/game/camera/map-camera-controller.hpp"
#include "lc1/game/player/player-controller.hpp"
#include "lc1/scene/camera.hpp"

#include <optional>

namespace lc1 {

// Both views share one character. GameSession supplies input gates; GameSession
// applies cursor capture when the view changes. This class owns no window or input router.
class PlayerView {
  public:
    PlayerView(Continent const &continent, glm::vec3 player_position);
    void toggle(Character const &player);
    // Handles view switching, lock and recenter before resolve_input() each frame.
    void update_view_mode(GameInputRouter const &input, bool keyboard_enabled,
                          Character const &player);
    // Resolves the action-to-intent mapping without mutating the character; GameWorld applies it.
    // Discards look motion on a view switch, before GameSession changes cursor capture.
    CharacterIntent resolve_input(Character const &player, GameInputRouter const &input,
                                  glm::vec2 look_delta, bool keyboard_enabled, bool mouse_enabled);
    // Call only while mouse input is enabled; pointer_active additionally gates edge pan.
    void update_pointer(float delta_seconds, InputState const &devices, glm::vec2 viewport_size,
                        bool pointer_active);
    // Optional animated eye world transform adds mount rotation to gameplay look in first person.
    void update_camera(float delta_seconds, Character const &player,
                       std::optional<glm::mat4> eye_transform = {});

    void set_aspect_ratio(float aspect_ratio);

    bool oblique() const;

    scene::FpsCamera const &camera() const;

  private:
    void follow_first_person(glm::vec3 eye_position, glm::vec3 look_direction);

    Continent const *continent_; // Borrowed; the game session owns the world.
    MapCameraController map_camera_;
    scene::FpsCamera camera_;
    bool oblique_ = true;
    glm::vec2 first_person_angles_{};
    bool view_changed_ = false;
    bool recenter_ = false;
};

} // namespace lc1
