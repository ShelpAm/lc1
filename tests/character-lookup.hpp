#pragma once

#include "lc1/game/character/character-lookup.hpp"
#include "lc1/game/character/character.hpp"
#include <algorithm>
#include <initializer_list>
#include <stdexcept>
#include <vector>

inline lc1::CharacterLookup character_lookup(std::initializer_list<lc1::Character *> characters)
{
    return [characters = std::vector(characters)](lc1::CharacterId id) -> lc1::Character & {
        auto const found = std::ranges::find(characters, id, &lc1::Character::id);
        if (found == characters.end())
            throw std::runtime_error("unknown test character");
        return **found;
    };
}
