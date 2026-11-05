#pragma once

#include <glm/glm.hpp>

#include <optional>

namespace lc1 {

// One frame of "what does this character want to do". Controllers produce an intent;
// Character resolves it against its own pose, state and gameplay rules. An intent never
// contains velocity, movement speed or an already-applied state change.
//
// This is deliberately a value type with no dependencies beyond GLM: player and AI
// controllers can both fill it in, and Character can apply it without knowing which one
// produced it.
struct CharacterIntent {
    // Desired horizontal direction in world space (x, z). Length <= 1 preserves analog
    // input strength; the character decides the actual speed and collision result.
    glm::vec2 move{};

    // Absolute world yaw in degrees about +Y. std::nullopt keeps the current body yaw.
    std::optional<float> body_yaw;

    // Desired horizontal body direction in world space. Applied after body_yaw when both
    // are present. This expresses "face this target" without forcing a controller to
    // duplicate the yaw convention; a zero-length direction keeps the current yaw.
    std::optional<glm::vec3> body_direction;

    // Absolute look target relative to the body: x is yaw, y is pitch in degrees.
    // std::nullopt keeps the current look target. First person supplies an absolute
    // target every frame; third person only resets it when the view changes.
    std::optional<glm::vec2> look;

    // Turn the body toward move. First person keeps the body aligned with the view and
    // therefore leaves this false; third person and AI turn toward movement.
    bool face_movement = false;

    // Requests, not commands: the character drops an impossible jump or applies the
    // movement rules that fit its current state.
    bool jump = false;
    bool sprint = false;
    bool swing_weapon = false;
    bool nod = false;
};

} // namespace lc1
