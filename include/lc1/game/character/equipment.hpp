#pragma once

#include <cstdint>
#include <string_view>

namespace lc1 {

enum class EquipmentType : std::uint8_t {
    Head,
    Body,
    Foot,
};

std::string_view to_string(EquipmentType type);

struct EquipmentProperty {
    float armor;
};

class Equipment {
  public:
    Equipment(std::uint32_t keyid, EquipmentType type, EquipmentProperty property);

    std::uint32_t keyid() const;

    EquipmentType type() const;

    EquipmentProperty property() const;

  private:
    std::uint32_t keyid_;
    EquipmentType type_;
    EquipmentProperty property_;
};

} // namespace lc1
