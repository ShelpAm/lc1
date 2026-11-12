#pragma once

#include "lc1/game/character/bag.hpp"
#include "lc1/game/character/character-body.hpp"
#include "lc1/game/character/character-controller.hpp"
#include "lc1/game/character/character-id.hpp"
#include "lc1/game/character/character-intent.hpp"
#include "lc1/game/character/combat.hpp"
#include "lc1/game/character/equipment.hpp"
#include "lc1/game/character/weapon.hpp"
#include "lc1/game/party/companion-order.hpp"
#include "lc1/game/physics/character-motion.hpp"
#include "lc1/scene/transform.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace lc1 {

class AiCharacterController;

class Continent;
class Equipment;
class Faction;
class Interactable;
class Party;
class Weapon;
struct EquipmentProperty;

enum class CharacterStatus : std::uint8_t {
    Alive,
    Injured,
    Dead,
    Captive,
};

struct CharacterConfig {
    std::uint32_t keyid;
    std::string name;
    bool sex;
    int age;
    Party *party;
    int level;

    std::map<EquipmentType, Equipment *> equipment;
    Weapon *main_hand = nullptr;
    ModelConfig model;

    float attack_animation_duration = 0; // Resolved from CPU animation data during world assembly.
    float health_lost = 0;

    float max_health() const { return 1.0F * static_cast<float>(level) + 100.F; }

    float health() const { return max_health() - health_lost; }

    float strenth() const { return 0.1F * static_cast<float>(level); }

    float agility() const { return 0.1F * static_cast<float>(level); }

    float walk_speed() const { return 4.5F + glm::log(1.0F + agility()); }

    float run_speed() const { return walk_speed() + 4.5F; }
};

// One upright, ground-level character, independent of input devices and cameras.
// transform_ is the sole world pose; its origin is at the character's feet.
class Character {
  public:
    Character(Continent const &continent, scene::Transform transform, CharacterConfig config,
              CharacterBody body = {}, CharacterMotionSettings motion_settings = {});

    Character(Character const &) = delete;
    Character &operator=(Character const &) = delete;
    Character(Character &&) = delete;
    Character &operator=(Character &&) = delete;
    ~Character();

    CharacterConfig const &config() const;
    CharacterId id() const;

    std::uint64_t attack_sequence() const;
    std::uint64_t nod_sequence() const;

    CharacterBody const &body() const;

    float half_width() const;

    float height() const;

    glm::vec3 position() const;

    scene::Transform const &transform() const;

    void move(Continent const &continent, glm::vec3 displacement);
    void walk(float delta_seconds, Continent const &continent, glm::vec3 direction, bool running,
              bool face_movement = true);

    // Right-handed degrees about +Y. Zero faces -Z, independently of asset correction.
    float body_yaw() const;

    void set_body_yaw(float degrees);
    void set_body_direction(glm::vec3 direction);
    glm::vec3 body_heading() const;

    // Relative to the body: positive yaw turns left, positive pitch looks up.
    // These are look targets, not a skeleton pose. Yaw/pitch are limited to +/-85/89.
    float look_yaw() const;

    float look_pitch() const;

    void set_look_angles(float yaw_degrees, float pitch_degrees);
    glm::vec3 look_direction() const;
    glm::vec3 heading() const; // Horizontal look direction, without pitch.
    glm::vec3 right() const;
    glm::vec3 eye_position() const;

    void request_jump();

    bool on_ground() const;

    float attack_progress() const { return attack_elapsed_ / combat::swing_duration; }

    bool running() const { return running_; }

    bool moving() const { return moving_; }

    // Resolves the installed controller once, then applies character rules and physics.
    // A character without a controller receives an empty intent.
    void update(float delta_seconds, Continent const &continent);

    // Ambient dialog requests are accepted only while standing still, grounded and not
    // asking to jump or sprint. This rule belongs to the character, so a player and an AI
    // companion follow the same one.
    bool accepts_ambient_requests() const;

    // The current controller's persistent companion order, or None when the controller is
    // a patrol/non-companion controller.
    CompanionOrder companion_order() const;

    CharacterController *controller();
    CharacterController const *controller() const;
    AiCharacterController *ai_controller();
    AiCharacterController const *ai_controller() const;
    // Borrows controller; it must remain alive while attached. nullptr detaches.
    void set_controller(CharacterController *controller);

    Interactable *interactable();
    void set_interactable(std::unique_ptr<Interactable> interactable);

    std::uint32_t faction() const;

    game::Bag &bag();
    game::Bag const &bag() const;

    // Definitions borrow the world's immutable tables, which outlive this character.
    bool equip(game::Resource const &resource, Equipment &equipment);
    bool equip(game::Resource const &resource, Weapon &weapon);
    bool unequip(EquipmentType slot);
    bool unequip_weapon();

    bool flashlight_on() const;
    void set_flashlight_on(bool on);

    void take_damage(float amount);
    bool dead() const;
    float reaction_time() const;
    std::uint64_t hit_sequence() const;

    // Called once after all characters move; consumes this tick's melee sweep.
    void resolve_attack(std::span<Character *const> targets, Continent const &continent);

  private:
    // Resolves the requested actions before physics. Called only by update().
    void apply_intent(float delta_seconds, CharacterIntent const &intent);
    glm::vec3 walking_velocity(glm::vec3 direction, bool running, bool face_movement);

    friend class MeleeWeapon;
    friend class Flashlight;
    void begin_melee_attack(WeaponProperty property);
    void toggle_flashlight();

    // Party updates both sides of membership together during recruitment.
    friend class Party;

    std::uint64_t attack_sequence_ = 0;
    std::uint64_t nod_sequence_ = 0;

    CharacterConfig config_;
    CharacterBody body_;
    game::Bag bag_;

    bool flashlight_on_ = false;
    float look_yaw_ = 0.0F;
    float look_pitch_ = 0.0F;
    scene::Transform transform_;
    CharacterMotion motion_;
    CharacterController *controller_ = nullptr;
    std::unique_ptr<Interactable> interactable_;

    bool ambient_requests_blocked_ = false;
    bool running_ = false;
    bool moving_ = false;
    float weapon_attack_cooldown_ = 0;
    float attack_elapsed_ = combat::swing_duration; // Normalized to the default hit timeline.
    float attack_duration_ = combat::swing_duration;
    float reaction_time_ = combat::death_duration;
    std::uint64_t hit_sequence_ = 0;
    WeaponProperty attack_property_{};
    std::vector<CharacterId> attack_victims_;

    struct AttackSweep {
        float begin;
        float end;
        glm::vec3 from;
        glm::vec3 to;
        float from_yaw;
        float to_yaw;
    };

    std::optional<AttackSweep> attack_sweep_;
};

} // namespace lc1
