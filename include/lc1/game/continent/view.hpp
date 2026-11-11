#pragma once

#include "lc1/scene/scene.hpp"

#include "lc1/vk/render/material.hpp"
#include "lc1/vk/resources/gpu-mesh.hpp"
#include "lc1/vk/resources/gpu-texture.hpp"

#include <array>

#include <memory>
#include <span>
#include <vector>

namespace lc1 {

class Continent;
class Device;
class GpuTexture;

// Static GPU snapshot, divided into 16 x 16 tile chunks (256 m at the current scale).
// Ground follows tile/patch edges and refines locally for blended road borders.
// All chunks are resident for this first outline; streaming is not implemented yet.
// Device and GpuTexture must outlive it.
class ContinentView {
  public:
    ContinentView(Device const &device, GpuTexture const &texture, Continent const &continent);
    ContinentView(ContinentView const &) = delete;
    ContinentView &operator=(ContinentView const &) = delete;
    ContinentView(ContinentView &&) = delete;
    ContinentView &operator=(ContinentView &&) = delete;

    struct GrassBatch {
        std::size_t draw_index;
        int level; // 1..8; each group contributes roughly 25% of the default density.
    };

    std::span<scene::SceneModel const> draw_items() const;
    std::span<GrassBatch const> grass_batches() const;

  private:
    std::array<std::unique_ptr<GpuTexture>, 3> ground_textures_;
    MaterialInfo ground_material_;
    MaterialInfo material_;
    std::vector<std::unique_ptr<scene::GpuModel>> models_;
    // Placements borrow the owned models and material_ until this view is destroyed.
    std::vector<scene::SceneModel> draws_;
    std::vector<GrassBatch> grass_batches_;
};

} // namespace lc1
