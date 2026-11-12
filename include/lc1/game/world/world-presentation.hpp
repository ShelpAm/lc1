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

#include <array>

namespace lc1 {

struct WorldPresentationInput {
    bool cycle_lighting = false;
    bool toggle_showcase_motion = false;
    int exposure_steps = 0;
    bool first_person = false;
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
    double time_of_day() const;
    float time_rate() const;
    // Finite multiplier in [0.01, 100], validated by the UI.
    void set_time_rate(float rate);
    int grass_density() const;
    // UI-validated density level in [0, 8], where 4 is the default.
    void set_grass_density(int density);

    std::uint32_t object_capacity() const;

    audio::AudioContext const &audio_context() const;

    void set_volume(float volume_percent);

  private:
    void update_lighting();
    void update_instances();
    void update_particles(float delta_seconds);

    // Main-thread snapshot and immutable continent borrows; both outlive this presentation.
    WorldSnapshot const *world_;
    Continent const *continent_;
    PlayerId observer_;

    struct WeaponModel {
        scene::SceneModel *model;
        std::optional<std::size_t> attack_animation;
        WeaponAction action;
    };

    struct CharacterModels {
        std::uint32_t keyid;
        scene::SceneModel *character;
        scene::SceneModel *weapon;
        std::optional<std::size_t> mount;
        std::optional<std::size_t> attack_animation;
        std::uint64_t attack_sequence;
        std::optional<std::size_t> nod_animation;
        std::uint64_t nod_sequence;
        std::uint64_t hit_sequence = 0;
        bool dead = false;
        std::map<std::uint32_t, WeaponModel> weapons;
        scene::SceneLight *hand_light = nullptr;
        bool uses_flashlight = false;

        struct FlameParticle {
            glm::vec3 position;
            glm::vec3 velocity;
            float age;
            float lifetime;
            float variation;
            bool spark;
        };

        std::vector<FlameParticle> particles;
        glm::vec3 previous_tip{};
        float emission = 0;
        std::uint32_t particle_sequence = 0;
        bool burning = false;
        scene::SceneModel *helmet = nullptr;
        std::optional<std::size_t> head_mount;
        std::optional<std::size_t> eyes_mount;
        glm::mat4 eyes_offset{1};
        std::optional<std::size_t> idle_animation;
        std::optional<std::size_t> move_animation;
        std::optional<std::size_t> run_animation;
        std::optional<std::size_t> body_attack_animation;
        std::optional<std::size_t> death_animation;
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

    float flame_time_ = 0;
    scene::GpuModel helmet_model_;
    MaterialInfo helmet_material_{
        .parameters = {.metallic_factor = 1.0F, .roughness_factor = 0.35F}};
    // Declared after assets: scene instances borrow their meshes and materials.
    scene::Scene scene_;
    OutputSettings output_;
    std::size_t lighting_mode_ = 0;
    bool first_person_ = false;
    bool showcase_animated_ = true;
    float showcase_time_ = 0.0F;
    double time_of_day_ = 9.0; // Hours, wrapped to [0, 24).
    float time_rate_ = 1.0F;
    int grass_density_ = 4;
    std::vector<std::pair<scene::SceneModel *, int>> grass_models_;
    scene::SceneLight *sun_ = nullptr; // Scene-owned, valid until scene_ is destroyed.
    audio::AudioContext audio_context_;
    audio::Listener audio_listener_{audio_context_};
};

} // namespace lc1
