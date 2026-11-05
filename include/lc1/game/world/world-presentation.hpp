#pragma once

#include "lc1/audio/audio.hpp"
#include "lc1/game/continent/view.hpp"
#include "lc1/game/debug/pbr-showcase.hpp"
#include "lc1/game/world/world-snapshot.hpp"
#include "lc1/scene/camera.hpp"
#include "lc1/scene/gpu-model.hpp"
#include "lc1/scene/model-instance.hpp"
#include "lc1/scene/scene.hpp"
#include "lc1/vk/render/output-settings.hpp"
#include "lc1/vk/resources/gpu-font.hpp"

namespace lc1 {

struct WorldPresentationInput {
    bool cycle_lighting = false;
    bool toggle_showcase_motion = false;
    int exposure_steps = 0;
};

struct GameLocation {
    std::string_view region;
    std::string_view place;
};

// Manages GPU resources.
//
// Owns presentation resources. Device, continent and snapshot outlive this presentation;
// submitted GPU work must complete before teardown. Character transforms live only in GameWorld.
class WorldPresentation {
  public:
    WorldPresentation(Device const &device, std::filesystem::path const &asset_root,
                      Continent const &continent, WorldSnapshot const &world, OutputSettings output,
                      PlayerId observer);

    WorldPresentation(WorldPresentation const &) = delete;
    WorldPresentation &operator=(WorldPresentation const &) = delete;
    WorldPresentation(WorldPresentation &&) = delete;
    WorldPresentation &operator=(WorldPresentation &&) = delete;

    ~WorldPresentation() = default;

    scene::SceneModel const &character_model(std::uint32_t keyid) const;
    scene::SceneModel const *weapon_model(std::uint32_t keyid) const;

    // Advance animation before the camera reads its mount, then update the listener afterwards.
    // Time is the finite, nonnegative render delta accepted by GameSession.
    void update(float delta_seconds, WorldPresentationInput const &input);
    void update_listener(scene::FpsCamera const &camera);
    glm::mat4 observer_eye_transform() const;
    scene::Scene const &scene() const;
    std::vector<WorldTextItem> world_labels(scene::FpsCamera const &camera) const;

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
    void update_instances();

    // Main-thread snapshot and immutable continent borrows; both outlive this presentation.
    WorldSnapshot const *world_;
    Continent const *continent_;
    PlayerId observer_;
    std::size_t observed_model_index_ = 0;

    struct CharacterModels {
        std::uint32_t keyid;
        scene::SceneModel *character;
        scene::SceneModel *weapon;
        std::optional<std::size_t> mount;
        std::optional<std::size_t> attack_animation;
        std::uint64_t attack_sequence;
        std::optional<std::size_t> nod_animation;
        std::uint64_t nod_sequence;
    };

    std::vector<CharacterModels> character_models_;
    GpuFont const font_;
    GpuTexture const white_;
    ContinentView const continent_view_;
    demo::PbrShowcase showcase_;

    struct ModelResources {
        ModelConfig config;
        CharacterBody body;
        scene::GpuModel model;
        MaterialInfo material;

        ModelResources(Device const &device, std::filesystem::path const &asset_root,
                       ModelConfig const &config, CharacterBody const &body);
    };

    std::map<std::pair<std::string, TextureColorSpace>, std::unique_ptr<GpuTexture>>
        model_textures_;
    std::vector<std::unique_ptr<ModelResources>> model_resources_;
    // Required scene-owned borrows, valid until scene_ is destroyed.
    std::vector<scene::SceneModel *> showcase_models_;
    std::vector<scene::SceneLight *> lights_;
    scene::SceneLight *flashlight_ = nullptr;

    std::optional<std::size_t> eyes_node_index_;
    // Declared after assets: scene instances borrow their meshes and materials.
    scene::Scene scene_;
    OutputSettings output_;
    std::size_t lighting_mode_ = 0;
    bool showcase_animated_ = true;
    float showcase_time_ = 0.0F;
    audio::AudioContext audio_context_;
    audio::Listener audio_listener_{audio_context_};
};

} // namespace lc1
