#pragma once

#include <cstdint>
#include <string_view>

namespace lc1 {

enum class WeaponType : std::uint8_t {
    MeleeWeapon,
    RangedWeapon,
};

std::string_view to_string(WeaponType type);

struct WeaponProperty {
    float attack;
    float range;
};

class Weapon {
  public:
    Weapon(std::uint32_t keyid, WeaponType type, WeaponProperty property);

    std::uint32_t keyid() const;

    WeaponType type() const;

    WeaponProperty property() const;

  private:
    std::uint32_t keyid_;
    WeaponType type_;
    WeaponProperty property_;
};

} // namespace lc1
