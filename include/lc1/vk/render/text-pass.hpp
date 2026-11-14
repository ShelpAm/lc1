#pragma once

#include "lc1/vertex.hpp"
#include "lc1/vk/render/graphics-shaders.hpp"
#include "lc1/vk/render/graphics-state.hpp"
#include "lc1/vk/resources/gpu-buffer.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace lc1 {

class CommandBuffer;
class Device;
class GpuFont;
struct FrameResources;

// Text standing in the world: `center` is the horizontal centre of the first
// line's top (the ascent line) in its oriented plane, `line_height` world units
// tall from ascent to descent, and squeezed to `max_width` when the line would
// otherwise be wider than that. The plane faces `normal`; the caller keeps it clear of
// the surface it belongs to so the depth test does not eat the glyphs.
struct WorldTextItem {
    std::string text;
    glm::vec3 center{};
    float line_height = 1.0F;
    float max_width = 0.0F; // Zero means no cap. World units.
    glm::vec3 color{1.0F};
    // Finite, nonzero front direction; normalized during layout. Up follows world
    // +Y projected onto the plane, or +Z when nearly parallel to +Y.
    glm::vec3 normal{0.0F, 0.0F, 1.0F};
};

// One line of screen text. `position` is the top-left corner of the first line in
// framebuffer pixels from the top-left corner of the screen: +x is right and +y
// is down.
struct ScreenTextItem {
    std::string text;
    glm::vec2 position{};
    float line_height = 16.0F; // Framebuffer pixels.
    glm::vec3 color{1.0F};
};

// Everything the frame draws as text. One font only: the frame's descriptor heap
// holds a single atlas slot, so two fonts in one frame would both sample
// whichever atlas was written last. The single pointer makes that unbuildable
// rather than merely undocumented.
struct TextFrameInput {
    GpuFont const *font = nullptr;
    std::vector<WorldTextItem> world;
    std::vector<ScreenTextItem> screen;
};

// Text drawing, shared by the scene pass (world text, depth-tested, blended into
// the HDR target before tone mapping) and the UI pass (screen text, blended over
// the tone-mapped output). Layout runs per frame into a reused vertex array: the
// strings are short and rebuilding them costs less than tracking what changed.
class TextPass {
  public:
    TextPass(Device const &device, vk::SampleCountFlagBits samples);

    TextPass(TextPass const &) = delete;
    TextPass &operator=(TextPass const &) = delete;
    TextPass(TextPass &&) = delete;
    TextPass &operator=(TextPass &&) = delete;
    ~TextPass() = default;

    // One draw for all items. The caller has already begun rendering with the
    // scene's colour and depth attachments and set the matching viewport; the
    // pushed transform is the camera's view projection.
    void draw_world(CommandBuffer &commands, FrameResources &resources, GpuFont const &font,
                    std::span<WorldTextItem const> items, glm::mat4 const &view_projection);

    // One draw for all items, into the caller's begun rendering of the output
    // target. `extent` is that target's; item positions are top-left-origin
    // pixels and are converted to the pass's bottom-left pixel transform, so up
    // stays up under the positive viewport just as it does in the scene pass
    // under its negative one.
    void draw_screen(CommandBuffer &commands, FrameResources &resources, vk::Extent2D extent,
                     GpuFont const &font, std::span<ScreenTextItem const> items);

  private:
    // Matches shaders/text.slang; the transform travels as its four columns so
    // this pass uses no matrix type. See docs/external-defects.md.
    struct PushData {
        glm::mat4 transform{1.0F};
        std::uint32_t atlas_index = 0;
        std::uint32_t sampler_index = 0;
    };

    static_assert(sizeof(PushData) == 72);

    // Uploads `vertices_` into the slot's buffer, growing it when this frame needs
    // more room, and binds everything a draw needs. Runs after the slot fence, so
    // replacing the buffer is safe. The caller issues one or more draws over the
    // uploaded range with draw_vertices().
    void upload_vertices(CommandBuffer &commands, FrameResources &resources,
                         std::optional<GpuBuffer> &buffer, GpuFont const &font,
                         glm::mat4 const &transform);

    // Applies `state` and draws one contiguous vertex range from the bound buffer.
    void draw_vertices(CommandBuffer &commands, GraphicsState const &state, std::size_t first,
                       std::size_t count);

    // Uploads, then draws the whole range once.
    void upload_and_draw(CommandBuffer &commands, FrameResources &resources,
                         std::optional<GpuBuffer> &buffer, GpuFont const &font,
                         glm::mat4 const &transform, GraphicsState const &state);

    // Non-owning, non-null; established by the constructor.
    Device const *device_;
    GraphicsShaders shaders_;
    GraphicsState world_state_;
    // The outline is drawn first with depth writes off: its offset copies must not
    // win the depth test against the fill that follows them.
    GraphicsState world_outline_state_;
    GraphicsState screen_state_;
    // Reused across both passes; each uploads before the other rebuilds it.
    std::vector<Vertex> vertices_;
};

} // namespace lc1
