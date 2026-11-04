#include "lc1/game/character/weapon.hpp"

#include <utility>

namespace lc1 {

std::string_view to_string(WeaponType type)
{
    switch (type) {
    case WeaponType::MeleeWeapon:
        return "MeleeWeapon";
    case WeaponType::RangedWeapon:
        return "RangedWeapon";
    default:
        return "Unknown";
    }
}

Weapon::Weapon(std::uint32_t keyid, WeaponType type, WeaponProperty property,
               ModelConfig model_config)
    : keyid_(keyid), type_(type), property_(property), model_config_(std::move(model_config))
{
}

std::uint32_t Weapon::keyid() const
{
    return keyid_;
}

WeaponType Weapon::type() const
{
    return type_;
}

WeaponProperty Weapon::property() const
{
    return property_;
}

ModelConfig const &Weapon::model_config() const
{
    return model_config_;
}

} // namespace lc1
