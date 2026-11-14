#pragma once

#include "lc1/vk/render/graphics-shaders.hpp"
#include "lc1/vk/render/graphics-state.hpp"
#include "lc1/vk/resources/gpu-image.hpp"

#include <glm/glm.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace lc1 {

class CommandBuffer;
class Device;
class RenderGraph;
struct FrameResources;
struct FrameScene;

// Runtime-tunable layer-based volumetric cloud configuration.
//
// Distances are metres and radiances are linear HDR, the same units as the rest of
// the renderer. Defaults match a 1.5 km to 3.5 km layer observed from the ground.
struct CloudSettings {
    enum class Debug { Off, Density, SunTransmittance, ViewTransmittance, StepCount };

    // Opt-in: with false the renderer registers no cloud pass, so no 3D texture is
    // generated and the HDR chain keeps its single image.
    bool enabled = false;

    float bottom = 1500.0F;
    float top = 3500.0F;
    // Fraction of the sky covered by cloud; the density threshold moves with it.
    float coverage = 0.35F;
    float density = 1.0F;
    // World-space extent of one shape-noise tile, i.e. the size of the large cloud
    // masses. Smaller values make more, smaller clouds out of the same field.
    float noise_scale = 14000.0F;
    // Second sample of the same field at `detail_scale` times the mass frequency: it
    // erodes cloud edges. One scale alone either looks like popcorn or like mush.
    float detail_scale = 8.0F;
    // Fraction of density the detail field may remove, 0 to 1.
    float detail_strength = 0.40F;
    glm::vec3 wind_velocity{5.0F, 0.0F, 2.0F};
    // Extinction at unit density, per metre.
    float extinction = 0.02F;
    float scattering_albedo = 0.9F;
    // Henyey-Greenstein anisotropy: positive is forward scattering, towards the sun.
    float anisotropy = 0.55F;
    std::uint32_t view_steps = 48;
    std::uint32_t light_steps = 6;
    float light_march_distance = 400.0F;
    float max_trace_distance = 40000.0F;
    // Fraction of the scene's sky colour used as ambient cloud lighting. The sky and
    // the layer therefore brighten and darken together instead of disagreeing.
    float ambient_scale = 0.45F;
    // Scales the direct sunlight term. The single-scattering phase function plus the
    // two multiple-scattering octaves still under-report the energy a real cloud
    // scatters, and the scene's photometric light scale is far below its painted sky,
    // so 1.0 leaves the layer darker than the background it sits in. 1.0 is physically
    // consistent with the scene lights; higher values are the usual cloud brightness
    // control.
    float radiance_scale = 4.0F;
    Debug debug = Debug::Off;
};

// Per-frame cloud layer parameters, as uploaded to shaders/clouds.slang. Every member
// is a 16-byte row: the shader reads the inverse view-projection column by column
// because whole-matrix loads through a heap ConstantBuffer are mishandled on NVIDIA.
struct CloudData {
    std::array<glm::vec4, 4> inverse_view_projection;
    glm::vec4 camera_position_time;     // xyz camera position; w wind time in seconds
    glm::vec4 sun_direction_anisotropy; // xyz unit vector towards the sun; w anisotropy
    glm::vec4 sun_radiance_albedo;      // rgb linear sun radiance; w scattering albedo
    glm::vec4 ambient_extinction;       // rgb linear ambient radiance; w extinction per metre
    glm::vec4 layer;                    // bottom, top, coverage, density scale
    glm::vec4 wind_frequency;           // xyz wind velocity in m/s; w mass frequency, 1/m
    glm::vec4 detail;                   // x detail frequency multiplier, y erosion strength
    glm::vec4 march;                    // x max trace distance, y light march distance
    glm::uvec4 steps;                   // x view steps, y light steps, z debug mode, w reserved
};

static_assert(sizeof(CloudData) == 208);
static_assert(offsetof(CloudData, camera_position_time) == 64);
static_assert(offsetof(CloudData, detail) == 160);
static_assert(offsetof(CloudData, steps) == 192);

// Layer-based volumetric clouds: one ray-march pass into a radiance/transmittance
// image, then one composite pass into a second HDR image before Bloom and tone
// mapping. Both intermediates belong to the frame slot, only the shape volume is
// owned here. Device outlives this object.
class VolumetricCloudRenderer {
  public:
    explicit VolumetricCloudRenderer(Device const &device);

    // Registers "Cloud ray marching" ("hdr.color" -> "cloud.result") and
    // "Cloud composite" ("cloud.result" -> "hdr.cloud"). The caller decides what
    // consumes "hdr.cloud". Validates settings here, once per registration.
    void add_passes(RenderGraph &graph, FrameResources &resources, vk::Extent2D extent,
                    CloudSettings const &settings, std::shared_ptr<FrameScene const> scene);

  private:
    void ensure_shape_noise();
    void record_march(CommandBuffer &cmd, FrameResources &resources, vk::Extent2D extent,
                      CloudSettings const &settings, FrameScene const &scene);
    void record_composite(CommandBuffer &cmd, FrameResources &resources, vk::Extent2D extent);

    Device const *device_;
    // Generated on first use: the CPU field costs real time, and a disabled pass
    // must not pay for it.
    std::optional<GpuImage> shape_noise_;
    GraphicsShaders march_shaders_;
    GraphicsShaders composite_shaders_;
    // Wind time origin. Cloud motion is a function of elapsed wall time, not of the
    // frame count, so the layer moves at the configured metres per second.
    std::chrono::steady_clock::time_point origin_;
};

} // namespace lc1
