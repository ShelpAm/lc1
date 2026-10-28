#include "lc1/vk/render/material.hpp"
#include "lc1/error.hpp"
#include "lc1/vk/resources/gpu-texture.hpp"

#include <array>
#include <cmath>

namespace lc1 {
void validate_material(MaterialInfo const &info)
{
    auto const &p = info.parameters;
    auto unit = [](float value) { return value >= 0.0F && value <= 1.0F; };
    if (!unit(p.base_color_factor.r) || !unit(p.base_color_factor.g) ||
        !unit(p.base_color_factor.b) || !unit(p.base_color_factor.a) || !unit(p.metallic_factor) ||
        !unit(p.roughness_factor) || !unit(p.occlusion_strength) ||
        !std::isfinite(p.normal_scale) || p.normal_scale < 0.0F ||
        !std::isfinite(p.emissive_factor.r) || !std::isfinite(p.emissive_factor.g) ||
        !std::isfinite(p.emissive_factor.b) ||
        glm::any(glm::lessThan(p.emissive_factor, glm::vec3{0})))
        fail("invalid PBR material factors");
    if (info.occlusion.texture)
        fail("occlusion textures require the IBL milestone");
    std::array const textures{info.base_color, info.metallic_roughness, info.normal, info.occlusion,
                              info.emissive};
    std::array const color_spaces{TextureColorSpace::Srgb, TextureColorSpace::Linear,
                                  TextureColorSpace::Linear, TextureColorSpace::Linear,
                                  TextureColorSpace::Srgb};
    // Validate every slot, including slots not yet sampled.
    for (std::size_t i = 0; i < textures.size(); ++i)
        if (textures[i].texture && textures[i].texture->color_space() != color_spaces[i])
            fail("PBR texture slot {} has the wrong color space", i);
}
} // namespace lc1
