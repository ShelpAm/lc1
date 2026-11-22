#pragma once

#include "lc1/game/camera/first-person-camera-controller.hpp"
#include "lc1/game/camera/oblique-camera-controller.hpp"
#include "lc1/game/camera/third-person-camera-controller.hpp"
#include "lc1/game/character/character-pose.hpp"
#include "lc1/game/player/player-character-controller.hpp"
#include "lc1/scene/camera.hpp"

#include <optional>

namespace lc1 {

class InputState;

struct CameraActions {
    bool toggle_view = false;
    bool toggle_lock = false;
    bool recenter_held = false;
};

// All views share one character. GameSession supplies input gates; GameSession
// applies cursor capture when the view changes. This class owns no window or input router.
class PlayerView {
  public:
    PlayerView(Continent const &continent, glm::vec3 player_position);

    void toggle(CharacterPose const &player);

    // Handles view switching, lock and recenter before prepare_input() each frame.
    void update_view_mode(CameraActions actions, CharacterPose const &player);
    // Prepares view-dependent input before copying it to the simulation.
    // Discards look motion on a view switch before cursor capture changes.
    void prepare_input(glm::vec2 look_delta, bool keyboard_enabled, bool mouse_enabled);
    PlayerCharacterInput const &character_input() const;
    // Call only while mouse input is enabled; pointer_active additionally gates edge pan.
    void update_pointer(float delta_seconds, InputState const &devices, glm::vec2 viewport_size,
                        bool pointer_active);
    // Optional animated eye world transform adds mount rotation to gameplay look in first person.
    void update_camera(float delta_seconds, CharacterPose const &player,
                       std::optional<glm::mat4> eye_transform = {});

    void set_aspect_ratio(float aspect_ratio);

    bool oblique() const;
    bool first_person() const;

    scene::FpsCamera const &camera() const;

  private:
    Continent const *continent_; // Immutable borrow; the game simulation owns the continent.
    ObliqueCameraController oblique_camera_;
    FirstPersonCameraController first_person_camera_;
    ThirdPersonCameraController third_person_camera_;
    scene::FpsCamera camera_;
    ViewMode mode_ = ViewMode::Oblique;
    bool view_changed_ = false;
    PlayerCharacterInput character_input_;
};

} // namespace lc1
