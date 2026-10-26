#pragma once

#include "lc1/scene/transform.hpp"
#include "lc1/vk/render/draw-item.hpp"

namespace lc1::scene {

// GpuMesh and material are borrowed and must outlive submitted draws.
struct Object {
    Transform transform;
    GpuMesh const *mesh = nullptr;
    MaterialInfo const *material = nullptr;
    bool casts_shadow = true;

    DrawItem draw_item() const;
};

} // namespace lc1::scene
