#pragma once

#include "lc1/vk/core/common.hpp"
#include "lc1/vk/core/device.hpp"

namespace lc1 {

class Sampler {
  public:
    static vk::SamplerCreateInfo default_create_info(Device const &device);

    explicit Sampler(Device const &device);

    vk::SamplerCreateInfo const &create_info() const;

    vk::raii::Sampler const &raii() const;

  private:
    vk::SamplerCreateInfo info_;
    vk::raii::Sampler sampler_;
};

} // namespace lc1
