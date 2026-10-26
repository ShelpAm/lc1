#pragma once

#include "lc1/vk/core/common.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace lc1 {

class Device;

// Owns an executable shader. Device outlives it; GPU uses finish before destruction.
class ShaderObject {
  public:
    static ShaderObject load_from_file(Device const &device, std::filesystem::path const &path,
                                       std::string const &entry, vk::ShaderStageFlagBits stage,
                                       vk::ShaderStageFlags next_stages = {});

    ShaderObject(Device const &device, std::span<std::uint32_t const> code,
                 std::string const &entry, vk::ShaderStageFlagBits stage,
                 vk::ShaderStageFlags next_stages = {});

    vk::raii::ShaderEXT const &raii() const;

  private:
    vk::raii::ShaderEXT handle_;
};

} // namespace lc1
