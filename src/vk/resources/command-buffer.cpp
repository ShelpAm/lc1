#include "lc1/vk/resources/command-buffer.hpp"

namespace lc1 {

CommandBuffer::CommandBuffer(vk::raii::CommandBuffer &&handle) : handle_(std::move(handle))
{
}

vk::raii::CommandBuffer &CommandBuffer::raii()
{
    return handle_;
}

void CommandBuffer::begin(vk::CommandBufferUsageFlags usage)
{
    handle_.begin(vk::CommandBufferBeginInfo{
        .flags = usage,
    });
}

void CommandBuffer::end()
{
    handle_.end();
}

void CommandBuffer::reset()
{
    handle_.reset({});
    bound_heap_ = nullptr;
}

void CommandBuffer::bind_descriptor_heap(DescriptorHeap const &heap)
{
    if (bound_heap_ != &heap) {
        heap.bind_to_command_buffer(*this);
        bound_heap_ = &heap;
    }
}

void CommandBuffer::bind_shaders(std::span<vk::ShaderStageFlagBits const> stages,
                                 std::span<vk::ShaderEXT const> shaders)
{
    if (stages.size() != shaders.size())
        fail("CommandBuffer::bind_shaders: stage and shader counts differ");
    handle_.bindShadersEXT(stages, shaders);
}

} // namespace lc1
