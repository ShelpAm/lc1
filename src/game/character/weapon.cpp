#include "lc1/game/character/weapon.hpp"

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

Weapon::Weapon(std::uint32_t keyid, WeaponType type, WeaponProperty property)
    : keyid_(keyid), type_(type), property_(property)
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

} // namespace lc1
