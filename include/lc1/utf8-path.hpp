#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace lc1 {

// File formats store paths as UTF-8, independently of the Windows ANSI code page.
inline std::filesystem::path path_from_utf8(std::string_view text)
{
    return std::filesystem::path{std::u8string{text.begin(), text.end()}};
}

} // namespace lc1
