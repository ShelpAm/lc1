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

WorldPresentation::WorldPresentation(Device const &device, std::filesystem::path const &asset_root,
                                     GameWorld const &world, OutputSettings output)
    : world_(&world), font_{device, text::GlyphAtlas{asset_root / font_file,
                                                     {.codepoints = font_codepoints(world)}}},
      white_{device, Image::load_from_file(asset_root / "textures/prototype-white.png")},
      continent_view_{device, white_, world.continent()},
      showcase_{device, world.continent().spawn()},
      character_resources_{device, make_character_model(player_body)},
      player_material_{
          MaterialInfo{.parameters = {.metallic_factor = 0.0F, .roughness_factor = 0.8F},
                       .base_color = {.texture = &white_}}},
      output_(output)
{
    character_models_.reserve(world.characters().size());
    for (std::size_t i = 0; i < world.characters().size(); ++i) {
        auto *model = scene_.add_model(scene::SceneModel{
            .model = &character_resources_,
            .material = &player_material_,
            .world_transform = world.characters()[i]->transform().model_matrix()});
        character_models_.push_back(model);
        auto &instance = model->pose;
        bool const player = world.characters()[i] == &world.player();
        std::string_view const clip_name = player ? "Nod" : "Idle";
        auto const clips = character_resources_.definition().animations();
        auto const clip = std::ranges::find(clips, clip_name, &scene::AnimationClip::name);
        if (clip == clips.end())
            fail("character model is missing animation {}", clip_name);
        auto const animation = static_cast<std::size_t>(clip - clips.begin());
        if (player) {
            player_model_ = model;
            nod_animation_index_ = animation;
        }
        else {
            instance.play_animation(animation);
            instance.update_animation(static_cast<float>(i % 11) * clip->duration() / 11.0F);
        }
    }

    if (!player_model_)
        fail("player presentation is missing");
    auto const eyes = character_resources_.definition().find_node("Eyes");
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

scene::Scene const &WorldPresentation::scene() const
{
    return scene_;
}

void WorldPresentation::update_instances()
{
    auto const showcase = showcase_.draws();
    for (std::size_t i = 0; i < showcase.size(); ++i)
        showcase_models_[i]->world_transform = showcase[i].world_transform;
    for (std::size_t i = 0; i < character_models_.size(); ++i)
        character_models_[i]->world_transform = world_->characters()[i]->transform().model_matrix();
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
    if (input.nod)
        player_model_->pose.play_animation(nod_animation_index_, false);
    scene_.update_animations(std::min(delta_seconds, 0.1F));
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
    return player_model_->node_world_matrix(eyes_node_index_);
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
