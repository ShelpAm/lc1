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

// The player's body. The placeholder mesh below and the collision body are built from the
// same numbers, so the two cannot silently disagree.
constexpr CharacterBody player_body{};

std::vector<GpuMesh> upload_model(Device const &device, scene::Model const &model)
{
    std::vector<GpuMesh> meshes;
    meshes.reserve(model.mesh_count());
    for (std::size_t i = 0; i < model.mesh_count(); ++i)
        meshes.push_back(model.gen_mesh(device, i));
    return meshes;
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

WorldPresentation::CharacterResources
WorldPresentation::make_character_resources(Device const &device)
{
    auto model = make_character_model(player_body);
    auto meshes = upload_model(device, model);
    return {.definition = std::move(model).definition(), .meshes = std::move(meshes)};
}

WorldPresentation::WorldPresentation(Device const &device, std::filesystem::path const &asset_root,
                                     GameWorld const &world, OutputSettings output)
    : world_(&world), font_{device, text::GlyphAtlas{asset_root / font_file,
                                                     {.codepoints = font_codepoints(world)}}},
      white_{device, Image::load_from_file(asset_root / "textures/prototype-white.png")},
      continent_view_{device, white_, world.continent()},
      showcase_{device, world.continent().spawn()},
      character_resources_{make_character_resources(device)},
      player_material_{
          MaterialInfo{.parameters = {.metallic_factor = 0.0F, .roughness_factor = 0.8F},
                       .base_color = {.texture = &white_}}},
      output_(output)
{
    character_models_.reserve(world.characters().size());
    for (std::size_t i = 0; i < world.characters().size(); ++i) {
        auto &instance = character_models_.emplace_back(character_resources_.definition);
        bool const player = world.characters()[i] == &world.player();
        std::string_view const clip_name = player ? "Nod" : "Idle";
        auto const clips = character_resources_.definition.animations();
        auto const clip = std::ranges::find(clips, clip_name, &scene::AnimationClip::name);
        if (clip == clips.end())
            fail("character model is missing animation {}", clip_name);
        auto const animation = static_cast<std::size_t>(clip - clips.begin());
        if (player)
            nod_animation_index_ = animation;
        else {
            instance.play_animation(animation);
            instance.update_animation(static_cast<float>(i % 11) * clip->duration() / 11.0F);
        }
    }

    auto const player = std::ranges::find(world.characters(), &world.player());
    if (player == world.characters().end())
        fail("player presentation is missing");
    player_model_index_ = static_cast<std::size_t>(player - world.characters().begin());
    auto const eyes = character_resources_.definition.find_node("Eyes");
    if (!eyes)
        fail("character model is missing the Eyes mount");
    eyes_node_index_ = *eyes;

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
    lights_ = {to_light(sun), to_light(lamp)};
    lights_.insert(lights_.end(), showcase_.lights().begin(), showcase_.lights().end());
    flashlight_index_ = lights_.size();
    lights_.push_back(to_light(spotlight));
    active_lights_ = lights_;

    set_volume(50.0F);
}

std::vector<DrawItem> WorldPresentation::draws() const
{
    std::vector<DrawItem> items;
    items.reserve(object_capacity());
    items.append_range(continent_view_.draw_items());
    items.append_range(showcase_.draws());
    for (std::size_t i = 0; i < character_models_.size(); ++i) {
        auto const *character = world_->characters()[i];
        for (auto const &mesh :
             character_models_[i].mesh_instances(character->transform().model_matrix()))
            items.push_back({.mesh = &character_resources_.meshes[mesh.mesh],
                             .material = &player_material_,
                             .model_matrix = mesh.model_matrix,
                             .casts_shadow = true});
    }
    return items;
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
    std::size_t const terrain = continent_view_.draw_items().size();
    std::size_t const showcase = showcase_.draws().size();
    if (terrain > maximum || showcase > maximum - terrain)
        fail("world draw capacity exceeds the supported range");
    std::size_t const fixed = terrain + showcase;
    std::size_t const per_character = character_resources_.definition.draw_count();
    if (per_character != 0 && character_models_.size() > (maximum - fixed) / per_character)
        fail("character draw capacity exceeds the supported range");
    return static_cast<std::uint32_t>(fixed + character_models_.size() * per_character);
}

void WorldPresentation::update_lighting()
{
    // Hold the flashlight above ground and outside the mesh, on the aiming side.
    lights_[flashlight_index_].position =
        world_->player().position() + glm::vec3{0.0F, 1.3F, 0.0F} +
        world_->player().heading() * 0.5F + world_->player().right() * 0.5F;
    lights_[flashlight_index_].direction = world_->player().heading();
    active_lights_.clear();
    for (auto const &light : lights_)
        if (lighting_mode_ == 0 || (lighting_mode_ <= 4 && light.type == lighting_mode_ - 1))
            active_lights_.push_back(light);
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

void WorldPresentation::update(WorldPresentationInput const &input, float delta_seconds)
{
    if (!std::isfinite(delta_seconds) || delta_seconds < 0.0F)
        fail("presentation delta must be finite and nonnegative");
    if (input.nod)
        character_models_[player_model_index_].play_animation(nod_animation_index_, false);
    for (auto &instance : character_models_)
        instance.update_animation(std::min(delta_seconds, 0.1F));
    if (input.cycle_lighting)
        lighting_mode_ = (lighting_mode_ + 1) % lighting_names.size();
    if (input.toggle_showcase_motion)
        showcase_animated_ = !showcase_animated_;
    output_.exposure =
        std::clamp(output_.exposure * std::exp2(static_cast<float>(input.exposure_steps) * 0.5F),
                   0.03125F, 16.0F);
    if (showcase_animated_)
        showcase_time_ += std::clamp(delta_seconds, 0.0F, 0.1F);
    showcase_.update(showcase_time_);
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

glm::mat4 WorldPresentation::player_eye_transform() const
{
    auto const matrix = character_models_[player_model_index_].node_world_matrix(
        eyes_node_index_, world_->player().transform().model_matrix());
    return matrix;
}

void WorldPresentation::set_volume(float volume_percent)
{
    audio_listener_.set_gain(volume_to_gain(volume_percent));
}

} // namespace lc1

namespace lc1 {

std::span<scene::Light const> WorldPresentation::lights() const
{
    return active_lights_;
}

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
