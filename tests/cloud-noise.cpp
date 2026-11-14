#include "lc1/cloud-noise.hpp"
#include "lc1/error.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <span>
#include <string>
#include <vector>

namespace {

void check(bool condition, std::string const &message)
{
    if (!condition) {
        std::cerr << "cloud noise: " << message << '\n';
        std::exit(1);
    }
}

std::size_t index(std::uint32_t size, std::uint32_t x, std::uint32_t y, std::uint32_t z)
{
    return static_cast<std::size_t>(x) +
           static_cast<std::size_t>(size) * (y + static_cast<std::size_t>(size) * z);
}

void rejects(std::uint32_t size, std::string const &message)
{
    bool rejected = false;
    try {
        (void)lc1::make_cloud_shape_noise(size);
    }
    catch (lc1::Error const &) {
        rejected = true;
    }
    check(rejected, message);
}

} // namespace

int main()
{
    auto const volume = lc1::make_cloud_shape_noise();
    auto const size = lc1::cloud_noise_size;
    check(volume.size() == static_cast<std::size_t>(size) * size * size,
          "the default volume must be a full cube of voxels");

    // Offline data: the same request must produce the same bytes every run.
    check(lc1::make_cloud_shape_noise() == volume, "generation must be deterministic");

    double const sum = std::accumulate(volume.begin(), volume.end(), 0.0);
    double const mean = sum / static_cast<double>(volume.size()) / 255.0;
    auto const [low, high] = std::minmax_element(volume.begin(), volume.end());
    check(mean > 0.35 && mean < 0.65, "the field must stay centred for coverage remapping");
    check(*low < 90 && *high > 165, "the field must span enough range to carve coverage");

    // Whole percent of voxels below the mid point drives how much of the sky a coverage
    // of 0.5 covers; a degenerate or clustered field would collapse to 0 or 100.
    auto const below_mid =
        std::count_if(volume.begin(), volume.end(), [](std::uint8_t value) { return value < 128; });
    double const fraction = static_cast<double>(below_mid) / static_cast<double>(volume.size());
    check(fraction > 0.3 && fraction < 0.7, "the density histogram must not be degenerate");

    // Seamless tiling: the drop across the wrap must look like any other voxel step, or
    // repeat addressing would show a grid of planes in the sky.
    double seam = 0.0;
    double interior = 0.0;
    double steps = 0.0;
    for (std::uint32_t y = 0; y < size; ++y)
        for (std::uint32_t z = 0; z < size; ++z) {
            seam += std::abs(static_cast<int>(volume[index(size, size - 1, y, z)]) -
                             static_cast<int>(volume[index(size, 0, y, z)]));
            interior += std::abs(static_cast<int>(volume[index(size, size / 2, y, z)]) -
                                 static_cast<int>(volume[index(size, size / 2 + 1, y, z)]));
            ++steps;
        }
    check(seam / steps <= interior / steps * 1.5,
          "the volume must wrap without a density discontinuity");

    // Any size the renderer might ask for stays deterministic and correctly packed.
    auto const small = lc1::make_cloud_shape_noise(16);
    check(small.size() == 16 * 16 * 16 && small == lc1::make_cloud_shape_noise(16),
          "small volumes must follow the same packing and determinism");

    rejects(4, "too few voxels per axis must be rejected");
    rejects(300, "an oversized volume must be rejected");

    std::cout << "cloud shape noise: tiling, range, histogram and determinism passed\n";
    return 0;
}
