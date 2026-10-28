#include "application.hpp"

#include "lc1/error.hpp"
#include "lc1/game-clock.hpp"
#include "lc1/vk/presentation/swapchain-policy.hpp"
#include "lc1/vk/render/render-graph.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>

namespace lc1::app {
namespace {

constexpr double event_wait_timeout_seconds = 0.1;

Instance make_instance(VulkanLoader const &loader, Window const &window, bool enable_validation)
{
    auto extensions = window.required_instance_extensions();
    std::vector<char const *> layers;
    if (enable_validation) {
        extensions.push_back(vk::EXTDebugUtilsExtensionName);
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }
    Instance instance{loader, std::move(extensions), std::move(layers)};
    if (enable_validation)
        instance.setup_debug_messenger();
    return instance;
}

std::optional<uint32_t> find_graphics_present_family(vk::raii::PhysicalDevice const &device,
                                                     vk::SurfaceKHR surface)
{
    auto const families = device.getQueueFamilyProperties();
    for (uint32_t i = 0; i < static_cast<uint32_t>(families.size()); ++i) {
        if (!(families[i].queueFlags & vk::QueueFlagBits::eGraphics) ||
            !(families[i].queueFlags & vk::QueueFlagBits::eCompute))
            continue;
        if (device.getSurfaceSupportKHR(i, surface) == vk::False) {
            continue;
        }
        return i;
    }
    return std::nullopt;
}

int score_device(vk::PhysicalDeviceType type)
{
    switch (type) {
    case vk::PhysicalDeviceType::eDiscreteGpu:
        return 300;
    case vk::PhysicalDeviceType::eIntegratedGpu:
        return 200;
    case vk::PhysicalDeviceType::eVirtualGpu:
        return 100;
    default:
        return 50;
    }
}

struct PickedDevice {
    vk::raii::PhysicalDevice *device;
    std::uint32_t queue_family;
};

PickedDevice pick_physical_device(std::vector<vk::raii::PhysicalDevice> &devices,
                                  vk::SurfaceKHR surface)
{
    char const *const want = std::getenv("LC1_DEVICE");
    bool const filtered = want != nullptr && *want != '\0';

    PickedDevice best{.device = nullptr, .queue_family = 0};
    int best_score = -1; // negative means nothing has been picked yet

    for (auto &device : devices) {
        auto const props = device.getProperties();

        // llvmpipe is enumerated as a real physical device here, and nothing
        // reorders devices for us: VK_LAYER_NV_optimus is dormant unless
        // __NV_PRIME_RENDER_OFFLOAD=1 is set, and VK_LAYER_MESA_device_select
        // only filters. Excluding CPU outright is what stops this game from
        // silently rendering on the CPU rasterizer.
        if (props.deviceType == vk::PhysicalDeviceType::eCpu)
            continue;
        if (props.apiVersion < vk::ApiVersion14)
            continue;
        if (filtered && std::strstr(props.deviceName, want) == nullptr)
            continue;

        auto const family = find_graphics_present_family(device, surface);
        if (!family)
            continue;

        int const score = score_device(props.deviceType);
        if (score > best_score) {
            best_score = score;
            best = {.device = &device, .queue_family = *family};
        }
    }

    // best_score, not the handle, says whether anything was picked: a raii
    // wrapper offers no null-handle comparison.
    if (best_score < 0) {
        if (filtered) {
            fail("LC1_DEVICE=\"{}\" matched no usable device", want);
        }
        fail("no suitable Vulkan 1.4 device found (need a non-CPU device with "
             "ray query, acceleration structures, buffer device address, all requested features, "
             "and a graphics+compute+present queue family)");
    }
    return best;
}

Device make_device(Instance const &instance, Surface const &surface)
{
    DeviceRequirements requirements{
        .vulkan_version = vk::ApiVersion14,
        .extensions =
            {
                vk::KHRSwapchainExtensionName,

                vk::KHRAccelerationStructureExtensionName,
                vk::KHRRayQueryExtensionName,

                vk::KHRDeferredHostOperationsExtensionName,

                vk::EXTDescriptorHeapExtensionName,
                vk::KHRShaderUntypedPointersExtensionName,

                vk::EXTShaderObjectExtensionName,
            },
        .features =
            {
                &vk::PhysicalDeviceFeatures::sampleRateShading,
                &vk::PhysicalDeviceFeatures::samplerAnisotropy,
                // Slang loads heap acceleration-structure addresses as 64-bit integers.
                &vk::PhysicalDeviceFeatures::shaderInt64,
                &vk::PhysicalDeviceVulkan11Features::shaderDrawParameters,
                &vk::PhysicalDeviceVulkan12Features::bufferDeviceAddress,
                &vk::PhysicalDeviceVulkan13Features::synchronization2,
                &vk::PhysicalDeviceVulkan13Features::dynamicRendering,
                &vk::PhysicalDeviceVulkan14Features::maintenance5,
                &vk::PhysicalDeviceAccelerationStructureFeaturesKHR::accelerationStructure,
                &vk::PhysicalDeviceRayQueryFeaturesKHR::rayQuery,
                &vk::PhysicalDeviceDescriptorHeapFeaturesEXT::descriptorHeap,
                &vk::PhysicalDeviceShaderUntypedPointersFeaturesKHR::shaderUntypedPointers,
                &vk::PhysicalDeviceShaderObjectFeaturesEXT::shaderObject,
            },
    };
    auto [devices, unavailable_reasons] = filter_physical_devices(instance, requirements);
    if (devices.empty())
        fail("no Vulkan physical device meets the requirements");
    for (auto const &[dev, reasons] : unavailable_reasons) {
        spdlog::info("device {} unavailable, reasons:", dev);
        for (auto const &reason : reasons)
            spdlog::info("\t- {}", reason);
    }
    auto picked = pick_physical_device(devices, surface.raii());
    return Device{instance, std::move(*picked.device), picked.queue_family, requirements};
}

OutputSettings output_settings(vk::Format format)
{
    return {.encoding = format == vk::Format::eR8G8B8A8Unorm || format == vk::Format::eB8G8R8A8Unorm
                            ? OutputEncoding::Srgb
                            : OutputEncoding::Linear};
}

// The imgui Vulkan backend is compiled with IMGUI_IMPL_VULKAN_NO_PROTOTYPES (see
// third-party/CMakeLists.txt): every vk* it calls goes through a function-pointer
// table it asks us to fill in. Fill it from the same dlopened loader the raii
// dispatchers come from -- the loader's trampolines answer for device-level
// commands too, so one instance-level lookup covers the backend's whole map.
struct ImGuiVulkanFunctions {
    PFN_vkGetInstanceProcAddr get_instance_proc_addr;
    VkInstance instance;
};

PFN_vkVoidFunction load_imgui_vulkan_function(char const *name, void *user_data)
{
    auto const *functions = static_cast<ImGuiVulkanFunctions *>(user_data);
    return functions->get_instance_proc_addr(functions->instance, name);
}

} // namespace

Application::Application(ApplicationConfig const &config)
    : window_{1280, 720, "lc1"},
      instance_{make_instance(loader_, window_, config.enable_validation)},
      surface_{instance_, window_}, device_{make_device(instance_, surface_)},
      surface_format_{
          pick_surface_format(device_.raii_physical().getSurfaceFormatsKHR(surface_.raii()))},
      renderer_{device_,
                {surface_format_.format},
                std::min(device_.max_sample_count(), vk::SampleCountFlagBits::e4)},
      asset_root_(config.asset_root),
      game_(std::make_unique<GameSession>(device_, asset_root_, Save{config.save_root},
                                          output_settings(surface_format_.format), &window_)),
      frame_loop_(std::make_unique<FrameLoop<FrameResources>>(device_, [this] {
          return renderer_.make_frame_resources(game_->world_presentation().object_capacity());
      }))
{
    spdlog::info("[world] WASD: walk, wheel: zoom, Shift: run, Y: camera lock, Space: "
                 "recenter, M: first-person/oblique view, Esc: quit");
    spdlog::info("[showcase] material exhibits beside the spawn road; F4: lighting, "
                 "F5: pause motion, PageUp/PageDown: exposure");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    // The game owns the keyboard; ImGui takes it only while one of its widgets is active
    // (WantCaptureKeyboard). Keyboard navigation is off for two reasons: it consumes the
    // arrows and Space the character uses, and a merely focused window -- the always-open
    // debug panel included -- then reports WantCaptureKeyboard on every frame, which the
    // session reads as "the game gets no keys". Gamepad navigation cannot capture the
    // keyboard and stays available; ConfigNavCaptureKeyboard stays false so re-enabling
    // keyboard navigation later cannot mute gameplay again by focus alone.
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigNavCaptureKeyboard = false;
    if (!ImGui_ImplGlfw_InitForVulkan(window_.handle(), true)) {
        ImGui::DestroyContext();
        fail("the imgui GLFW backend could not initialize");
    }
}

void Application::init_imgui_vulkan(std::uint32_t min_image_count)
{
    auto const ui_format = static_cast<VkFormat>(swapchain_->format());
    ImGuiVulkanFunctions imgui_functions{
        .get_instance_proc_addr = loader_.raii().getDispatcher()->vkGetInstanceProcAddr,
        .instance = instance_.handle(),
    };
    if (!ImGui_ImplVulkan_LoadFunctions(vk::ApiVersion14, &load_imgui_vulkan_function,
                                        &imgui_functions))
        fail("the imgui Vulkan backend could not load a required Vulkan function");
    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.ApiVersion = vk::ApiVersion14;
    init_info.Instance = instance_.handle();
    init_info.PhysicalDevice = *device_.raii_physical();
    init_info.Device = *device_.raii();
    init_info.QueueFamily = device_.queue_family();
    init_info.Queue = *device_.raii_queue();
    // Leave DescriptorPool null: the backend owns the automatically created pool.
    init_info.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE;
    init_info.MinImageCount = min_image_count;
    init_info.ImageCount = static_cast<std::uint32_t>(swapchain_->images().size());
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    auto &rendering = init_info.PipelineInfoMain.PipelineRenderingCreateInfo;
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &ui_format;
    init_info.UseDynamicRendering = true;
    init_info.CheckVkResultFn = [](VkResult result) {
        if (result < 0)
            fail("imgui Vulkan operation failed: {}", static_cast<int>(result));
    };
    if (!ImGui_ImplVulkan_Init(&init_info))
        fail("the imgui Vulkan backend could not initialize");
    imgui_image_count_ = static_cast<std::uint32_t>(swapchain_->images().size());
}

Application::~Application()
{
    // Initialization may never run (for example, while the window is minimized).
    // Check backend ownership too, so a partially failed Init is cleaned up.
    if (ImGui::GetIO().BackendRendererUserData)
        ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

bool Application::prepare_swapchain()
{
    // Consume every resize event, even when recreation is already pending.
    bool const framebuffer_resized = window_.take_resize_event();
    recreate_requested_ = recreate_requested_ || framebuffer_resized;
    if (swapchain_ && !recreate_requested_)
        return true;

    auto const caps = device_.raii_physical().getSurfaceCapabilitiesKHR(surface_.raii());
    auto const framebuffer = window_.framebuffer_extent();
    vk::Extent2D extent{};
    if (!compute_extent({.width = framebuffer.width, .height = framebuffer.height}, caps,
                        &extent)) {
        // Keep creation/recreation pending. Bounded waits also observe shutdown
        // signals when no further window event arrives.
        window_.wait_events(event_wait_timeout_seconds);
        return false;
    }

    auto const modes = device_.raii_physical().getSurfacePresentModesKHR(surface_.raii());
    SwapchainConfig const config{
        .surface_format = surface_format_,
        .present_mode = pick_present_mode(modes),
        .min_image_count = pick_image_count(caps),
        .composite_alpha = pick_composite_alpha(caps.supportedCompositeAlpha),
    };
    if (config.min_image_count < 2)
        fail("the imgui Vulkan backend requires at least two swapchain images");
    if (swapchain_)
        swapchain_->recreate(config, extent);
    else
        swapchain_.emplace(device_, surface_, config, extent);
    // SetMinImageCount does not update the backend's actual ImageCount.
    // Reinitialize when that count changes; recreation has already drained the GPU.
    if (imgui_image_count_ != 0 && imgui_image_count_ != swapchain_->images().size()) {
        ImGui_ImplVulkan_Shutdown();
        imgui_image_count_ = 0;
    }
    if (imgui_image_count_ == 0)
        init_imgui_vulkan(config.min_image_count);
    else
        ImGui_ImplVulkan_SetMinImageCount(config.min_image_count);
    recreate_requested_ = false;
    game_->set_aspect_ratio(static_cast<float>(extent.width) / static_cast<float>(extent.height));
    return true;
}

void Application::load_game(std::filesystem::path const &directory)
{
    // Drain all slots before creating uploads or releasing any resources borrowed by a frame.
    device_.wait_idle();
    try {
        auto next = std::make_unique<GameSession>(device_, asset_root_, Save{directory},
                                                  game_->world_presentation().output(), &window_);
        auto frames = std::make_unique<FrameLoop<FrameResources>>(device_, [&] {
            return renderer_.make_frame_resources(next->world_presentation().object_capacity());
        });
        auto const extent = window_.framebuffer_extent();
        if (extent.height != 0)
            next->set_aspect_ratio(static_cast<float>(extent.width) /
                                   static_cast<float>(extent.height));
        next->resume_loaded();
        // Old per-frame snapshots must die before the session resources they borrow.
        frame_loop_.reset();
        game_ = std::move(next);
        frame_loop_ = std::move(frames);
    }
    catch (Error const &error) {
        spdlog::error("[load] {}", error.diagnostic());
        game_->load_failed(std::format("Load failed: {}", error.what()));
    }
    catch (vk::SystemError const &) {
        throw;
    }
    catch (std::exception const &error) {
        spdlog::error("[load] {}", error.what());
        game_->load_failed(std::format("Load failed: {}", error.what()));
    }
}

void Application::run_loop(std::function<bool()> const &stop_requested)
{
    Stopwatch stopwatch;
    // Construct once; FrameLoop invokes the callback synchronously and never retains it.
    FrameLoop<FrameResources>::RecordCallback const record = [this](CommandBuffer &command_buffer,
                                                                    FrameResources &resources,
                                                                    RenderTarget const &target) {
        auto const &presentation = game_->world_presentation();
        auto const &camera = game_->player_view().camera();
        // Text items own their strings and remain alive until graph.execute() returns.
        TextFrameInput const text{
            .font = &presentation.font(),
            .world = presentation.world_labels(camera),
            .screen =
                game_->ui().screen_labels(game_->world(), presentation.font(),
                                          window_.framebuffer_extent(), presentation.location()),
        };
        RenderGraph graph;
        renderer_.add_passes(graph, resources, target, presentation.scene(), camera,
                             presentation.output(), text);
        graph.add_pass(
            "ImGui overlay", {"output.ui_text"}, {"output.ui"}, [target](CommandBuffer &commands) {
                // Make the UI text's writes visible to this attachment load and blending.
                vk::MemoryBarrier2 const barrier{
                    .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                    .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
                    .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                    .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentRead |
                                     vk::AccessFlagBits2::eColorAttachmentWrite,
                };
                commands.raii().pipelineBarrier2(vk::DependencyInfo{}.setMemoryBarriers(barrier));
                vk::RenderingAttachmentInfo const color{
                    .imageView = **target.view,
                    .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                    .loadOp = vk::AttachmentLoadOp::eLoad,
                    .storeOp = vk::AttachmentStoreOp::eStore,
                };
                vk::RenderingInfo rendering{
                    .renderArea = {.offset = {.x = 0, .y = 0}, .extent = target.extent},
                    .layerCount = 1,
                };
                rendering.setColorAttachments(color);
                commands.raii().beginRendering(rendering);
                ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), *commands.raii());
                commands.raii().endRendering();
            });
        if (auto result = graph.compile(); !result)
            fail("Cannot compile frame render graph: {}", result.error());
        graph.execute(command_buffer);
    };

    while (!window_.should_close() && !stop_requested()) {
        window_.poll_events();
        if (window_.should_close() || stop_requested())
            break;

        if (auto directory = game_->take_load_request()) {
            load_game(*directory);
            stopwatch.tick(); // Loading time does not advance the new world's simulation.
        }

        // Recreate only at the top of a frame, before acquiring an image.
        if (!prepare_swapchain())
            continue;

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        auto const delta_seconds = stopwatch.tick();
        game_->update(delta_seconds, window_.input());
        if (game_->quit_requested())
            break;

        ImGui::Render();

        switch (frame_loop_->draw_frame(*swapchain_, record)) {
        case FrameResult::Ok:
            break;
        case FrameResult::RecreateRequested:
            recreate_requested_ = true;
            break;
        case FrameResult::SurfaceLost:
            fail("VK_ERROR_SURFACE_LOST_KHR: the presentation surface is gone "
                 "and is not recoverable in this milestone");
        }
    }
}

void Application::run(std::function<bool()> const &stop_requested)
{
    try {
        run_loop(stop_requested);
    }
    catch (...) {
        // Another frame slot may still reference the game's meshes/materials.
        device_.wait_idle();
        throw;
    }
    device_.wait_idle();
}

} // namespace lc1::app
