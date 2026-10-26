#pragma once

#include "lc1/vk/resources/descriptor-heap.hpp"

#include <map>
#include <optional>

namespace lc1 {

// Render-thread-only lookup cache borrowing an unmoved DescriptorHeap. The heap
// must outlive this cache. Resources are borrowed, never kept alive by the cache.
// Invalidate before destroying/replacing a cached resource (handles can be reused).
// Separately wait for all GPU uses before destroying the resource itself.
class DescriptorHeapCache {
  public:
    explicit DescriptorHeapCache(DescriptorHeap &heap);

    DescriptorHeapCache(DescriptorHeapCache const &) = delete;
    DescriptorHeapCache(DescriptorHeapCache &&) = delete;
    DescriptorHeapCache &operator=(DescriptorHeapCache const &) = delete;
    DescriptorHeapCache &operator=(DescriptorHeapCache &&) = delete;

    ~DescriptorHeapCache() = default;

    // Same contracts as DescriptorHeap: the image's whole sampled view in
    // ShaderReadOnlyOptimal, the buffer's whole uniform range, and the heap's
    // single fixed sampler configuration. Moving a resource preserves its entry.
    HeapIndex image(GpuImage const &image);
    HeapIndex buffer(GpuBuffer const &buffer);
    HeapIndex sampler();

    // Forget lookups only: existing indices remain valid while the resource and
    // heap live. No slots are overwritten/recycled, and no GPU wait is performed.
    // A later lookup allocates a new slot in the append-only heap.
    void invalidate(GpuImage const &image);
    void invalidate(GpuBuffer const &buffer);
    void clear();

  private:
    DescriptorHeap *heap_; // Non-owning, non-null.
    // Handles are identity keys, not owning Vulkan wrappers. GpuImage's view
    // configuration and GpuBuffer's size are immutable for each live handle.
    std::map<vk::ImageView, HeapIndex> images_;
    std::map<vk::Buffer, HeapIndex> buffers_;
    std::optional<HeapIndex> sampler_;
};

} // namespace lc1
