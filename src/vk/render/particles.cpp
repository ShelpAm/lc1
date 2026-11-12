#include "lc1/scene/camera.hpp"
#include "lc1/vk/render/renderer.hpp"
#include "lc1/vk/resources/command-buffer.hpp"

#include <algorithm>

namespace lc1 {
void Renderer::record_particles(CommandBuffer &commands, scene::FpsCamera const &camera,
                                std::span<scene::Particle const> particles)
{
    if (particles.empty())
        return;
    std::vector<scene::Particle const *> ordered;
    ordered.reserve(particles.size());
    for (auto const &particle : particles)
        ordered.push_back(&particle);
    auto const forward = camera.forward();
    std::ranges::stable_sort(ordered, [&](auto const *a, auto const *b) {
        return glm::dot(a->position, forward) > glm::dot(b->position, forward);
    });

    struct PushData {
        glm::mat4 transform;
        glm::vec4 center_width;
        glm::vec4 color_alpha;
        glm::vec4 right_height;
        glm::vec4 up_rotation;
    };

    static_assert(sizeof(PushData) == 128);
    particle_shaders_.bind_to_command_buffer(commands);
    set_graphics_state(commands.raii(), particle_state_);
    for (auto const *particle : ordered) {
        PushData const data{
            camera.view_projection_matrix(),
            {particle->position, particle->size.x},
            {particle->radiance, particle->opacity},
            {camera.right(), particle->spark ? -particle->size.y : particle->size.y},
            {camera.up(), particle->rotation}};
        commands.push_data(std::span{&data, 1});
        commands.raii().draw(6, 1, 0, 0);
    }
}
} // namespace lc1
