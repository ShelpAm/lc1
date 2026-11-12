#include "lc1/game/character/character.hpp"

#include "lc1/error.hpp"
#include "lc1/game/character/ai-character-controller.hpp"
#include "lc1/game/character/character-pose.hpp"
#include "lc1/game/continent/continent.hpp"
#include "lc1/game/party/faction.hpp"
#include "lc1/game/party/party.hpp"
#include "lc1/game/physics/world-collision.hpp"
#include "lc1/game/resources/catalog.hpp"
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
        body.eye_height > body.height || !std::isfinite(body.step_height) ||
        body.step_height < 0.0F || !std::isfinite(body.ground_snap_distance) ||
        body.ground_snap_distance < 0.0F)
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
    : config_(std::move(config)), body_(validated_body(body)), bag_{game::default_bag_capacity},
      transform_(validated_transform(continent, transform, body_)),
      motion_(WorldCollision{continent}, body_, transform_.position, motion_settings)
{
    if (!std::isfinite(config_.attack_animation_duration) || config_.attack_animation_duration < 0)
        fail("character attack animation duration must be finite and nonnegative");
    if (!std::isfinite(config_.health_lost) || config_.health_lost < 0 ||
        config_.max_health() <= 0 || config_.health_lost > config_.max_health())
        fail("invalid character health");
    set_body_yaw(transform_.rotation.y);
}

Character::~Character() = default;

void Character::set_interactable(std::unique_ptr<Interactable> interactable)
{
    interactable_ = std::move(interactable);
}

std::uint32_t Character::faction() const
{
    return config_.party->faction();
}

void Character::apply_intent(float delta_seconds, CharacterIntent const &intent)
{
    if (!finite(intent.move) || (intent.body_yaw && !std::isfinite(*intent.body_yaw)) ||
        (intent.body_direction && !finite(*intent.body_direction)) ||
        (intent.look && !finite(*intent.look)))
        fail("invalid character intent");

    ambient_requests_blocked_ = intent.jump || intent.sprint || intent.move != glm::vec2{};

    // Pose changes come first so an intent that supplies both orientation and movement
    // applies them in a fixed order: body yaw, body direction, then look target.
    if (intent.body_yaw)
        set_body_yaw(*intent.body_yaw);
    if (intent.body_direction)
        set_body_direction(*intent.body_direction);
    if (intent.look)
        set_look_angles(intent.look->x, intent.look->y);

    if (intent.nod)
        ++nod_sequence_;

    weapon_attack_cooldown_ = std::max(0.0F, weapon_attack_cooldown_ - delta_seconds);
    if (intent.swing_weapon && config_.main_hand)
        config_.main_hand->use(*this);

    // Resolve attacks first so attacking and jumping in the same tick cannot bypass the lock.
    if (intent.jump && attack_elapsed_ >= combat::swing_duration)
        request_jump();
}

void Character::begin_melee_attack(WeaponProperty property)
{
    if (weapon_attack_cooldown_ > 0 || attack_elapsed_ < combat::swing_duration)
        return;
    weapon_attack_cooldown_ = combat::cooldown;
    ++attack_sequence_;
    attack_elapsed_ = 0;
    attack_duration_ = std::max(config_.attack_animation_duration, property.attack_duration);
    attack_property_ = property;
    attack_victims_.clear();
}

void Character::toggle_flashlight()
{
    flashlight_on_ = !flashlight_on_;
}

CompanionOrder Character::companion_order() const
{
    auto const *ai = ai_controller();
    return ai ? ai->companion_order().value_or(CompanionOrder::None) : CompanionOrder::None;
}

bool Character::accepts_ambient_requests() const
{
    return !dead() && reaction_time_ >= combat::hit_duration &&
           attack_elapsed_ >= combat::swing_duration && on_ground() && !ambient_requests_blocked_;
}

void Character::update(float delta_seconds, Continent const &continent)
{
    if (!std::isfinite(delta_seconds) || delta_seconds < 0)
        fail("invalid character update time");
    attack_sweep_.reset();
    auto const from = position();
    float from_yaw = body_yaw();
    bool const blocked = dead() || reaction_time_ < combat::hit_duration;
    reaction_time_ = std::min(combat::death_duration, reaction_time_ + delta_seconds);
    auto const intent =
        !blocked && controller_ ? controller_->decide(delta_seconds) : CharacterIntent{};
    auto const sequence = attack_sequence_;
    apply_intent(delta_seconds, intent);
    running_ =
        attack_elapsed_ >= combat::swing_duration && intent.sprint && intent.move != glm::vec2{};
    if (sequence != attack_sequence_)
        from_yaw = body_yaw();
    auto const velocity =
        walking_velocity({intent.move.x, 0.0F, intent.move.y}, intent.sprint, intent.face_movement);
    motion_.update(delta_seconds, WorldCollision{continent}, body_, transform_.position, velocity);
    auto const displacement = position() - from;
    moving_ = glm::length(glm::vec2{displacement.x, displacement.z}) > 0.0001F;
    float const begin = attack_elapsed_;
    auto const attack_delta = delta_seconds * combat::swing_duration / attack_duration_;
    attack_elapsed_ = std::min(combat::swing_duration, attack_elapsed_ + attack_delta);
    if (!blocked && begin < combat::active_end && attack_elapsed_ > combat::windup)
        attack_sweep_ =
            AttackSweep{begin, begin + attack_delta, from, position(), from_yaw, body_yaw()};
}

glm::vec3 Character::walking_velocity(glm::vec3 direction, bool running, bool face_movement)
{
    if (attack_elapsed_ < combat::swing_duration)
        return {};
    direction.y = 0.0F;
    // double avoids overflow when normalizing large but finite input vectors.
    double const length =
        std::hypot(static_cast<double>(direction.x), static_cast<double>(direction.z));
    if (length == 0.0)
        return {};
    direction *= static_cast<float>(1.0 / std::max(length, 1.0));

    if (face_movement)
        set_body_yaw(glm::degrees(std::atan2(-direction.x, -direction.z)));

    float const speed = running ? config_.run_speed() : config_.walk_speed();
    return direction * speed;
}

void Character::walk(float delta_seconds, Continent const &continent, glm::vec3 direction,
                     bool running, bool face_movement)
{
    if (!std::isfinite(delta_seconds) || delta_seconds < 0.0F)
        fail("invalid character walking time");
    if (!finite(direction))
        fail("invalid character walking input");
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
    return CharacterPose{*this}.look_direction();
}

glm::vec3 Character::heading() const
{
    return CharacterPose{*this}.heading();
}

glm::vec3 Character::right() const
{
    return CharacterPose{*this}.right();
}

glm::vec3 Character::eye_position() const
{
    // Character-space metres; independent of the source model's origin and units.
    return CharacterPose{*this}.eye_position();
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
    return controller_;
}

CharacterController const *Character::controller() const
{
    return controller_;
}

AiCharacterController *Character::ai_controller()
{
    return dynamic_cast<AiCharacterController *>(controller_);
}

AiCharacterController const *Character::ai_controller() const
{
    return dynamic_cast<AiCharacterController const *>(controller_);
}

void Character::set_controller(CharacterController *controller)
{
    if (controller)
        controller->possess(*this);
    controller_ = controller;
}

Interactable *Character::interactable()
{
    return interactable_.get();
}

game::Bag const &Character::bag() const
{
    return bag_;
}

bool Character::equip(game::Resource const &resource, Equipment &equipment)
{
    if (resource.keyid != equipment.keyid())
        fail("equipment definition does not match resource {}", resource.keyid);
    if (dead())
        return false;
    auto candidate = bag_;
    if (!candidate.remove(resource, 1))
        return false;
    auto const previous = config_.equipment.find(equipment.type());
    if (previous != config_.equipment.end() &&
        !candidate.add(*game::find_resource(previous->second->keyid()), 1))
        return false;
    // Allocate a new slot before committing the inventory.
    config_.equipment.insert_or_assign(equipment.type(), &equipment);
    bag_ = std::move(candidate);
    return true;
}

bool Character::equip(game::Resource const &resource, Weapon &weapon)
{
    if (resource.keyid != weapon.keyid())
        fail("weapon definition does not match resource {}", resource.keyid);
    if (dead())
        return false;
    auto candidate = bag_;
    if (!candidate.remove(resource, 1) ||
        (config_.main_hand && !candidate.add(*game::find_resource(config_.main_hand->keyid()), 1)))
        return false;
    bag_ = std::move(candidate);
    config_.main_hand = &weapon;
    flashlight_on_ = false;
    attack_sweep_.reset();
    attack_elapsed_ = combat::swing_duration;
    return true;
}

bool Character::unequip(EquipmentType slot)
{
    if (dead())
        return false;
    auto const found = config_.equipment.find(slot);
    if (found == config_.equipment.end())
        return false;
    auto candidate = bag_;
    if (!candidate.add(*game::find_resource(found->second->keyid()), 1))
        return false;
    bag_ = std::move(candidate);
    config_.equipment.erase(found);
    return true;
}

bool Character::unequip_weapon()
{
    if (dead() || !config_.main_hand)
        return false;
    auto candidate = bag_;
    if (!candidate.add(*game::find_resource(config_.main_hand->keyid()), 1))
        return false;
    bag_ = std::move(candidate);
    config_.main_hand = nullptr;
    flashlight_on_ = false;
    attack_sweep_.reset();
    attack_elapsed_ = combat::swing_duration;
    return true;
}

bool Character::flashlight_on() const
{
    return !dead() && flashlight_on_ && config_.main_hand &&
           config_.main_hand->action() == WeaponAction::ToggleLight;
}

void Character::set_flashlight_on(bool on)
{
    flashlight_on_ = on;
}

void Character::take_damage(float amount)
{
    if (!std::isfinite(amount) || amount < 0)
        fail("damage must be finite and nonnegative");
    if (dead() || amount == 0)
        return;
    config_.health_lost += std::min(amount, config_.health());
    ++hit_sequence_;
    reaction_time_ = 0;
    attack_elapsed_ = combat::swing_duration;
    attack_sweep_.reset();
}

game::Bag &Character::bag()
{
    return bag_;
}

std::uint64_t Character::nod_sequence() const
{
    return nod_sequence_;
}

std::uint64_t Character::attack_sequence() const
{
    return attack_sequence_;
}

CharacterId Character::id() const
{
    return CharacterId{config_.keyid};
}

} // namespace lc1

namespace lc1 {
bool Character::dead() const
{
    return config_.health() == 0;
}

float Character::reaction_time() const
{
    return reaction_time_;
}

std::uint64_t Character::hit_sequence() const
{
    return hit_sequence_;
}

void Character::resolve_attack(std::span<Character *const> targets, Continent const &continent)
{
    auto const pending = std::exchange(attack_sweep_, std::nullopt);
    if (!pending || dead())
        return;
    auto const &sweep = *pending;
    float const start = std::max(sweep.begin, combat::windup);
    float const finish = std::min(sweep.end, combat::active_end);
    float const first = (start - sweep.begin) / (sweep.end - sweep.begin);
    float const last = (finish - sweep.begin) / (sweep.end - sweep.begin);
    float const yaw_delta = std::remainder(sweep.to_yaw - sweep.from_yaw, 360.0F);
    // Subdivide the complete active interval, including ticks spanning the whole attack.
    // Expand by the maximum half-step travel so a fast blade cannot pass between samples.
    float const angle = (finish - start) * (145.0F / (combat::active_end - combat::windup)) +
                        std::abs(yaw_delta) * (last - first);
    float const travel = glm::length(sweep.to - sweep.from) * (last - first);
    auto const steps = static_cast<std::size_t>(
        std::max(1.0F, std::ceil(std::max(angle / 2.0F, travel / 0.025F))));
    float const radius = 0.03F + (travel + attack_property_.range * glm::radians(angle)) /
                                     (2.0F * static_cast<float>(steps));
    WorldCollision const collision{continent};
    for (std::size_t step = 0; step <= steps; ++step) {
        float const progress = static_cast<float>(step) / static_cast<float>(steps);
        float const time = glm::mix(start, finish, progress);
        float const alpha = glm::mix(first, last, progress);
        auto const origin =
            glm::mix(sweep.from, sweep.to, alpha) + glm::vec3{0, body_.height * (1.05F / 1.8F), 0};
        auto const heading = horizontal_direction(sweep.from_yaw + yaw_delta * alpha);
        // Matches the default Swing's 70 -> -75 degrees and the -45 degree grip.
        float const pitch = glm::radians(
            25.0F - (time - combat::windup) * (145.0F / (combat::active_end - combat::windup)));
        auto const tip = origin + attack_property_.range * (glm::vec3{0, std::cos(pitch), 0} -
                                                            heading * std::sin(pitch));
        for (auto *target : targets) {
            if (target == this || target->dead() ||
                std::ranges::contains(attack_victims_, target->id()))
                continue;
            auto const feet = target->position();
            auto const extent = glm::vec3{target->half_width(), 0, target->half_width()};
            auto const hit = segment_box_fraction(
                origin, tip, feet - extent - glm::vec3{radius},
                feet + extent + glm::vec3{0, target->height(), 0} + glm::vec3{radius});
            if (!hit || collision.segment_blocked(origin, glm::mix(origin, tip, *hit), radius))
                continue;
            attack_victims_.push_back(target->id());
            target->take_damage(attack_property_.attack);
        }
    }
}
} // namespace lc1
