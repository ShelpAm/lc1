#pragma once
#include "lc1/vk/resources/command-buffer.hpp"
#include "lc1/vk/resources/image-barrier.hpp"
#include <chrono>
#include <cstdio>

#include "lc1/vk/core/common.hpp"
#include "lc1/vk/core/device.hpp"
#include "lc1/vk/presentation/swapchain.hpp"
#include "lc1/vk/resources/render-target.hpp"

#include <cstdint>
#include <functional>
#include <print>
#include <vector>

namespace lc1 {

class Device;
class Swapchain;

enum class FrameResult : std::uint8_t {
    Ok,
    // OUT_OF_DATE or SUBOPTIMAL. The caller recreates at the top of the next
    // frame -- never mid-frame.
    RecreateRequested,
    // Not recoverable in this milestone.
    SurfaceLost,
};

// The swapchain/WSI state machine: wait, acquire, submit, present. Owns the
// command buffer's recording boundaries and the swapchain image's layout
// transitions around the caller's recording callback.
//
// Owns only per-FRAME resources (fences, image-available semaphores, command
// buffers), plus one opaque GpuResource per slot. Per-IMAGE resources live in SwapchainImage, so
// swapchain recreation never has to reach into this class -- which is what makes "recreation forgot
// to rebuild X" hard to write.
//
// Must not outlive the Device it was constructed from.
template <typename FrameResource> class FrameLoop {
  public:
    using RecordCallback =
        std::function<void(CommandBuffer &, FrameResource &, RenderTarget const &)>;
    static constexpr uint32_t max_frames_in_flight = 2;

    // The queue comes from the Device: there is exactly one vk::raii::Queue in
    // the program, and it belongs to the Device.
    // Factory is called once per slot during construction and is not retained.
    FrameLoop(Device const &device, std::function<FrameResource()> const &make_resource);

    std::uint32_t frame_count() const
    {
        assert(static_cast<std::uint32_t>(frames_.size()) == max_frames_in_flight);
        return max_frames_in_flight;
    }

    // Calls record synchronously after this slot's fence has completed, with
    // an already recording command buffer and a COLOR_ATTACHMENT_OPTIMAL target.
    // record must leave the target in that layout and close any rendering pass.
    // The callback is not retained. If it throws, stop drawing and wait for the
    // device before releasing resources; the acquired image is not presented.
    FrameResult draw_frame(Swapchain &swapchain, RecordCallback const &record);

  private:
    // Everything one frame in flight owns. The per-FRAME counterpart of
    // SwapchainImage: grouping them makes "slot i's fence guards slot i's command
    // buffer" a fact about the type rather than about three parallel indices.
    struct Frame {
        CommandBuffer command_buffer;
        // Signaled by acquire, waited on by this frame's submit.
        vk::raii::Semaphore image_available;
        // Signaled when this frame's submit finishes on the GPU; waited on before
        // the slot is reused.
        vk::raii::Fence in_flight; // Signaled when available

        FrameResource gpu_resource; // Reused only after this slot's fence completes.
    };

    // Non-owning, non-null: the constructor takes a reference; Device must outlive this loop.
    Device const *device_;

    vk::raii::CommandPool command_pool_{nullptr};
    // A vector rather than std::array: raii handles have no default
    // constructor. Sized to frames_in_flight in the constructor.
    std::vector<Frame> frames_;

    std::uint32_t frame_index_ = 0;
};

} // namespace lc1

namespace lc1 {
namespace {

// Bounded timeouts, not UINT64_MAX: an unbounded wait makes a deadlock
// indistinguishable from a slow frame or a hung GPU, and leaves no way out.
constexpr uint64_t fence_timeout_ns = 1'000'000'000ULL;   // 1s
constexpr uint64_t acquire_timeout_ns = 1'000'000'000ULL; // 1s

} // namespace

template <typename GpuResource>
FrameLoop<GpuResource>::FrameLoop(Device const &device,
                                  std::function<GpuResource()> const &make_resource)
    : device_{&device}
{
    if (!make_resource) {
        fail("frame resource factory is empty");
    }
    vk::raii::Device const &raii_device = device_->raii();

    command_pool_ = raii_device.createCommandPool({
        // RESET_COMMAND_BUFFER_BIT lets us re-begin buffers each frame without
        // reallocating; re-recording every frame is this milestone's model.
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = device_->queue_family(),
    });

    auto command_buffers = raii_device.allocateCommandBuffers({
        .commandPool = command_pool_,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = max_frames_in_flight,
    });

    frames_.reserve(max_frames_in_flight);
    for (auto &command_buffer : command_buffers) {
        frames_.push_back(Frame{
            .command_buffer = std::move(command_buffer),
            .image_available = raii_device.createSemaphore({}),
            // Start signaled so the first wait on each slot returns immediately
            // instead of blocking on a fence that was never submitted.
            .in_flight = raii_device.createFence({.flags = vk::FenceCreateFlagBits::eSignaled}),
            .gpu_resource = make_resource(),
        });
    }
}

template <typename GpuResource>
FrameResult FrameLoop<GpuResource>::draw_frame(Swapchain &swapchain, RecordCallback const &record)
{
    if (!record) {
        fail("frame recording callback is empty");
    }
    using ProbeClock = std::chrono::steady_clock;
    auto const probe_start = ProbeClock::now();
    Frame &frame = frames_[frame_index_];
    auto const &device = device_->raii();
    auto const &queue = device_->raii_queue();

    try {
        // Returns a Result instead of throwing: VK_TIMEOUT is a legal outcome
        // here, not a failure.
        vk::Result const wait_result =
            device.waitForFences(*frame.in_flight, vk::True, fence_timeout_ns);
        if (wait_result == vk::Result::eTimeout) {
            fail("fence wait timed out on slot {} -- deadlock or GPU hang. The usual cause is "
                 "vkResetFences on a path that never reaches vkQueueSubmit.",
                 frame_index_);
        }
        if (wait_result != vk::Result::eSuccess) {
            fail("vkWaitForFences failed: {}", result_string(wait_result));
        }

        // On a non-success result the driver left the index unwritten, so
        // .value must not be read before .result has been checked.
        auto const probe_fence = ProbeClock::now();
        auto const acquired =
            swapchain.raii().acquireNextImage(acquire_timeout_ns, *frame.image_available, nullptr);

        if (acquired.result == vk::Result::eErrorOutOfDateKHR) {
            // No image was acquired and both the semaphore and the fence are
            // left unaffected, so skipping the frame is safe. The fence stays
            // signaled; it gets reset on the next use of this slot, immediately
            // before the submit.
            return FrameResult::RecreateRequested;
        }
        // SURFACE_LOST is not in this list because it never gets here: it is
        // not among hpp's success codes for this call, so it arrives as a
        // vk::SurfaceLostKHRError and is caught below.
        if (acquired.result != vk::Result::eSuccess &&
            acquired.result != vk::Result::eSuboptimalKHR) {
            fail("vkAcquireNextImageKHR failed: {}", result_string(acquired.result));
        }
        auto const image_index = acquired.value;
        auto const probe_acquire = ProbeClock::now();

        // VK_SUBOPTIMAL_KHR means an image WAS acquired and
        // frame.image_available IS signaled. Render and present it normally
        // -- returning early would abandon a signaled semaphore, and the next
        // acquire handed that same handle trips
        // VUID-vkAcquireNextImageKHR-semaphore-01286. Recreate after
        // presenting.
        bool const recreate_after_present = (acquired.result == vk::Result::eSuboptimalKHR);

        // Reset THIS slot's command buffer, never the whole pool.
        // vkResetCommandPool resets every buffer allocated from the pool,
        // including the other in-flight frame's -- which is still pending, and
        // trips
        //   VUID-vkResetCommandPool-commandPool-00040: ... is in use
        // Waiting on frame.in_flight only proves this slot is free. This is
        // exactly why the pool is created with
        // VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT.
        frame.command_buffer.reset();
        frame.command_buffer.begin(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);

        SwapchainImage const &image = swapchain.images()[image_index];

        // UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL.
        //
        // srcStageMask must be COLOR_ATTACHMENT_OUTPUT, matching the stage at
        // which submit waits on the acquire semaphore. NONE leaves this layout
        // transition unordered against acquire, and synchronization validation
        // reports SYNC-HAZARD-WRITE-AFTER-READ.
        // srcAccessMask stays NONE: the presentation engine's read is not
        // tracked by access masks. This is an execution-only dependency.
        transition_image_layout(
            frame.command_buffer.raii(), image.image, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlags2{},
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::AccessFlagBits2::eColorAttachmentWrite, vk::ImageAspectFlagBits::eColor);

        RenderTarget const target{
            .view = &image.view,
            .extent = swapchain.extent(),
        };

        // The fence waited on above protects this slot's drawing resources.
        record(frame.command_buffer, frame.gpu_resource, target);

        // COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR.
        // Destination stage/access are NONE, not BOTTOM_OF_PIPE: presentation
        // is not a pipeline stage. The submit -> present semaphore pair orders
        // this write against the presentation engine's read.
        transition_image_layout(
            frame.command_buffer.raii(), image.image, vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::ePresentSrcKHR, vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2{}, // VK_PIPELINE_STAGE_2_NONE
            vk::AccessFlags2{},           // VK_ACCESS_2_NONE
            vk::ImageAspectFlagBits::eColor);

        frame.command_buffer.end();

        vk::SemaphoreSubmitInfo wait_info{
            .semaphore = frame.image_available,
            // The first thing that touches the image is the layout transition and the attachment
            // write, both in the color-attachment-output scope.
            .stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        };
        vk::CommandBufferSubmitInfo command_info{
            .commandBuffer = frame.command_buffer.raii(),
        };
        vk::SemaphoreSubmitInfo signal_info{
            .semaphore = image.render_finished,
            .stageMask = vk::PipelineStageFlagBits2::eAllCommands,
        };

        // Reset only after recording succeeds, immediately before submitting.
        // An early return or a throwing callback must not leave an unsignaled
        // fence with no submission to signal it.
        device.resetFences(*frame.in_flight);
        queue.submit2(vk::SubmitInfo2{}
                          .setWaitSemaphoreInfos(wait_info)
                          .setCommandBufferInfos(command_info)
                          .setSignalSemaphoreInfos(signal_info),
                      *frame.in_flight);

        auto const probe_submit = ProbeClock::now();

        // Never present without the submit above: the wait semaphore must
        // reference a signal operation that was actually submitted.
        auto wait_semaphores = std::array{*image.render_finished};
        auto swapchains = std::array{*swapchain.raii()};
        auto const presented = queue.presentKHR(vk::PresentInfoKHR{}
                                                    .setWaitSemaphores(wait_semaphores)
                                                    .setSwapchains(swapchains)
                                                    .setImageIndices(image_index));
        auto const probe_present = ProbeClock::now();
        static unsigned probe_count = 0;
        static double probe_sums[4]{};
        probe_sums[0] +=
            std::chrono::duration<double, std::milli>(probe_fence - probe_start).count();
        probe_sums[1] +=
            std::chrono::duration<double, std::milli>(probe_acquire - probe_fence).count();
        probe_sums[2] +=
            std::chrono::duration<double, std::milli>(probe_submit - probe_acquire).count();
        probe_sums[3] +=
            std::chrono::duration<double, std::milli>(probe_present - probe_submit).count();
        if (++probe_count == 120) {
            std::println(
                stderr, "PROBE fence {:.2f} acquire {:.2f} record+submit {:.2f} present {:.2f} ms",
                probe_sums[0] / 120, probe_sums[1] / 120, probe_sums[2] / 120, probe_sums[3] / 120);
            probe_count = 0;
            for (auto &value : probe_sums)
                value = 0;
        }
        if (presented == vk::Result::eErrorOutOfDateKHR ||
            presented == vk::Result::eSuboptimalKHR) {
            return FrameResult::RecreateRequested;
        }
        if (presented != vk::Result::eSuccess) {
            fail("vkQueuePresentKHR failed: {}", result_string(presented));
        }

        frame_index_ = (frame_index_ + 1) % max_frames_in_flight;
        return recreate_after_present ? FrameResult::RecreateRequested : FrameResult::Ok;
    }
    catch (vk::SurfaceLostKHRError const &) {
        return FrameResult::SurfaceLost;
    }
}

} // namespace lc1
