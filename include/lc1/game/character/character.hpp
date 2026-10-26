#pragma once

#include "lc1/game/character/bag.hpp"
#include "lc1/game/character/character-controller.hpp"
#include "lc1/game/character/character-intent.hpp"
#include "lc1/game/character/equipment.hpp"
#include "lc1/game/character/physics.hpp"
#include "lc1/game/character/weapon.hpp"
#include "lc1/game/party/companion-order.hpp"
#include "lc1/scene/transform.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <string>

namespace lc1 {

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

// Body proportions in metres, plus walking and running speed. The placeholder mesh and the
// collision body are both built from these numbers, so the two cannot silently disagree.
struct CharacterBody {
    float half_width = 0.3F;
    float height = 1.8F;
    float eye_height = 1.65F;
    float walk_speed = 4.5F;
    float run_speed = 18.0F;
    // Maximum ledge/ramp rise followed while grounded, and maximum distance the body
    // snaps downward to remain attached to a slope.
    float step_height = 0.35F;
    float ground_snap_distance = 0.35F;
};

struct CharacterConfig {
    std::uint32_t keyid;
    std::string name;
    bool sex;
    int age;
    Party *party;
    int level;

    std::map<EquipmentType, Equipment *> equipment;
    std::map<WeaponType, Weapon *> weapon;

    float health();
    float strenth();
    float agility();
};

// One upright, ground-level character, independent of input devices and cameras.
// transform_ is the sole world pose; its origin is at the character's feet.
class Character {
  public:
    Character(Continent const &continent, scene::Transform transform, CharacterConfig config,
              CharacterBody body = {});

    Character(Character const &) = delete;
    Character &operator=(Character const &) = delete;
    Character(Character &&) noexcept;
    Character &operator=(Character &&) noexcept;
    ~Character();

    CharacterConfig &config();

    CharacterConfig const &config() const;

    CharacterBody const &body() const;

    float half_width() const;

    float height() const;

    glm::vec3 position() const;

    scene::Transform const &transform() const;

    void move(Continent const &continent, glm::vec3 displacement);
    void walk(Continent const &continent, glm::vec3 direction, float delta_seconds, bool running,
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

    // The single state-update entry point: applies the intent against this character's
    // own gameplay rules (body/look targets, locomotion speed, collision, delta cap and
    // the jump request), then advances jump, gravity and ground support. It never trusts
    // the intent to be legal and does not pull from a controller itself.
    void update(Continent const &continent, float delta_seconds, CharacterIntent const &intent);

    // Ambient dialog requests are accepted only while standing still, grounded and not
    // asking to jump or sprint. This rule belongs to the character, so a player and an AI
    // companion follow the same one.
    bool accepts_ambient_requests(CharacterIntent const &intent) const;

    // The current controller's persistent companion order, or None when the controller is
    // a patrol/non-companion controller.
    CompanionOrder companion_order() const;

    CharacterController *controller();

    CharacterController const *controller() const;

    void set_controller(std::unique_ptr<CharacterController> controller);

    Interactable *interactable();

    void set_interactable(std::unique_ptr<Interactable> interactable);

    Faction const *faction() const;

    game::Bag const &bag() const;

    game::Bag &bag();

  private:
    // Resolves the requested actions before physics. Called only by update().
    void apply_intent(Continent const &continent, CharacterIntent const &intent,
                      float delta_seconds);

    // Party updates both sides of membership together during recruitment.
    friend class Party;

    CharacterConfig config_;
    CharacterBody body_;
    game::Bag bag_;

    float look_yaw_ = 0.0F;
    float look_pitch_ = 0.0F;
    bool on_ground_;
    glm::vec3 velocity_;
    bool jump_requested_ = false;
    scene::Transform transform_;
    std::unique_ptr<CharacterController> controller_;
    std::unique_ptr<Interactable> interactable_;
};

} // namespace lc1
