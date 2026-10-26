#pragma once

#include "lc1/vk/core/common.hpp"
#include "lc1/vk/resources/descriptor-heap.hpp"

#include <span>
#include <utility>

namespace lc1 {

// Owns the command buffer and tracks bindings for its current recording.
// The allocating command pool and Device must outlive this object.
class CommandBuffer {
  public:
    CommandBuffer(vk::raii::CommandBuffer &&handle);

    // Use the wrapper for heap binding and reset. After external heap binding
    // through this handle, invalidate_bindings() before using cached bindings.
    vk::raii::CommandBuffer &raii();

    void begin(vk::CommandBufferUsageFlags usage);

    void end();

    // The caller must wait for completion before resetting this command buffer.
    void reset();

    void bind_descriptor_heap(DescriptorHeap const &heap);

    void bind_shaders(std::span<vk::ShaderStageFlagBits const> stages,
                      std::span<vk::ShaderEXT const> shaders);

    template <typename T> void push_data(std::span<T> data);

  private:
    vk::raii::CommandBuffer handle_;
    DescriptorHeap const *bound_heap_ = nullptr;
};

template <typename T> inline void CommandBuffer::push_data(std::span<T> data)
{
    if (!bound_heap_)
        fail("CommandBuffer::push_data: no heap bound to this command buffer");

    if (data.empty() || data.size_bytes() % 4 != 0 ||
        data.size_bytes() > bound_heap_->descriptor_heap_properties_.maxPushDataSize)
        fail("CommandBuffer::push_data: size must be nonzero, a multiple of 4, and within "
             "maxPushDataSize");
    vk::PushDataInfoEXT push_data_info{
        .data = {.address = data.data(), .size = data.size_bytes()},
    };
    handle_.pushDataEXT(push_data_info);
}

} // namespace lc1
