#include "lc1/game/character/character.hpp"

#include "lc1/error.hpp"
#include "lc1/game/continent/continent.hpp"
#include "lc1/game/party/faction.hpp"
#include "lc1/game/party/party.hpp"
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

} // namespace

Character::Character(Continent const &continent, scene::Transform transform, CharacterConfig config,
                     CharacterBody body)
    : config_(std::move(config)), body_(body), bag_{10000}, on_ground_{}, velocity_{},
      transform_(transform)
{
    if (!std::isfinite(body_.half_width) || body_.half_width <= 0.0F ||
        !std::isfinite(body_.height) || body_.height <= 0.0F || !std::isfinite(body_.eye_height) ||
        body_.eye_height < 0.0F || body_.eye_height > body_.height ||
        !std::isfinite(body_.walk_speed) || body_.walk_speed < 0.0F ||
        !std::isfinite(body_.run_speed) || body_.run_speed < 0.0F ||
        !std::isfinite(body_.step_height) || body_.step_height < 0.0F ||
        !std::isfinite(body_.ground_snap_distance) || body_.ground_snap_distance < 0.0F)
        fail("invalid character body");

    auto const &pose = transform_;
    if (pose.rotation.x != 0.0F || pose.rotation.z != 0.0F || pose.scale != glm::vec3{1.0F})
        fail("ground character needs an upright, unscaled world transform");
    if (!continent.can_stand(position(), body_.half_width, body_.height))
        fail("character spawn is obstructed or outside the continent");
    set_body_yaw(pose.rotation.y);
    // Spawning in the air is valid: the first update will apply gravity and land the
    // character. If a support surface is already within step/snap range, start grounded
    // and align the feet to it instead of requiring the caller to know the exact height.
    auto const support =
        continent.support_below({position().x, position().y + body_.step_height, position().z},
                                body_.step_height + body_.ground_snap_distance, body_.half_width);
    if (support) {
        float const offset = support->height - position().y;
        if (offset >= -body_.ground_snap_distance && offset <= body_.step_height) {
            transform_.position.y = support->height;
            on_ground_ = true;
        }
    }
}

Character::Character(Character &&) noexcept = default;
Character &Character::operator=(Character &&) noexcept = default;
Character::~Character() = default;

void Character::set_interactable(std::unique_ptr<Interactable> interactable)
{
    interactable_ = std::move(interactable);
}

Faction const *Character::faction() const
{
    return config_.party->faction();
}

void Character::apply_intent(float delta_seconds, Continent const &continent,
                             CharacterIntent const &intent)
{
    if (!finite(intent.move) || (intent.body_yaw && !std::isfinite(*intent.body_yaw)) ||
        (intent.body_direction && !finite(*intent.body_direction)) ||
        (intent.look && !finite(*intent.look)) ||
        (intent.interaction_direction && !finite(*intent.interaction_direction)))
        fail("invalid character intent");

    // Pose changes come first so an intent that supplies both orientation and movement
    // applies them in a fixed order: body yaw, body direction, look target, then locomotion.
    if (intent.body_yaw)
        set_body_yaw(*intent.body_yaw);
    if (intent.body_direction)
        set_body_direction(*intent.body_direction);
    if (intent.look)
        set_look_angles(intent.look->x, intent.look->y);

    // Character owns speed, facing policy and collision. Sprint is only a request: a
    // future stamina/state gate belongs here, not in the player or AI controller.
    walk(delta_seconds, continent, {intent.move.x, 0.0F, intent.move.y}, intent.sprint,
         intent.face_movement);

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
    return on_ground_ && !intent.jump && !intent.sprint && intent.move == glm::vec2{};
}

void Character::update(float delta_seconds, Continent const &continent,
                       CharacterIntent const &intent)
{
    if (!std::isfinite(delta_seconds) || delta_seconds < 0.0F)
        fail("invalid character update time");
    // Match the horizontal movement cap after a resize or debugger stall.
    float const elapsed = std::min(delta_seconds, 0.1F);
    apply_intent(elapsed, continent, intent);
    if (elapsed == 0.0F)
        return;

    if (jump_requested_ && on_ground_) {
        constexpr float jump_height = 0.5F;
        velocity_.y = std::sqrt(2.0F * gravityf * jump_height);
        on_ground_ = false;
    }
    jump_requested_ = false;

    // While grounded, follow ramps and small steps. Query from slightly above the feet
    // so a rising surface within step_height is considered, and look down far enough to
    // stay attached while descending a slope.
    if (on_ground_) {
        auto const support = continent.support_below(
            {position().x, position().y + body_.step_height, position().z},
            body_.step_height + body_.ground_snap_distance, body_.half_width);
        if (support && support->height >= position().y - body_.ground_snap_distance &&
            support->height <= position().y + body_.step_height) {
            transform_.position.y = support->height;
            velocity_.y = 0.0F;
        }
        else {
            on_ground_ = false;
        }
    }

    if (!on_ground_) {
        float const previous_height = position().y;
        // Constant-acceleration integration keeps the jump height independent of frame rate.
        transform_.position.y += velocity_.y * elapsed - 0.5F * gravityf * elapsed * elapsed;
        velocity_.y -= gravityf * elapsed;

        // A fast uphill run can make the terrain rise into the body while the jump is
        // still ascending. Resolve that penetration first; otherwise the character ends
        // up below the height field and the downward support query can never find it.
        auto const terrain = continent.ground_height({position().x, position().z});
        if (terrain && position().y < *terrain) {
            transform_.position.y = *terrain;
            velocity_.y = 0.0F;
            on_ground_ = true;
        }
        // Query from the previous foot height so high-speed falls cannot tunnel through
        // the terrain. Normal landing is only possible while descending.
        else if (velocity_.y <= 0.0F) {
            float const fall_distance = std::max(0.0F, previous_height - position().y);
            auto const support =
                continent.support_below({position().x, previous_height, position().z},
                                        fall_distance + 0.001F, body_.half_width);
            if (support && position().y <= support->height) {
                transform_.position.y = support->height;
                velocity_.y = 0.0F;
                on_ground_ = true;
            }
        }
    }
}

void Character::move(Continent const &continent, glm::vec3 displacement)
{
    if (!finite(displacement) || displacement.y != 0.0F)
        fail("ground character displacement must be finite and horizontal");
    auto const feet = position();
    glm::vec2 const horizontal{displacement.x, displacement.z};
    float const requested_distance = glm::length(horizontal);

    // Horizontal collision is height-aware. The resolved Y is the input feet height;
    // Character::update() follows terrain and walkable surfaces vertically.
    auto const normal = continent.move(feet, horizontal, body_.half_width, body_.height);
    float const normal_progress =
        glm::distance(glm::vec2{normal.x, normal.z}, glm::vec2{feet.x, feet.z});

    // A walkable step can block the normal sweep even though its top is within the
    // body's step height. Retry from a raised feet position and accept only if it
    // makes progress onto a support surface above the current feet.
    if (on_ground_ && normal_progress + 0.001F < requested_distance) {
        glm::vec3 raised = feet;
        raised.y += body_.step_height;
        if (continent.can_stand(raised, body_.half_width, body_.height)) {
            auto const stepped = continent.move(raised, horizontal, body_.half_width, body_.height);
            float const stepped_progress =
                glm::distance(glm::vec2{stepped.x, stepped.z}, glm::vec2{feet.x, feet.z});
            if (stepped_progress > normal_progress + 0.001F) {
                auto const support = continent.support_below(
                    {stepped.x, raised.y, stepped.z},
                    body_.step_height + body_.ground_snap_distance, body_.half_width);
                if (support && support->height > feet.y + 0.001F &&
                    support->height <= feet.y + body_.step_height + 0.001F) {
                    glm::vec3 const candidate{stepped.x, support->height, stepped.z};
                    if (continent.can_stand(candidate, body_.half_width, body_.height)) {
                        transform_.position = candidate;
                        return;
                    }
                }
            }
        }
    }

    transform_.position = normal;
}

void Character::walk(float delta_seconds, Continent const &continent, glm::vec3 direction,
                     bool running, bool face_movement)
{
    if (!finite(direction) || !std::isfinite(delta_seconds) || delta_seconds < 0.0F)
        fail("invalid character walking input");
    direction.y = 0.0F;
    // double avoids overflow when normalizing large but finite input vectors.
    double const length =
        std::hypot(static_cast<double>(direction.x), static_cast<double>(direction.z));
    if (length == 0.0)
        return;
    direction *= static_cast<float>(1.0 / std::max(length, 1.0));

    if (face_movement)
        set_body_yaw(glm::degrees(std::atan2(-direction.x, -direction.z)));

    float const speed = running ? body_.run_speed : body_.walk_speed;
    // A resize or debugger stall must not teleport the character.
    float const elapsed = std::min(delta_seconds, 0.1F);
    move(continent, direction * speed * elapsed);
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

} // namespace lc1

namespace lc1 {

CharacterConfig &Character::config()
{
    return config_;
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
    jump_requested_ = true;
}

bool Character::on_ground() const
{
    return on_ground_;
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
