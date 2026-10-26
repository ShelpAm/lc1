#pragma once

#include "lc1/vk/core/common.hpp"

namespace lc1 {

// The Vulkan loader: the process's only dlopen of it, and the root of every
// raii dispatcher. The constructor also hands its vkGetInstanceProcAddr to GLFW
// through glfwInitVulkanLoader, so GLFW never loads a loader of its own.
//
// Two ordering rules, both load-bearing:
// - Construct it before Window. GLFW reads the hint only in glfwInit; after
//   that it has already dlopened libvulkan.so.1 itself.
// - It must outlive every Vulkan object, GLFW's included: ~vk::raii::Context
//   dlcloses the library that all their function pointers point into.
//
// One per process: the GLFW hint is process-wide state, so copies and moves are
// deleted.
class VulkanLoader {
  public:
    VulkanLoader();
    ~VulkanLoader();

    VulkanLoader(VulkanLoader const &) = delete;
    VulkanLoader &operator=(VulkanLoader const &) = delete;
    VulkanLoader(VulkanLoader &&) = delete;
    VulkanLoader &operator=(VulkanLoader &&) = delete;

    vk::raii::Context const &raii() const;

  private:
    vk::raii::Context context_;
};

} // namespace lc1
