#include "lc1/game/character/weapon.hpp"
#include "lc1/game/character/character.hpp"

#include "lc1/error.hpp"
#include <cmath>
#include <utility>

namespace lc1 {

WeaponDefinition::WeaponDefinition(std::uint32_t keyid, WeaponProperty property,
                                   ModelConfig model_config, WeaponAction action)
    : keyid_(keyid), property_(property), model_config_(std::move(model_config)), action_(action)
{
    if (!std::isfinite(property.attack_duration) || property.attack_duration <= 0)
        fail("weapon attack duration must be finite and positive");
    if (!std::isfinite(property.attack) || property.attack < 0 || !std::isfinite(property.range) ||
        property.range <= 0)
        fail("weapon attack must be nonnegative and range positive, both finite");
}

std::uint32_t WeaponDefinition::keyid() const
{
    return keyid_;
}

WeaponProperty WeaponDefinition::property() const
{
    return property_;
}

ModelConfig const &WeaponDefinition::model_config() const
{
    return model_config_;
}

MeleeWeapon::MeleeWeapon(std::uint32_t keyid, WeaponProperty property, ModelConfig model_config)
    : Weapon(keyid, property, std::move(model_config), WeaponAction::MeleeAttack)
{
}

void MeleeWeapon::use(Character &user) const
{
    user.begin_melee_attack(property());
}

RangedWeapon::RangedWeapon(std::uint32_t keyid, WeaponProperty property, ModelConfig model_config)
    : Weapon(keyid, property, std::move(model_config), WeaponAction::RangedAttack)
{
}

void RangedWeapon::use(Character &) const
{
    // Projectile firing is not implemented; preserve the existing ranged behavior.
}

Flashlight::Flashlight(std::uint32_t keyid, WeaponProperty property, ModelConfig model_config)
    : Weapon(keyid, property, std::move(model_config), WeaponAction::ToggleLight)
{
}

void Flashlight::use(Character &user) const
{
    user.toggle_flashlight();
}

Torch::Torch(std::uint32_t keyid, WeaponProperty property, ModelConfig model_config)
    : Weapon(keyid, property, std::move(model_config), WeaponAction::Torch)
{
}

void Torch::use(Character &) const
{
    // Equipped torches burn continuously; using one does not attack or toggle it.
}

} // namespace lc1
