#pragma once

#include "lc1/vk/render/renderer.hpp"

#include "lc1/game/session/game-session.hpp"
#include "lc1/game/session/save.hpp"

#include "lc1/game/actions.hpp"
#include "lc1/platform/window.hpp"
#include "lc1/vk/core/device.hpp"
#include "lc1/vk/core/instance.hpp"
#include "lc1/vk/core/loader.hpp"
#include "lc1/vk/presentation/frame_loop.hpp"
#include "lc1/vk/presentation/surface.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

namespace lc1::app {

struct ApplicationConfig {
    bool enable_validation = true;
    std::filesystem::path asset_root = "runtime/assets";
    std::filesystem::path save_root = "runtime/saves/01";
};

// Owns platform/graphics lifetime, input routing and presentation. GameSession owns the local
// prototype's world, presentation and in-session UI through separate owners.
class Application {
  public:
    explicit Application(ApplicationConfig const &config);
    Application(Application const &) = delete;
    Application &operator=(Application const &) = delete;
    Application(Application &&) = delete;
    Application &operator=(Application &&) = delete;
    ~Application();

    // Drains the GPU on both normal and exceptional exits, before any member dies.
    void run(std::function<bool()> const &stop_requested);

  private:
    bool prepare_swapchain();
    void load_game(std::filesystem::path const &directory);
    void init_imgui_vulkan(std::uint32_t min_image_count);
    void run_loop(std::function<bool()> const &stop_requested);

    // Declaration order is the lifetime contract: the loader dies last, Window
    // outlives the surface, and all GPU objects die before Device/Instance.
    VulkanLoader loader_;
    Window window_;
    Instance instance_;
    Surface surface_;
    Device device_;
    vk::SurfaceFormatKHR const surface_format_;
    std::optional<Swapchain> swapchain_;
    Renderer renderer_;
    // The frame factory sizes GPU resources from the session's current draw count.
    // Keep the session alive until all frame resources have been destroyed.
    std::filesystem::path asset_root_;
    std::unique_ptr<GameSession> game_;
    std::unique_ptr<FrameLoop<FrameResources>> frame_loop_;
    bool recreate_requested_ = false;
    std::uint32_t imgui_image_count_ = 0;
};

} // namespace lc1::app
