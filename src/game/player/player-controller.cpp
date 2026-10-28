#include "lc1/game/player/player-controller.hpp"

#include "lc1/error.hpp"
#include "lc1/game/character/character.hpp"

#include <algorithm>
#include <cmath>

namespace lc1 {
namespace {
bool finite(glm::vec2 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y);
}

bool finite(glm::vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

} // namespace

CharacterIntent PlayerController::first_person(Character const &character, glm::vec2 movement,
                                               glm::vec2 look_delta,
                                               std::optional<glm::vec2> restored_angles)
{
    if (!finite(movement) || !finite(look_delta))
        fail("invalid player input");
    auto const angles = restored_angles.value_or(
        glm::vec2{character.body_yaw() + character.look_yaw(), character.look_pitch()});
    if (restored_angles && !finite(*restored_angles))
        fail("invalid restored look angles");
    constexpr float look_sensitivity = 0.05F;
    float const yaw = angles.x - look_delta.x * look_sensitivity;
    float const pitch = std::clamp(angles.y - look_delta.y * look_sensitivity, -89.0F, 89.0F);
    float const radians = glm::radians(yaw);
    glm::vec3 const heading{-std::sin(radians), 0, -std::cos(radians)};
    auto const right = glm::cross(heading, glm::vec3{0, 1, 0});
    auto const direction = right * movement.x + heading * movement.y;
    return {.move = {direction.x, direction.z},
            .body_yaw = yaw,
            .look = glm::vec2{0, pitch},
            .interaction_direction = heading,
            .face_movement = false};
}

CharacterIntent PlayerController::third_person(glm::vec2 movement, glm::vec3 view_heading)
{
    if (!finite(movement))
        fail("invalid player movement");
    if (!finite(view_heading))
        fail("invalid third-person movement basis");
    double const length =
        std::hypot(static_cast<double>(view_heading.x), static_cast<double>(view_heading.z));
    if (length == 0)
        fail("third-person movement basis must have a horizontal direction");
    view_heading = {static_cast<float>(view_heading.x / length), 0,
                    static_cast<float>(view_heading.z / length)};
    auto const right = glm::cross(view_heading, glm::vec3{0, 1, 0});
    auto const direction = right * movement.x + view_heading * movement.y;
    CharacterIntent intent{.move = {direction.x, direction.z}, .face_movement = true};
    if (direction != glm::vec3{})
        intent.look = glm::vec2{};
    return intent;
}

CharacterIntent PlayerController::update(Character const &character, GameInputRouter const &input,
                                         glm::vec2 look_delta, bool keyboard_enabled,
                                         bool mouse_enabled, bool oblique, bool view_changed,
                                         std::optional<glm::vec2> restored_first_person_angles,
                                         glm::vec3 view_heading)
{
    auto const movement =
        keyboard_enabled ? glm::vec2{static_cast<float>(input.held(GameAction::MoveRight)) -
                                         static_cast<float>(input.held(GameAction::MoveLeft)),
                                     static_cast<float>(input.held(GameAction::MoveForward)) -
                                         static_cast<float>(input.held(GameAction::MoveBackward))}
                         : glm::vec2{};
    bool const enabled = keyboard_enabled || mouse_enabled;
    CharacterIntent intent;
    if (!oblique) {
        if (enabled || view_changed)
            intent = first_person(character, movement, mouse_enabled ? look_delta : glm::vec2{},
                                  view_changed ? restored_first_person_angles : std::nullopt);
    }
    else {
        if (enabled)
            intent = third_person(movement, view_heading);
        if (view_changed)
            intent.look = glm::vec2{};
    }
    intent.sprint = keyboard_enabled && input.held(GameAction::Sprint);
    intent.jump = keyboard_enabled && input.pressed(GameAction::Jump);
    intent.interact = keyboard_enabled && input.pressed(GameAction::Interact);
    return intent;
}

} // namespace lc1
