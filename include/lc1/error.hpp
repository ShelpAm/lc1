#pragma once

#include <format>
#include <source_location>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace lc1 {

// Project setup/runtime failure. Keep the message separate from its call site so UI
// can show the message while diagnostic logs include file, line and function.
class Error : public std::runtime_error {
  public:
    explicit Error(std::string const &message,
                   std::source_location location = std::source_location::current());

    std::source_location location() const noexcept;

    std::string diagnostic() const;

  private:
    std::source_location location_;
};

namespace detail {

// Conversion at the call site captures its location before entering fail(). A trailing
// default location parameter on fail() itself would interfere with pack deduction.
template <typename... Args> struct LocatedFormat {
    std::format_string<Args...> format;
    std::source_location location;

    // Already checked format strings can also be forwarded by ordinary functions.
    constexpr LocatedFormat(std::format_string<Args...> text,
                            std::source_location location = std::source_location::current())
        : format(text), location(location)
    {
    }

    template <typename Text>
    consteval LocatedFormat(Text const &text,
                            std::source_location location = std::source_location::current())
        : format(text), location(location)
    {
    }
};

} // namespace detail

// Formats like std::println, with compile-time format checking. For a message built
// at runtime, use fail("{}", message). Logging belongs to the catching layer.
template <typename... Args>
[[noreturn]] void fail(detail::LocatedFormat<std::type_identity_t<Args>...> fmt, Args &&...args)
{
    throw Error(std::format(fmt.format, std::forward<Args>(args)...), fmt.location);
}

} // namespace lc1
