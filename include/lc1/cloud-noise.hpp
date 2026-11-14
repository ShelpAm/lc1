#pragma once

#include <cstdint>
#include <vector>

namespace lc1 {

// Voxels per axis of the default cloud shape volume. The volume tiles seamlessly,
// so a shader samples it with repeat addressing instead of clamping at the edges.
constexpr std::uint32_t cloud_noise_size = 128;
constexpr std::uint32_t cloud_detail_noise_size = 64;

// Deterministic, tileable Perlin-Worley field: tightly packed R8_UNORM voxels,
// x-fastest, at exactly `size` voxels per axis.
//
// This is offline data, not a simulation: the same size always produces the same
// bytes, so the shape of a cloud layer is reproducible across runs and machines.
// Generation is CPU work done once when the renderer is constructed; callers must
// not run it per frame.
std::vector<std::uint8_t> make_cloud_shape_noise(std::uint32_t size = cloud_noise_size);

// Independent periodic Worley octaves: RG8_UNORM, x-fastest. R contains medium
// billows, G finer edge erosion; bright values are near cell centres.
std::vector<std::uint8_t> make_cloud_detail_noise(std::uint32_t size = cloud_detail_noise_size);

} // namespace lc1
