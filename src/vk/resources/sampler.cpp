#include "lc1/vk/resources/sampler.hpp"

namespace lc1 {

vk::SamplerCreateInfo Sampler::default_create_info(Device const &device)
{
    return vk::SamplerCreateInfo{
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eLinear,
        .addressModeU = vk::SamplerAddressMode::eRepeat,
        .addressModeV = vk::SamplerAddressMode::eRepeat,
        .addressModeW = vk::SamplerAddressMode::eRepeat,
        .anisotropyEnable = vk::True,
        .maxAnisotropy = device.raii_physical().getProperties().limits.maxSamplerAnisotropy,
        .compareEnable = vk::False,
        .compareOp = vk::CompareOp::eAlways,
        .maxLod = vk::LodClampNone,
        .borderColor = vk::BorderColor::eIntOpaqueBlack,
        .unnormalizedCoordinates = vk::False,
    };
}

Sampler::Sampler(Device const &device)
    : info_(default_create_info(device)), sampler_(device.raii().createSampler(info_))
{
}

vk::SamplerCreateInfo const &Sampler::create_info() const
{
    return info_;
}

vk::raii::Sampler const &Sampler::raii() const
{
    return sampler_;
}

} // namespace lc1
