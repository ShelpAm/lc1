#include "lc1/error.hpp"

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void check(bool condition, std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string{message});
}

void check_location(lc1::Error const &error, std::source_location expected,
                    std::uint_least32_t offset)
{
    auto const location = error.location();
    check(std::string_view{location.file_name()} == expected.file_name(),
          "errors retain the caller's file");
    check(location.line() == expected.line() + offset, "errors retain the caller's exact line");
    check(std::string_view{location.function_name()} == expected.function_name(),
          "errors retain the caller's function rather than fail's function");
    auto const diagnostic = error.diagnostic();
    check(diagnostic.contains(error.what()) && diagnostic.contains(location.file_name()) &&
              diagnostic.contains(std::to_string(location.line())) &&
              diagnostic.contains(location.function_name()),
          "diagnostics contain the original message, file, line and function");
}

lc1::Error formatted_failure()
{
    int value = 42;
    std::string const name = "character";
    auto expected = std::source_location{};
    try {
        expected = std::source_location::current();
        lc1::fail("unknown {} {}", name, value);
    }
    catch (lc1::Error const &error) {
        check(std::string_view{error.what()} == "unknown character 42",
              "what() preserves the message for UI and contextual wrapping");
        check_location(error, expected, 1);
        return error;
    }
    throw std::runtime_error("fail must throw lc1::Error");
}

void construction_and_context()
{
    auto const expected = std::source_location::current();
    lc1::Error const direct{"direct failure"};
    check_location(direct, expected, 1);

    auto const original = formatted_failure();
    lc1::Error const contextual{std::format("loading save: {}", original.what()),
                                original.location()};
    check_location(contextual, original.location(), 0);
    check(std::string_view{contextual.what()} == "loading save: unknown character 42",
          "context adds to the message without replacing the failure location");
    auto const copy = contextual;
    check_location(copy, original.location(), 0);
}

} // namespace

int main()
{
    try {
        formatted_failure();
        construction_and_context();
        std::cout << "error diagnostic checks passed\n";
    }
    catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
