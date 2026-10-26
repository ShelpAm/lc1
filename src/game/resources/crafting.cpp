#include "lc1/game/resources/crafting.hpp"

namespace lc1::game {

Recipe const &iron_helmet_recipe()
{
    static Recipe const recipe{{{&pig_iron, 5}, {&wood, 2}, {&moneta, 20}}, {{&iron_helmet, 1}}};
    return recipe;
}

} // namespace lc1::game
