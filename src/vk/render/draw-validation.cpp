#include "draw-validation.hpp"

namespace lc1 {

void validate_draws(FrameResources const &resources, vk::Extent2D extent,
                    std::span<DrawItem const> draws)
{
    if (extent.width == 0 || extent.height == 0)
        fail("cannot render to a target with a zero extent");
    if (draws.size() > resources.object_buffers.size())
        fail("draw count {} exceeds frame object capacity {}", draws.size(),
             resources.object_buffers.size());
    for (std::size_t i = 0; i < draws.size(); ++i) {
        if (!draws[i].mesh || !draws[i].material)
            fail("draw {} requires a mesh and material", i);
        validate_material(*draws[i].material);
        if (draws[i].material->normal.texture != nullptr && !draws[i].mesh->has_tangents())
            fail("draw {} uses a normal map but its mesh has no valid tangent frame", i);
        if (glm::determinant(glm::mat3{draws[i].model_matrix}) == 0.0F)
            fail("draw {} has a singular model transform", i);
    }
}

} // namespace lc1
