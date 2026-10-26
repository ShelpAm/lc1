#include "lc1/vk/resources/descriptor-heap-cache.hpp"

namespace lc1 {
namespace {
template <typename Key, typename Allocate>
HeapIndex find_or_allocate(std::map<Key, HeapIndex> &entries, Key key, Allocate allocate)
{
    // Reserve the map node before consuming a heap slot. Allocation failure must
    // not leave a placeholder that a later lookup could mistake for a valid hit.
    auto const [entry, inserted] = entries.try_emplace(key);
    if (inserted) {
        try {
            entry->second = allocate();
        }
        catch (...) {
            entries.erase(entry);
            throw;
        }
    }
    return entry->second;
}
} // namespace

HeapIndex DescriptorHeapCache::image(GpuImage const &image)
{
    return find_or_allocate(images_, *image.view(), [&] { return heap_->allocate_image(image); });
}

HeapIndex DescriptorHeapCache::buffer(GpuBuffer const &buffer)
{
    return find_or_allocate(buffers_, *buffer.raii(),
                            [&] { return heap_->allocate_buffer(buffer); });
}

HeapIndex DescriptorHeapCache::sampler()
{
    if (!sampler_)
        sampler_ = heap_->allocate_sampler();
    return *sampler_;
}

void DescriptorHeapCache::invalidate(GpuImage const &image)
{
    images_.erase(*image.view());
}

void DescriptorHeapCache::invalidate(GpuBuffer const &buffer)
{
    buffers_.erase(*buffer.raii());
}

void DescriptorHeapCache::clear()
{
    images_.clear();
    buffers_.clear();
    sampler_.reset();
}

} // namespace lc1

namespace lc1 {

DescriptorHeapCache::DescriptorHeapCache(DescriptorHeap &heap) : heap_(&heap)
{
}

} // namespace lc1
