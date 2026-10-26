#include "lc1/game/character/character-model.hpp"
#include "lc1/game/character/character.hpp"
#include "lc1/scene/model-instance.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace lc1::scene;

void check(bool condition, char const *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

bool near(glm::vec3 a, glm::vec3 b)
{
    return glm::length(a - b) < 1e-5F;
}

template <typename F> void rejects(F &&action)
{
    try {
        action();
    }
    catch (lc1::Error const &) {
        return;
    }
    throw std::runtime_error("expected animation validation failure");
}

AnimationChannel translation()
{
    return {.times = {0, 2}, .values = std::vector<glm::vec3>{{0, 0, 0}, {4, 0, 0}}};
}

Model animated_model(AnimationClip clip)
{
    return {std::vector<Mesh>{},
            {{.transform = {.translation = {7, 0, 0}}},
             {.parent = 0, .transform = {.translation = {0, 3, 0}}}},
            {std::move(clip)}};
}

void linear_and_independent_playback()
{
    auto move = translation();
    move.times = {1, 3};
    glm::quat const rest{1, 0, 0, 0};
    auto const turn = glm::angleAxis(glm::radians(90.0F), glm::vec3{0, 0, 1});
    Model const model =
        animated_model({.name = "Move",
                        .channels = {move,
                                     {.node = 1,
                                      .path = AnimationChannel::Path::Rotation,
                                      .times = {0, 2},
                                      .values = std::vector<glm::quat>{rest, turn}},
                                     {.node = 1,
                                      .path = AnimationChannel::Path::Scale,
                                      .times = {0, 2},
                                      .values = std::vector<glm::vec3>{{1, 1, 1}, {3, 3, 3}}}}});
    ModelInstance first{model.definition()};
    ModelInstance second{model.definition()};
    first.play_animation(0, false);
    second.play_animation(0);
    first.update_animation(1);
    check(near(first.local_transforms()[0].translation, {0, 0, 0}),
          "hold the first key before its time");
    check(near(first.local_transforms()[1].scale, {2, 2, 2}), "linearly interpolate scale");
    float const diagonal = std::sqrt(0.5F);
    check(near(first.local_transforms()[1].rotation * glm::vec3{1, 0, 0}, {diagonal, diagonal, 0}),
          "spherical quaternion interpolation");
    check(near(first.local_transforms()[1].translation, {0, 3, 0}),
          "unanimated properties retain the default pose");
    check(second.playback()->time == 0 && near(second.local_transforms()[1].scale, {1, 1, 1}),
          "shared clips have independent playback state and poses");
    first.update_animation(1);
    check(near(first.local_transforms()[0].translation, {2, 0, 0}),
          "interpolate keys with a nonzero start time");
    check(near(glm::vec3{first.model_matrices()[1][3]}, {2, 3, 0}),
          "animated parents move child nodes");
    first.update_animation(100);
    check(first.playback()->time == 3 && near(first.local_transforms()[0].translation, {4, 0, 0}),
          "nonlooping playback holds the final pose");
    second.update_animation(6.5F);
    check(second.playback()->time == 0.5, "loop across multiple periods");
    second.update_animation(std::numeric_limits<float>::max());
    check(std::isfinite(second.playback()->time) && second.playback()->time < 3,
          "large finite deltas wrap without overflow");
    first.reset_pose();
    check(!first.playback() && near(first.local_transforms()[0].translation, {7, 0, 0}),
          "reset stops playback and restores the shared default pose");
    first.update_animation(1);
    check(near(first.local_transforms()[0].translation, {7, 0, 0}),
          "stopped instances keep their pose");
}

void steps_and_rotations()
{
    auto channel = translation();
    channel.interpolation = AnimationChannel::Interpolation::Step;
    channel.times = {0, 1, 2};
    channel.values = std::vector<glm::vec3>{{0, 0, 0}, {2, 0, 0}, {4, 0, 0}};
    Model const model = animated_model({.channels = {channel}});
    ModelInstance instance{model.definition()};
    instance.play_animation(0);
    instance.update_animation(0.5F);
    check(near(instance.local_transforms()[0].translation, {0, 0, 0}),
          "step interpolation holds the left key");
    instance.update_animation(0.5F);
    check(near(instance.local_transforms()[0].translation, {2, 0, 0}),
          "step switches exactly on a key");
    instance.update_animation(1);
    check(instance.playback()->time == 0 &&
              near(instance.local_transforms()[0].translation, {0, 0, 0}),
          "loop boundary samples the first key");

    glm::quat const rotation = glm::angleAxis(0.7F, glm::vec3{0, 1, 0});
    Model const antipodal = animated_model(
        {.channels = {{.path = AnimationChannel::Path::Rotation,
                       .times = {0, 1},
                       .values = std::vector<glm::quat>{rotation * 2.0F, -rotation * 3.0F}}}});
    ModelInstance turn{antipodal.definition()};
    turn.play_animation(0);
    turn.update_animation(0.5F);
    check(near(turn.local_transforms()[0].rotation * glm::vec3{0, 0, -1},
               rotation * glm::vec3{0, 0, -1}),
          "equivalent antipodal quaternions follow the shortest path");
    check(std::abs(glm::length(turn.local_transforms()[0].rotation) - 1) < 1e-5F,
          "sampled quaternions are normalized");

    Model const single =
        animated_model({.channels = {{.times = {0}, .values = std::vector<glm::vec3>{{1, 2, 3}}}}});
    ModelInstance hold{single.definition()};
    hold.play_animation(0);
    hold.update_animation(100);
    check(hold.playback()->time == 0 && near(hold.local_transforms()[0].translation, {1, 2, 3}),
          "zero-duration clips hold a single key");
}

void invalid_data_and_atomic_updates()
{
    auto invalid = [](AnimationChannel channel) {
        rejects([&] { animated_model({.channels = {channel}}); });
    };
    auto channel = translation();
    channel.node = 2;
    invalid(channel);
    channel = translation();
    channel.times = {};
    invalid(channel);
    channel = translation();
    channel.times = {0, 0};
    invalid(channel);
    channel = translation();
    channel.times = {-1, 2};
    invalid(channel);
    channel = translation();
    channel.times = {0, std::numeric_limits<float>::infinity()};
    invalid(channel);
    channel = translation();
    channel.values = std::vector<glm::vec3>{{1, 2, 3}};
    invalid(channel);
    channel = translation();
    channel.values =
        std::vector<glm::vec3>{{0, 0, 0}, {std::numeric_limits<float>::quiet_NaN(), 0, 0}};
    invalid(channel);
    channel = translation();
    channel.path = AnimationChannel::Path::Rotation;
    invalid(channel);
    channel = translation();
    channel.path = static_cast<AnimationChannel::Path>(99);
    invalid(channel);
    channel = translation();
    channel.interpolation = static_cast<AnimationChannel::Interpolation>(99);
    invalid(channel);
    channel = translation();
    channel.path = AnimationChannel::Path::Scale;
    channel.values = std::vector<glm::vec3>{{1, 1, 1}, {0, 1, 1}};
    invalid(channel);
    channel = translation();
    channel.path = AnimationChannel::Path::Rotation;
    channel.values = std::vector<glm::quat>{{1, 0, 0, 0}, {0, 0, 0, 0}};
    invalid(channel);
    rejects([] { animated_model({.channels = {}}); });
    channel = translation();
    rejects([&] { animated_model({.channels = {channel, channel}}); });

    Model const model = animated_model({.channels = {translation()}});
    ModelInstance instance{model.definition()};
    instance.play_animation(0);
    instance.update_animation(0.5F);
    rejects([&] { instance.play_animation(1); });
    rejects([&] { instance.update_animation(-1); });
    rejects([&] { instance.update_animation(std::numeric_limits<float>::quiet_NaN()); });
    check(instance.playback()->time == 0.5 &&
              near(instance.local_transforms()[0].translation, {1, 0, 0}),
          "invalid commands preserve playback and pose");
    channel = translation();
    channel.path = AnimationChannel::Path::Scale;
    channel.times = {0, 1};
    channel.values = std::vector<glm::vec3>{{1, 1, 1}, {-1, 1, 1}};
    Model const crossing = animated_model({.channels = {channel}});
    ModelInstance scale{crossing.definition()};
    scale.play_animation(0);
    rejects([&] { scale.update_animation(0.5F); });
    check(scale.playback()->time == 0 && near(scale.local_transforms()[0].scale, {1, 1, 1}),
          "an interpolated singular scale cannot partially commit the pose or time");
}

void definition_outlives_geometry()
{
    auto const definition = [] {
        auto model = lc1::make_character_model(lc1::CharacterBody{});
        return std::move(model).definition();
    }();
    ModelInstance first{definition};
    ModelInstance second{definition};
    first.play_animation(0);
    first.update_animation(1);
    check(first.mesh_instances(glm::mat4{1}).size() == 2,
          "runtime definition expands meshes after the CPU model has been destroyed");
    check(!near(first.local_transforms()[1].rotation * glm::vec3{0, 0, -1},
                second.local_transforms()[1].rotation * glm::vec3{0, 0, -1}),
          "animation remains independent after CPU geometry is released");
    first.reset_pose();
    check(near(first.local_transforms()[1].rotation * glm::vec3{0, 0, -1},
               second.local_transforms()[1].rotation * glm::vec3{0, 0, -1}),
          "reset uses the retained default pose after CPU geometry is released");
}

void mesh_expansion_and_character_content()
{
    Model const model{std::vector<Mesh>{Mesh{{}, {}}, Mesh{{}, {}}},
                      {{.transform = {.translation = {1, 0, 0}}},
                       {.parent = 0, .transform = {.translation = {0, 2, 0}}, .mesh = 1},
                       {.parent = 0, .transform = {.translation = {0, 0, 3}}, .mesh = 1}},
                      {{.channels = {translation()}}}};
    ModelInstance instance{model.definition()};
    instance.play_animation(0);
    instance.update_animation(1);
    auto const meshes = instance.mesh_instances(glm::translate(glm::mat4{1}, glm::vec3{10, 0, 0}));
    check(model.definition().draw_count() == 2 && meshes.size() == 2 && meshes[0].mesh == 1 &&
              meshes[1].mesh == 1,
          "mesh-less nodes are skipped and repeated mesh references produce separate draws");
    check(near(glm::vec3{meshes[0].model_matrix[3]}, {12, 2, 0}) &&
              near(glm::vec3{meshes[1].model_matrix[3]}, {12, 0, 3}),
          "animated hierarchy and external world transform are applied exactly once");
    rejects([&] { instance.mesh_instances(glm::mat4{0}); });

    lc1::CharacterBody const body{.half_width = 0.4F, .height = 2.1F};
    auto const character = lc1::make_character_model(body);
    ModelInstance character_instance{character.definition()};
    check(character.mesh_count() == 2 && character.definition().draw_count() == 2 &&
              character.definition().animations().size() == 2,
          "character placeholder has independently drawable body and head plus shared idle "
          "animation");
    float bottom = std::numeric_limits<float>::max();
    float top = std::numeric_limits<float>::lowest();
    for (auto const &mesh : character_instance.mesh_instances(glm::mat4{1}))
        for (auto const &vertex : character.vertices(mesh.mesh)) {
            auto const point = mesh.model_matrix * glm::vec4{vertex.position, 1};
            bottom = std::min(bottom, point.y);
            top = std::max(top, point.y);
        }
    check(std::abs(bottom) < 1e-5F && std::abs(top - body.height) < 1e-5F,
          "articulated default pose retains the collision body's dimensions and foot origin");
    character_instance.play_animation(0);
    character_instance.update_animation(1);
    check(
        !near(character_instance.local_transforms()[1].rotation * glm::vec3{0, 0, -1}, {0, 0, -1}),
        "the shared idle clip changes the head pose");
}

void player_nod_clip()
{
    auto const model = lc1::make_character_model(lc1::CharacterBody{});
    auto const clips = model.definition().animations();
    auto const clip = std::ranges::find(clips, "Nod", &AnimationClip::name);
    check(clip != clips.end() && clip->duration() == 3, "player nod clip lasts three seconds");
    ModelInstance player{model.definition()};
    ModelInstance npc{model.definition()};
    check(!player.playback(), "player starts without automatic animation playback");
    player.play_animation(static_cast<std::size_t>(clip - clips.begin()), false);
    npc.play_animation(0);
    player.update_animation(0.3F);
    auto const forward = player.local_transforms()[1].rotation * glm::vec3{0, 0, -1};
    check(forward.y < -0.4F, "player head pitches down at the first nod key");
    check(near(player.local_transforms()[0].translation, {0, 0, 0}) &&
              near(player.local_transforms()[0].scale, {1, 1, 1}),
          "nodding leaves the body transform unchanged");
    check(near(npc.local_transforms()[1].rotation * glm::vec3{0, 0, -1}, {0, 0, -1}),
          "player nodding does not change the NPC's pose");
    player.update_animation(0.3F);
    check(near(player.local_transforms()[1].rotation * glm::vec3{0, 0, -1}, {0, 0, -1}),
          "the head returns upright between nods");
    player.update_animation(0.3F);
    check((player.local_transforms()[1].rotation * glm::vec3{0, 0, -1}).y < -0.4F,
          "the clip contains a second nod");
    player.update_animation(2.1F);
    check(near(player.local_transforms()[1].rotation * glm::vec3{0, 0, -1}, {0, 0, -1}),
          "the single nod sequence returns to its resting pose");
    player.update_animation(0.3F);
    check(near(player.local_transforms()[1].rotation * glm::vec3{0, 0, -1}, {0, 0, -1}),
          "completed nodding does not repeat");
}

} // namespace

int main()
{
    try {
        linear_and_independent_playback();
        steps_and_rotations();
        invalid_data_and_atomic_updates();
        mesh_expansion_and_character_content();
        definition_outlives_geometry();
        player_nod_clip();
        std::cout << "Model animation tests passed\n";
    }
    catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
