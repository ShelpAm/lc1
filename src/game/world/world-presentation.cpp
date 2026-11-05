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
std::u32string font_codepoints(WorldSnapshot const &world)
{
    std::u32string codepoints = text::ascii_codepoints();
    auto const append = [&codepoints](std::string_view name) {
        codepoints.append(text::decode_utf8(name));
    };
    for (auto const &character : world.characters) {
        append(character.name);
        append(character.faction);
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
                                     Continent const &continent, WorldSnapshot const &world,
                                     OutputSettings output, PlayerId observer)
    : world_(&world), continent_(&continent), observer_(observer),
      font_{device,
            text::GlyphAtlas{asset_root / font_file, {.codepoints = font_codepoints(world)}}},
      white_{device, Image::load_from_file(asset_root / "textures/prototype-white.png")},
      continent_view_{device, white_, continent}, showcase_{device, continent.spawn()},
      output_(output)
{
    character_models_.reserve(world.characters.size());
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
    for (std::size_t i = 0; i < world.characters.size(); ++i) {
        auto const &character = world.characters[i];
        auto const &config = character.model;
        auto &resources = resources_for(config, character.pose.body);
        auto *model = scene_.add_model(
            scene::SceneModel{.model = &resources.model,
                              .material = &resources.material,
                              .world_transform = character.pose.transform().model_matrix()});

        if (character.keyid == world.player(observer_).keyid) {
            observed_model_index_ = i;
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
        scene::SceneModel *weapon_model = nullptr;
        std::optional<std::size_t> attack_animation;
        if (character.weapon_model && !character.weapon_model->path.value().empty()) {
            mount = resources.model.definition().find_node(config.weapon_mount.value());
            if (!mount)
                fail("character {} model is missing weapon mount {}", character.keyid,
                     config.weapon_mount.value());
            auto const &weapon_config = *character.weapon_model;
            auto &weapon_resources = resources_for(weapon_config, character.pose.body);
            weapon_model = scene_.add_model(
                scene::SceneModel{.model = &weapon_resources.model,
                                  .material = &weapon_resources.material,
                                  .world_transform = model->node_world_matrix(*mount)});
            auto const &name = weapon_config.action_animation.value();
            if (!name.empty()) {
                attack_animation = weapon_resources.model.definition().find_animatinon(name);
                if (!attack_animation)
                    fail("model is missing animation {}", name);
            }
        }
        character_models_.push_back({.keyid = character.keyid,
                                     .character = model,
                                     .weapon = weapon_model,
                                     .mount = mount,
                                     .attack_animation = attack_animation,
                                     .attack_sequence = character.attack_sequence,
                                     .nod_animation = resources.model.definition().find_animatinon(
                                         *config.action_animation),
                                     .nod_sequence = character.nod_sequence});
    }

    scene::DirectionalLight const sun{
        .base = {.color = glm::vec3{1.0F, 0.95F, 0.9F}, .intensity = 0.3F},
        .direction = glm::normalize(glm::vec3{-1.0F, -1.0F, -1.0F})};
    scene::SphereLight const lamp{.base = {.color = glm::vec3{1.0F, 0.5F, 0.5F}, .intensity = 128},
                                  .position = continent_->spawn() + glm::vec3{3.0F, 10.0F, 0.0F},
                                  .radius = 1.0F,
                                  .shadow_sample_count = 2};
    scene::SpotLight const spotlight{
        .base = {.color = glm::vec3{0.5F, 0.5F, 1.0F}, .intensity = 64},
        .position = continent_->spawn() + glm::vec3{-3.0F, 10.0F, 0.0F},
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
}

scene::SceneModel const &WorldPresentation::character_model(std::uint32_t keyid) const
{
    auto const found = std::ranges::find(character_models_, keyid, &CharacterModels::keyid);
    if (found == character_models_.end())
        fail("unknown character model {}", keyid);
    return *found->character;
}

scene::SceneModel const *WorldPresentation::weapon_model(std::uint32_t keyid) const
{
    auto const found = std::ranges::find(character_models_, keyid, &CharacterModels::keyid);
    if (found == character_models_.end())
        fail("unknown character model {}", keyid);
    return found->weapon;
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
    for (std::size_t i = 0; i < character_models_.size(); ++i) {
        auto const &state = world_->characters[i];
        auto &models = character_models_[i];
        models.character->world_transform = state.pose.transform().model_matrix();
        if (models.mount)
            models.weapon->world_transform = models.character->node_world_matrix(*models.mount);
    }
}

std::vector<WorldTextItem> WorldPresentation::world_labels(scene::FpsCamera const &camera) const
{
    std::vector<WorldTextItem> labels;
    labels.append_range(showcase_.labels());

    if (continent_->identity().id == "present") {
        glm::vec3 const center{27.0F, 3.4F, 34.0F};
        labels.push_back({.text = "Patrol House",
                          .center = center,
                          .line_height = 0.5F,
                          .normal = camera.position() - center});
    }

    for (auto const &character : world_->characters) {
        auto const center =
            character.pose.position() + glm::vec3{0, character.pose.body.height + 0.5F, 0};
        labels.push_back({.text = std::format("{} from {}", character.name, character.faction),
                          .center = center,
                          .line_height = 0.3F,
                          .normal = camera.position() - center});
        if (!character.speech.empty() && character.keyid != world_->player(observer_).keyid &&
            character.dialog_partner != world_->player(observer_).keyid) {
            auto const speech_center = center + glm::vec3{0, 0.5F, 0};
            labels.push_back({.text = character.speech,
                              .center = speech_center,
                              .line_height = 0.25F,
                              .normal = camera.position() - speech_center});
        }
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
    flashlight.position = world_->player(observer_).pose.position() + glm::vec3{0.0F, 1.3F, 0.0F} +
                          world_->player(observer_).pose.heading() * 0.5F +
                          world_->player(observer_).pose.right() * 0.5F;
    flashlight.direction = world_->player(observer_).pose.heading();
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
    glm::vec2 const position{world_->player(observer_).pose.position().x,
                             world_->player(observer_).pose.position().z};
    auto const *region = continent_->region_at(position);
    auto const *place = continent_->location_at(position);
    auto const *road = continent_->road_at(position);
    auto const local = world_->player(observer_).pose.position() - continent_->spawn();
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
    for (std::size_t i = 0; i < character_models_.size(); ++i) {
        auto &models = character_models_[i];
        auto const nod = world_->characters[i].nod_sequence;
        if (nod != models.nod_sequence) {
            if (models.nod_animation)
                models.character->pose.play_animation(*models.nod_animation, false);
            models.nod_sequence = nod;
        }
        auto const sequence = world_->characters[i].attack_sequence;
        if (sequence != models.attack_sequence) {
            if (models.attack_animation)
                models.weapon->pose.play_animation(*models.attack_animation, false);
            models.attack_sequence = sequence;
        }
    }
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

glm::mat4 WorldPresentation::observer_eye_transform() const
{
    if (eyes_node_index_)
        return character_models_[observed_model_index_].character->node_world_matrix(
            *eyes_node_index_);
    auto transform = world_->player(observer_).pose.transform().model_matrix();
    transform[3] = glm::vec4{world_->player(observer_).pose.eye_position(), 1.0F};
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
