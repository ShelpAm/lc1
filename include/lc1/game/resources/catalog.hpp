#pragma once

#include "lc1/game/resources/resource.hpp"

#include <array>
#include <cstdint>

namespace lc1::game {
// Immutable built-in definitions. Names and definitions have process lifetime.
// NOLINTBEGIN
// clang-format off
inline Resource const moneta{.keyid=1, .name="Moneta", .category=ResourceCategory::Currency, .weight=10};
inline Resource const wood{.keyid=2, .name="Wood", .category=ResourceCategory::Material, .weight=1000};
inline Resource const pig_iron{.keyid=3, .name="Pig iron", .category=ResourceCategory::Material, .weight=2000};
inline Resource const iron_helmet{.keyid=4, .name="Iron helmet", .category=ResourceCategory::Equipment, .weight=2500};

inline Resource const body_armor{.keyid=5, .name="Body armor", .category=ResourceCategory::Equipment, .weight=4000};
inline Resource const boots{.keyid=6, .name="Boots", .category=ResourceCategory::Equipment, .weight=1000};
inline Resource const sword{.keyid=1001, .name="Sword", .category=ResourceCategory::Equipment, .weight=1500};
inline Resource const sword_2{.keyid=1002, .name="Sword", .category=ResourceCategory::Equipment, .weight=1500};
inline Resource const sword_3{.keyid=1003, .name="Sword", .category=ResourceCategory::Equipment, .weight=1500};
inline Resource const bow{.keyid=1004, .name="Bow", .category=ResourceCategory::Equipment, .weight=1000};

inline Resource const flashlight{.keyid=1005, .name="Flashlight", .category=ResourceCategory::Equipment, .weight=500};

inline Resource const torch{.keyid=1006, .name="Torch", .category=ResourceCategory::Equipment, .weight=700};

// Pointers, so the named definitions above stay the only instances: a lookup returns the same
// object a recipe or a caller holds.
inline std::array const resources{&moneta, &wood, &pig_iron, &iron_helmet, &body_armor, &boots, &sword, &sword_2, &sword_3, &bow, &flashlight, &torch};
// clang-format on
// NOLINTEND

// The only keyid to definition lookup. Callers validate a keyid at their entry point and can
// assume the result afterwards; editing the table is guarded by a uniqueness test rather than by
// an ordering rule, so entries stay in any order they are added in.
inline Resource const *find_resource(std::uint32_t keyid)
{
    for (auto const *resource : resources)
        if (resource->keyid == keyid)
            return resource;
    return nullptr;
}
} // namespace lc1::game
