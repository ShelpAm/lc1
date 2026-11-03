#include "lc1/game/world/world-presentation.hpp"
#include "lc1/game/character/character-model.hpp"
#include "lc1/game/party/party.hpp"
#include "lc1/image.hpp"
#include "lc1/scene/lights/directional-light.hpp"
#include "lc1/scene/lights/sphere-light.hpp"
#include "lc1/scene/lights/spot-light.hpp"
#include "lc1/scene/model.hpp"
#include "lc1/text/utf8.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>

namespace lc1 {
namespace {
constexpr std::array lighting_names{"All",  "Directional", "Point",
                                    "Spot", "Sphere",      "Emission Only"};

// UI volume is linear in slider position, but AL_GAIN is a linear amplitude.
// Squaring gives the low end finer control, which feels closer to a normal volume knob.
float volume_to_gain(float volume_percent)
{
    float const normalized = std::clamp(volume_percent / 100.0F, 0.0F, 1.0F);
    return normalized * normalized;
}

// One font per frame, and its atlas is fixed once constructed: the face has to cover every
// script save data can hold, because the atlas requests only the codepoints that will be
// drawn. `runtime/assets/` is not tracked, so this is a runtime input; see
// docs/text-rendering.md.
constexpr std::string_view font_file = "fonts/SourceHanSansCN-Regular.otf";

// The atlas holds one fixed set of glyphs and layout silently skips what it does not hold,
// so every character the world or the HUD can draw has to be requested up front. Names come
// from save records, which is where non-ASCII text enters the game; the remaining drawn text
// is ASCII (`ascii_codepoints()`, the map's own names and the debug UI).
std::u32string font_codepoints(GameWorld const &world)
{
    std::u32string codepoints = text::ascii_codepoints();
    auto const append = [&codepoints](std::string_view name) {
        codepoints.append(text::decode_utf8(name));
    };
    for (auto const *character : world.characters()) {
        append(character->config().name);
        append(character->faction()->name());
    }
    return codepoints;
}

} // namespace

WorldPresentation::ModelResources::ModelResources(Device const &device,
                                                  std::filesystem::path const &asset_root,
                                                  ModelConfig const &config,
                                                  CharacterBody const &body)
    : config(config), body(body), model(device, load_configured_model(config, asset_root, body)),
      material(config.material.value().material())
{
}

WorldPresentation::WorldPresentation(Device const &device, std::filesystem::path const &asset_root,
                                     GameWorld &world, OutputSettings output)
    : world_(&world), font_{device, text::GlyphAtlas{asset_root / font_file,
                                                     {.codepoints = font_codepoints(world)}}},
      white_{device, Image::load_from_file(asset_root / "textures/prototype-white.png")},
      continent_view_{device, white_, world.continent()},
      showcase_{device, world.continent().spawn()}, output_(output)
{
    std::vector<scene::SceneModel *> character_models;
    character_models.reserve(world.characters().size());
    std::vector<std::optional<std::size_t>> weapon_mounts;
    std::vector<std::pair<Weapon *, scene::SceneModel *>> weapon_models;
    auto const resources_for = [&](ModelConfig const &config,
                                   CharacterBody const &body) -> ModelResources & {
        auto const found = std::ranges::find_if(model_resources_, [&](auto const &entry) {
            return entry->config == config &&
                   (!config.path.value().empty() || (entry->body.half_width == body.half_width &&
                                                     entry->body.height == body.height &&
                                                     entry->body.eye_height == body.eye_height));
        });
        if (found != model_resources_.end())
            return **found;
        auto resources = std::make_unique<ModelResources>(device, asset_root, config, body);
        auto const &material = config.material.value();
        struct TextureSlot {
            MaterialTexture *slot;
            std::string const *path;
            TextureColorSpace color_space;
        };
        std::array const slots{
            TextureSlot{&resources->material.base_color, &material.base_color_texture.value(),
                        TextureColorSpace::Srgb},
            TextureSlot{&resources->material.metallic_roughness,
                        &material.metallic_roughness_texture.value(), TextureColorSpace::Linear},
            TextureSlot{&resources->material.normal, &material.normal_texture.value(),
                        TextureColorSpace::Linear},
            TextureSlot{&resources->material.emissive, &material.emissive_texture.value(),
                        TextureColorSpace::Srgb}};
        for (auto const &slot : slots) {
            if (slot.path->empty())
                continue;
            auto const key =
                std::pair{std::filesystem::path{*slot.path}.lexically_normal().generic_string(),
                          slot.color_space};
            auto found_texture = model_textures_.find(key);
            if (found_texture == model_textures_.end()) {
                auto texture = std::make_unique<GpuTexture>(
                    device, Image::load_from_file(asset_root / key.first), slot.color_space);
                found_texture = model_textures_.emplace(key, std::move(texture)).first;
            }
            slot.slot->texture = found_texture->second.get();
        }
        model_resources_.push_back(std::move(resources));
        return *model_resources_.back();
    };
    for (std::size_t i = 0; i < world.characters().size(); ++i) {
        auto &character = *world.characters()[i];
        auto const &config = character.config().model;
        auto &resources = resources_for(config, character.body());
        auto *model = scene_.add_model(
            scene::SceneModel{.model = &resources.model,
                              .material = &resources.material,
                              .world_transform = character.transform().model_matrix()});
        character_models.push_back(model);
        if (&character == &world.player()) {
            nod_animation_index_ =
                resources.model.definition().find_animatinon(*config.action_animation);
            if (!config.eyes.value().empty()) {
                eyes_node_index_ = resources.model.definition().find_node(config.eyes.value());
                if (!eyes_node_index_)
                    fail("character model is missing eyes mount {}", config.eyes.value());
            }
        }
        else if (auto const idle =
                     resources.model.definition().find_animatinon(*config.idle_animation)) {
            model->pose.play_animation(*idle);
            auto const &clip = resources.model.definition().animations()[*idle];
            model->pose.update_animation(static_cast<float>(i % 11) * clip.duration() / 11.0F);
        }

        std::optional<std::size_t> mount;
        auto const slot = character.config().weapon.find(WeaponType::MeleeWeapon);
        if (slot != character.config().weapon.end() &&
            !slot->second->model_config().path.value().empty()) {
            mount = resources.model.definition().find_node(config.weapon_mount.value());
            if (!mount)
                fail("character {} model is missing weapon mount {}", character.config().keyid,
                     config.weapon_mount.value());
            auto const &weapon_config = slot->second->model_config();
            auto &weapon_resources = resources_for(weapon_config, character.body());
            auto *weapon_model = scene_.add_model(
                scene::SceneModel{.model = &weapon_resources.model,
                                  .material = &weapon_resources.material,
                                  .world_transform = model->node_world_matrix(*mount)});
            weapon_models.emplace_back(slot->second, weapon_model);
        }
        weapon_mounts.push_back(mount);
    }

    scene::DirectionalLight const sun{
        .base = {.color = glm::vec3{1.0F, 0.95F, 0.9F}, .intensity = 0.3F},
        .direction = glm::normalize(glm::vec3{-1.0F, -1.0F, -1.0F})};
    scene::SphereLight const lamp{.base = {.color = glm::vec3{1.0F, 0.5F, 0.5F}, .intensity = 128},
                                  .position =
                                      world_->continent().spawn() + glm::vec3{3.0F, 10.0F, 0.0F},
                                  .radius = 1.0F,
                                  .shadow_sample_count = 2};
    scene::SpotLight const spotlight{
        .base = {.color = glm::vec3{0.5F, 0.5F, 1.0F}, .intensity = 64},
        .position = world_->continent().spawn() + glm::vec3{-3.0F, 10.0F, 0.0F},
        .direction = glm::normalize(glm::vec3{1.0F, -1.0F, 0.0F}),
        .inner_angle = glm::radians(15.0F),
        .outer_angle = glm::radians(30.0F)};
    lights_.push_back(scene_.add_light(to_light(sun)));
    lights_.push_back(scene_.add_light(to_light(lamp)));
    for (auto const &light : showcase_.lights())
        lights_.push_back(scene_.add_light(light));
    flashlight_ = scene_.add_light(to_light(spotlight));
    lights_.push_back(flashlight_);

    auto const add_draw = [this](scene::SceneModel const &draw) { return scene_.add_model(draw); };
    for (auto const &draw : continent_view_.draw_items())
        add_draw(draw);
    for (auto const &draw : showcase_.draws())
        showcase_models_.push_back(add_draw(draw));
    update_lighting();

    set_volume(50.0F);

    // Publish borrows only after all potentially failing resource setup succeeds.
    for (std::size_t i = 0; i < character_models.size(); ++i) {
        world.characters()[i]->model_ = character_models[i];
        world.characters()[i]->weapon_mount_index_ = weapon_mounts[i];
    }
    for (auto const &[weapon, model] : weapon_models)
        weapon->model_ = model;
}

WorldPresentation::~WorldPresentation()
{
    for (auto *character : world_->characters()) {
        character->model_ = nullptr;
        character->weapon_mount_index_.reset();
        for (auto const &[_, weapon] : character->config().weapon)
            weapon->model_ = nullptr;
    }
}

scene::Scene const &WorldPresentation::scene() const
{
    return scene_;
}

void WorldPresentation::update_instances()
{
    auto const showcase = showcase_.draws();
    for (std::size_t i = 0; i < showcase.size(); ++i)
        showcase_models_[i]->world_transform = showcase[i].world_transform;
    for (auto *character : world_->characters()) {
        character->model()->world_transform = character->transform().model_matrix();
        if (character->weapon_mount_index_)
            character->config().weapon.at(WeaponType::MeleeWeapon)->model()->world_transform =
                character->model()->node_world_matrix(*character->weapon_mount_index_);
    }
}

std::vector<WorldTextItem> WorldPresentation::world_labels(scene::FpsCamera const &camera) const
{
    std::vector<WorldTextItem> labels;
    labels.append_range(showcase_.labels());

    if (world_->continent().identity().id == "present") {
        glm::vec3 const center{27.0F, 3.4F, 34.0F};
        labels.push_back({.text = "Patrol House",
                          .center = center,
                          .line_height = 0.5F,
                          .normal = camera.position() - center});
    }

    for (auto const *character : world_->characters())
        labels.push_back(WorldTextItem{
            .text = std::format("{} from {}", character->config().name,
                                character->config().party->faction()->name()),
            .center = character->position() + glm::vec3{0, character->height() + 0.5, 0},
            .line_height = 0.3F,
            .normal =
                camera.position() - (character->position() + glm::vec3{0, character->height() + 0.5,
                                                                       0}), // from center to cam
        });

    for (auto const *character : world_->characters()) {
        auto const *dialog = world_->dialogs().find(*character);
        if (!dialog || dialog->contains(world_->player()) || dialog->speaker() != character)
            continue;
        auto const center = character->position() + glm::vec3{0, character->height() + 1.0F, 0};
        labels.push_back(WorldTextItem{.text = std::string{dialog->current_speech_text()},
                                       .center = center,
                                       .line_height = 0.25F,
                                       .normal = camera.position() - center});
    }
    return labels;
}

std::uint32_t WorldPresentation::object_capacity() const
{
    std::size_t const maximum = std::numeric_limits<std::uint32_t>::max();
    auto const count = scene_.draw_count();
    if (count > maximum)
        fail("world draw capacity exceeds the supported range");
    return static_cast<std::uint32_t>(count);
}

void WorldPresentation::update_lighting()
{
    // Hold the flashlight above ground and outside the mesh, on the aiming side.
    auto &flashlight = flashlight_->parameters;
    flashlight.position = world_->player().position() + glm::vec3{0.0F, 1.3F, 0.0F} +
                          world_->player().heading() * 0.5F + world_->player().right() * 0.5F;
    flashlight.direction = world_->player().heading();
    auto const select = [this](scene::SceneLight &light) {
        light.enabled = lighting_mode_ == 0 ||
                        (lighting_mode_ <= 4 && light.parameters.type == lighting_mode_ - 1);
    };
    for (auto *light : lights_)
        select(*light);
}

GameLocation WorldPresentation::location() const
{
    // Location follows the character even while the camera is panned elsewhere.
    glm::vec2 const position{world_->player().position().x, world_->player().position().z};
    auto const *region = world_->continent().region_at(position);
    auto const *place = world_->continent().location_at(position);
    auto const *road = world_->continent().road_at(position);
    auto const local = world_->player().position() - world_->continent().spawn();
    bool const in_showcase = std::abs(local.x) < 24 && std::abs(local.z) < 23;
    return {.region = region->name,
            .place = in_showcase ? "Material Showcase"
                     : place     ? std::string_view{place->name}
                     : road      ? std::string_view{road->name}
                                 : "Wilderness"};
}

std::string_view WorldPresentation::lighting_name() const
{
    return lighting_names[lighting_mode_];
}

void WorldPresentation::update(float delta_seconds, WorldPresentationInput const &input)
{
    if (input.nod && nod_animation_index_)
        world_->player().model()->pose.play_animation(*nod_animation_index_, false);
    if (input.cycle_lighting)
        lighting_mode_ = (lighting_mode_ + 1) % lighting_names.size();
    if (input.toggle_showcase_motion)
        showcase_animated_ = !showcase_animated_;
    output_.exposure =
        std::clamp(output_.exposure * std::exp2(static_cast<float>(input.exposure_steps) * 0.5F),
                   0.03125F, 16.0F);
    if (showcase_animated_)
        showcase_time_ += delta_seconds;
    showcase_.update(showcase_time_);
    update_lighting();
    scene_.update_animations(delta_seconds);
    update_instances();
}

void WorldPresentation::update_listener(scene::FpsCamera const &camera)
{
    auto const position = camera.position();
    auto const forward = camera.forward();
    auto const up = camera.up();
    audio_listener_.set_position(position.x, position.y, position.z);
    audio_listener_.set_orientation(forward.x, forward.y, forward.z, up.x, up.y, up.z);
}

glm::mat4 WorldPresentation::player_eye_transform() const
{
    if (eyes_node_index_)
        return world_->player().model()->node_world_matrix(*eyes_node_index_);
    auto transform = world_->player().transform().model_matrix();
    transform[3] = glm::vec4{world_->player().eye_position(), 1.0F};
    return transform;
}

void WorldPresentation::set_volume(float volume_percent)
{
    audio_listener_.set_gain(volume_to_gain(volume_percent));
}

} // namespace lc1

namespace lc1 {

OutputSettings const &WorldPresentation::output() const
{
    return output_;
}

GpuFont const &WorldPresentation::font() const
{
    return font_;
}

bool WorldPresentation::showcase_animated() const
{
    return showcase_animated_;
}

audio::AudioContext const &WorldPresentation::audio_context() const
{
    return audio_context_;
}

} // namespace lc1
