#include "lc1/vk/core/loader.hpp"

// After the Vulkan headers: glfw3.h declares glfwInitVulkanLoader only when
// VK_VERSION_1_0 is already defined.
#include <GLFW/glfw3.h>

#include <stdexcept>

namespace lc1 {

// A function-try-block, because the dlopen happens in context_'s initializer,
// before the body runs.
VulkanLoader::VulkanLoader()
try {
    glfwInitVulkanLoader(context_.getDispatcher()->vkGetInstanceProcAddr);
}
catch (std::runtime_error const &error) {
    // What vk::raii::Context throws when the dlopen fails, i.e. no Vulkan
    // loader is installed.
    fail("could not load the Vulkan loader: {}", error.what());
}

VulkanLoader::~VulkanLoader()
{
    // Runs before context_ dlcloses the library, so GLFW is never left holding
    // a pointer into unmapped memory.
    glfwInitVulkanLoader(nullptr);
}

} // namespace lc1

namespace lc1 {

vk::raii::Context const &VulkanLoader::raii() const
{
    return context_;
}

} // namespace lc1
