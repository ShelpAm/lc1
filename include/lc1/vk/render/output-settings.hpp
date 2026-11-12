#pragma once

namespace lc1 {

// Linear writes are encoded by sRGB attachments automatically. Srgb is for
// manually encoding display output into a UNORM attachment only.
enum class OutputEncoding { Linear, Srgb };

struct OutputSettings {
    float exposure = 1.0F;
    float bloom_strength = 0.0F;
    OutputEncoding encoding = OutputEncoding::Linear;
};
} // namespace lc1
