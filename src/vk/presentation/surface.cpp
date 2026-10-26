#include "lc1/vk/presentation/surface.hpp"

#include "lc1/vk/core/instance.hpp"
#include "lc1/window.hpp"

#include <GLFW/glfw3.h>

namespace lc1 {
namespace {

vk::raii::SurfaceKHR make_surface(Instance const &instance, Window const &window)
{
    // GLFW requires the C API's output handle. Adopt it immediately into RAII.
    VkSurfaceKHR raw_surface = VK_NULL_HANDLE;
    VK_CHECK(glfwCreateWindowSurface(instance.handle(), window.handle(), nullptr, &raw_surface));
    return vk::raii::SurfaceKHR{instance.raii(), raw_surface};
}

} // namespace

Surface::Surface(Instance const &instance, Window const &window)
    : surface_{make_surface(instance, window)}
{
}

} // namespace lc1

namespace lc1 {

vk::raii::SurfaceKHR const &Surface::raii() const
{
    return surface_;
}

} // namespace lc1
