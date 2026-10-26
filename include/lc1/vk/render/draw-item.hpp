#pragma once

#include <glm/mat4x4.hpp>

namespace lc1 {

class GpuMesh;
struct MaterialInfo;

// One entry of "what to draw this frame", rebuilt every frame. Non-owning: the
// material parameters must survive record(); mesh, textures and samplers must
// survive GPU execution. Renderer snapshots parameters into the current frame slot.
struct DrawItem {
    GpuMesh const *mesh = nullptr;
    MaterialInfo const *material = nullptr;
    glm::mat4 model_matrix{1.0F};
    // Content that renders without casting, such as flat ground decals. Text is
    // never a draw item: it is drawn by the text pass and casts nothing.
    bool casts_shadow = true;
    // Pulls coplanar decals toward the camera in depth without moving their geometry.
    // Road overlays use this instead of relying on a large visual lift.
    bool depth_bias = false;
};

} // namespace lc1
