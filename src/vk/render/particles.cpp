#include "lc1/scene/camera.hpp"
#include "lc1/vk/render/renderer.hpp"
#include "lc1/vk/resources/command-buffer.hpp"

#include <algorithm>
#include <bit>

namespace lc1 {
void Renderer::record_particles(CommandBuffer &commands, FrameResources &resources,
                                scene::FpsCamera const &camera,
                                std::span<scene::Particle const> particles,
                                std::optional<scene::DistanceFog> const &fog)
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

    struct Instance {
        glm::vec4 center_width;
        glm::vec4 color_alpha;
        glm::vec4 height_rotation;
    };

    static_assert(sizeof(Instance) == 48);
    std::vector<Instance> instances;
    instances.reserve(ordered.size());
    for (auto const *particle : ordered) {
        auto radiance = particle->radiance;
        if (fog) {
            float const amount = glm::smoothstep(
                fog->start, fog->end, glm::distance(camera.position(), particle->position));
            radiance = glm::mix(radiance, fog->radiance, amount);
        }
        instances.push_back(
            {{particle->position, particle->size.x},
             {radiance, particle->opacity},
             {particle->spark ? -particle->size.y : particle->size.y, particle->rotation, 0, 0}});
    }
    auto const bytes = std::as_bytes(std::span{instances});
    if (!resources.particle_instances || resources.particle_instances->size() < bytes.size())
        resources.particle_instances.emplace(
            *device_, std::bit_ceil(bytes.size()), vk::BufferUsageFlagBits2::eShaderDeviceAddress,
            vma::AllocationCreateFlagBits::eHostAccessSequentialWrite);
    resources.particle_instances->upload(bytes);

    struct PushData {
        glm::mat4 transform;
        glm::vec4 right;
        glm::vec4 up;
        vk::DeviceAddress instances;
    };

    static_assert(offsetof(PushData, instances) == 96 && sizeof(PushData) == 104);
    PushData const data{camera.view_projection_matrix(),
                        {camera.right(), 0},
                        {camera.up(), 0},
                        resources.particle_instances->device_address()};
    particle_shaders_.bind_to_command_buffer(commands);
    set_graphics_state(commands.raii(), particle_state_);
    commands.push_data(std::span{&data, 1});
    commands.raii().draw(6, static_cast<std::uint32_t>(instances.size()), 0, 0);
}
} // namespace lc1
