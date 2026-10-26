#include "lc1/error.hpp"

namespace lc1 {

Error::Error(std::string const &message, std::source_location location)
    : std::runtime_error(message), location_(location)
{
}

std::source_location Error::location() const noexcept
{
    return location_;
}

std::string Error::diagnostic() const
{
    return std::format("{}\n  at {}:{} in {}", what(), location_.file_name(), location_.line(),
                       location_.function_name());
}

} // namespace lc1
