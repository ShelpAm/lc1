#include "lc1/game/party/faction.hpp"

namespace lc1 {

Faction::Faction(std::uint32_t keyid, std::string name) : keyid_(keyid), name_(std::move(name))
{
}

std::uint32_t Faction::keyid() const
{
    return keyid_;
}

std::string const &Faction::name() const
{
    return name_;
}

} // namespace lc1
