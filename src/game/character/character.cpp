#include "lc1/game/character/character.hpp"

#include "lc1/error.hpp"
#include "lc1/game/continent/continent.hpp"
#include "lc1/game/party/faction.hpp"
#include "lc1/game/party/party.hpp"
#include "lc1/game/physics/world-collision.hpp"
#include "lc1/game/world/interactable.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

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

glm::vec3 horizontal_direction(float degrees)
{
    float const angle = glm::radians(degrees);
    return {-std::sin(angle), 0.0F, -std::cos(angle)};
}

CharacterBody validated_body(CharacterBody body)
{
    if (!std::isfinite(body.half_width) || body.half_width <= 0.0F || !std::isfinite(body.height) ||
        body.height <= 0.0F || !std::isfinite(body.eye_height) || body.eye_height < 0.0F ||
        body.eye_height > body.height || !std::isfinite(body.walk_speed) ||
        body.walk_speed < 0.0F || !std::isfinite(body.run_speed) || body.run_speed < 0.0F ||
        !std::isfinite(body.step_height) || body.step_height < 0.0F ||
        !std::isfinite(body.ground_snap_distance) || body.ground_snap_distance < 0.0F)
        fail("invalid character body");

    return body;
}

scene::Transform validated_transform(Continent const &continent, scene::Transform transform,
                                     CharacterBody const &body)
{
    auto const &pose = transform;
    if (pose.rotation.x != 0.0F || pose.rotation.z != 0.0F || pose.scale != glm::vec3{1.0F})
        fail("ground character needs an upright, unscaled world transform");
    if (!WorldCollision{continent}.can_stand(transform.position, body.half_width, body.height))
        fail("character spawn is obstructed or outside the continent");
    return transform;
}

} // namespace

Character::Character(Continent const &continent, scene::Transform transform, CharacterConfig config,
                     CharacterBody body, CharacterMotionSettings motion_settings)
    : config_(std::move(config)), body_(validated_body(body)), bag_{10000},
      transform_(validated_transform(continent, transform, body_)),
      motion_(WorldCollision{continent}, body_, transform_.position, motion_settings)
{
    set_body_yaw(transform_.rotation.y);
}

Character::~Character() = default;

void Character::set_interactable(std::unique_ptr<Interactable> interactable)
{
    interactable_ = std::move(interactable);
}

Faction const *Character::faction() const
{
    return config_.party->faction();
}

void Character::apply_intent(CharacterIntent const &intent)
{
    if (!finite(intent.move) || (intent.body_yaw && !std::isfinite(*intent.body_yaw)) ||
        (intent.body_direction && !finite(*intent.body_direction)) ||
        (intent.look && !finite(*intent.look)) ||
        (intent.interaction_direction && !finite(*intent.interaction_direction)))
        fail("invalid character intent");

    // Pose changes come first so an intent that supplies both orientation and movement
    // applies them in a fixed order: body yaw, body direction, then look target.
    if (intent.body_yaw)
        set_body_yaw(*intent.body_yaw);
    if (intent.body_direction)
        set_body_direction(*intent.body_direction);
    if (intent.look)
        set_look_angles(intent.look->x, intent.look->y);

    // The jump stays a request; update() grants it only while grounded, so an airborne
    // intent cannot double jump.
    if (intent.jump)
        request_jump();

    // interact/interaction_direction need a world target, so GameWorld resolves them after
    // this call. They are not pose changes and do not belong in Character.
}

CompanionOrder Character::companion_order() const
{
    return controller_ ? controller_->companion_order().value_or(CompanionOrder::None)
                       : CompanionOrder::None;
}

bool Character::accepts_ambient_requests(CharacterIntent const &intent) const
{
    return on_ground() && !intent.jump && !intent.sprint && intent.move == glm::vec2{};
}

void Character::update(float delta_seconds, Continent const &continent,
                       CharacterIntent const &intent)
{
    if (!std::isfinite(delta_seconds) || delta_seconds < 0.0F)
        fail("invalid character update time");
    apply_intent(intent);
    auto const velocity =
        walking_velocity({intent.move.x, 0.0F, intent.move.y}, intent.sprint, intent.face_movement);
    motion_.update(delta_seconds, WorldCollision{continent}, body_, transform_.position, velocity);
}

glm::vec3 Character::walking_velocity(glm::vec3 direction, bool running, bool face_movement)
{
    if (!finite(direction))
        fail("invalid character walking input");
    direction.y = 0.0F;
    // double avoids overflow when normalizing large but finite input vectors.
    double const length =
        std::hypot(static_cast<double>(direction.x), static_cast<double>(direction.z));
    if (length == 0.0)
        return {};
    direction *= static_cast<float>(1.0 / std::max(length, 1.0));

    if (face_movement)
        set_body_yaw(glm::degrees(std::atan2(-direction.x, -direction.z)));

    float const speed = running ? body_.run_speed : body_.walk_speed;
    return direction * speed;
}

void Character::walk(float delta_seconds, Continent const &continent, glm::vec3 direction,
                     bool running, bool face_movement)
{
    if (!std::isfinite(delta_seconds) || delta_seconds < 0.0F)
        fail("invalid character walking time");
    move(continent, walking_velocity(direction, running, face_movement) * delta_seconds);
}

void Character::move(Continent const &continent, glm::vec3 displacement)
{
    motion_.move(WorldCollision{continent}, body_, transform_.position, displacement);
}

void Character::set_body_yaw(float degrees)
{
    if (!std::isfinite(degrees))
        fail("character body yaw must be finite");
    transform_.rotation.y = std::remainder(degrees, 360.0F);
}

void Character::set_body_direction(glm::vec3 direction)
{
    if (!finite(direction))
        fail("character body direction must be finite");
    if (direction.x == 0.0F && direction.z == 0.0F)
        return;
    set_body_yaw(glm::degrees(std::atan2(-direction.x, -direction.z)));
}

glm::vec3 Character::body_heading() const
{
    return horizontal_direction(body_yaw());
}

void Character::set_look_angles(float yaw_degrees, float pitch_degrees)
{
    if (!std::isfinite(yaw_degrees) || !std::isfinite(pitch_degrees))
        fail("character look angles must be finite");
    look_yaw_ = std::clamp(yaw_degrees, -85.0F, 85.0F);
    look_pitch_ = std::clamp(pitch_degrees, -89.0F, 89.0F);
}

glm::vec3 Character::look_direction() const
{
    float const pitch = glm::radians(look_pitch_);
    return heading() * std::cos(pitch) + glm::vec3{0.0F, std::sin(pitch), 0.0F};
}

glm::vec3 Character::heading() const
{
    return horizontal_direction(body_yaw() + look_yaw_);
}

glm::vec3 Character::right() const
{
    return glm::cross(heading(), glm::vec3{0.0F, 1.0F, 0.0F});
}

glm::vec3 Character::eye_position() const
{
    // Character-space metres; independent of the source model's origin and units.
    return position() + glm::vec3{0.0F, body_.eye_height, 0.0F};
}

CharacterConfig const &Character::config() const
{
    return config_;
}

CharacterBody const &Character::body() const
{
    return body_;
}

float Character::half_width() const
{
    return body_.half_width;
}

float Character::height() const
{
    return body_.height;
}

glm::vec3 Character::position() const
{
    return transform_.position;
}

scene::Transform const &Character::transform() const
{
    return transform_;
}

float Character::body_yaw() const
{
    return transform_.rotation.y;
}

float Character::look_yaw() const
{
    return look_yaw_;
}

float Character::look_pitch() const
{
    return look_pitch_;
}

void Character::request_jump()
{
    motion_.request_jump();
}

bool Character::on_ground() const
{
    return motion_.on_ground();
}

CharacterController *Character::controller()
{
    return controller_.get();
}

CharacterController const *Character::controller() const
{
    return controller_.get();
}

void Character::set_controller(std::unique_ptr<CharacterController> controller)
{
    controller_ = std::move(controller);
}

Interactable *Character::interactable()
{
    return interactable_.get();
}

game::Bag const &Character::bag() const
{
    return bag_;
}

game::Bag &Character::bag()
{
    return bag_;
}

} // namespace lc1
