#include "lc1/game/world/world-presentation.hpp"
#include "lc1/game/character/character-model.hpp"
#include "lc1/game/continent/village.hpp"
#include "lc1/game/debug/world-setup.hpp"
#include "lc1/game/dialog/dialog.hpp"
#include "lc1/game/party/party.hpp"
#include "lc1/game/resources/catalog.hpp"
#include "lc1/image.hpp"
#include "lc1/scene/lights/directional-light.hpp"
#include "lc1/scene/lights/sphere-light.hpp"
#include "lc1/scene/lights/spot-light.hpp"
#include "lc1/scene/model.hpp"
#include "lc1/text/utf8.hpp"
#include "lc1/utf8-path.hpp"
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
// drawn. `runtime/assets/` is versioned via Git LFS; see
// docs/text-rendering.md.
constexpr std::string_view font_file = "fonts/SourceHanSansCN-Regular.otf";

// Include every possible dialog branch before the fixed atlas is uploaded.
std::u32string font_codepoints(WorldSnapshot const &world, DialogCatalog const &dialogs)
{
    std::u32string codepoints = text::ascii_codepoints();
    codepoints.append(dialogs.codepoints());
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
                                                  CharacterBody const &body,
                                                  MaterialInfo const &material)
    : config(config), body(body),
      model(device, load_configured_model(config, asset_root, body), material), material(material)
{
}

WorldPresentation::ModelResources &
WorldPresentation::resources_for(Device const &device, std::filesystem::path const &asset_root,
                                 ModelConfig const &config, CharacterBody const &body)
{
    auto const found = std::ranges::find_if(model_resources_, [&](auto const &entry) {
        return entry->config == config &&
               (!config.path.value().empty() ||
                (entry->body.half_width == body.half_width && entry->body.height == body.height &&
                 entry->body.eye_height == body.eye_height));
    });
    if (found != model_resources_.end())
        return **found;

    auto const &configured = config.material.value();
    auto material = configured.material();
    auto const load_texture = [&](std::string const &path,
                                  TextureColorSpace space) -> MaterialTexture {
        if (path.empty())
            return {};
        auto const key = std::pair{path_from_utf8(path).lexically_normal(), space};
        auto found = model_textures_.find(key);
        if (found == model_textures_.end())
            found =
                model_textures_
                    .emplace(key, std::make_unique<GpuTexture>(
                                      device, Image::load_from_file(asset_root / key.first), space))
                    .first;
        return {.texture = found->second.get()};
    };
    material.base_color =
        load_texture(configured.base_color_texture.value(), TextureColorSpace::Srgb);
    material.metallic_roughness =
        load_texture(configured.metallic_roughness_texture.value(), TextureColorSpace::Linear);
    material.normal = load_texture(configured.normal_texture.value(), TextureColorSpace::Linear);
    material.emissive = load_texture(configured.emissive_texture.value(), TextureColorSpace::Srgb);
    model_resources_.push_back(
        std::make_unique<ModelResources>(device, asset_root, config, body, material));
    return *model_resources_.back();
}

WorldPresentation::WorldPresentation(Device const &device, std::filesystem::path const &asset_root,
                                     Continent const &continent, WorldSnapshot const &world,
                                     OutputSettings output, PlayerId observer,
                                     DialogCatalog const &dialogs)
    : world_(&world), continent_(&continent), observer_(observer), device_(&device),
      asset_root_(asset_root),
      font_{device, text::GlyphAtlas{asset_root / font_file,
                                     {.codepoints = font_codepoints(world, dialogs)}}},
      white_{device, Image::load_from_file(asset_root / "textures/prototype-white.png")},
      continent_view_{device, white_, continent},
      showcase_{device, continent.spawn() + (continent.identity().id == "present"
                                                 ? demo::present_showcase_offset
                                                 : glm::vec3{0})},
      helmet_model_(device, make_helmet_model()), output_(output)
{
    output_.bloom_strength = 0.12F;
    sync_character_models();

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
    if (continent_->location(village::smithy)) {
        lights_.push_back(scene_.add_light(
            to_light(scene::SphereLight{.base = {.color = {1.0F, 0.28F, 0.045F}, .intensity = 28},
                                        .position = village::hearth + glm::vec3{0, 0.35F, 0},
                                        .radius = 0.4F,
                                        .shadow_sample_count = 2})));
    }
    if (continent_->location(village::smithy) && continent_->location(village::tower)) {
        for (auto const *name : {"wind.wav", "hammer.wav", "bell.wav"}) {
            auto &source = village_sounds_.emplace_back(audio_context_);
            source.set_looping(true);
            source.set_gain(0);
            source.play(
                audio::Sound::load_from_file(audio_context_, asset_root / "audio/village" / name));
        }
        village_sounds_[1].set_reference_distance(8);
        village_sounds_[2].set_reference_distance(18);
        village_sounds_[1].set_position(village::hearth.x, village::hearth.y, village::hearth.z);
        auto const *tower = continent_->location(village::tower);
        auto const center = (tower->bounds.min + tower->bounds.max) * 0.5F;
        village_sounds_[2].set_position(center.x, 17, center.y);
    }
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
    update_particles(0);

    set_volume(50.0F);
}

void WorldPresentation::sync_character_models()
{
    if (std::ranges::equal(character_models_, world_->characters, {}, &CharacterModels::keyid,
                           &CharacterSnapshot::keyid))
        return;
    for (auto const &models : character_models_) {
        auto const found = std::ranges::lower_bound(world_->characters, models.keyid, {},
                                                    &CharacterSnapshot::keyid);
        if (found != world_->characters.end() && found->keyid == models.keyid)
            continue;
        for (auto const &[id, weapon] : models.weapons)
            scene_.remove_model(weapon.model);
        scene_.remove_model(models.character);
        scene_.remove_model(models.helmet);
        scene_.remove_light(models.hand_light);
    }

    std::vector<CharacterModels> next;
    next.reserve(world_->characters.size());
    auto existing = character_models_.begin();
    for (auto const &character : world_->characters) {
        while (existing != character_models_.end() && existing->keyid < character.keyid)
            ++existing;
        if (existing != character_models_.end() && existing->keyid == character.keyid) {
            next.push_back(std::move(*existing));
            continue;
        }
        auto const i = next.size();
        auto const &config = character.model;
        auto &resources = resources_for(*device_, asset_root_, config, character.pose.body);
        auto *model = scene_.add_model(scene::SceneModel{
            .model = &resources.model,
            .material = resources.model.has_embedded_materials() ? nullptr : &resources.material,
            .world_transform = character.pose.transform().model_matrix(),
            .view_hidden_nodes =
                first_person_hidden_nodes(config, resources.model.definition().nodes())});

        std::array<std::optional<std::size_t>, character_animation_count> animations{};
        for (std::size_t index = 0; index < animations.size(); ++index) {
            auto const name = config.animation_name(static_cast<CharacterAnimation>(index));
            if (name.empty())
                continue;
            auto const clip = resources.model.definition().find_animatinon(name);
            if (!clip)
                fail("character model is missing animation {}", name);
            animations[index] = clip;
        }
        auto const idle_animation = animations[static_cast<std::size_t>(CharacterAnimation::Idle)];
        auto const death_animation =
            animations[static_cast<std::size_t>(CharacterAnimation::Death)];
        if (auto const animation = character.timed_animation;
            animation && animations[static_cast<std::size_t>(animation->clip)]) {
            model->pose.play_animation(*animations[static_cast<std::size_t>(animation->clip)],
                                       false);
            model->pose.update_animation(animation->seconds);
        }
        else if (idle_animation) {
            model->pose.play_animation(*idle_animation);
            auto const &clip = resources.model.definition().animations()[*idle_animation];
            model->pose.update_animation(static_cast<float>(i % 11) * clip.duration() / 11.0F);
        }
        if (character.dead && !death_animation) {
            model->pose.reset_pose();
        }
        else if (character.dead && !character.timed_animation) {
            model->pose.play_animation(*death_animation, false);
            model->pose.update_animation(character.reaction_time);
        }
        auto const mount = resources.model.definition().find_node(config.weapon_mount.value());
        std::optional<std::size_t> eyes_mount;
        if (!config.eyes.value().empty()) {
            eyes_mount = resources.model.definition().find_node(config.eyes.value());
            if (!eyes_mount)
                fail("character model is missing eyes mount {}", config.eyes.value());
        }
        std::map<std::uint32_t, WeaponModel> weapons;
        scene::SceneModel *weapon_model = nullptr;
        std::optional<std::size_t> attack_animation;
        bool uses_flashlight = false;
        // Preload definition assets and retain hidden placements. Switching equipment
        // does not destroy resources borrowed by in-flight frames or grow frame capacity.
        for (auto const &[id, definition] : world_->weapons) {
            auto const &weapon_config = definition.model_config();
            if (weapon_config.path.value().empty())
                continue;
            auto &weapon_resources =
                resources_for(*device_, asset_root_, weapon_config, character.pose.body);
            auto *placement =
                scene_.add_model({.model = &weapon_resources.model,
                                  .material = weapon_resources.model.has_embedded_materials()
                                                  ? nullptr
                                                  : &weapon_resources.material,
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
        auto *hand_light = scene_.add_light(
            to_light(scene::SpotLight{.base = {.color = {1.0F, 0.95F, 0.8F}, .intensity = 64},
                                      .position = character.pose.position(),
                                      .direction = character.pose.look_direction(),
                                      .inner_angle = glm::radians(15.0F),
                                      .outer_angle = glm::radians(30.0F)}));
        hand_light->enabled = character.flashlight_on;
        next.push_back({.keyid = character.keyid,
                        .character = model,
                        .weapon = weapon_model,
                        .mount = mount,
                        .attack_animation = attack_animation,
                        .attack_sequence = character.attack_sequence,
                        .nod_sequence = character.nod_sequence,
                        .hit_sequence = character.hit_sequence,
                        .dead = character.dead,
                        .weapons = std::move(weapons),
                        .hand_light = hand_light,
                        .uses_flashlight = uses_flashlight,
                        .particles = {},
                        .helmet = helmet,
                        .head_mount = resources.model.definition().find_node("Head"),
                        .eyes_mount = eyes_mount,
                        .eyes_offset = config.eyes_offset.value().transform().matrix(),
                        .animations = animations});
    }
    character_models_ = std::move(next);
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

std::optional<std::size_t> WorldPresentation::CharacterModels::locomotion(bool moving,
                                                                          bool running) const
{
    if (moving) {
        if (auto const sprint = animation(CharacterAnimation::Sprint); running && sprint)
            return sprint;
        if (auto const walk = animation(CharacterAnimation::Walk))
            return walk;
    }
    return animation(CharacterAnimation::Idle);
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
            ((state.dead && models.animation(CharacterAnimation::Death)) ||
                     (!state.dead && models.animation(CharacterAnimation::HitReact))
                 ? glm::mat4{1}
                 : character_reaction_transform(state.dead, state.reaction_time, state.pose.body));
        auto const observer = first_person_ && state.keyid == world_->player(observer_).keyid;
        models.character->use_view_indices = observer;
        models.helmet->view_visible = !observer;
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
            else if (state.main_hand && state.main_hand->action() == WeaponAction::Torch)
                models.weapon->world_transform =
                    glm::translate(glm::mat4{1}, hand) *
                    glm::rotate(glm::mat4{1}, glm::radians(state.pose.body_yaw()),
                                glm::vec3{0, 1, 0});
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
    // Billboard labels stay at the bloom soft knee's zero-contribution boundary.
    // Printed panel lettering keeps the caller's colour.
    constexpr glm::vec3 label_color{1.F, 1.F, 1.F};
    std::vector<WorldTextItem> labels;
    labels.append_range(showcase_.labels());

    if (continent_->identity().id == "present") {
        glm::vec3 const center{27.0F, 3.4F, 34.0F};
        labels.push_back({.text = "Patrol House",
                          .center = center,
                          .line_height = 0.5F,
                          .color = label_color,
                          .normal = camera.position() - center});
    }

    for (auto const &character : world_->characters) {
        auto const center =
            character.pose.position() + glm::vec3{0, character.pose.body.height + 0.5F, 0};
        labels.push_back({.text = std::format("{} from {}", character.name, character.faction),
                          .center = center,
                          .line_height = 0.3F,
                          .color = label_color,
                          .normal = camera.position() - center});
        if (!character.speech.empty() && character.keyid != world_->player(observer_).keyid &&
            character.dialog_partner != world_->player(observer_).keyid) {
            auto const speech_center = center + glm::vec3{0, 0.5F, 0};
            labels.push_back({.text = character.speech,
                              .center = speech_center,
                              .line_height = 0.25F,
                              .color = label_color,
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
    auto const position = world_->player(observer_).pose.position();
    float const village_weather =
        continent_->location(village::smithy)
            ? 1.0F - glm::smoothstep(
                         65.0F, 130.0F,
                         glm::distance(glm::vec2{position.x, position.z}, glm::vec2{0, 130}))
            : 0.0F;
    sun_->parameters.color =
        glm::mix(glm::vec3{1, 0.95F, 0.9F}, glm::vec3{0.7F, 0.8F, 1}, village_weather);
    sun_->parameters.intensity = glm::mix(0.45F, 0.09F, village_weather) * std::max(altitude, 0.0F);
    sun_->enabled = sun_->enabled && altitude > 0.0F;
    // The visible sky follows the same sun the cloud layer is lit by: daylight blue
    // towards noon, a warm band while the sun crosses the horizon, and near-black at
    // night. A flat painted colour is what made the layer look pasted on.
    auto const rise = glm::smoothstep(0.0F, 0.28F, altitude);
    auto const twilight =
        glm::smoothstep(-0.10F, 0.05F, altitude) * (1.0F - glm::smoothstep(0.10F, 0.28F, altitude));
    glm::vec3 const daylight{0.22F, 0.34F, 0.55F};
    glm::vec3 const warm{0.60F, 0.30F, 0.15F};
    glm::vec3 const night_sky{0.010F, 0.014F, 0.030F};
    scene_.set_sky(scene::Sky{.color = night_sky + daylight * rise + warm * (0.55F * twilight)});
    scene_.set_fog(continent_->location(village::smithy)
                       ? std::optional{scene::DistanceFog{
                             .radiance = glm::vec3{0.24F, 0.28F, 0.31F} * std::max(altitude, 0.0F),
                             .start = 60,
                             .end = 320}}
                       : std::nullopt);
    scene_.set_diffuse_gi(scene::DiffuseGi{
        .center = world_->player(observer_).pose.position() + glm::vec3{0, 6, 0},
        .sky_radiance = sun_->enabled ? glm::mix(glm::vec3{0.08F, 0.105F, 0.14F},
                                                 glm::vec3{0.13F, 0.16F, 0.21F}, village_weather) *
                                            altitude
                                      : glm::vec3{0},
    });
    for (std::size_t i = 0; i < character_models_.size(); ++i) {
        auto &models = character_models_[i];
        auto &light = *models.hand_light;
        auto const &state = world_->characters[i];
        bool const burning = models.weapon && state.main_hand &&
                             state.main_hand->action() == WeaponAction::Torch && !state.dead;
        auto const phase = flame_time_ + static_cast<float>(models.keyid) * 1.73F;
        auto const flicker = std::sin(phase * 9.1F) * 0.07F + std::sin(phase * 15.7F) * 0.04F;
        glm::vec3 const tip =
            burning ? glm::vec3{models.weapon->world_transform * glm::vec4{0, 0.46F, 0, 1}}
                    : glm::vec3{0};
        if (burning) {
            light.parameters = to_light(scene::SphereLight{
                .base = {.color = {1, 0.38F, 0.07F}, .intensity = 18.0F * (1.0F + flicker)},
                .position = tip + glm::vec3{0, 0.14F, 0},
                .radius = 0.08F,
                .shadow_sample_count = 2});
            light.enabled = lighting_mode_ == 0 || lighting_mode_ == 4;
            continue;
        }
        light.parameters =
            to_light(scene::SpotLight{.base = {.color = {1.0F, 0.95F, 0.8F}, .intensity = 64},
                                      .inner_angle = glm::radians(15.0F),
                                      .outer_angle = glm::radians(30.0F)});
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
    auto const local =
        world_->player(observer_).pose.position() - continent_->spawn() -
        (continent_->identity().id == "present" ? demo::present_showcase_offset : glm::vec3{0});
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
    sync_character_models();
    first_person_ = input.first_person;
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
    flame_time_ += delta_seconds;
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
        if (state.timed_animation && models.animation(state.timed_animation->clip)) {
            if (models.weapon && (state.hit_sequence != models.hit_sequence ||
                                  state.attack_sequence != models.attack_sequence))
                models.weapon->pose.reset_pose();
            models.hit_sequence = state.hit_sequence;
            models.attack_sequence = state.attack_sequence;
            continue;
        }
        if (state.dead || state.hit_sequence != models.hit_sequence) {
            if (state.dead && !models.dead) {
                if (auto const death = models.animation(CharacterAnimation::Death))
                    models.character->pose.play_animation(*death, false);
                else
                    models.character->pose.reset_pose();
            }
            if (!state.dead)
                if (auto const hit = models.animation(CharacterAnimation::HitReact))
                    models.character->pose.play_animation(*hit, false);
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
            if (auto const action = models.animation(CharacterAnimation::Action))
                models.character->pose.play_animation(*action, false);
            models.nod_sequence = nod;
        }
        auto const body_attack = models.animation(CharacterAnimation::Attack);
        auto const sequence = state.attack_sequence;
        if (sequence != models.attack_sequence) {
            if (models.attack_animation)
                models.weapon->pose.play_animation(*models.attack_animation, false);
            if (body_attack)
                models.character->pose.play_animation(*body_attack, false);
            models.attack_sequence = sequence;
        }
        auto &pose = models.character->pose;
        auto const playback = pose.playback();
        bool const action_playing =
            !state.timed_animation && playback && !playback->looping &&
            (playback->animation != body_attack || state.attack_progress < 1) &&
            playback->time < pose.model().animations()[playback->animation].duration();
        auto const locomotion = models.locomotion(state.moving, state.running);
        if (!action_playing) {
            if (locomotion &&
                (!playback || !playback->looping || playback->animation != *locomotion))
                pose.play_animation(*locomotion);
            else if (!locomotion && playback)
                pose.reset_pose();
        }
    }
    scene_.update_animations(delta_seconds);
    for (std::size_t i = 0; i < character_models_.size(); ++i) {
        auto &models = character_models_[i];
        auto const &state = world_->characters[i];
        if (auto const animation = state.timed_animation) {
            if (auto const index = models.animation(animation->clip)) {
                auto &pose = models.character->pose;
                pose.play_animation(*index, false);
                pose.update_animation(animation->seconds);
                continue;
            }
        }
        auto const body_attack = models.animation(CharacterAnimation::Attack);
        if (!state.dead && body_attack && state.attack_progress < 1) {
            auto &pose = models.character->pose;
            pose.play_animation(*body_attack, false);
            pose.update_animation(state.attack_progress *
                                  pose.model().animations()[*body_attack].duration());
        }
        if (!state.dead && models.weapon && models.attack_animation && state.attack_progress < 1) {
            auto &pose = models.weapon->pose;
            pose.play_animation(*models.attack_animation, false);
            pose.update_animation(state.attack_progress *
                                  pose.model().animations()[*models.attack_animation].duration());
        }
    }
    update_instances();
    update_lighting();
    update_particles(delta_seconds);
}

void WorldPresentation::update_particles(float delta_seconds)
{
    std::vector<scene::Particle> visible;
    for (std::size_t i = 0; i < character_models_.size(); ++i) {
        auto &emitter = character_models_[i];
        auto const &state = world_->characters[i];
        bool const burning = emitter.weapon && state.main_hand && !state.dead &&
                             state.main_hand->action() == WeaponAction::Torch;
        if (!burning) {
            emitter.particles.clear();
            emitter.burning = false;
            emitter.emission = 0;
            continue;
        }
        auto const tip = glm::vec3{emitter.weapon->world_transform * glm::vec4{0, 0.46F, 0, 1}};
        if (!emitter.burning) {
            emitter.previous_tip = tip;
            emitter.emission = 8;
        }
        for (auto &particle : emitter.particles) {
            auto const acceleration = glm::vec3{0, particle.spark ? -0.6F : 0.7F, 0};
            particle.age += delta_seconds;
            particle.position += particle.velocity * delta_seconds +
                                 acceleration * (0.5F * delta_seconds * delta_seconds);
            particle.velocity += acceleration * delta_seconds;
        }
        std::erase_if(emitter.particles,
                      [](auto const &particle) { return particle.age >= particle.lifetime; });
        // Spawn only births still relevant after a long frame, rather than a stale backlog.
        auto const interval = std::min(delta_seconds, 1.0F);
        emitter.emission += interval * 50;
        auto const count = static_cast<unsigned>(emitter.emission);
        emitter.emission -= static_cast<float>(count);
        for (unsigned birth = 0; birth < count; ++birth) {
            auto const sequence = ++emitter.particle_sequence;
            // Integer mixing gives repeatable independent variation without shared RNG state.
            auto hash = sequence * 747796405U + emitter.keyid * 2891336453U;
            hash = ((hash >> ((hash >> 28U) + 4U)) ^ hash) * 277803737U;
            hash = (hash >> 22U) ^ hash;
            float const variation = static_cast<float>(hash & 0xffffU) / 65535.0F;
            float const angle = static_cast<float>(hash >> 16U) / 65535.0F * glm::two_pi<float>();
            bool const spark = sequence % 12 == 0;
            float const lifetime = spark ? 0.8F + variation * 0.7F : 0.38F + variation * 0.25F;
            float const age =
                interval * (static_cast<float>(birth) + 0.5F) / static_cast<float>(count);
            if (age >= lifetime)
                continue;
            auto const velocity = glm::vec3{std::cos(angle) * (spark ? 0.25F : 0.08F),
                                            spark ? 0.9F + variation : 0.45F + variation * 0.35F,
                                            std::sin(angle) * (spark ? 0.25F : 0.08F)};
            auto const fraction = delta_seconds > 0 ? age / delta_seconds : 0;
            auto const origin = glm::mix(tip, emitter.previous_tip, fraction);
            auto const acceleration = glm::vec3{0, spark ? -0.6F : 0.7F, 0};
            emitter.particles.push_back(
                {origin + velocity * age + acceleration * (0.5F * age * age),
                 velocity + acceleration * age, age, lifetime, variation, spark});
        }
        emitter.previous_tip = tip;
        emitter.burning = true;
        for (auto const &particle : emitter.particles) {
            float const life = particle.age / particle.lifetime;
            float const fade = (1 - life) * (1 - life);
            auto const width = particle.spark ? 0.012F : 0.19F * (1 - life * 0.7F);
            visible.push_back({
                .position = particle.position,
                .size = {width,
                         particle.spark ? 0.04F : width * (1.6F + particle.variation * 0.5F)},
                .radiance = particle.spark ? glm::vec3{8, 2.2F, 0.2F}
                                           : glm::mix(glm::vec3{12, 5.5F, 0.5F},
                                                      glm::vec3{4, 0.25F, 0.015F}, life),
                .opacity = particle.spark ? fade : fade * 0.65F,
                .rotation = particle.spark
                                ? particle.variation * 3
                                : std::sin(flame_time_ * 5 + particle.variation * 8) * 0.16F,
                .spark = particle.spark,
            });
        }
    }
    if (continent_->location(village::smithy)) {
        // Continuous furnace tongues use the same transparent particle pass as torches.
        for (int i = 0; i < 24; ++i) {
            float const phase = glm::fract(flame_time_ * 0.8F + static_cast<float>(i) / 24.0F);
            float const angle = static_cast<float>(i) * 2.39996F;
            visible.push_back({
                .position = village::hearth + glm::vec3{std::cos(angle) * 0.5F, phase * 0.85F,
                                                        std::sin(angle) * 0.65F},
                .size = {0.22F * (1 - phase * 0.5F), 0.45F * (1 - phase * 0.7F)},
                .radiance = glm::mix(glm::vec3{10, 3.5F, 0.2F}, glm::vec3{3, 0.15F, 0.01F}, phase),
                .opacity = (1 - phase) * 0.65F,
                .rotation = std::sin(flame_time_ * 3 + angle) * 0.2F,
                .spark = false,
            });
        }
    }
    if (continent_->location(village::smithy)) {
        // Sparse smoke shares the sorted transparent pass, with no emissive light.
        for (int i = 0; i < 10; ++i) {
            float const phase = glm::fract(flame_time_ * 0.12F + static_cast<float>(i) / 10);
            float const width = 0.55F + phase * 1.4F;
            visible.push_back({
                .position = village::chimney +
                            glm::vec3{phase * 3.5F, 0.3F + phase * 7, std::sin(phase * 5) * 0.35F},
                .size = {width, width * 1.4F},
                .radiance = {0.08F, 0.085F, 0.09F},
                .opacity = 0.14F * (1 - phase) * (1 - phase),
                .rotation = phase * 0.3F,
                .spark = false,
            });
        }
    }
    scene_.set_particles(std::move(visible));
}

void WorldPresentation::update_listener(scene::FpsCamera const &camera)
{
    auto const position = camera.position();
    auto const forward = camera.forward();
    auto const up = camera.up();
    audio_listener_.set_position(position.x, position.y, position.z);
    audio_listener_.set_orientation(forward.x, forward.y, forward.z, up.x, up.y, up.z);
    if (!village_sounds_.empty()) {
        float const presence = 1 - glm::smoothstep(65.0F, 130.0F,
                                                   glm::distance(glm::vec2{position.x, position.z},
                                                                 glm::vec2{0, 130}));
        village_sounds_[0].set_position(position.x, position.y, position.z);
        village_sounds_[0].set_gain(0.35F * presence);
        village_sounds_[1].set_gain(0.7F * presence);
        village_sounds_[2].set_gain(0.8F * presence);
    }
}

glm::mat4 WorldPresentation::observer_eye_transform() const
{
    auto const &observer = world_->player(observer_);
    auto const models =
        std::ranges::find(character_models_, observer.keyid, &CharacterModels::keyid);
    if (models != character_models_.end() && models->eyes_mount)
        return models->character->node_world_matrix(*models->eyes_mount) * models->eyes_offset;
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
