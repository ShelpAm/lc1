#pragma once

#include "lc1/game/character/combat.hpp"
#include "lc1/game/character/model-config.hpp"

#include <cstdint>
#include <string_view>

namespace lc1 {

class Character;

enum class WeaponAction : std::uint8_t { MeleeAttack, RangedAttack, ToggleLight, Torch };
enum class WeaponKind : std::uint8_t { Melee, Ranged, Tool };

struct WeaponProperty {
    float attack;
    float range;
    float attack_duration =
        combat::swing_duration; // CPU-resolved animation duration; default for unanimated weapons.
};

// Copyable definition data for save/presentation snapshots; no runtime behavior.
class WeaponDefinition {
  public:
    WeaponDefinition(std::uint32_t keyid, WeaponProperty property, ModelConfig model_config = {},
                     WeaponAction action = WeaponAction::MeleeAttack);

    std::uint32_t keyid() const;

    WeaponAction action() const { return action_; }

    WeaponKind kind() const
    {
        switch (action_) {
        case WeaponAction::MeleeAttack:
            return WeaponKind::Melee;
        case WeaponAction::RangedAttack:
            return WeaponKind::Ranged;
        case WeaponAction::ToggleLight:
        case WeaponAction::Torch:
            return WeaponKind::Tool;
        }
        return WeaponKind::Tool;
    }

    WeaponProperty property() const;

    ModelConfig const &model_config() const;

  private:
    std::uint32_t keyid_;
    WeaponProperty property_;
    ModelConfig model_config_;
    WeaponAction action_;
};

// Shared, immutable simulation definitions. Per-character use state stays on Character.
class Weapon : public WeaponDefinition {
  public:
    virtual ~Weapon() = default;
    virtual void use(Character &user) const = 0;

  protected:
    using WeaponDefinition::WeaponDefinition;
};

class MeleeWeapon final : public Weapon {
  public:
    MeleeWeapon(std::uint32_t keyid, WeaponProperty property, ModelConfig model_config = {});
    void use(Character &user) const override;
};

class RangedWeapon final : public Weapon {
  public:
    RangedWeapon(std::uint32_t keyid, WeaponProperty property, ModelConfig model_config = {});
    void use(Character &user) const override;
};

class Flashlight final : public Weapon {
  public:
    Flashlight(std::uint32_t keyid, WeaponProperty property, ModelConfig model_config = {});
    void use(Character &user) const override;
};

class Torch final : public Weapon {
  public:
    Torch(std::uint32_t keyid, WeaponProperty property, ModelConfig model_config = {});
    void use(Character &user) const override;
};

} // namespace lc1
