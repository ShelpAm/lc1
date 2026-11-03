#pragma once

#include "lc1/game/character/model-config.hpp"

#include <cstdint>
#include <string_view>

namespace lc1::scene {
struct SceneModel;
}

namespace lc1 {

class WorldPresentation;

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
    Weapon(std::uint32_t keyid, WeaponType type, WeaponProperty property,
           ModelConfig model_config = {});

    std::uint32_t keyid() const;
    WeaponType type() const;
    WeaponProperty property() const;

    ModelConfig const &model_config() const;

    scene::SceneModel *model();
    scene::SceneModel const *model() const;

  private:
    friend class WorldPresentation;

    // Optional Scene-owned borrow; WorldPresentation clears it before Scene teardown.
    scene::SceneModel *model_ = nullptr;

    std::uint32_t keyid_;
    WeaponType type_;
    WeaponProperty property_;
    ModelConfig model_config_;
};

} // namespace lc1
