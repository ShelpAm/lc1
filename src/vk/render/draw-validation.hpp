#pragma once

#include "lc1/vk/render/draw-item.hpp"

#include "lc1/vk/render/frame-resources.hpp"
#include "lc1/vk/render/material.hpp"
#include "lc1/vk/resources/gpu-mesh.hpp"

namespace lc1 {

void validate_draws(FrameResources const &resources, vk::Extent2D extent,
                    std::span<DrawItem const> draws);

} // namespace lc1
