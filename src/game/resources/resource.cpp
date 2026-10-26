#include "lc1/game/resources/resource.hpp"

std::format_context::iterator
std::formatter<lc1::game::ResourceCategory>::format(lc1::game::ResourceCategory value,
                                                    std::format_context &ctx) const
{
    std::string_view name;

    switch (value) {
    case lc1::game::ResourceCategory::Currency:
        name = "Currency";
        break;
    case lc1::game::ResourceCategory::Equipment:
        name = "Equipment";
        break;
    case lc1::game::ResourceCategory::Material:
        name = "Material";
        break;
    }

    return std::formatter<std::string_view>::format(name, ctx);
}
