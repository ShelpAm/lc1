#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace lc1::game {

enum class ResourceCategory : std::uint8_t {
    Currency,
    Material,
    Equipment,
};

struct Resource {
    std::uint32_t keyid;
    std::string name;
    ResourceCategory category;
    std::uint32_t weight; // Grams per unit.
};

} // namespace lc1::game

template <> struct std::formatter<lc1::game::ResourceCategory> : std::formatter<std::string_view> {
    std::format_context::iterator format(lc1::game::ResourceCategory value,
                                         std::format_context &ctx) const;
};
