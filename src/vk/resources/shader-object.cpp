#include "lc1/vk/resources/shader-object.hpp"

#include "lc1/vk/core/device.hpp"

#include <cstring>
#include <fstream>
#include <limits>
#include <vector>

namespace lc1 {

ShaderObject ShaderObject::load_from_file(Device const &device, std::filesystem::path const &path,
                                          std::string const &entry, vk::ShaderStageFlagBits stage,
                                          vk::ShaderStageFlags next_stages)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        fail("failed to open shader {}", std::filesystem::absolute(path).string());
    std::streamoff const size = file.tellg();
    if (size < 20 || size % static_cast<std::streamoff>(sizeof(std::uint32_t)) != 0 ||
        size > std::numeric_limits<std::streamsize>::max())
        fail("invalid SPIR-V size in {}", path.string());
    std::string bytes(static_cast<std::size_t>(size), '\0');
    file.seekg(0);
    if (!file.read(bytes.data(), static_cast<std::streamsize>(size)))
        fail("failed to read shader {}", path.string());
    // SPIR-V passed to Vulkan requires uint32_t alignment, which a string does not promise.
    std::vector<std::uint32_t> code(bytes.size() / sizeof(std::uint32_t));
    std::memcpy(code.data(), bytes.data(), bytes.size());
    return {device, code, entry, stage, next_stages};
}

ShaderObject::ShaderObject(Device const &device, std::span<std::uint32_t const> code,
                           std::string const &entry, vk::ShaderStageFlagBits stage,
                           vk::ShaderStageFlags next_stages)
    : handle_([&] {
          if (code.size() < 5 || code.front() != 0x07230203U)
              fail("shader requires valid SPIR-V words");
          if (entry.empty() || entry.find('\0') != std::string::npos)
              fail("shader entry point must be a nonempty name without embedded nulls");
          return device.raii().createShaderEXT(vk::ShaderCreateInfoEXT{
              .flags = vk::ShaderCreateFlagBitsEXT::eDescriptorHeap,
              .stage = stage,
              .nextStage = next_stages,
              .codeType = vk::ShaderCodeTypeEXT::eSpirv,
              .codeSize = code.size_bytes(),
              .pCode = code.data(),
              .pName = entry.c_str(),
              // Native descriptor heaps and push data need neither set layouts nor ranges.
          });
      }())
{
}

} // namespace lc1

namespace lc1 {

vk::raii::ShaderEXT const &ShaderObject::raii() const
{
    return handle_;
}

} // namespace lc1
