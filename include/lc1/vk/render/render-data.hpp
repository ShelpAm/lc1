#pragma once

#include "lc1/scene/lights/light.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace lc1 {

// Matches PassData in shaders/pass_data.slang. Unused fields stay zero.
struct PassData {
    std::uint32_t camera{};
    std::uint32_t lights{};
    std::uint32_t object{};
    std::uint32_t temporal{};
    std::uint32_t scene{};
    std::uint32_t current_visibility{};
    std::uint32_t current_surface{};
    std::uint32_t history_visibility{};
    std::uint32_t history_surface{};
    std::uint32_t resolved_visibility{};
    std::uint32_t tone_map{};
    std::uint32_t hdr_image{};
    std::uint32_t bloom_image{};
    std::uint32_t bloom_sampler{};
};

static_assert(sizeof(PassData) == 56);

struct ToneMapData {
    glm::vec4 output; // Exposure, manual sRGB encoding (0/1), Bloom strength, reserved.
};

static_assert(sizeof(ToneMapData) == 16);

// Matches Camera in shaders/shader.slang, field for field.
struct CameraData {
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 position; // World-space camera position; w is unused.
};

struct ObjectData {
    glm::mat4 model;
    glm::mat4 normal_transform; // Inverse transpose; padded to match shader matrices.
    glm::uvec4 shadow_identity; // x: surface ID; y: moving receiver rejects history.
};

static_assert(sizeof(CameraData) == 144 && offsetof(CameraData, position) == 128);
static_assert(sizeof(ObjectData) == 144 && offsetof(ObjectData, normal_transform) == 64);

constexpr auto max_num_lights = 10U;

struct LightsData {
    std::uint32_t count{};
    std::array<scene::Light, max_num_lights> lights;
};

} // namespace lc1
