#include "lc1/cloud-noise.hpp"

#include "lc1/error.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace lc1 {
namespace {

// 32-bit integer hash: one finaliser, no table, so the field is identical on every
// platform and every run. Values are taken from the high bits of the mixed word.
constexpr std::uint32_t mix(std::uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;
    return value;
}

constexpr std::uint32_t lattice_hash(std::uint32_t x, std::uint32_t y, std::uint32_t z,
                                     std::uint32_t seed)
{
    return mix(x ^ mix(y ^ mix(z ^ seed)));
}

// Uniform value in [0, 1) per lattice point.
float lattice_value(std::uint32_t x, std::uint32_t y, std::uint32_t z, std::uint32_t seed)
{
    return static_cast<float>(lattice_hash(x, y, z, seed) >> 8) * (1.0F / 16777216.0F);
}

float fade(float value)
{
    return value * value * (3.0F - 2.0F * value);
}

// Per-axis sample positions of one periodic lattice, folded into wrap-safe indices
// and interpolation weights. Every voxel then costs lookups instead of hashes:
// generating the volume is pure integer and float arithmetic, which is what keeps
// this affordable in a Debug build.
struct AxisKernel {
    std::vector<std::uint32_t> low_index;
    std::vector<std::uint32_t> high_index;
    std::vector<float> low_weight;
    std::vector<float> high_weight;
};

AxisKernel make_axis_kernel(std::uint32_t samples, std::uint32_t period)
{
    AxisKernel kernel;
    kernel.low_index.resize(samples);
    kernel.high_index.resize(samples);
    kernel.low_weight.resize(samples);
    kernel.high_weight.resize(samples);
    auto const scale = static_cast<float>(period) / static_cast<float>(samples);
    for (std::uint32_t i = 0; i < samples; ++i) {
        // Sample at voxel centres, so the field wraps with matching slope at the
        // volume boundary rather than only matching value.
        float const position = (static_cast<float>(i) + 0.5F) * scale;
        auto const base = static_cast<std::uint32_t>(std::floor(position));
        float const fraction = position - static_cast<float>(base);
        kernel.low_index[i] = base % period;
        kernel.high_index[i] = (base + 1U) % period;
        kernel.low_weight[i] = 1.0F - fade(fraction);
        kernel.high_weight[i] = fade(fraction);
    }
    return kernel;
}

struct Lattice {
    std::uint32_t period;
    std::vector<float> values;

    float at(std::uint32_t x, std::uint32_t y, std::uint32_t z) const
    {
        return values[(static_cast<std::size_t>(x) * period + y) * period + z];
    }
};

Lattice make_lattice(std::uint32_t period, std::uint32_t seed)
{
    Lattice lattice{period, std::vector<float>(static_cast<std::size_t>(period) * period * period)};
    for (std::uint32_t z = 0; z < period; ++z)
        for (std::uint32_t y = 0; y < period; ++y)
            for (std::uint32_t x = 0; x < period; ++x)
                lattice.values[(static_cast<std::size_t>(x) * period + y) * period + z] =
                    lattice_value(x, y, z, seed);
    return lattice;
}

// One jittered feature point per Worley cell. The cell index is what wraps; the
// distance itself is measured in unwrapped space, so the volume stays seamless.
struct WorleyCells {
    std::uint32_t count;
    std::vector<glm::vec3> points;

    glm::vec3 const &at(std::uint32_t x, std::uint32_t y, std::uint32_t z) const
    {
        return points[(static_cast<std::size_t>(x) * count + y) * count + z];
    }
};

WorleyCells make_worley_cells(std::uint32_t count, std::uint32_t seed)
{
    WorleyCells cells{count,
                      std::vector<glm::vec3>(static_cast<std::size_t>(count) * count * count)};
    for (std::uint32_t z = 0; z < count; ++z)
        for (std::uint32_t y = 0; y < count; ++y)
            for (std::uint32_t x = 0; x < count; ++x)
                cells.points[(static_cast<std::size_t>(x) * count + y) * count + z] = {
                    lattice_value(x, y, z, seed) * 0.75F + 0.125F,
                    lattice_value(x, y, z, seed + 1U) * 0.75F + 0.125F,
                    lattice_value(x, y, z, seed + 2U) * 0.75F + 0.125F,
                };
    return cells;
}

// Wrapped cell index for a neighbour step of -1, 0 or +1 around `cell`.
std::uint32_t neighbour(std::uint32_t cell, std::int32_t step, std::uint32_t count)
{
    auto const sum = static_cast<std::int32_t>(cell) + step;
    return static_cast<std::uint32_t>(sum < 0 ? sum + static_cast<std::int32_t>(count) : sum) %
           count;
}

} // namespace

std::vector<std::uint8_t> make_cloud_shape_noise(std::uint32_t size)
{
    if (size < 8 || size > 256)
        fail("cloud shape noise requires 8 to 256 voxels per axis, got {}", size);

    // Octave periods all divide the volume's period, so every octave wraps inside
    // the same tile. The gain sum normalises them back to ~0.5, which keeps the
    // coverage remap behaving the same at any resolution.
    constexpr std::array<std::uint32_t, 3> octave_periods{4U, 8U, 16U};
    constexpr float octave_gain = 0.5F;
    constexpr std::uint32_t worley_cells = 8U;
    // The classic Perlin-Worley blend: fbm for the large shape, inverted Worley for
    // the billowing cell edges. The weights sum to one.
    constexpr float shape_weight = 0.65F;
    constexpr float billow_weight = 0.35F;

    float octave_total = 0.0F;
    float gain = 1.0F;
    for (std::size_t octave = 0; octave < octave_periods.size(); ++octave) {
        octave_total += gain;
        gain *= octave_gain;
    }

    std::array<AxisKernel, octave_periods.size()> shape_kernels;
    std::array<Lattice, octave_periods.size()> lattices;
    for (std::size_t octave = 0; octave < octave_periods.size(); ++octave) {
        shape_kernels[octave] = make_axis_kernel(size, octave_periods[octave]);
        lattices[octave] =
            make_lattice(octave_periods[octave], static_cast<std::uint32_t>(octave) * 8U + 1U);
    }
    AxisKernel const billow_kernel = make_axis_kernel(size, worley_cells);
    WorleyCells const worley = make_worley_cells(worley_cells, 97U);

    auto const cell_scale = static_cast<float>(worley_cells) / static_cast<float>(size);

    std::vector<std::uint8_t> voxels(static_cast<std::size_t>(size) * size * size);
    auto index = std::size_t{0};
    for (std::uint32_t z = 0; z < size; ++z)
        for (std::uint32_t y = 0; y < size; ++y)
            for (std::uint32_t x = 0; x < size; ++x) {
                float shape = 0.0F;
                float amplitude = 1.0F;
                for (std::size_t octave = 0; octave < octave_periods.size(); ++octave) {
                    auto const &kernel = shape_kernels[octave];
                    auto const &lattice = lattices[octave];
                    float octave_value = 0.0F;
                    for (std::size_t corner = 0; corner < 8; ++corner) {
                        auto const dx = corner & 1U;
                        auto const dy = (corner >> 1) & 1U;
                        auto const dz = (corner >> 2) & 1U;
                        float const weight = (dx ? kernel.high_weight[x] : kernel.low_weight[x]) *
                                             (dy ? kernel.high_weight[y] : kernel.low_weight[y]) *
                                             (dz ? kernel.high_weight[z] : kernel.low_weight[z]);
                        octave_value +=
                            weight * lattice.at(dx ? kernel.high_index[x] : kernel.low_index[x],
                                                dy ? kernel.high_index[y] : kernel.low_index[y],
                                                dz ? kernel.high_index[z] : kernel.low_index[z]);
                    }
                    shape += amplitude * octave_value;
                    amplitude *= octave_gain;
                }
                shape /= octave_total;

                // Worley in unwrapped cell space: q = cell + fraction.
                float const qx = (static_cast<float>(x) + 0.5F) * cell_scale;
                float const qy = (static_cast<float>(y) + 0.5F) * cell_scale;
                float const qz = (static_cast<float>(z) + 0.5F) * cell_scale;
                std::array<float, 3> const fraction{qx - std::floor(qx), qy - std::floor(qy),
                                                    qz - std::floor(qz)};
                std::array<std::uint32_t, 3> const centre{billow_kernel.low_index[x],
                                                          billow_kernel.low_index[y],
                                                          billow_kernel.low_index[z]};
                float nearest = 8.0F;
                for (std::int32_t dz = -1; dz <= 1; ++dz)
                    for (std::int32_t dy = -1; dy <= 1; ++dy)
                        for (std::int32_t dx = -1; dx <= 1; ++dx) {
                            glm::vec3 const &jitter =
                                worley.at(neighbour(centre[0], dx, worley_cells),
                                          neighbour(centre[1], dy, worley_cells),
                                          neighbour(centre[2], dz, worley_cells));
                            float const ex = fraction[0] - static_cast<float>(dx) - jitter.x;
                            float const ey = fraction[1] - static_cast<float>(dy) - jitter.y;
                            float const ez = fraction[2] - static_cast<float>(dz) - jitter.z;
                            nearest = std::min(nearest, ex * ex + ey * ey + ez * ez);
                        }
                float const billow = 1.0F - std::sqrt(nearest);

                float const combined =
                    std::clamp(shape * shape_weight + billow * billow_weight, 0.0F, 1.0F);
                voxels[index++] = static_cast<std::uint8_t>(std::lround(combined * 255.0F));
            }
    return voxels;
}

} // namespace lc1
