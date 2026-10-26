#include "lc1/game/player/player-view.hpp"

#include "lc1/game/character/character-model.hpp"
#include "lc1/game/character/character.hpp"
#include "lc1/game/continent/continent.hpp"
#include "lc1/scene/model-instance.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

void check(bool condition, std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string{message});
}

bool near(float left, float right)
{
    return std::abs(left - right) < 0.001F;
}

bool near(glm::vec3 left, glm::vec3 right)
{
    return glm::length(left - right) < 0.001F;
}

lc1::Continent make_world()
{
    return lc1::Continent{{
        .origin = {-128.0F, -128.0F},
        .tile_size = 128.0F,
        .tiles = std::vector(2, std::vector<lc1::TileData>(2)),
        .spawn = {0.0F, 0.0F, 0.0F},
        .patches = {},
        .blocks = {},
        .regions = {{lc1::RegionId{1}, "Plain", lc1::RegionType::Plain}},
        .locations = {},
        .roads = {},
        .height_samples = {},
    }};
}

void update_view(lc1::PlayerView &view, lc1::Continent const &world, lc1::Character &player,
                 std::initializer_list<lc1::Key> keys, float delta_seconds,
                 glm::vec2 look_delta = {})
{
    lc1::InputState devices;
    for (auto key : keys)
        devices.on_key(key, true);
    auto bindings = lc1::default_game_bindings();
    // Isolate camera recenter from the default Space binding that also requests a jump.
    bindings.push_back(
        {lc1::GameInputContext::Gameplay, lc1::GameAction::RecenterCamera, lc1::Key::Home});
    lc1::GameInputRouter input{bindings, lc1::GameInputContext::Gameplay};
    input.update(devices);
    view.update_view_mode(input, true, player);
    auto const intent = view.resolve_input(player, input, look_delta, true, true);
    player.update(world, delta_seconds, intent);
    view.update_camera(player, delta_seconds);
}

void shared_position_and_remembered_look()
{
    auto const world = make_world();
    lc1::Character player{world, {.position = world.spawn()}, {}};
    lc1::PlayerView view{world, player.position()};
    check(view.oblique(), "a session starts in the oblique view");
    check(view.camera().position().y > player.eye_position().y,
          "oblique camera is above the player");

    update_view(view, world, player, {lc1::Key::M}, 0, {800, -200});
    check(near(player.body_yaw(), 0.0F) && near(player.look_pitch(), 0.0F),
          "entering first person discards motion collected with the visible cursor");
    check(!view.oblique() && near(view.camera().position(), player.eye_position()),
          "first person uses the same character's eye position");
    update_view(view, world, player, {lc1::Key::W}, 0.1F, {800, -200});
    auto const first_person_yaw = player.body_yaw();
    auto const first_person_pitch = player.look_pitch();
    auto const first_person_position = player.position();
    check(!near(first_person_position, world.spawn()),
          "first-person movement advances the character");
    check(near(view.camera().forward(), player.look_direction()),
          "first-person camera tracks mouse look");

    view.toggle(player);
    update_view(view, world, player, {}, 0);
    check(near(player.position(), first_person_position),
          "view switching does not teleport the player");
    update_view(view, world, player, {lc1::Key::D}, 0.1F);
    auto const oblique_position = player.position();
    check(!near(oblique_position, first_person_position),
          "oblique movement continues from that position");
    check(!near(player.body_yaw(), first_person_yaw), "oblique walking turns the body");

    view.toggle(player);
    update_view(view, world, player, {}, 0, {-1200, 600});
    check(near(player.position(), oblique_position),
          "returning to first person preserves world position");
    check(near(player.body_yaw(), first_person_yaw) &&
              near(player.look_pitch(), first_person_pitch),
          "returning to first person restores the previous mouse-look angles");
    check(near(view.camera().position(), player.eye_position()) &&
              near(view.camera().forward(), player.look_direction()),
          "restored first-person camera matches the character pose");
}

void zoom_and_unlock_survive_switching()
{
    auto const world = make_world();
    lc1::Character player{world, {.position = world.spawn()}, {}};
    lc1::PlayerView view{world, player.position()};
    auto const initial_camera = view.camera().position();
    update_view(view, world, player, {lc1::Key::Y}, 0.1F);
    lc1::InputState pointer;
    pointer.on_scroll({0, 2});
    view.update_pointer(pointer, {1000, 700}, false, 0.1F);
    view.update_camera(player, 0.1F);
    auto const zoomed_camera = view.camera().position();
    check(!near(initial_camera, zoomed_camera), "wheel input changes oblique zoom");

    view.toggle(player);
    update_view(view, world, player, {}, 0);
    glm::vec3 const first_move{3, 0, 1};
    player.move(world, first_move);
    view.toggle(player);
    update_view(view, world, player, {}, 0);
    update_view(view, world, player, {}, 0.1F);
    check(near(view.camera().position(), zoomed_camera + first_move),
          "returning to oblique recenters at the player while preserving zoom");

    auto const recentered_camera = view.camera().position();
    player.move(world, {2, 0, 0});
    update_view(view, world, player, {}, 0.1F);
    check(near(view.camera().position(), recentered_camera),
          "the unlocked camera remains unlocked");
    update_view(view, world, player, {lc1::Key::Home}, 0.1F);
    check(near(view.camera().position(), recentered_camera + glm::vec3{2, 0, 0}),
          "holding recenter temporarily follows an unlocked player");
    auto const held_camera = view.camera().position();
    player.move(world, {2, 0, 0});
    update_view(view, world, player, {}, 0.1F);
    check(near(view.camera().position(), held_camera),
          "releasing recenter restores free camera movement");
    update_view(view, world, player, {lc1::Key::Y}, 0.1F);
    check(near(view.camera().position(), held_camera + glm::vec3{2, 0, 0}),
          "locking again follows the current player position");
}

void disabled_input_preserves_pose()
{
    auto const world = make_world();
    lc1::Character player{world, {.position = world.spawn()}, {}};
    lc1::PlayerView view{world, player.position()};
    for (int mode = 0; mode < 2; ++mode) {
        bool const oblique = view.oblique();
        auto const position = player.position();
        auto const direction = player.look_direction();
        auto const camera = view.camera().position();
        lc1::InputState devices;
        for (auto key : {lc1::Key::W, lc1::Key::D, lc1::Key::LeftShift, lc1::Key::Y,
                         lc1::Key::Space, lc1::Key::M})
            devices.on_key(key, true);
        lc1::GameInputRouter input{lc1::default_game_bindings(), lc1::GameInputContext::Gameplay};
        input.update(devices);
        view.update_view_mode(input, false, player);
        auto const intent = view.resolve_input(player, input, {500, 500}, false, false);
        player.update(world, 0.1F, intent);
        view.update_camera(player, 0.1F);
        check(view.oblique() == oblique, "disabled keyboard cannot switch views");
        check(near(player.position(), position) && near(player.look_direction(), direction),
              "UI/focus gating prevents player movement and mouse look in either view");
        check(near(view.camera().position(), camera),
              "disabled input cannot zoom or pan the camera");
        view.toggle(player);
        update_view(view, world, player, {}, 0);
    }
}

void independent_capture_gates()
{
    auto const world = make_world();
    lc1::Character player{world, {.position = world.spawn()}, {}};
    lc1::PlayerView view{world, player.position()};
    view.toggle(player);
    update_view(view, world, player, {}, 0);
    lc1::InputState devices;
    devices.on_key(lc1::Key::W, true);
    devices.on_key(lc1::Key::Space, true);
    devices.on_key(lc1::Key::LeftShift, true);
    devices.on_key(lc1::Key::F, true);
    lc1::GameInputRouter input{lc1::default_game_bindings(), lc1::GameInputContext::Gameplay};
    input.update(devices);
    view.update_view_mode(input, false, player);
    auto const mouse_only = view.resolve_input(player, input, {200, -100}, false, true);
    check(mouse_only.move == glm::vec2{} && !mouse_only.sprint && !mouse_only.jump &&
              mouse_only.body_yaw && near(*mouse_only.body_yaw, -10),
          "keyboard capture blocks actions while allowing mouse look");
    view.update_view_mode(input, true, player);
    auto const keyboard_only = view.resolve_input(player, input, {200, -100}, true, false);
    check(keyboard_only.move != glm::vec2{} && keyboard_only.sprint && keyboard_only.jump &&
              keyboard_only.body_yaw && near(*keyboard_only.body_yaw, 0),
          "mouse capture blocks look while allowing keyboard actions");
}

void pointer_pan_and_recenter_priority()
{
    auto const world = make_world();
    lc1::Character player{world, {.position = world.spawn()}, {}};
    lc1::PlayerView view{world, player.position()};
    update_view(view, world, player, {lc1::Key::Y}, 0.1F);
    auto const initial = view.camera().position();
    lc1::InputState devices;
    devices.on_cursor_position({0, 350});
    view.update_pointer(devices, {1000, 700}, false, 0.1F);
    view.update_camera(player, 0.1F);
    check(near(view.camera().position(), initial), "inactive pointer does not cause edge pan");
    view.update_pointer(devices, {1000, 700}, true, 0.1F);
    view.update_camera(player, 0.1F);
    auto const panned = view.camera().position();
    check(glm::dot(panned - initial, view.camera().right()) < -1,
          "logical left edge pans left in the view basis");
    for (auto cursor : {glm::vec2{-1, 350}, glm::vec2{1000, 350}, glm::vec2{500, 350}}) {
        devices.on_cursor_position(cursor);
        view.update_pointer(devices, {1000, 700}, true, 0.1F);
        view.update_camera(player, 0.1F);
        check(near(view.camera().position(), panned),
              "outside coordinates and the content center do not pan");
    }
    devices.on_cursor_position({0, 350});
    view.update_pointer(devices, {}, true, 0.1F);
    view.update_camera(player, 0.1F);
    check(near(view.camera().position(), panned), "empty viewport cannot cause edge pan");
    player.move(world, {3, 0, 2});
    update_view(view, world, player, {lc1::Key::Home}, 0.1F);
    auto const recentered = view.camera().position();
    view.update_pointer(devices, {1000, 700}, true, 0.1F);
    view.update_camera(player, 0.1F);
    check(near(view.camera().position(), recentered), "held recenter takes priority over edge pan");
    update_view(view, world, player, {}, 0.1F);
    view.update_pointer(devices, {1000, 700}, true, 0.1F);
    view.update_camera(player, 0.1F);
    check(!near(view.camera().position(), recentered), "releasing recenter restores edge pan");
}

void animated_eye_mount()
{
    auto const world = make_world();
    lc1::Character player{world, {.position = world.spawn()}, {}};
    auto const model = lc1::make_character_model(player.body());
    auto const eyes = model.definition().find_node("Eyes");
    check(eyes.has_value() && !model.definition().find_node("Missing"),
          "named eye mount resolves and missing mount names do not");
    check(!model.definition().nodes()[*eyes].mesh && model.definition().draw_count() == 2,
          "eye mount adds no drawable geometry");
    lc1::scene::ModelInstance instance{model.definition()};
    auto const eye_transform = [&] {
        return instance.node_world_matrix(*eyes, player.transform().model_matrix());
    };
    auto const eye_position = [&] { return glm::vec3{eye_transform()[3]}; };
    check(near(eye_position(), player.eye_position()),
          "default eye mount preserves the configured eye height");
    lc1::PlayerView view{world, player.position()};
    view.toggle(player);
    auto const clips = model.definition().animations();
    auto const nod = std::ranges::find(clips, "Nod", &lc1::scene::AnimationClip::name);
    check(nod != clips.end(), "character has the nod animation");
    instance.play_animation(static_cast<std::size_t>(nod - clips.begin()));
    instance.update_animation(0.3F);
    auto const animated_eye = eye_position();
    check(animated_eye.y < player.eye_position().y && animated_eye.z < player.eye_position().z,
          "nodding moves the eye mount down and forward through its head parent");
    view.update_camera(player, 0, eye_transform());
    check(near(view.camera().position(), animated_eye),
          "first person follows the animated eye mount");
    check(near(view.camera().forward(),
               glm::normalize(glm::vec3{eye_transform() * glm::vec4{0, 0, -1, 0}})),
          "first person inherits the animated eye mount direction");
    player.set_body_yaw(90);
    player.set_look_angles(15, 20);
    player.move(world, {2, 0, 1});
    view.update_camera(player, 0, eye_transform());
    check(near(view.camera().position(), eye_position()),
          "mount queries use the current character position and yaw");
    auto const local_look =
        glm::inverse(glm::mat3{player.transform().model_matrix()}) * player.look_direction();
    check(near(view.camera().forward(), glm::normalize(glm::mat3{eye_transform()} * local_look)),
          "mount rotation composes with mouse look without applying body yaw twice");
    view.update_camera(player, 0);
    check(near(view.camera().position(), player.eye_position()),
          "callers without a mount retain the gameplay eye fallback");
    view.toggle(player);
    auto const oblique = view.camera().position();
    view.update_camera(player, 0, eye_transform());
    check(near(view.camera().position(), oblique),
          "oblique view ignores the animated eye position");
    bool rejected = false;
    try {
        instance.node_world_matrix(model.definition().nodes().size(), glm::mat4{1});
    }
    catch (lc1::Error const &) {
        rejected = true;
    }
    check(rejected, "invalid mount indices are rejected");
}

} // namespace

int main()
{
    try {
        shared_position_and_remembered_look();
        zoom_and_unlock_survive_switching();
        disabled_input_preserves_pose();
        independent_capture_gates();
        pointer_pan_and_recenter_priority();
        animated_eye_mount();
    }
    catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
