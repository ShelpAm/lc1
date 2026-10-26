#include "lc1/game/character/equipment.hpp"

namespace lc1 {

std::string_view to_string(EquipmentType type)
{
    switch (type) {
    case EquipmentType::Head:
        return "Head";
    case EquipmentType::Body:
        return "Body";
    case EquipmentType::Foot:
        return "Foot";
    default:
        return "Unknown";
    }
}

Equipment::Equipment(std::uint32_t keyid, EquipmentType type, EquipmentProperty property)
    : keyid_(keyid), type_(type), property_(property)
{
}

std::uint32_t Equipment::keyid() const
{
    return keyid_;
}

EquipmentType Equipment::type() const
{
    return type_;
}

EquipmentProperty Equipment::property() const
{
    return property_;
}

} // namespace lc1
