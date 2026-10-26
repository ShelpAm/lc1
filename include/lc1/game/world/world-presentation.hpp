#pragma once

#include "lc1/audio/audio.hpp"
#include "lc1/game/continent/view.hpp"
#include "lc1/game/debug/pbr-showcase.hpp"
#include "lc1/game/world/game-world.hpp"
#include "lc1/scene/camera.hpp"
#include "lc1/scene/model-instance.hpp"
#include "lc1/vk/render/output-settings.hpp"
#include "lc1/vk/resources/gpu-font.hpp"

namespace lc1 {

struct WorldPresentationInput {
    bool nod = false;
    bool cycle_lighting = false;
    bool toggle_showcase_motion = false;
    int exposure_steps = 0;
};

struct GameLocation {
    std::string_view region;
    std::string_view place;
};

// Owns presentation resources. Device and world must outlive this immovable presentation;
// submitted GPU work must complete before teardown. Character transforms live only in GameWorld.
class WorldPresentation {
  public:
    WorldPresentation(Device const &device, std::filesystem::path const &asset_root,
                      GameWorld const &world, OutputSettings output);

    WorldPresentation(WorldPresentation const &) = delete;
    WorldPresentation &operator=(WorldPresentation const &) = delete;
    WorldPresentation(WorldPresentation &&) = delete;
    WorldPresentation &operator=(WorldPresentation &&) = delete;

    ~WorldPresentation() = default;

    // Advance animation before the camera reads its mount, then update the listener afterwards.
    void update(WorldPresentationInput const &input, float delta_seconds);
    void update_listener(scene::FpsCamera const &camera);
    glm::mat4 player_eye_transform() const;
    std::vector<DrawItem> draws() const;
    std::vector<WorldTextItem> world_labels(scene::FpsCamera const &camera) const;

    std::span<scene::Light const> lights() const;

    OutputSettings const &output() const;

    GpuFont const &font() const;

    GameLocation location() const;
    std::string_view lighting_name() const;

    bool showcase_animated() const;

    std::uint32_t object_capacity() const;

    audio::AudioContext const &audio_context() const;

    void set_volume(float volume_percent);

  private:
    void update_lighting();

    GameWorld const *world_; // Required non-null borrow, outlives this presentation.
    GpuFont const font_;
    GpuTexture const white_;
    ContinentView const continent_view_;
    demo::PbrShowcase showcase_;

    struct CharacterResources {
        scene::ModelDefinition definition;
        std::vector<GpuMesh> meshes;
    };

    static CharacterResources make_character_resources(Device const &device);

    CharacterResources const character_resources_;
    std::vector<scene::ModelInstance> character_models_;
    std::size_t player_model_index_ = 0;
    std::size_t nod_animation_index_ = 0;
    std::size_t eyes_node_index_ = 0;
    MaterialInfo const player_material_;
    std::vector<scene::Light> lights_;
    std::size_t flashlight_index_ = 0;
    std::vector<scene::Light> active_lights_;
    OutputSettings output_;
    std::size_t lighting_mode_ = 0;
    bool showcase_animated_ = true;
    float showcase_time_ = 0.0F;
    audio::AudioContext audio_context_;
    audio::Listener audio_listener_{audio_context_};
};

} // namespace lc1
