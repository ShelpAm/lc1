#include "lc1/game/world/game-world.hpp"
#include "character-input.hpp"
#include "player-input.hpp"

#include "lc1/error.hpp"
#include "lc1/game/continent/patrol-house.hpp"
#include "lc1/game/party/party.hpp"
#include "lc1/game/player/player-view.hpp"
#include "lc1/game/resources/catalog.hpp"
#include "lc1/game/resources/crafting.hpp"
#include "lc1/game/world/world-snapshot.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {
static_assert(std::is_copy_constructible_v<lc1::Party>);
static_assert(!std::is_convertible_v<lc1::PlayerId, lc1::CharacterId>);
static_assert(!std::is_invocable_v<decltype(&lc1::Party::command), lc1::Party const &,
                                   lc1::Character const &, lc1::Character &, lc1::CompanionOrder>);

void check(bool condition, std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string{message});
}

struct TestInteraction {
    lc1::CharacterIntent player_intent{};
    bool interact = false;
    bool player_input_enabled = false;
    int response_index = -1;
};

void update_world(lc1::GameWorld &world, float delta_seconds, TestInteraction const &input)
{
    TestCharacterController controller{input.player_intent};
    auto &player = world.player(lc1::PlayerId{1});
    RestoreCharacterController restore{player, player.controller()};
    player.set_controller(&controller);
    world.update(delta_seconds, {{lc1::PlayerId{1},
                                  {.view = {.keyboard_enabled = input.player_input_enabled},
                                   .interact = input.interact,
                                   .response_index = input.response_index}}});
}

class Checkpoint {
  public:
    Checkpoint()
        : directory_(std::filesystem::temp_directory_path() /
                     std::format("lc1-world-test-{:p}", static_cast<void const *>(this)))
    {
        auto data = lc1::Save{"saves/01"}.data();
        std::ranges::sort(data.characters, {}, &lc1::SerializedCharacterConfig::keyid);
        for (auto const &character : data.characters) {
            auto const index = data.states.size();
            data.states.push_back({.keyid = character.keyid,
                                   .x = static_cast<float>(index) * 10.0F,
                                   .y = 0,
                                   .z = 64,
                                   .body_yaw = 0,
                                   .look_yaw = 0,
                                   .look_pitch = 0,
                                   .patrol = false});
        }
        for (auto &state : data.states) {
            if (state.keyid == data.session.players.front().character) {
                state.x = 0;
                state.z = 64;
            }
            if (state.keyid == 100025) {
                state.x = 0;
                state.z = 62;
            }
        }
        lc1::Save::write(directory_, data);
    }

    ~Checkpoint()
    {
        std::error_code ignored;
        std::filesystem::remove_all(directory_, ignored);
    }

    std::filesystem::path const &directory() const { return directory_; }

  private:
    std::filesystem::path directory_;
};

lc1::Character const &find(lc1::GameWorld const &world, std::uint32_t id)
{
    auto const characters = world.characters();
    auto const result = std::ranges::find_if(
        characters, [id](auto const *character) { return character->config().keyid == id; });
    check(result != characters.end(), "character exists in runtime roster");
    return **result;
}

void inventory_equipment_and_flashlight()
{
    using namespace lc1;
    using namespace lc1::game;
    Checkpoint checkpoint;
    GameWorld world{Save{checkpoint.directory()}, "dialogs/catalog.yaml"};
    auto &player = world.player(PlayerId{1});
    auto &bag = player.bag();
    check(dynamic_cast<MeleeWeapon const *>(world.weapons().at(sword.keyid).get()) &&
              dynamic_cast<RangedWeapon const *>(world.weapons().at(bow.keyid).get()) &&
              dynamic_cast<Flashlight const *>(world.weapons().at(flashlight.keyid).get()),
          "save discriminators create their concrete weapon definitions");
    auto request = [&](std::uint32_t id, bool equip, CharacterId target) {
        world.update(0, {{PlayerId{1}, {.equipment = EquipmentRequest{target, id, equip}}}});
    };
    for (auto const &input : iron_helmet_recipe().inputs)
        check(bag.add(*input.resource, input.quantity), "prepare crafting materials");
    check(craft(bag, iron_helmet_recipe()), "forge a wearable helmet");
    request(iron_helmet.keyid, true, player.id());
    check(player.config().equipment.at(EquipmentType::Head)->keyid() == iron_helmet.keyid &&
              bag.quantity_of(iron_helmet.keyid) == 0,
          "crafted resource moves into the head slot");
    world.update(0, {});
    check(bag.quantity_of(iron_helmet.keyid) == 0, "equipment request is consumed once");
    Save::write(checkpoint.directory(), world.snapshot());
    GameWorld restored{Save{checkpoint.directory()}, "dialogs/catalog.yaml"};
    check(restored.player(PlayerId{1}).config().equipment.at(EquipmentType::Head)->keyid() ==
              iron_helmet.keyid,
          "equipped crafted helmet survives checkpoint assembly");
    check(bag.add(wood, 20), "fill backpack while helmet is equipped");
    request(iron_helmet.keyid, false, player.id());
    check(player.config().equipment.contains(EquipmentType::Head) &&
              bag.quantity_of(iron_helmet.keyid) == 0,
          "failed unequip preserves worn item and inventory");
    check(bag.remove(wood, 20), "make room for unequipping");
    request(iron_helmet.keyid, false, player.id());
    request(iron_helmet.keyid, false, player.id());
    check(!player.config().equipment.contains(EquipmentType::Head) &&
              bag.quantity_of(iron_helmet.keyid) == 1,
          "unequipping returns exactly one item");
    request(iron_helmet.keyid, true, CharacterId{100024});
    check(bag.quantity_of(iron_helmet.keyid) == 1,
          "cannot equip an unrelated character from this backpack");
    request(moneta.keyid, true, player.id());
    request(999999, true, player.id());
    check(!player.config().equipment.contains(EquipmentType::Head),
          "ordinary and unknown resources cannot be equipped");
    check(bag.remove(iron_helmet, 1) && bag.add(sword, 1) && bag.add(flashlight, 1),
          "prepare hand items");
    request(sword.keyid, true, player.id());
    check(bag.add(wood, 19), "fill pack before replacing heavy weapon with lighter tool");
    request(flashlight.keyid, true, player.id());
    check(player.config().main_hand->keyid() == sword.keyid &&
              bag.quantity_of(flashlight.keyid) == 1 && bag.quantity_of(sword.keyid) == 0,
          "failed swap cannot lose or duplicate either hand item");
    check(bag.remove(wood, 19), "make room for old weapon");
    request(flashlight.keyid, true, player.id());
    check(player.config().main_hand->kind() == WeaponKind::Tool &&
              bag.quantity_of(sword.keyid) == 1 && bag.quantity_of(flashlight.keyid) == 0 &&
              !player.flashlight_on(),
          "flashlight occupies the one main-hand slot, initially off");
    auto const attacks = player.attack_sequence();
    world.update(0, {{PlayerId{1}, {.swing_weapon = true}}});
    check(player.flashlight_on() && player.attack_sequence() == attacks,
          "left click toggles tool without a melee attack");
    world.update(0, {});
    check(player.flashlight_on(), "held tick does not repeatedly toggle the light");
    Save::write(checkpoint.directory(), world.snapshot());
    GameWorld lit{Save{checkpoint.directory()}, "dialogs/catalog.yaml"};
    check(lit.player(PlayerId{1}).flashlight_on() &&
              capture_world(lit).player(PlayerId{1}).flashlight_on,
          "checkpoint restores equipped flashlight and its switch");
    world.update(0, {{PlayerId{1}, {.swing_weapon = true}}});
    check(!player.flashlight_on(), "second left click turns the flashlight off");
    world.update(0, {{PlayerId{1}, {.swing_weapon = true}}});
    request(flashlight.keyid, false, player.id());
    check(!player.config().main_hand && !player.flashlight_on() &&
              bag.quantity_of(flashlight.keyid) == 1,
          "unequipping the flashlight returns it and removes its light");
    check(bag.add(torch, 1), "prepare torch");
    request(torch.keyid, true, player.id());
    check(player.config().main_hand && player.config().main_hand->action() == WeaponAction::Torch &&
              player.config().main_hand->kind() == WeaponKind::Tool && !player.flashlight_on(),
          "torch equips as a tool without using the flashlight switch");
    auto const torch_attacks = player.attack_sequence();
    world.update(0, {{PlayerId{1}, {.swing_weapon = true}}});
    check(player.attack_sequence() == torch_attacks, "using a torch never starts melee combat");
    Save::write(checkpoint.directory(), world.snapshot());
    GameWorld restored_torch{Save{checkpoint.directory()}, "dialogs/catalog.yaml"};
    check(restored_torch.player(PlayerId{1}).config().main_hand->action() == WeaponAction::Torch,
          "equipped torch survives checkpoint loading");
    request(torch.keyid, false, player.id());
    check(!player.config().main_hand && bag.quantity_of(torch.keyid) == 1,
          "torch returns to backpack on unequip");
}

void melee_damage_and_checkpoint()
{
    Checkpoint checkpoint;
    auto data = lc1::Save{checkpoint.directory()}.data();
    data.session.example = 0;
    auto &weapon = *std::ranges::find(data.weapons, 1001U, &lc1::SerializedWeapon::keyid);
    weapon.attack = 10;
    weapon.range = 1.5F;
    weapon.model = lc1::ModelConfig{};
    auto &player_config =
        *std::ranges::find(data.characters, 1U, &lc1::SerializedCharacterConfig::keyid);
    player_config.main_hand = 1001;
    auto &target_state =
        *std::ranges::find(data.states, 100025U, &lc1::SerializedCharacterState::keyid);
    target_state.z = 63;
    lc1::Save::write(checkpoint.directory(), data);
    lc1::GameWorld world{lc1::Save{checkpoint.directory()}, "dialogs/catalog.yaml"};
    auto &target = world.character(lc1::CharacterId{100025});
    float const maximum = target.config().max_health();
    update_world(world, 0, {.player_intent = {.swing_weapon = true}});
    auto const before = lc1::capture_world(world);
    world.update(0.1F, {});
    check(target.config().health() == maximum, "world attack windup causes no damage");
    world.update(0.2F, {});
    auto after = lc1::capture_world(world);
    auto const &hit = after.character(target.id());
    check(hit.health == maximum - 10 && hit.hit_sequence == 1 && !hit.dead,
          "world resolves melee damage into a health and reaction snapshot");
    check(before.character(target.id()).health == maximum,
          "damage does not mutate an already published snapshot");
    check(!world.dialogs().can_start(world.player(lc1::PlayerId{1}), target),
          "hit stun prevents conversations");
    lc1::Save::write(checkpoint.directory(), world.snapshot());
    lc1::GameWorld injured{lc1::Save{checkpoint.directory()}, "dialogs/catalog.yaml"};
    check(injured.character(target.id()).config().health() == maximum - 10,
          "checkpoint round trip preserves partial damage");
    target.take_damage(maximum);
    world.update(0.7F, {});
    lc1::Save::write(checkpoint.directory(), world.snapshot());
    lc1::GameWorld restored{lc1::Save{checkpoint.directory()}, "dialogs/catalog.yaml"};
    auto const dead = lc1::capture_world(restored).character(target.id());
    check(dead.dead && dead.health == 0 && dead.reaction_time == 0.6F,
          "loading a dead character restores the final fallen pose");
    check(!restored.dialogs().can_start(restored.player(lc1::PlayerId{1}),
                                        restored.character(target.id())),
          "a corpse cannot enter a conversation after loading");
    // Legacy version-5 checkpoints omitted health_lost: only that new field is optional.
    auto const path = checkpoint.directory() / "checkpoint.yaml";
    std::ifstream input{path};
    std::string legacy;
    for (std::string line; std::getline(input, line);)
        if (line.find("health_lost:") == std::string::npos)
            legacy += line + '\n';
    input.close();
    {
        std::ofstream output{path};
        output << legacy;
    }
    lc1::GameWorld old_save{lc1::Save{checkpoint.directory()}, "dialogs/catalog.yaml"};
    check(old_save.character(target.id()).config().health() == maximum,
          "old checkpoints default to full health");
    data = world.snapshot();
    auto &state =
        *std::ranges::find(data.states, target.id().value, &lc1::SerializedCharacterState::keyid);
    state.health_lost = std::numeric_limits<float>::quiet_NaN();
    bool rejected = false;
    try {
        lc1::Save::write(checkpoint.directory(), data);
    }
    catch (lc1::Error const &) {
        rejected = true;
    }
    check(rejected, "invalid saved health is rejected before writing");
}

void independent_interactions()
{
    Checkpoint checkpoint;
    auto data = lc1::Save{checkpoint.directory()}.data();
    data.session.players = {{17, 1}, {29, 100024}};
    for (auto &state : data.states) {
        if (state.keyid == 100024) {
            state.x = 20;
            state.z = 64;
        }
        if (state.keyid == 100026) {
            state.x = 20;
            state.z = 62;
        }
    }
    lc1::Save::write(checkpoint.directory(), data);
    lc1::GameWorld world{lc1::Save{checkpoint.directory()}, "dialogs/catalog.yaml"};
    lc1::PlayerId const first{17};
    lc1::PlayerId const second{29};
    world.player(first).bag().add(lc1::game::wood, 1);
    world.player(second).bag().add(lc1::game::wood, 2);
    lc1::PlayerControls interact;
    interact.view.keyboard_enabled = true;
    interact.interact = true;
    world.update(0, {{first, interact}, {second, interact}});
    auto state = lc1::capture_world(world);
    check(state.players.at(first).interaction_target == "Ron" &&
              state.players.at(second).interaction_target == "Martin",
          "each player targets from their own position and heading");
    check(state.players.at(first).dialog && state.players.at(second).dialog &&
              !state.players.at(first).dialog->responses.empty() &&
              !state.players.at(second).dialog->responses.empty(),
          "parallel conversations expose choices to their own respondents");
    check(state.players.at(first).bag.quantity_of(lc1::game::wood.keyid) == 1 &&
              state.players.at(second).bag.quantity_of(lc1::game::wood.keyid) == 2,
          "player snapshots preserve separate inventories");
    auto const first_choices = state.players.at(first).dialog->responses;
    auto const second_choices = state.players.at(second).dialog->responses;
    world.update(2, {});
    state = lc1::capture_world(world);
    check(state.players.at(first).dialog->responses == first_choices &&
              state.players.at(second).dialog->responses == second_choices,
          "neither human-controlled respondent is answered automatically");
    lc1::PlayerControls respond;
    respond.view.keyboard_enabled = true;
    respond.response_index = 0;
    world.update(0, {{first, respond}});
    state = lc1::capture_world(world);
    check(state.players.at(first).dialog->responses.empty() &&
              state.players.at(second).dialog->responses == second_choices,
          "a response only affects its bound player's conversation");
}

void snapshot_lifetime()
{
    auto const snapshot = [] {
        lc1::GameWorld world{lc1::Save{"saves/01"}, "dialogs/catalog.yaml"};
        return lc1::capture_world(world);
    }();
    for (auto const &party : snapshot.parties) {
        if (party.leader())
            check(!snapshot.character(*party.leader()).name.empty(),
                  "copied party leaders resolve after the simulation world is destroyed");
        for (auto id : party.members())
            check(party.has_member(id) && !snapshot.character(id).name.empty(),
                  "copied party members resolve without simulation pointers");
        for (auto id : party.captives())
            check(party.has_captive(id) && !party.has_member(id) &&
                      !snapshot.character(id).name.empty(),
                  "copied parties preserve captive membership semantics");
    }
}

void player_party_membership()
{
    Checkpoint checkpoint;
    auto data = lc1::Save{checkpoint.directory()}.data();
    data.session.players = {{17, 1}, {29, 100025}};
    lc1::Save::write(checkpoint.directory(), data);
    lc1::GameWorld world{lc1::Save{checkpoint.directory()}, "dialogs/catalog.yaml"};
    auto &leader = world.player(lc1::PlayerId{17});
    auto &member = world.player(lc1::PlayerId{29});
    auto *controller = member.controller();
    auto *party = leader.config().party;
    check(party->can_recruit(leader, member),
          "a human-controlled ordinary member can join another party");
    auto const snapshot_before = lc1::capture_world(world);
    auto const find_party = [](lc1::WorldSnapshot const &snapshot,
                               std::uint32_t id) -> lc1::Party const & {
        auto const found = std::ranges::find(snapshot.parties, id, &lc1::Party::keyid);
        if (found == snapshot.parties.end())
            throw std::runtime_error("missing snapshot party");
        return *found;
    };
    auto const &old_party = find_party(snapshot_before, party->keyid());
    check(old_party.leader() == leader.id() && !old_party.has_member(leader.id()) &&
              !old_party.has_member(member.id()),
          "shared Party queries distinguish the leader from ordinary members");
    party->recruit(leader, member);
    check(member.config().party == party && member.controller() == controller &&
              member.companion_order() == lc1::CompanionOrder::None &&
              !party->can_command(leader, member),
          "recruitment preserves human control and does not offer AI orders");
    auto const snapshot_after = lc1::capture_world(world);
    check(party->has_member(member.id()) &&
              find_party(snapshot_after, party->keyid()).has_member(member.id()) &&
              !old_party.has_member(member.id()),
          "snapshot parties use the same ID queries but own independent rosters");
    check(&world.character(member.id()) == &member &&
              snapshot_after.character(member.id()).keyid == member.id().value,
          "the same CharacterId resolves in the simulation and in its snapshot");
    auto const before = member.position();
    lc1::PlayerControls controls;
    controls.view.keyboard_enabled = true;
    controls.movement = {1, 0};
    world.update(0.1F, {{lc1::PlayerId{29}, controls}});
    check(member.position().x > before.x, "recruited players continue consuming their own input");
    lc1::Save::write(checkpoint.directory(), world.snapshot());
    lc1::GameWorld restored{lc1::Save{checkpoint.directory()}, "dialogs/catalog.yaml"};
    check(restored.player(lc1::PlayerId{17}).config().party ==
                  restored.player(lc1::PlayerId{29}).config().party &&
              !restored.player(lc1::PlayerId{29}).ai_controller(),
          "checkpoint restore preserves multiple human members in a party");
}

void world_and_checkpoint()
{
    Checkpoint checkpoint;
    // Save is destroyed immediately after construction. The world retains no borrow to it.
    lc1::GameWorld world{lc1::Save{checkpoint.directory()}, "dialogs/catalog.yaml"};
    check(world.characters().size() == 7, "headless world builds the entire fixture");
    check(std::ranges::is_sorted(world.characters(), {},
                                 [](auto const *character) { return character->config().keyid; }),
          "runtime roster has stable identity order");
    check(world.player(lc1::PlayerId{1}).config().party->leader() ==
              world.player(lc1::PlayerId{1}).id(),
          "membership is linked");
    check(find(world, 100025).config().equipment.size() == 1,
          "equipment links survive world assembly");

    check(world.player(lc1::PlayerId{1}).bag().total_weight() == 0,
          "a plain world has no implicit demonstration supplies");
    world.player(lc1::PlayerId{1}).bag() = lc1::game::Bag{16000};
    check(world.player(lc1::PlayerId{1}).bag().add(lc1::game::wood, 3), "test supplies fit");
    update_world(world, 0, {.interact = true});
    check(world.player(lc1::PlayerId{1}).bag().quantity_of(lc1::game::wood.keyid) == 3 &&
              world.player(lc1::PlayerId{1}).bag().total_weight() == 3000,
          "zero-time updates do not produce resources");
    check(world.interaction_target(lc1::PlayerId{1}) == &find(world, 100025),
          "facing selects the nearby NPC");
    check(world.dialogs().dialog_id(world.player(lc1::PlayerId{1})) == "recruit",
          "world starts recruitment");
    update_world(world, 0, {.response_index = 0});
    update_world(world, 10, {});
    check(world.dialogs().find(world.player(lc1::PlayerId{1}))->current_node_id() == "offer",
          "conversation consumes the full elapsed time of a long frame");
    update_world(world, 0, {.response_index = 0});
    check(find(world, 100025).config().party == world.player(lc1::PlayerId{1}).config().party,
          "dialog action transfers both sides of membership");
    check(find(world, 100025).companion_order() == lc1::CompanionOrder::Follow,
          "recruited character follows the leader");
    update_world(world, 0, {.interact = true});
    check(!world.dialogs().find(world.player(lc1::PlayerId{1})),
          "player can interrupt a conversation");
    world.player(lc1::PlayerId{1}).move(world.continent(), {10, 0, 0});
    auto const before = find(world, 100025).position();
    update_world(world, 0.1F, {});
    check(glm::distance(find(world, 100025).position(), world.player(lc1::PlayerId{1}).position()) <
              glm::distance(before, world.player(lc1::PlayerId{1}).position()),
          "follower uses updated leader pose");
    world.player(lc1::PlayerId{1}).set_body_yaw(40);
    world.player(lc1::PlayerId{1}).set_look_angles(20, 10);
    lc1::Save::write(checkpoint.directory(), world.snapshot());
    lc1::GameWorld restored{lc1::Save{checkpoint.directory()}, "dialogs/catalog.yaml"};
    check(restored.player(lc1::PlayerId{1}).bag().quantity_of(lc1::game::wood.keyid) == 3 &&
              restored.player(lc1::PlayerId{1}).bag().total_weight() == 3000 &&
              restored.player(lc1::PlayerId{1}).bag().max_weight() == 16000,
          "checkpoint restores actual inventory without per-frame production");
    check(restored.player(lc1::PlayerId{1}).position() == world.player(lc1::PlayerId{1}).position(),
          "checkpoint restores position");
    check(restored.player(lc1::PlayerId{1}).body_yaw() == 40 &&
              restored.player(lc1::PlayerId{1}).look_yaw() == 20 &&
              restored.player(lc1::PlayerId{1}).look_pitch() == 10,
          "checkpoint restores body and look angles");
    check(find(restored, 100025).config().party ==
                  restored.player(lc1::PlayerId{1}).config().party &&
              find(restored, 100025).companion_order() == lc1::CompanionOrder::Follow,
          "checkpoint restores recruitment and companion commands");
    check(!restored.dialogs().find(restored.player(lc1::PlayerId{1})),
          "active dialogue is intentionally transient");

    for (auto elapsed : {-1.0F, std::numeric_limits<float>::quiet_NaN()}) {
        bool rejected = false;
        try {
            update_world(restored, elapsed, {});
        }
        catch (lc1::Error const &) {
            rejected = true;
        }
        check(rejected, "world rejects invalid simulation time");
    }
}

void reload_world()
{
    Checkpoint checkpoint;
    lc1::GameWorld world{lc1::Save{checkpoint.directory()}, "dialogs/catalog.yaml"};
    auto const initial_position = world.player(lc1::PlayerId{1}).position();
    TestInteraction interact{.interact = true};
    update_world(world, 0, interact);
    check(world.dialogs().find(world.player(lc1::PlayerId{1})),
          "reload begins with an active conversation");
    world.player(lc1::PlayerId{1}).move(world.continent(), {10, 0, 0});
    world.player(lc1::PlayerId{1}).set_look_angles(20, 10);

    for (int i = 0; i < 2; ++i) {
        world.load(lc1::Save{checkpoint.directory()});
        check(world.characters().size() == 7, "reload replaces rather than appends the roster");
        check(world.player(lc1::PlayerId{1}).position() == initial_position &&
                  world.player(lc1::PlayerId{1}).look_yaw() == 0,
              "reload discards the old character state");
        check(!world.interaction_target(lc1::PlayerId{1}) &&
                  !world.dialogs().find(world.player(lc1::PlayerId{1})),
              "reload clears interactions and active conversations");
        check(world.player(lc1::PlayerId{1}).config().party->leader() ==
                  world.player(lc1::PlayerId{1}).id(),
              "reload relinks party membership");
        update_world(world, 0, interact);
        check(world.dialogs().dialog_id(world.player(lc1::PlayerId{1})) == "recruit",
              "reloaded interactions borrow the live dialog system");
    }

    auto data = lc1::Save{checkpoint.directory()}.data();
    auto alternate = data;
    alternate.continent = {"prototype", 1};
    lc1::Save::write(checkpoint.directory(), alternate);
    bool rejected = false;
    try {
        world.load(lc1::Save{checkpoint.directory()});
    }
    catch (lc1::Error const &) {
        rejected = true;
    }
    check(rejected && world.player(lc1::PlayerId{1}).position() == initial_position,
          "changing continent is rejected without clearing the existing world");
    check(world.snapshot().continent == data.continent,
          "world snapshots retain the actual continent identity");
    data.session.players.front().character = 100025;
    data.session.example = 0;
    lc1::Save::write(checkpoint.directory(), data);
    world.load(lc1::Save{checkpoint.directory()});
    check(world.player(lc1::PlayerId{1}).config().keyid == 100025,
          "reload changes player identity");
    check(world.snapshot().session.example == 0, "reload clears the previous example identity");
    update_world(world, 0.1F, {});
}

void scenario_inventory()
{
    lc1::Save const seed{"saves/01"};
    lc1::GameWorld world{seed, "dialogs/catalog.yaml"};
    auto &bag = world.player(lc1::PlayerId{1}).bag();
    check(bag.quantity_of(lc1::game::wood.keyid) == 2 && bag.total_weight() == 9300,
          "temporary demonstration setup supplies a bounded initial inventory");
    update_world(world, 0, {});
    update_world(world, 1.0F, {});
    check(bag.total_weight() == 9300, "simulation does not refill demonstration supplies");
    check(bag.remove(lc1::game::wood, 1), "consume initial supplies");
    Checkpoint checkpoint;
    lc1::Save::write(checkpoint.directory(), world.snapshot());
    lc1::Save const saved{checkpoint.directory()};
    lc1::GameWorld restored{saved, "dialogs/catalog.yaml"};
    check(restored.player(lc1::PlayerId{1}).bag().quantity_of(lc1::game::wood.keyid) == 1 &&
              restored.player(lc1::PlayerId{1}).bag().total_weight() == 8300,
          "checkpoint restores consumed inventory without applying initial supplies again");
    world.load(saved);
    check(world.player(lc1::PlayerId{1}).bag().total_weight() == 8300,
          "in-place reload also restores inventory exactly");
}

void restored_house_patrol()
{
    Checkpoint checkpoint;
    auto data = lc1::Save{checkpoint.directory()}.data();
    auto const example = data.session.example;
    for (auto &state : data.states) {
        if (state.keyid != example)
            continue;
        state.x = 36;
        state.y = 7.8F;
        state.z = 23;
        state.patrol = true;
    }
    lc1::Save::write(checkpoint.directory(), data);
    lc1::GameWorld world{lc1::Save{checkpoint.directory()}, "dialogs/catalog.yaml"};
    check(find(world, example).position().y == 7.8F,
          "house patrol restores the saved floor without teleporting");
    bool descended = false;
    bool climbed_again = false;
    for (int frame = 0; frame < 60 * 160; ++frame) {
        update_world(world, 1.0F / 60.0F, {});
        auto const height = find(world, example).position().y;
        descended |= height < 0.7F;
        climbed_again |= descended && height > 7.7F;
    }
    check(descended && climbed_again,
          "world restores a multi-floor patrol and completes its return route");
    auto snapshot = world.snapshot();
    check(
        std::ranges::find(snapshot.states, example, &lc1::SerializedCharacterState::keyid)->patrol,
        "house patrol retains the existing save patrol flag");
}

void world_applies_control()
{
    Checkpoint checkpoint;
    lc1::GameWorld world{lc1::Save{checkpoint.directory()}, "dialogs/catalog.yaml"};
    auto const before = world.player(lc1::PlayerId{1}).position();
    update_world(
        world, 0.5F,
        {.player_intent = {
             .move = {1, 0}, .body_yaw = 45, .look = glm::vec2{0, 10}, .face_movement = false}});
    check(std::abs(world.player(lc1::PlayerId{1}).position().x - before.x -
                   world.player(lc1::PlayerId{1}).config().walk_speed() * 0.5F) < 0.001F,
          "world applies movement with the full elapsed time");
    check(world.player(lc1::PlayerId{1}).body_yaw() == 45 &&
              world.player(lc1::PlayerId{1}).look_pitch() == 10,
          "world applies orientation without a view or controller update");
    auto const pose = world.player(lc1::PlayerId{1}).position();
    bool rejected = false;
    try {
        update_world(world, 0.1F,
                     {.player_intent = {.move = {0, 1},
                                        .body_yaw = std::numeric_limits<float>::quiet_NaN()}});
    }
    catch (lc1::Error const &) {
        rejected = true;
    }
    check(rejected && world.player(lc1::PlayerId{1}).position() == pose,
          "invalid control is rejected before mutating the player");
}

void attack_animation_timing()
{
    Checkpoint checkpoint;
    auto data = lc1::Save{checkpoint.directory()}.data();
    auto &record = *std::ranges::find(data.characters, 1U, &lc1::SerializedCharacterConfig::keyid);
    record.main_hand = 1001;
    lc1::ModelConfig model;
    model.nodes.value().push_back({.name = "Body"});
    lc1::ModelAnimationConfig clip{.name = "Attack", .channels = {}};
    lc1::ModelAnimationChannelConfig channel;
    channel.node = 0;
    channel.path = lc1::scene::AnimationChannel::Path::Rotation;
    channel.times = {0, 3};
    channel.values = {{0, 0, 0}, {0, 20, 0}};
    clip.channels.push_back(channel);
    model.animations.value().push_back(clip);
    model.attack_animation.value() = "Attack";
    record.model.value() = model;
    lc1::Save::write(checkpoint.directory(), data);
    lc1::GameWorld world{lc1::Save{checkpoint.directory()}, "dialogs/catalog.yaml"};
    check(world.player(lc1::PlayerId{1}).config().attack_animation_duration == 3,
          "world resolves the configured attack clip duration without graphics");
    lc1::PlayerControls controls;
    controls.view.keyboard_enabled = true;
    controls.swing_weapon = true;
    controls.movement = {1, 0};
    auto const before = world.player(lc1::PlayerId{1}).position();
    world.update(0, {{lc1::PlayerId{1}, controls}});
    controls.swing_weapon = false;
    world.update(1.5F, {{lc1::PlayerId{1}, controls}});
    auto const state = lc1::capture_world(world);
    check(std::abs(state.player(lc1::PlayerId{1}).attack_progress - 0.5F) < 0.0001F &&
              world.player(lc1::PlayerId{1}).position().x == before.x,
          "snapshot animation progress and movement lock share the resolved three-second timeline");
}

void transfer_control_and_checkpoint()
{
    Checkpoint checkpoint;
    lc1::GameWorld world{lc1::Save{checkpoint.directory()}, "dialogs/catalog.yaml"};
    auto const original = world.player(lc1::PlayerId{1}).id();
    auto const target = lc1::CharacterId{world.snapshot().session.example};
    check(world.transfer_control(lc1::PlayerId{1}, target), "transfer to example succeeds");
    check(world.player(lc1::PlayerId{1}).id() == target &&
              world.character(original).ai_controller(),
          "old character resumes AI control");
    auto const before = world.player(lc1::PlayerId{1}).position();
    world.update(0.1F,
                 {{lc1::PlayerId{1},
                   {.view = {.keyboard_enabled = true}, .movement = {0, 1}, .sprint = true}}});
    auto const distance = glm::length(world.player(lc1::PlayerId{1}).position() - before);
    check(distance > world.player(lc1::PlayerId{1}).config().walk_speed() * 0.1F,
          "Shift sprint reaches the newly controlled character");
    auto const running = lc1::capture_world(world);
    check(running.player(lc1::PlayerId{1}).running && running.player(lc1::PlayerId{1}).moving,
          "newly controlled character publishes running movement from the simulation tick");
    check(lc1::interpolate_world(running, running, 1.0F).player(lc1::PlayerId{1}).moving,
          "repeated render samples of one tick keep the movement animation active");
    world.update(0.1F, {{lc1::PlayerId{1}, {}}});
    auto const stopped = lc1::capture_world(world).player(lc1::PlayerId{1});
    check(!stopped.running && !stopped.moving, "stopping returns to idle animation state");
    lc1::Save::write(checkpoint.directory(), world.snapshot());
    lc1::GameWorld restored{lc1::Save{checkpoint.directory()}, "dialogs/catalog.yaml"};
    check(restored.player(lc1::PlayerId{1}).id() == target,
          "checkpoint restores transferred control");
}

void control_physics_camera_order()
{
    Checkpoint checkpoint;
    lc1::GameWorld world{lc1::Save{checkpoint.directory()}, "dialogs/catalog.yaml"};
    lc1::PlayerView view{world.continent(), world.player(lc1::PlayerId{1}).position()};
    world.player(lc1::PlayerId{1}).set_body_yaw(25);
    world.player(lc1::PlayerId{1}).set_look_angles(10, 5);
    view.toggle(world.player(lc1::PlayerId{1}));
    check(world.player(lc1::PlayerId{1}).body_yaw() == 25 &&
              world.player(lc1::PlayerId{1}).look_yaw() == 10 &&
              world.player(lc1::PlayerId{1}).look_pitch() == 5,
          "view switching leaves character pose unchanged until the world applies intent");
    auto const before_control = world.player(lc1::PlayerId{1}).position();
    lc1::InputState devices;
    devices.on_key(lc1::Key::Space, true);
    lc1::GameInputRouter input{lc1::default_game_bindings(), lc1::GameInputContext::Gameplay};
    input.update(devices);
    view.update_view_mode(input, true, world.player(lc1::PlayerId{1}));
    lc1::PlayerControls controls;
    lc1::PlayerCharacterController player_controller{controls};
    world.player(lc1::PlayerId{1}).set_controller(&player_controller);
    view.prepare_input({}, true, true);
    check(world.player(lc1::PlayerId{1}).position() == before_control &&
              world.player(lc1::PlayerId{1}).on_ground(),
          "resolving view input does not mutate world state");
    controls = lc1::PlayerCharacterController::read_input(input, view.character_input());
    world.update(0.1F, {});
    view.update_camera(0.1F, world.player(lc1::PlayerId{1}));
    check(!world.player(lc1::PlayerId{1}).on_ground() &&
              world.player(lc1::PlayerId{1}).position().y > 0,
          "jump input reaches physics in the same frame");
    check(view.camera().position() == world.player(lc1::PlayerId{1}).eye_position(),
          "camera follows the resolved physics pose in the same frame");
    auto const before = world.player(lc1::PlayerId{1}).position();
    devices.begin_frame();
    devices.on_key(lc1::Key::W, true);
    devices.on_key(lc1::Key::D, true);
    input.update(devices);
    view.update_view_mode(input, false, world.player(lc1::PlayerId{1}));
    view.prepare_input({500, 500}, false, false);
    controls = lc1::PlayerCharacterController::read_input(input, view.character_input());
    world.update(0, {});
    check(world.player(lc1::PlayerId{1}).position() == before &&
              world.player(lc1::PlayerId{1}).body_yaw() == 0,
          "disabled controls cannot move or turn the player");
}
} // namespace

int main()
{
    try {
        inventory_equipment_and_flashlight();
        melee_damage_and_checkpoint();
        control_physics_camera_order();
        independent_interactions();
        player_party_membership();
        snapshot_lifetime();
        world_and_checkpoint();
        reload_world();
        world_applies_control();
        transfer_control_and_checkpoint();
        attack_animation_timing();
        restored_house_patrol();
        scenario_inventory();
        std::cout << "Game world tests passed\n";
        return EXIT_SUCCESS;
    }
    catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
