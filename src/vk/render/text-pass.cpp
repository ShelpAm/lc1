#include "lc1/vk/render/text-pass.hpp"

#include "lc1/error.hpp"
#include "lc1/text/text-layout.hpp"
#include "lc1/vk/core/device.hpp"
#include "lc1/vk/render/frame-resources.hpp"
#include "lc1/vk/resources/command-buffer.hpp"
#include "lc1/vk/resources/gpu-font.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace lc1 {
namespace {

// Text is rebuilt from scratch every frame; a frame needing more than this is a
// bug, not a larger font.
constexpr std::size_t max_vertices = 1U << 16;

} // namespace

TextPass::TextPass(Device const &device, vk::SampleCountFlagBits samples)
    : device_{&device}, shaders_{device, "shaders/text.spv"},
      world_state_{
          .vertex_input = VertexInput::Mesh,
          .samples = samples,
          .depth_test = true,
          .blend = true,
      },
      world_outline_state_{
          .vertex_input = VertexInput::Mesh,
          .samples = samples,
          .depth_test = true,
          .depth_write = false,
          .blend = true,
      },
      screen_state_{
          .vertex_input = VertexInput::Mesh,
          .samples = vk::SampleCountFlagBits::e1,
          .depth_test = false,
          .blend = true,
      }
{
}

void TextPass::draw_world(CommandBuffer &commands, FrameResources &resources, GpuFont const &font,
                          std::span<WorldTextItem const> items, glm::mat4 const &view_projection)
{
    if (items.empty())
        return;
    auto const &atlas = font.atlas();
    vertices_.clear();
    // World labels sit over sky, cloud and terrain alike, so they carry a dark outline
    // in their own plane: a pale flat colour alone disappears against a bright or a
    // cloudy background once the sky starts changing. The outline is one contiguous
    // vertex range drawn before the fill with depth writes off, so its offsets cannot
    // hide the fill behind them.
    // Eight directions, not four: a diagonal-only ring leaves the top, bottom and
    // sides of a stroke thin, and this label has to survive sunlit cloud behind it.
    constexpr glm::vec3 outline_color{0.0F, 0.0F, 0.0F};
    constexpr std::array<std::array<float, 2>, 8> outline_offsets{{
        {-1.0F, -1.0F},
        {0.0F, -1.0F},
        {1.0F, -1.0F},
        {-1.0F, 0.0F},
        {1.0F, 0.0F},
        {-1.0F, 1.0F},
        {0.0F, 1.0F},
        {1.0F, 1.0F},
    }};

    struct Placement {
        WorldTextItem const *item;
        float line_height;
    };

    std::vector<Placement> placements;
    placements.reserve(items.size());
    for (auto const &item : items) {
        auto const metrics = text::measure_text(atlas, item.text, item.line_height);
        // `line_height` is the first line's ascent-to-descent box, so a line keeps
        // the size the caller asked for whatever the face's own em is.
        auto target_line_height = item.line_height;
        if (item.max_width > 0.0F && metrics.width > 0.0F)
            target_line_height *= std::min(1.0F, item.max_width / metrics.width);
        placements.push_back({&item, target_line_height});
    }
    auto const append = [&](Placement const &placement, glm::vec3 const &color,
                            glm::vec3 const &offset) {
        text::append_text(vertices_, atlas, placement.item->text,
                          {
                              .origin = placement.item->center + offset,
                              .line_height = placement.line_height,
                              .color = color,
                              .center_x = true,
                              .normal = placement.item->normal,
                          });
    };
    for (auto const &placement : placements) {
        auto const basis = text::plane_basis(placement.item->normal);
        auto const reach = 0.05F * placement.line_height;
        for (auto const &offset : outline_offsets)
            append(placement, outline_color,
                   (basis.right * offset[0] + basis.up * offset[1]) * reach);
    }
    auto const outline_vertices = vertices_.size();
    for (auto const &placement : placements)
        append(placement, placement.item->color, glm::vec3{0.0F});

    upload_vertices(commands, resources, resources.world_text_vertices, font, view_projection);
    draw_vertices(commands, world_outline_state_, 0, outline_vertices);
    draw_vertices(commands, world_state_, outline_vertices, vertices_.size() - outline_vertices);
}

void TextPass::draw_screen(CommandBuffer &commands, FrameResources &resources, vk::Extent2D extent,
                           GpuFont const &font, std::span<ScreenTextItem const> items)
{
    if (items.empty())
        return;
    auto const &atlas = font.atlas();
    vertices_.clear();
    for (auto const &item : items) {
        // Screen positions use a top-left origin; append_text lays out from a
        // top-left corner in the bottom-left pixel space this pass transforms.
        auto const origin_y = static_cast<float>(extent.height) - item.position.y;
        text::append_text(vertices_, atlas, item.text,
                          {
                              .origin = {item.position.x, origin_y, 0.0F},
                              .line_height = item.line_height,
                              .color = item.color,
                          });
    }
    // Pixels with the origin at the bottom-left corner to clip space. The negative
    // height is what keeps `up` pointing up under this pass's positive viewport,
    // mirroring the scene's negative-height viewport over a y-up world.
    glm::mat4 transform{1.0F};
    transform[0][0] = 2.0F / static_cast<float>(extent.width);
    transform[1][1] = -2.0F / static_cast<float>(extent.height);
    transform[3][0] = -1.0F;
    transform[3][1] = 1.0F;
    upload_and_draw(commands, resources, resources.screen_text_vertices, font, transform,
                    screen_state_);
}

void TextPass::upload_vertices(CommandBuffer &commands, FrameResources &resources,
                               std::optional<GpuBuffer> &buffer, GpuFont const &font,
                               glm::mat4 const &transform)
{
    auto const bytes = std::span<Vertex const>{vertices_}.size_bytes();
    if (bytes == 0)
        return;
    if (vertices_.size() > max_vertices)
        fail("text needs {} vertices, over the {} supported", vertices_.size(), max_vertices);

    if (!buffer || buffer->size() < bytes) {
        // The slot fence was waited before recording, so the previous buffer has
        // no uses left.
        buffer.emplace(*device_, bytes, vk::BufferUsageFlagBits2::eVertexBuffer,
                       vma::AllocationCreateFlagBits::eHostAccessSequentialWrite);
    }
    buffer->upload(std::as_bytes(std::span<Vertex const>{vertices_}));

    auto &heap = *resources.descriptor_heap;
    commands.bind_descriptor_heap(heap);
    PushData const push{
        .transform = transform,
        .atlas_index = resources.image_index(resources.font_atlas_index, font.texture().image()),
        .sampler_index = resources.sampler_index(resources.font_sampler_index, font.sampler_info()),
    };
    commands.push_data(std::span{&push, 1});

    shaders_.bind_to_command_buffer(commands);
    commands.raii().bindVertexBuffers(0, *buffer->raii(), vk::DeviceSize{0});
}

void TextPass::draw_vertices(CommandBuffer &commands, GraphicsState const &state, std::size_t first,
                             std::size_t count)
{
    if (count == 0)
        return;
    set_graphics_state(commands.raii(), state);
    commands.raii().draw(static_cast<std::uint32_t>(count), 1, static_cast<std::uint32_t>(first),
                         0);
}

void TextPass::upload_and_draw(CommandBuffer &commands, FrameResources &resources,
                               std::optional<GpuBuffer> &buffer, GpuFont const &font,
                               glm::mat4 const &transform, GraphicsState const &state)
{
    upload_vertices(commands, resources, buffer, font, transform);
    draw_vertices(commands, state, 0, vertices_.size());
}

} // namespace lc1
