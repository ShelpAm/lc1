#include "character-input.hpp"
#include "lc1/game/character/character-model.hpp"
#include "lc1/game/party/party.hpp"
#include "lc1/game/physics/world-collision.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace {
void check(bool condition, char const *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

template <typename F> void rejects(F &&action)
{
    try {
        action();
    }
    catch (lc1::Error const &) {
        return;
    }
    throw std::runtime_error("expected invalid combat input to be rejected");
}

lc1::ContinentData terrain(std::vector<lc1::ContinentBlock> blocks)
{
    return {.origin = {-16, -16},
            .tile_size = 16,
            .tiles = std::vector(2, std::vector<lc1::TileData>(2)),
            .spawn = {0, 0, 0},
            .patches = {},
            .blocks = std::move(blocks),
            .locations = {},
            .roads = {},
            .height_samples = {}};
}

lc1::CharacterConfig config(std::uint32_t id, lc1::Weapon *weapon = nullptr)
{
    lc1::CharacterConfig result{};
    result.keyid = id;
    if (weapon)
        result.main_hand = weapon;
    return result;
}

struct Arena {
    lc1::Continent world;
    lc1::MeleeWeapon weapon{1001, {.attack = 10, .range = 1.5F}};
    lc1::Character attacker;
    lc1::Character target;
    std::array<lc1::Character *, 2> characters{&attacker, &target};

    explicit Arena(std::vector<lc1::ContinentBlock> blocks = {}, glm::vec3 target_at = {0, 0, -1})
        : world{terrain(std::move(blocks))},
          attacker{world, {.position = {0, 0, 0}}, config(1, &weapon)},
          target{world, {.position = target_at}, config(2)}
    {
    }

    void swing()
    {
        lc1::CharacterIntent intent{};
        intent.swing_weapon = true;
        apply_character_intent(attacker, 0, world, intent);
        attacker.set_controller(nullptr);
    }

    void step(float delta)
    {
        for (auto *character : characters)
            character->update(delta, world);
        for (auto *character : characters)
            character->resolve_attack(characters, world);
    }
};

void polymorphic_weapon_use()
{
    static_assert(std::is_abstract_v<lc1::Weapon>);
    static_assert(std::has_virtual_destructor_v<lc1::Weapon>);
    static_assert(std::is_copy_constructible_v<lc1::WeaponDefinition>);

    class CustomWeapon final : public lc1::Weapon {
      public:
        CustomWeapon() : Weapon(1001, {.attack = 10, .range = 1.5F}) {}

        void use(lc1::Character &user) const override { user.take_damage(2); }
    } weapon;

    lc1::Continent world{terrain({})};
    lc1::Character user{world, {.position = {0, 0, 0}}, config(1, &weapon)};
    auto const health = user.config().health();
    apply_character_intent(user, 0, world, {.swing_weapon = true});
    check(user.config().health() == health - 2 && user.attack_sequence() == 0,
          "main-hand use dispatches to the derived behavior rather than its saved discriminator");

    lc1::RangedWeapon ranged{1004, {.attack = 10, .range = 20}};
    lc1::Character archer{world, {.position = {2, 0, 0}}, config(2, &ranged)};
    apply_character_intent(archer, 0, world, {.swing_weapon = true});
    check(archer.attack_sequence() == 0 && !archer.flashlight_on(),
          "unimplemented ranged fire does not dispatch melee or flashlight behavior");
}

void damage_and_reactions()
{
    Arena arena;
    auto &target = arena.target;
    target.take_damage(10);
    check(target.config().health() == 90 && target.hit_sequence() == 1,
          "ten damage removes ten health and records one hit");
    target.take_damage(0);
    rejects([&] { target.take_damage(-1); });
    rejects([&] { target.take_damage(std::numeric_limits<float>::quiet_NaN()); });
    check(target.config().health() == 90 && target.hit_sequence() == 1,
          "zero and invalid damage do not change health or restart recoil");
    lc1::CharacterIntent move{};
    move.move = {1, 0};
    move.jump = true;
    move.body_yaw = 90;
    auto const before = target.position();
    TestCharacterController controller{move};
    RestoreCharacterController restore{target, target.controller()};
    target.set_controller(&controller);
    target.update(0.1F, arena.world);
    check(target.position() == before && target.body_yaw() == 0,
          "hit stun prevents movement, jumping and turning");
    target.update(0.3F, arena.world);
    target.update(0.1F, arena.world);
    check(target.position().x > before.x, "movement resumes when hit stun ends");
    target.take_damage(1000);
    check(target.dead() && target.config().health() == 0, "lethal damage clamps health at zero");
    auto const death_sequence = target.hit_sequence();
    target.take_damage(1);
    check(target.hit_sequence() == death_sequence, "a corpse cannot be hit again");
    auto const death_position = target.position();
    target.update(1, arena.world);
    check(target.position().x == death_position.x,
          "death permanently prevents horizontal movement");
    check(!target.accepts_ambient_requests(), "corpses cannot accept ambient conversations");
}

void attack_blocks_locomotion()
{
    Arena arena;
    auto const start = arena.attacker.position();
    lc1::CharacterIntent attack{};
    attack.move = {1, 0};
    attack.sprint = true;
    attack.jump = true;
    attack.swing_weapon = true;
    apply_character_intent(arena.attacker, 0.1F, arena.world, attack);
    check(arena.attacker.attack_sequence() == 1, "movement input still allows an attack");
    check(arena.attacker.position() == start && !arena.attacker.running() &&
              !arena.attacker.moving(),
          "an attack immediately blocks movement, sprinting and jumping");

    attack.swing_weapon = false;
    attack.jump = false;
    apply_character_intent(arena.attacker, 0.2F, arena.world, attack);
    check(arena.attacker.position() == start, "movement is blocked during the active swing");
    apply_character_intent(arena.attacker, 0.25F, arena.world, attack);
    check(arena.attacker.position() == start, "movement is blocked through recovery");
    apply_character_intent(arena.attacker, 0.1F, arena.world, attack);
    check(arena.attacker.position().x > start.x && arena.attacker.running() &&
              arena.attacker.moving(),
          "movement resumes after recovery without waiting for the attack cooldown");

    Arena airborne;
    lc1::Character falling{airborne.world, {.position = {0, 2, 0}}, config(3, &airborne.weapon)};
    auto const height = falling.position().y;
    attack.swing_weapon = true;
    apply_character_intent(falling, 0.1F, airborne.world, attack);
    check(falling.position().x == 0 && falling.position().y < height,
          "attack movement lock preserves gravity");
}

void animation_controls_attack_duration()
{
    Arena arena;
    for (float duration : {0.2F, 1.2F, 3.7F}) {
        auto setup = config(3, &arena.weapon);
        setup.attack_animation_duration = duration;
        lc1::Character actor{arena.world, {}, setup};
        lc1::CharacterIntent intent{};
        intent.move = {1, 0};
        intent.swing_weapon = true;
        apply_character_intent(actor, 0, arena.world, intent);
        // The equipped default weapon has a 0.55-second clip.
        float const expected = std::max(duration, lc1::combat::swing_duration);
        apply_character_intent(actor, expected * 0.75F, arena.world, intent);
        check(actor.position().x == 0 && std::abs(actor.attack_progress() - 0.75F) < 0.0001F,
              "movement lock and progress follow the longest attack animation");
        check(actor.attack_sequence() == 1, "held attack cannot restart an unfinished animation");
        intent.swing_weapon = false;
        apply_character_intent(actor, expected * 0.25F + 0.001F, arena.world, intent);
        apply_character_intent(actor, 0.01F, arena.world, intent);
        check(actor.position().x > 0 && actor.attack_progress() == 1,
              "movement resumes after the complete animation, independent of its duration");
    }
    lc1::MeleeWeapon quick{1002, {.attack = 10, .range = 1.5F, .attack_duration = 0.2F}};
    lc1::Character actor{arena.world, {}, config(4, &quick)};
    apply_character_intent(actor, 0.1F, arena.world, {.swing_weapon = true});
    check(std::abs(actor.attack_progress() - 0.5F) < 0.0001F,
          "short weapon animations do not inherit a fixed minimum attack time");
}

void attack_timing_and_deduplication()
{
    Arena arena;
    arena.swing();
    arena.step(0.1F);
    check(arena.target.config().health() == 100, "windup does not deal damage");
    arena.step(0.15F);
    check(arena.target.config().health() == 90, "active sword sweep damages its target");
    arena.step(0.02F);
    arena.attacker.resolve_attack(arena.characters, arena.world);
    check(arena.target.config().health() == 90, "one swing hits each target at most once");
    arena.swing();
    check(arena.attacker.attack_sequence() == 1, "cooldown rejects repeated attack requests");
    arena.step(2);
    arena.swing();
    arena.step(0.3F);
    check(arena.target.config().health() == 80 && arena.attacker.attack_sequence() == 2,
          "a later swing can hit the same target again");

    Arena long_tick;
    long_tick.swing();
    long_tick.step(1);
    check(long_tick.target.config().health() == 90,
          "a tick spanning the complete active window still detects the swing");
    Arena interrupted;
    interrupted.swing();
    interrupted.attacker.take_damage(1);
    interrupted.step(1);
    check(interrupted.target.config().health() == 100, "being hit cancels pending damage");

    Arena unarmed;
    lc1::CharacterIntent intent{};
    intent.swing_weapon = true;
    apply_character_intent(unarmed.target, 1, unarmed.world, intent);
    check(unarmed.target.attack_sequence() == 0, "an unarmed character cannot swing a weapon");
}

void frame_rates_and_multiple_targets()
{
    for (float const delta : {1.0F / 30.0F, 1.0F / 60.0F, 1.0F / 144.0F}) {
        Arena arena;
        arena.swing();
        for (float elapsed = 0; elapsed < 0.6F; elapsed += delta)
            arena.step(delta);
        check(arena.target.config().health() == 90,
              "30, 60 and 144 Hz all produce one hit for the same swing");
    }
    Arena arena;
    lc1::Character other{arena.world, {.position = {0.1F, 0, -1}}, config(3)};
    std::array targets{&arena.attacker, &arena.target, &other};
    arena.swing();
    for (auto *character : targets)
        character->update(0.3F, arena.world);
    for (auto *character : targets)
        character->resolve_attack(targets, arena.world);
    check(arena.target.config().health() == 90 && other.config().health() == 90,
          "one swing can hit several intersecting characters");
    lc1::Party party{1, 1};
    auto friendly_attacker_config = config(4, &arena.weapon);
    friendly_attacker_config.party = &party;
    auto friendly_target_config = config(5);
    friendly_target_config.party = &party;
    lc1::Character friendly_attacker{arena.world, {}, std::move(friendly_attacker_config)};
    lc1::Character friendly_target{
        arena.world, {.position = {0, 0, -1}}, std::move(friendly_target_config)};
    std::array friends{&friendly_attacker, &friendly_target};
    lc1::CharacterIntent attack{};
    attack.swing_weapon = true;
    apply_character_intent(friendly_attacker, 0.3F, arena.world, attack);
    friendly_attacker.resolve_attack(friends, arena.world);
    check(friendly_target.config().health() == 90,
          "same-party characters can be damaged by friendly fire");
    arena.attacker.take_damage(1000);
    auto const sequence = arena.attacker.attack_sequence();
    arena.swing();
    arena.step(3);
    check(arena.attacker.attack_sequence() == sequence && arena.target.config().health() == 90,
          "a dead armed character cannot accept or resolve an attack");
}

void geometry_and_occlusion()
{
    for (auto const position : {glm::vec3{0, 0, -3}, glm::vec3{0, 0, 1.2F}, glm::vec3{1.2F, 0, -1},
                                glm::vec3{0, 4, -1}}) {
        Arena arena{{}, position};
        arena.swing();
        arena.step(0.3F);
        check(arena.target.config().health() == 100,
              "out-of-range, behind, side and other-floor targets are not hit");
    }
    Arena wall{{{{-2, 0, -0.65F}, {2, 3, -0.55F}}}};
    wall.swing();
    wall.step(0.3F);
    check(wall.target.config().health() == 100, "a thin solid wall blocks sword damage");
    Arena decoration{{{{-2, 0, -0.65F}, {2, 3, -0.55F}, lc1::ContinentSurface::Stone, false}}};
    decoration.swing();
    decoration.step(0.3F);
    check(decoration.target.config().health() == 90, "non-solid decoration does not block a sword");
    Arena turned{{}, {1, 0, 0}};
    turned.attacker.set_body_yaw(-90);
    turned.swing();
    turned.step(0.3F);
    check(turned.target.config().health() == 90, "sword sweep follows the attacker's body heading");
}

void visual_reactions()
{
    lc1::CharacterBody const body{};
    auto const head = glm::vec4{0, body.height, 0, 1};
    auto const rest = lc1::character_reaction_transform(false, 0.6F, body);
    auto const hit = lc1::character_reaction_transform(false, 0.15F, body);
    auto const falling = lc1::character_reaction_transform(true, 0.3F, body);
    auto const down = lc1::character_reaction_transform(true, 0.6F, body);
    check(glm::distance(glm::vec3{rest * head}, glm::vec3{head}) < 0.001F,
          "completed hit animation returns upright");
    check((hit * head).z > 0.2F, "hit animation visibly recoils backwards");
    check((falling * head).y < body.height && (falling * head).z < -0.5F,
          "death animation passes through an intermediate falling pose");
    check(std::abs((down * head).y - body.half_width) < 0.001F, "death ends lying on the ground");
    check(down == lc1::character_reaction_transform(true, 100, body),
          "death holds its final pose instead of looping or standing up");
}
} // namespace

int main()
{
    try {
        polymorphic_weapon_use();
        damage_and_reactions();
        attack_blocks_locomotion();
        animation_controls_attack_duration();
        attack_timing_and_deduplication();
        frame_rates_and_multiple_targets();
        geometry_and_occlusion();
        visual_reactions();
        std::cout << "Combat tests passed\n";
        return 0;
    }
    catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
