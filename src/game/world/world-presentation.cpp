#include "lc1/game/world/world-presentation.hpp"
#include "lc1/game/character/character-model.hpp"
#include "lc1/game/party/party.hpp"
#include "lc1/game/resources/catalog.hpp"
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
#include <numbers>

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
      helmet_model_(device, make_helmet_model()), output_(output)
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

        if (character.dead)
            model->pose.reset_pose();
        auto const mount = resources.model.definition().find_node(config.weapon_mount.value());
        std::map<std::uint32_t, WeaponModel> weapons;
        scene::SceneModel *weapon_model = nullptr;
        std::optional<std::size_t> attack_animation;
        bool uses_flashlight = false;
        // Preload definition assets and retain hidden placements. Switching equipment
        // does not destroy resources borrowed by in-flight frames or grow frame capacity.
        for (auto const &[id, definition] : world.weapons) {
            auto const &weapon_config = definition.model_config();
            if (weapon_config.path.value().empty())
                continue;
            auto &weapon_resources = resources_for(weapon_config, character.pose.body);
            auto *placement = scene_.add_model({.model = &weapon_resources.model,
                                                .material = &weapon_resources.material,
                                                .visible = false});
            auto const &name = weapon_config.action_animation.value();
            std::optional<std::size_t> animation;
            if (!name.empty()) {
                animation = weapon_resources.model.definition().find_animatinon(name);
                if (!animation)
                    fail("model is missing animation {}", name);
            }
            weapons.emplace(id, WeaponModel{placement, animation, definition.action()});
            if (character.main_hand && character.main_hand->keyid() == id) {
                placement->visible = true;
                weapon_model = placement;
                attack_animation = animation;
                uses_flashlight = definition.action() == WeaponAction::ToggleLight;
            }
        }
        auto *helmet = scene_.add_model(
            {.model = &helmet_model_, .material = &helmet_material_, .visible = false});
        auto *flashlight = scene_.add_light(
            to_light(scene::SpotLight{.base = {.color = {1.0F, 0.95F, 0.8F}, .intensity = 64},
                                      .position = character.pose.position(),
                                      .direction = character.pose.look_direction(),
                                      .inner_angle = glm::radians(15.0F),
                                      .outer_angle = glm::radians(30.0F)}));
        flashlight->enabled = character.flashlight_on;
        character_models_.push_back({.keyid = character.keyid,
                                     .character = model,
                                     .weapon = weapon_model,
                                     .mount = mount,
                                     .attack_animation = attack_animation,
                                     .attack_sequence = character.attack_sequence,
                                     .nod_animation = resources.model.definition().find_animatinon(
                                         *config.action_animation),
                                     .nod_sequence = character.nod_sequence,
                                     .hit_sequence = character.hit_sequence,
                                     .dead = character.dead,
                                     .weapons = std::move(weapons),
                                     .flashlight = flashlight,
                                     .uses_flashlight = uses_flashlight,
                                     .helmet = helmet,
                                     .head_mount = resources.model.definition().find_node("Head")});
    }

    scene::DirectionalLight const sun{
        .base = {.color = glm::vec3{1.0F, 0.95F, 0.9F}, .intensity = 0.45F},
        .direction = glm::normalize(glm::vec3{-1.0F, -1.0F, -1.0F})};
    scene::SphereLight const lamp{.base = {.color = glm::vec3{1.0F, 0.5F, 0.5F}, .intensity = 128},
                                  .position = continent_->spawn() + glm::vec3{3.0F, 10.0F, 0.0F},
                                  .radius = 1.0F,
                                  .shadow_sample_count = 2};
    sun_ = scene_.add_light(to_light(sun));
    lights_.push_back(sun_);
    lights_.push_back(scene_.add_light(to_light(lamp)));
    for (auto const &light : showcase_.lights())
        lights_.push_back(scene_.add_light(light));

    auto const add_draw = [this](scene::SceneModel const &draw) { return scene_.add_model(draw); };
    std::vector<scene::SceneModel *> terrain_models;
    for (auto const &draw : continent_view_.draw_items())
        terrain_models.push_back(add_draw(draw));
    for (auto const &batch : continent_view_.grass_batches())
        grass_models_.emplace_back(terrain_models[batch.draw_index], batch.level);
    for (auto const &draw : showcase_.draws())
        showcase_models_.push_back(add_draw(draw));
    update_instances();
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
        models.character->world_transform =
            state.pose.transform().model_matrix() *
            character_reaction_transform(state.dead, state.reaction_time, state.pose.body);
        auto const head = state.equipment.find(EquipmentType::Head);
        models.helmet->visible =
            head != state.equipment.end() && head->second.keyid() == game::iron_helmet.keyid;
        auto const radius = state.pose.body.half_width * 1.08F;
        auto const head_transform =
            models.head_mount
                ? models.character->node_world_matrix(*models.head_mount)
                : models.character->world_transform *
                      glm::translate(glm::mat4{1},
                                     glm::vec3{0, state.pose.body.height * (2.0F / 3.0F), 0});
        models.helmet->world_transform =
            head_transform *
            glm::translate(glm::mat4{1},
                           glm::vec3{0, state.pose.body.height / 3.0F - radius * 0.8F, 0}) *
            glm::scale(glm::mat4{1}, glm::vec3{radius});
        if (models.weapon) {
            auto const hand = models.mount
                                  ? glm::vec3{models.character->node_world_matrix(*models.mount)[3]}
                                  : state.pose.position() +
                                        glm::vec3{0, state.pose.body.eye_height - 0.3F, 0} +
                                        state.pose.right() * 0.35F;
            if (models.uses_flashlight)
                models.weapon->world_transform =
                    glm::translate(glm::mat4{1}, hand) *
                    glm::mat4_cast(
                        glm::quatLookAt(state.pose.look_direction(), glm::vec3{0, 1, 0}));
            else if (models.mount)
                models.weapon->world_transform = models.character->node_world_matrix(*models.mount);
            else
                models.weapon->world_transform =
                    glm::translate(glm::mat4{1}, hand) *
                    glm::rotate(glm::mat4{1}, glm::radians(state.pose.body_yaw()),
                                glm::vec3{0, 1, 0});
        }
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
    auto const select = [this](scene::SceneLight &light) {
        light.enabled = lighting_mode_ == 0 ||
                        (lighting_mode_ <= 4 && light.parameters.type == lighting_mode_ - 1);
    };
    for (auto *light : lights_)
        select(*light);
    // At 06:00 the sun rises in +X, reaches its zenith at noon and sets at 18:00.
    auto const angle = (time_of_day_ - 6.0) * std::numbers::pi / 12.0;
    auto const altitude = static_cast<float>(std::sin(angle));
    sun_->parameters.direction = glm::vec3{-static_cast<float>(std::cos(angle)), -altitude, 0.0F};
    sun_->parameters.intensity = 0.45F * std::max(altitude, 0.0F);
    sun_->enabled = sun_->enabled && altitude > 0.0F;
    scene_.set_diffuse_gi(scene::DiffuseGi{
        .center = world_->player(observer_).pose.position() + glm::vec3{0, 6, 0},
        .sky_radiance = sun_->enabled ? glm::vec3{0.08F, 0.105F, 0.14F} * altitude : glm::vec3{0},
    });
    for (std::size_t i = 0; i < character_models_.size(); ++i) {
        auto &models = character_models_[i];
        auto &light = *models.flashlight;
        light.enabled = world_->characters[i].flashlight_on && models.weapon &&
                        models.uses_flashlight && (lighting_mode_ == 0 || lighting_mode_ == 3);
        if (light.enabled) {
            light.parameters.position =
                glm::vec3{models.weapon->world_transform * glm::vec4{0, 0, -0.25F, 1}};
            light.parameters.direction =
                glm::normalize(glm::vec3{models.weapon->world_transform * glm::vec4{0, 0, -1, 0}});
        }
    }
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
    // One game hour per real minute at 1x: 24 minutes per day.
    time_of_day_ =
        std::fmod(time_of_day_ + static_cast<double>(delta_seconds) * time_rate_ / 60.0, 24.0);
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
    for (std::size_t i = 0; i < character_models_.size(); ++i) {
        auto &models = character_models_[i];
        auto const &state = world_->characters[i];
        auto const selected =
            state.main_hand ? models.weapons.find(state.main_hand->keyid()) : models.weapons.end();
        auto *placement = selected == models.weapons.end() ? nullptr : selected->second.model;
        if (models.weapon != placement) {
            if (models.weapon)
                models.weapon->visible = false;
            models.weapon = placement;
            models.attack_animation =
                selected == models.weapons.end() ? std::nullopt : selected->second.attack_animation;
            models.uses_flashlight = selected != models.weapons.end() &&
                                     selected->second.action == WeaponAction::ToggleLight;
            if (placement) {
                placement->pose.reset_pose();
                placement->visible = true;
            }
            models.attack_sequence = state.attack_sequence;
        }
        if (state.dead || state.hit_sequence != models.hit_sequence) {
            if (state.dead && !models.dead)
                models.character->pose.reset_pose();
            if (models.weapon && (state.hit_sequence != models.hit_sequence || !models.dead))
                models.weapon->pose.reset_pose();
            models.dead = state.dead;
            models.hit_sequence = state.hit_sequence;
            models.nod_sequence = state.nod_sequence;
            models.attack_sequence = state.attack_sequence;
            continue;
        }
        auto const nod = state.nod_sequence;
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
    update_lighting();
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

namespace lc1 {

double WorldPresentation::time_of_day() const
{
    return time_of_day_;
}

float WorldPresentation::time_rate() const
{
    return time_rate_;
}

void WorldPresentation::set_time_rate(float rate)
{
    time_rate_ = rate;
}

int WorldPresentation::grass_density() const
{
    return grass_density_;
}

void WorldPresentation::set_grass_density(int density)
{
    grass_density_ = density;
    for (auto const &[model, level] : grass_models_)
        model->visible = level <= density;
}

} // namespace lc1
