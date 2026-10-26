#include "lc1/vk/render/frame-resources.hpp"

namespace lc1 {

FrameResources::FrameResources(std::unique_ptr<DescriptorHeap> heap, GpuBuffer camera,
                               GpuBuffer lights, GpuBuffer temporal, GpuBuffer tone_map)
    : descriptor_heap(std::move(heap)),
      descriptor_cache(std::make_unique<DescriptorHeapCache>(*descriptor_heap)),
      camera_buffer(std::move(camera)), lights_buffer(std::move(lights)),
      temporal_buffer(std::move(temporal)), tone_map_buffer(std::move(tone_map))
{
}

HeapIndex FrameResources::image_index(std::optional<HeapIndex> &slot, GpuImage const &image)
{
    if (slot)
        descriptor_heap->write_image(*slot, image);
    else
        slot = descriptor_heap->allocate_image(image);
    return *slot;
}

HeapIndex FrameResources::sampler_index(std::optional<HeapIndex> &slot,
                                        vk::SamplerCreateInfo const &info)
{
    if (slot)
        descriptor_heap->write_sampler(*slot, info);
    else
        slot = descriptor_heap->allocate_sampler(info);
    return *slot;
}

} // namespace lc1
