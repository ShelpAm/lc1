#pragma once

#include "lc1/game/actions.hpp"
#include "lc1/game/character/character-controller.hpp"

#include <glm/glm.hpp>

#include <optional>

namespace lc1 {

class Character;

// One local player's frame input, prepared before character updates.
struct PlayerCharacterInput {
    glm::vec2 look_delta{};
    bool keyboard_enabled = false;
    bool mouse_enabled = false;
    bool oblique = true;
    bool view_changed = false;
    std::optional<glm::vec2> restored_first_person_angles;
    glm::vec3 view_heading{0, 0, -1};
};

// Device-independent input copied into the simulation at a tick boundary.
struct PlayerControls {
    PlayerCharacterInput view;
    glm::vec2 movement{};
    bool sprint = false;
    bool jump = false;
    bool swing_weapon = false;
    bool nod = false;
    bool interact = false;
    int response_index = -1;
};

// Turns player actions into a CharacterIntent. It owns device-to-action mapping, mouse
// look and the choice between camera-relative and body-relative movement. It does not
// know whether the character may move, sprint or jump: Character applies those rules.
// Camera lock, recenter and view switching stay in PlayerView because they change the
// camera rather than the character.
class PlayerCharacterController : public CharacterController {
  public:
    // Required input borrow, owned by the simulation and updated before each tick.
    PlayerCharacterController(PlayerControls const &input);

    static PlayerControls read_input(GameInputRouter const &input,
                                     PlayerCharacterInput const &frame);

    CharacterIntent update(float delta_seconds, Character const &character) override;

    // Pure resolution for the first-person view. `restored_angles` supplies the remembered
    // absolute yaw/pitch after a view switch; std::nullopt derives them from the character.
    static CharacterIntent first_person(Character const &character, glm::vec2 movement,
                                        glm::vec2 look_delta,
                                        std::optional<glm::vec2> restored_angles = {});
    // WASD in the horizontal camera basis; the body turns toward the requested movement.
    static CharacterIntent third_person(glm::vec2 movement, glm::vec3 view_heading);

  private:
    PlayerControls const *input_;
};

} // namespace lc1
