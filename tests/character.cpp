#include "lc1/game/character/character.hpp"
#include "lc1/game/actions.hpp"
#include "lc1/game/character/ai-character-controller.hpp"
#include "lc1/game/continent/continent.hpp"
#include "lc1/game/dialog/dialog.hpp"
#include "lc1/game/party/faction.hpp"
#include "lc1/game/party/party.hpp"
#include "lc1/game/player/player-controller.hpp"
#include "lc1/game/resources/crafting.hpp"
#include "lc1/game/session/save.hpp"
#include "lc1/scene/model.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {

void check(bool condition, std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string{message});
}

bool near(float a, float b)
{
    return std::abs(a - b) < 0.001F;
}

bool near(glm::vec3 a, glm::vec3 b)
{
    return glm::length(a - b) < 0.001F;
}

template <typename Function> void rejects(Function function, std::string_view message)
{
    bool rejected = false;
    try {
        function();
    }
    catch (lc1::Error const &) {
        rejected = true;
    }
    check(rejected, message);
}

lc1::ContinentData make_world_data()
{
    return {
        .origin = {-16.0F, -16.0F},
        .tile_size = 16.0F,
        .tiles = std::vector(2, std::vector<lc1::TileData>(2)),
        .spawn = {1.0F, 0.0F, 2.0F},
        .patches = {},
        .blocks = {{{5, 0, -10}, {5.1F, 4, 10}}},
        .regions = {{lc1::RegionId{1}, "Plain", lc1::RegionType::Plain}},
        .locations = {},
        .roads = {},
        .height_samples = {},
    };
}

lc1::Continent make_world()
{
    return lc1::Continent{make_world_data()};
}

void blacksmith_forging()
{
    using namespace lc1::game;
    auto continent = make_world();
    lc1::Character player{
        continent, {.position = continent.spawn()}, {.keyid = 101, .name = "Customer"}};
    lc1::Character smith{
        continent, {.position = continent.spawn()}, {.keyid = 4, .name = "Blacksmith"}};
    auto const &iron = pig_iron;
    auto const &helmet = iron_helmet;
    auto &bag = player.bag();
    // This recipe needs 12.2 kg of inputs; give the crafting fixture an explicit capacity.
    bag = Bag{12200};
    check(bag.add(moneta, 20) && bag.add(wood, 2) && bag.add(iron, 5),
          "prepare exact crafting inputs through the public bag interface");
    lc1::DialogCatalog catalog{"runtime/dialogs/catalog.yaml"};
    check(catalog.select(player, smith, lc1::DialogTrigger::Interaction) == "blacksmith",
          "authored smith rule takes precedence over generic interactions");
    auto const script = lc1::DialogScript::load("runtime/dialogs/blacksmith.yaml");
    lc1::Dialog dialog{script};
    dialog.start(player, smith);
    dialog.respond(player, 0);
    dialog.update(1.5F, &player, 1.5F);
    check(dialog.response_id(0) == "forge", "exact ingredients allow forging");
    check(bag.remove(moneta, 1), "remove one coin before choosing the stale response");
    dialog.respond(player, 0);
    check(!dialog.displaying_response() && bag.quantity_of(pig_iron.keyid) == 5 &&
              bag.quantity_of(wood.keyid) == 2 && bag.quantity_of(iron_helmet.keyid) == 0,
          "stale unaffordable choice consumes nothing");
    check(dialog.candidate_responses().size() == 1 && dialog.response_id(0) == "cancel",
          "unaffordable crafting still allows exiting");
    check(bag.add(moneta, 1), "restore the missing coin");
    Bag limited{12200};
    check(limited.add(moneta, 20) && limited.add(wood, 2) && limited.add(iron, 5),
          "prepare a bag exactly at its weight limit");
    auto const &recipe = iron_helmet_recipe();
    check(recipe.input_weight == 12200 && recipe.output_weight == 2500,
          "helmet recipe reports the weight of its inputs and outputs");
    Recipe too_large{recipe.inputs, {{&wood, 10}, {&iron, 18}}};
    check(!craft(limited, too_large) && limited.quantity_of(moneta.keyid) == 20 &&
              limited.quantity_of(wood.keyid) == 2 && limited.quantity_of(pig_iron.keyid) == 5,
          "failure on a later output preserves all materials and weight");
    check(can_craft(limited, recipe) && limited.quantity_of(moneta.keyid) == 20,
          "a full bag can craft when consuming the ingredients frees enough weight");
    dialog.clear();
    dialog.start(player, smith);
    dialog.respond(player, 0);
    dialog.update(1.5F, &player, 1.5F);
    dialog.respond(player, 0);
    dialog.respond(player, 0);
    check(bag.quantity_of(iron_helmet.keyid) == 1 && bag.quantity_of(moneta.keyid) == 0 &&
              bag.quantity_of(wood.keyid) == 0 && bag.quantity_of(pig_iron.keyid) == 0,
          "forging consumes exactly one recipe and repeated input cannot duplicate the result");
    check(!craft(bag, iron_helmet_recipe()), "missing materials cannot forge a second helmet");
    check(bag.remove(helmet, 1) && bag.add(wood, 12) && !bag.add(wood, 1),
          "crafting commits the weight counter together with the stacks");
    Bag overflow{std::numeric_limits<std::uint32_t>::max()};
    auto const max_helmets = std::numeric_limits<std::uint32_t>::max() / helmet.weight;
    check(overflow.add(helmet, max_helmets) && !overflow.add(helmet, 1),
          "capacity checks cannot wrap around the weight counter");
    Recipe overflowing_output{{}, {{&helmet, 1}}};
    check(!craft(overflow, overflowing_output) &&
              overflow.quantity_of(iron_helmet.keyid) == max_helmets,
          "output overflow preserves the existing inventory");
    Bag default_bag{10000};
    check(default_bag.max_weight() == 10000 && default_bag.add(wood, 10) &&
              !default_bag.add(moneta, 1),
          "default bag holds ten kilograms and rejects excess weight");
    Resource const crystal{9001, "Crystal", ResourceCategory::Material, 125};
    Bag custom{500};
    check(custom.add(crystal, 4) && custom.quantity_of(crystal.keyid) == 4 &&
              custom.total_weight() == 500 && !custom.add(crystal, 1),
          "new resource records work without an enum or subclass");
    Recipe crystal_recipe{{{&crystal, 4}}, {{&moneta, 10}}};
    check(craft(custom, crystal_recipe) && custom.quantity_of(crystal.keyid) == 0 &&
              custom.quantity_of(moneta.keyid) == 10 && custom.total_weight() == 100,
          "recipes consume and produce resources by their record identities");
    Bag weighted{3000};
    check(weighted.add(wood, 1) && weighted.add(iron, 1) && weighted.total_weight() == 3000 &&
              weighted.max_weight() == 3000 && !weighted.add(moneta, 1),
          "mixed resources use unit weights and reject excess weight without mutation");
    check(weighted.remove(iron, 1) && weighted.total_weight() == 1000 &&
              weighted.add(moneta, 200) && weighted.total_weight() == 3000,
          "removing weighted resources releases exactly their weight");
    check(!weighted.add(helmet, std::numeric_limits<std::uint32_t>::max()) &&
              weighted.total_weight() == 3000,
          "weight multiplication cannot wrap before the capacity check");
    Bag duplicate_inputs{10000};
    check(duplicate_inputs.add(wood, 2), "prepare repeated recipe inputs");
    Recipe repeated{{{&wood, 2}, {&wood, 1}}, {{&helmet, 1}}};
    check(!craft(duplicate_inputs, repeated) && duplicate_inputs.quantity_of(wood.keyid) == 2,
          "repeated inputs cannot consume the same resources twice");
}

void dialog_script_validation()
{
    auto const path = std::filesystem::path{"runtime/dialogs/recruit.yaml"};
    auto const script = lc1::DialogScript::load(path);
    auto invalid = [&](auto change, std::string_view message) {
        auto copy = script;
        change(copy);
        rejects([&] { copy.validate(); }, message);
    };
    invalid([](auto &s) { s.version = 3; }, "unsupported script version is rejected");
    invalid([](auto &s) { s.entry = "missing"; }, "missing entry is rejected");
    invalid([](auto &s) { s.nodes.push_back(s.nodes.front()); }, "duplicate nodes are rejected");
    invalid([](auto &s) { s.nodes[0].id = "end"; }, "end is a reserved node id");
    invalid([](auto &s) { s.nodes[0].choices[0].next = "missing"; },
            "dangling transitions are rejected");
    invalid([](auto &s) { s.nodes[0].choices[1].id = s.nodes[0].choices[0].id; },
            "duplicate choice ids are rejected");
    invalid([](auto &s) { s.nodes[0].choices.resize(1); },
            "a node needs an always available fallback");
    invalid([](auto &s) { s.nodes[0].choices.clear(); }, "empty choices are rejected");
    invalid([](auto &s) { s.nodes[0].choices.resize(5); }, "five choices are rejected");
    invalid([](auto &s) { s.nodes[0].text = "Hello {unknown}"; },
            "unknown placeholders are rejected");
    invalid([](auto &s) { s.nodes[0].text = "Hello }"; }, "unmatched braces are rejected");

    struct TemporaryFile {
        std::filesystem::path path =
            std::filesystem::temp_directory_path() /
            ("lc1-dialog-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".yaml");

        ~TemporaryFile()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    } temporary;

    auto input = std::ifstream{path};
    std::string const original{std::istreambuf_iterator<char>{input},
                               std::istreambuf_iterator<char>{}};
    auto malformed = [&](std::string text, std::string_view message) {
        {
            auto output = std::ofstream{temporary.path};
            output << text;
        }
        rejects([&] { lc1::DialogScript::load(temporary.path); }, message);
    };
    malformed(original + "unexpected: true\n", "unknown YAML fields are rejected");
    auto replaced = [&](std::string_view from, std::string_view to) {
        auto text = original;
        auto const at = text.find(from);
        check(at != std::string::npos, "test replacement exists in script");
        text.replace(at, from.size(), to);
        return text;
    };
    malformed(replaced("condition: CanRecruit", "condition: Unknown"),
              "unknown condition names are rejected");
    malformed(replaced("action: Recruit", "action: Unknown"), "unknown action names are rejected");
    malformed(replaced("        action: Recruit\n", ""), "missing fields are rejected");
    malformed(replaced("        action: Recruit", "        typo: Recruit"),
              "nested unknown fields are rejected");
    malformed("nodes: [", "malformed YAML is rejected");
    rejects([&] { lc1::DialogScript::load(temporary.path / "missing"); },
            "missing files are rejected");
}

// Finish the spoken response so assertions can inspect the following node.
void choose(lc1::Dialog &dialog, lc1::Character &character, int index)
{
    dialog.respond(character, index);
    if (dialog.displaying_response())
        dialog.update(1.5F, &character, 1.5F);
}

void companion_orders()
{
    auto const world = make_world();
    lc1::Faction faction{1, "Faction"};
    lc1::Party party{1, &faction};
    lc1::Party other{2, &faction};
    auto config = [](std::uint32_t id, lc1::Party &owner) {
        return lc1::CharacterConfig{.keyid = id,
                                    .name = "Character",
                                    .sex = false,
                                    .age = 18,
                                    .party = &owner,
                                    .level = 1,
                                    .equipment = {},
                                    .weapon = {}};
    };
    lc1::Character leader{world, {.position = {0, 0, 0}}, config(1, party)};
    lc1::Character member{world, {.position = {-8, 0, 0}}, config(2, party)};
    lc1::Character captive{world, {.position = {-6, 0, 0}}, config(3, party)};
    lc1::Character stranger{world, {.position = {-4, 0, 0}}, config(4, other)};
    party.set_leader(&leader);
    party.add_member(&member);
    party.add_captive(&captive);
    check(!party.can_command(member, leader) && !party.can_command(leader, captive) &&
              !party.can_command(leader, stranger) && !party.can_command(leader, leader),
          "commands require a leader and an ordinary member of the same party");
    rejects([&] { party.command(member, leader, lc1::CompanionOrder::Follow); },
            "members cannot command their leader");
    rejects([&] { party.command(leader, captive, lc1::CompanionOrder::Follow); },
            "captives cannot receive orders");
    rejects([&] { party.command(leader, member, lc1::CompanionOrder::None); },
            "orders cannot re-enable a discarded patrol");
    member.set_controller(std::make_unique<lc1::AiCharacterController>(
        std::vector<lc1::Waypoint>{{.position = {-8, 0, 0}}, {.position = {-12, 0, 0}}}));
    party.command(leader, member, lc1::CompanionOrder::Follow);
    check(member.controller() &&
              member.controller()->companion_order() == lc1::CompanionOrder::Follow,
          "following replaces the previous patrol with a companion controller");
    auto step = [&](lc1::Character &character, float delta_seconds,
                    bool controller_enabled = true) {
        auto intent = lc1::CharacterIntent{};
        if (controller_enabled && character.controller())
            intent = character.controller()->update(std::min(delta_seconds, 0.1F), character);
        character.update(world, delta_seconds, intent);
    };
    step(member, 0.1F, false);
    check(near(member.position().x, -8.0F), "a conversation pauses following");
    step(member, 0.1F);
    check(member.position().x > -8.0F, "followers move toward their leader");
    for (int i = 0; i < 100; ++i)
        step(member, 0.1F);
    check(near(member.position(), {-2, 0, 0}), "followers stop at two metres without overshooting");
    auto const stopped = member.position();
    step(member, 10.0F);
    check(near(member.position(), stopped), "a long frame does not move a settled follower");
    party.command(leader, member, lc1::CompanionOrder::Wait);
    leader.move(world, {-4, 0, 0});
    step(member, 0.1F);
    check(near(member.position(), stopped), "waiting members stay when their leader moves");
    member.request_jump();
    step(member, 0.1F);
    check(!member.on_ground(), "waiting preserves gravity and jumping");
    for (int i = 0; i < 30; ++i)
        step(member, 0.1F);
    check(member.on_ground(), "waiting members still land");
    party.command(leader, member, lc1::CompanionOrder::Follow);
    lc1::Character far_leader{world, {.position = {10, 0, 0}}, config(5, party)};
    party.set_leader(&far_leader);
    for (int i = 0; i < 100; ++i)
        step(member, 0.1F);
    check(member.position().x <= 4.701F && member.position().x > 4.6F,
          "following obeys collision and cannot cross a wall without a path");
    auto const saved = lc1::serialize_character_state(member);
    check(saved.companion_order == lc1::CompanionOrder::Follow && !saved.patrol,
          "a follow order is serialized independently of the patrol controller");
    party.set_leader(&leader);
    party.command(leader, member, lc1::CompanionOrder::Wait);
    check(lc1::serialize_character_state(member).companion_order == lc1::CompanionOrder::Wait,
          "wait is distinguishable from a stopped patrol");

    auto const script = lc1::DialogScript::load("runtime/dialogs/companion.yaml");
    lc1::Dialog dialog{script};
    check(dialog.start(leader, member), "leaders can talk to their members");
    auto choose_id = [&](std::string_view id) {
        for (std::size_t i = 0; i < dialog.candidate_responses().size(); ++i)
            if (dialog.response_id(i) == id) {
                dialog.respond(leader, static_cast<int>(i));
                return;
            }
        throw std::runtime_error("missing companion command");
    };
    choose_id("follow");
    check(member.companion_order() == lc1::CompanionOrder::Follow && dialog.displaying_response(),
          "the authored follow response issues an order");
    dialog.update(1.5F, &leader, 1.5F);
    check(!dialog.target(), "a companion command closes after displaying the response");
    dialog.start(leader, member);
    choose_id("wait");
    check(member.companion_order() == lc1::CompanionOrder::Wait,
          "the authored wait response issues an order");
    dialog.update(1.5F, &leader, 1.5F);
    dialog.start(leader, member);
    party.set_leader(nullptr);
    choose_id("follow");
    check(member.companion_order() == lc1::CompanionOrder::Wait && !dialog.displaying_response(),
          "stale command choices recheck authority before changing orders");
    check(dialog.candidate_responses().size() == 2,
          "unauthorized speakers see chat and goodbye without command choices");
}

void recruitment_dialog()
{
    auto const world = make_world();
    lc1::Faction faction{1, "Faction"};
    lc1::Party party{1, &faction};
    lc1::Party other{2, &faction};
    auto const config = [](std::uint32_t keyid, std::string_view name, lc1::Party &owner) {
        return lc1::CharacterConfig{.keyid = keyid,
                                    .name = std::string{name},
                                    .sex = false,
                                    .age = 18,
                                    .party = &owner,
                                    .level = 1,
                                    .equipment = {},
                                    .weapon = {}};
    };
    lc1::Character player{world, {.position = world.spawn()}, config(1, "Player", party)};
    lc1::Character npc{world, {.position = world.spawn()}, config(2, "Recruit", party)};
    lc1::Character stranger{world, {.position = world.spawn()}, config(3, "Stranger", other)};
    party.set_leader(&player);
    rejects([&] { party.add_member(&player); }, "the leader cannot also be an ordinary member");
    rejects([&] { party.add_captive(&player); }, "the leader cannot also be a captive");
    party.add_captive(&npc);
    rejects([&] { party.set_leader(&npc); }, "a captive cannot also be the leader");
    other.set_leader(&stranger);
    npc.set_controller(std::make_unique<lc1::AiCharacterController>(
        std::vector<lc1::Waypoint>{{.position = world.spawn()}}));

    // Exercise authored transitions and reordered choices before changing party state.
    auto script = lc1::DialogScript::load("runtime/dialogs/recruit.yaml");
    script.nodes.resize(1);
    script.nodes[0].choices[0].id = "recruit";
    script.nodes[0].choices[0].action = lc1::DialogAction::Recruit;
    script.nodes[0].choices[0].next = "end";
    auto follow_up = script.nodes.front();
    follow_up.id = "details";
    follow_up.text = "Welcome, {talker_name}.";
    std::ranges::reverse(follow_up.choices);
    script.nodes.push_back(std::move(follow_up));
    script.nodes[0].choices.insert(script.nodes[0].choices.begin(),
                                   {.id = "details",
                                    .text = "Tell me more, {target_name}.",
                                    .condition = lc1::DialogCondition::Always,
                                    .action = lc1::DialogAction::None,
                                    .recruiter = lc1::DialogRole::Talker,
                                    .npc_priority = 0,
                                    .next = "details"});
    lc1::Dialog branching{script};
    branching.start(player, npc);
    check(branching.current_speech_text() == "Hello, I am Recruit!" &&
              branching.candidate_responses()[0] == "Tell me more, Recruit.",
          "target placeholders expand in speech and choices");
    check(branching.response_id(0) == "details" && branching.response_id(99).empty(),
          "visible choices retain stable ids and invalid indices are empty");
    choose(branching, player, 0);
    check(branching.current_node_id() == "details" &&
              branching.current_speech_text() == "Welcome, Player." &&
              branching.response_id(0) == "goodbye" && branching.response_id(1) == "recruit",
          "a choice jumps to its authored node and preserves authored choice order");
    choose(branching, player, 0);
    check(!branching.target() && branching.current_node_id().empty(),
          "goodbye in the first position does not recruit");

    auto const recruitment_script = lc1::DialogScript::load("runtime/dialogs/recruit.yaml");
    lc1::Dialog dialog{recruitment_script};
    dialog.start(player, npc);
    check(dialog.target() == &npc && dialog.candidate_responses().size() == 2,
          "an own captive offers recruitment and goodbye");
    choose(dialog, player, -1);
    choose(dialog, player, 2);
    choose(dialog, stranger, 0);
    check(party.members().empty() && dialog.target() == &npc,
          "invalid choices and other talkers cannot recruit or close the conversation");
    choose(dialog, player, 1);
    check(!dialog.target() && dialog.candidate_responses().empty() &&
              dialog.current_speech_text().empty() && party.captives().size() == 1 &&
              npc.controller(),
          "goodbye clears all dialogue state without changing the NPC");

    dialog.start(player, npc);
    auto const bindings = lc1::default_game_bindings();
    lc1::GameInputRouter router{bindings, lc1::GameInputContext::Gameplay};
    lc1::InputState input;
    input.on_key(lc1::Key::Digit1, true);
    router.update(input);
    check(router.pressed(lc1::GameAction::DialogResponse0), "key 1 selects the first response");
    if (router.pressed(lc1::GameAction::DialogResponse0))
        choose(dialog, player, 0);
    check(dialog.current_node_id() == "offer" && party.members().empty() &&
              party.captives().size() == 1,
          "an invitation advances to confirmation without recruiting");
    choose(dialog, player, 1);
    check(dialog.current_node_id() == "greeting" && party.members().empty(),
          "the player can return to the previous round without recruiting");
    choose(dialog, player, 0);
    choose(dialog, player, 0);
    check(dialog.current_node_id() == "joined" && party.members().size() == 1 &&
              party.members().front() == &npc && party.captives().empty() &&
              npc.config().party == &party && npc.controller() &&
              npc.companion_order() == lc1::CompanionOrder::Follow,
          "recruiting releases an own captive, adds membership and stops the old patrol");
    check(dialog.candidate_responses().size() == 1 && dialog.response_id(0) == "goodbye",
          "successful recruitment advances to an acknowledgement round");
    choose(dialog, player, 0);
    check(!dialog.target(), "the final response closes the multi-round dialogue");
    choose(dialog, player, 0);
    dialog.start(player, npc);
    check(dialog.candidate_responses().size() == 1, "an existing companion only offers goodbye");
    choose(dialog, player, 0);
    check(party.members().size() == 1, "repeated responses cannot duplicate membership");
    rejects([&] { party.recruit(player, npc); }, "duplicate recruitment is rejected");
    check(!party.can_recruit(player, player) && !party.can_recruit(player, stranger) &&
              !party.can_recruit(npc, stranger),
          "self, foreign leaders and nonleader recruiters cannot recruit");

    lc1::Character outsider{world, {.position = world.spawn()}, config(4, "Outsider", other)};
    other.add_member(&outsider);
    rejects([&] { other.set_leader(&outsider); }, "an ordinary member cannot also be the leader");
    check(party.can_recruit(player, outsider), "another party's ordinary member is eligible");
    dialog.start(player, outsider);
    choose(dialog, player, 0);
    party.recruit(player, outsider);
    choose(dialog, player, 0);
    check(dialog.target() == &outsider && dialog.candidate_responses().size() == 2 &&
              dialog.current_node_id() == "offer" && party.members().size() == 2 &&
              other.members().empty() && outsider.config().party == &party &&
              other.leader() == &stranger,
          "stale choices refresh without a second transfer and preserve the source leader");
    choose(dialog, player, 1);
    check(!dialog.target(), "a stale confirmation can still exit the dialogue");

    auto const roster = lc1::serialize_party(party);
    auto const saved_npc = lc1::serialize_character(npc);
    auto const saved_state = lc1::serialize_character_state(npc);
    check(roster.leader == 1 && roster.members.size() == 2 &&
              !std::ranges::contains(roster.members, 1U) && roster.captives.empty() &&
              saved_npc.party == 1 && !saved_state.patrol && near(saved_state.x, npc.position().x),
          "serialization retains recruitment, a separate leader and the stopped patrol");

    lc1::Character captive{world, {.position = world.spawn()}, config(5, "Captive", other)};
    other.add_captive(&captive);
    rejects([&] { party.recruit(player, captive); }, "foreign captives cannot be recruited");
    check(captive.config().party == &other && other.captives().size() == 1 &&
              party.members().size() == 2,
          "a rejected transfer leaves both parties unchanged");
    lc1::Character reordered_recruit{
        world, {.position = world.spawn()}, config(6, "New recruit", other)};
    other.add_member(&reordered_recruit);
    // Always does not bypass the action's own eligibility check.
    auto reordered_script = script;
    reordered_script.nodes[1].choices[1].condition = lc1::DialogCondition::Always;
    lc1::Dialog reordered{reordered_script};
    reordered.start(player, reordered_recruit);
    choose(reordered, player, 0);
    choose(reordered, player, 1);
    check(!reordered.target() && reordered_recruit.config().party == &party,
          "recruitment executes by authored action rather than visible index");
    reordered.start(player, reordered_recruit);
    choose(reordered, player, 0);
    check(reordered.candidate_responses().size() == 1 && reordered.response_id(0) == "goodbye",
          "an Always recruit choice still checks gameplay eligibility");
    reordered.clear();

    dialog.start(player, player);
    check(!dialog.target(), "self conversation is ignored");

    input.begin_frame();
    input.on_key(lc1::Key::Digit2, true);
    router.push(lc1::GameInputContext::Ui);
    router.update(input);
    check(!router.pressed(lc1::GameAction::DialogResponse1), "UI context gates response input");
    router.pop();
    router.update(input);
    check(router.pressed(lc1::GameAction::DialogResponse1), "key 2 selects the second response");
}

void jumping()
{
    auto const world = make_world();
    auto make_character = [&] { return lc1::Character{world, {.position = world.spawn()}, {}}; };
    auto character = make_character();
    check(character.on_ground(), "spawn starts grounded");
    character.request_jump();
    character.update(world, 0.0F, {});
    check(character.on_ground(), "zero-time update does not start a jump");
    character.update(world, 0.01F, {});
    check(!character.on_ground() && character.position().y > 0.0F, "jump leaves the ground");
    float const airborne_height = character.position().y;
    character.walk(world, {1, 0, 0}, 0.01F, false);
    check(near(character.position().y, airborne_height), "walking preserves jump height");
    check(character.position().x > world.spawn().x, "walking works while airborne");
    character.move(world, {10, 0, 0});
    check(character.position().x < 4.701F, "jump retains ground-plane obstacle blocking");
    character.request_jump();
    character.update(world, 0.01F, {});
    float const expected_height =
        std::sqrt(2.0F * lc1::gravityf * 0.5F) * 0.02F - 0.5F * lc1::gravityf * 0.02F * 0.02F;
    check(near(character.position().y, expected_height), "airborne request cannot double jump");
    for (int i = 0; i < 200; ++i)
        character.update(world, 0.01F, {});
    check(character.on_ground() && character.position().y == 0.0F, "landing clamps to ground");
    character.update(world, 0.1F, {});
    check(character.position().y == 0.0F, "landing clears falling velocity");
    character.request_jump();
    character.update(world, 0.01F, {});
    check(near(character.position().y, airborne_height), "subsequent jump has the same impulse");

    for (float const step : {1.0F / 30.0F, 1.0F / 60.0F, 1.0F / 144.0F}) {
        auto jumper = make_character();
        jumper.request_jump();
        float peak = 0.0F;
        for (int i = 0; i < 300; ++i) {
            jumper.update(world, step, {});
            peak = std::max(peak, jumper.position().y);
        }
        check(std::abs(peak - 0.5F) < 0.002F, "jump height is stable across frame rates");
        check(jumper.on_ground(), "jump lands at each frame rate");
    }
    auto capped = make_character();
    auto regular = make_character();
    capped.request_jump();
    regular.request_jump();
    capped.update(world, 10.0F, {});
    regular.update(world, 0.1F, {});
    check(near(capped.position(), regular.position()), "update caps stalled frame time");
    rejects([&] { character.update(world, -1.0F, {}); }, "negative update time is rejected");
    rejects([&] { character.update(world, std::numeric_limits<float>::quiet_NaN(), {}); },
            "nonfinite update time is rejected");

    auto first = make_character();
    auto first_intent = lc1::PlayerController::first_person(first, {}, {});
    first_intent.jump = true;
    first.update(world, 0.01F, first_intent);
    check(!first.on_ground(), "first-person controller requests jump");
    auto third = make_character();
    auto third_intent = lc1::PlayerController::third_person({}, {0, 0, -1});
    third_intent.jump = true;
    third.update(world, 0.01F, third_intent);
    check(!third.on_ground(), "third-person controller requests jump");
}

void pose_and_movement()
{
    auto const world = make_world();
    lc1::Character character{world, {.position = world.spawn()}, {}};
    check(near(character.position(), world.spawn()),
          "character starts at the supplied world spawn");
    check(near(character.body_heading(), {0, 0, -1}) &&
              near(character.look_direction(), {0, 0, -1}),
          "initial body and look face -Z");
    character.set_body_yaw(450.0F);
    check(near(character.body_heading(), {-1, 0, 0}), "body yaw wraps and uses +Y rotation");
    character.set_look_angles(-90.0F, 100.0F);
    check(near(character.look_yaw(), -85) && near(character.look_pitch(), 89),
          "relative look angles have head limits");
    check(near(glm::length(character.look_direction()), 1) &&
              near(glm::length(character.right()), 1),
          "look basis remains normalized near vertical pitch");

    auto const body = character.body_heading();
    character.move(world, {10, 0, 3});
    check(near(character.position(), {4.7F, 0, 5}), "displacement sweeps and slides at a wall");
    check(near(character.body_heading(), body), "moving does not silently turn the body");
    auto const matrix = character.transform().model_matrix();
    check(near(glm::vec3{matrix * glm::vec4{0, 0, 0, 1}}, character.position()),
          "drawing and collision use the same world position");
    check(near(glm::vec3{matrix * glm::vec4{0, 0, -1, 0}}, body),
          "drawing and body heading use the same rotation");
    check(near(character.eye_position(), character.position() + glm::vec3{0, 1.65F, 0}),
          "eye height stays relative to the resolved feet position");

    lc1::Character walker{world, {.position = world.spawn()}, {}};
    walker.set_look_angles(0, 89);
    walker.walk(world, walker.look_direction(), 0.1F, false);
    check(near(walker.position(),
               world.spawn() + glm::vec3{0, 0, -0.45F} * std::cos(glm::radians(89.0F))),
          "walk discards vertical direction and preserves analog magnitude");
    walker.walk(world, {0, 0, -1}, 0.1F, true);
    check(near(walker.position().y, 0), "running cannot make the ground character fly");

    lc1::Character configured{
        world, {.position = {0, 0, 0}}, {}, {.eye_height = 1.2F, .walk_speed = 2.0F}};
    configured.walk(world, {0, 0, -0.5F}, 0.1F, false);
    check(near(configured.position(), {0, 0, -0.1F}) && near(configured.eye_position().y, 1.2F),
          "each character has its own dimensions and movement settings");

    auto const before = character.position();
    rejects([&] { character.move(world, {0, 1, 0}); }, "ground move rejects vertical displacement");
    rejects([&] { character.walk(world, {1, 0, 0}, -1, false); }, "negative time is rejected");
    rejects([&] { character.set_look_angles(std::numeric_limits<float>::quiet_NaN(), 0); },
            "non-finite look input is rejected");
    check(near(character.position(), before), "invalid input leaves position unchanged");
    rejects([&] { lc1::Character blocked{world, {.position = {5, 0, 0}}, {}}; },
            "obstructed character spawn is rejected");
    rejects([&] { lc1::Character scaled{world, {.scale = {2, 2, 2}}, {}}; },
            "visual unit correction cannot silently scale a collision body");
    rejects([&] { lc1::Character invalid{world, {}, {}, {.height = 0}}; },
            "invalid body configuration is rejected");
}

void height_and_support()
{
    auto data = make_world_data();
    data.origin = {-32.0F, -32.0F};
    data.tile_size = 16.0F;
    data.tiles = std::vector(4, std::vector<lc1::TileData>(4));
    data.spawn = {0.0F, 0.0F, 0.0F};
    data.blocks.clear();
    data.height_samples.assign(5, std::vector<float>(5, 0.0F));
    for (std::size_t z = 0; z < data.height_samples.size(); ++z) {
        for (std::size_t x = 0; x < data.height_samples[z].size(); ++x) {
            float const world_x = data.origin.x + static_cast<float>(x) * data.tile_size;
            data.height_samples[z][x] = std::clamp((world_x - 0.0F) * 0.25F, 0.0F, 4.0F);
        }
    }
    lc1::Continent const world{std::move(data)};

    auto const slope = world.terrain_sample({8.0F, 0.0F});
    check(slope && near(slope->height, 2.0F), "terrain height is bilinearly interpolated");
    check(slope->normal.y < 0.999F && slope->normal.x < 0.0F,
          "terrain normal points uphill from the rising slope");

    auto const support = world.support_below({8.0F, 2.5F, 0.0F}, 1.0F);
    check(support && near(support->height, 2.0F), "support query finds the slope under the feet");
    check(!world.support_below({8.0F, 5.0F, 0.0F}, 1.0F),
          "support query does not snap to a surface farther below than requested");

    lc1::Character walker{world, {.position = {0.0F, 0.0F, 0.0F}}, {}};
    check(walker.on_ground() && near(walker.position().y, 0.0F),
          "character spawns on the sampled ground height");

    lc1::Character slightly_above{world, {.position = {0.0F, 0.2F, 0.0F}}, {}};
    check(slightly_above.on_ground() && near(slightly_above.position().y, 0.0F),
          "spawn within step range snaps down to the support surface");

    lc1::Character airborne{world, {.position = {0.0F, 5.0F, 0.0F}}, {}};
    check(!airborne.on_ground() && near(airborne.position().y, 5.0F),
          "spawning in the air is allowed and does not fail construction");
    for (int frame = 0; frame < 300; ++frame)
        airborne.update(world, 1.0F / 60.0F, {});
    check(airborne.on_ground() && near(airborne.position().y, 0.0F),
          "an airborne character falls and lands on the sampled ground");
    for (int frame = 0; frame < 30; ++frame) {
        walker.walk(world, {1.0F, 0.0F, 0.0F}, 0.1F, false);
        walker.update(world, 0.1F, {});
    }
    check(walker.position().y > 2.0F && walker.on_ground(),
          "grounded character follows a rising terrain surface");
    float const uphill_height = walker.position().y;

    for (int frame = 0; frame < 30; ++frame) {
        walker.walk(world, {-1.0F, 0.0F, 0.0F}, 0.1F, false);
        walker.update(world, 0.1F, {});
    }
    check(walker.position().y < uphill_height && walker.on_ground(),
          "grounded character follows a descending terrain surface");

    auto platform_data = make_world_data();
    platform_data.blocks.clear();
    platform_data.blocks.push_back(
        {{0.0F, 0.0F, -5.0F}, {10.0F, 3.0F, 5.0F}, lc1::ContinentSurface::Stone, true, true});
    lc1::Continent const platform_world{std::move(platform_data)};
    lc1::Character on_platform{platform_world, {.position = {5.0F, 3.0F, 0.0F}}, {}};
    check(on_platform.on_ground() && near(on_platform.position().y, 3.0F),
          "character can spawn on a walkable block top");
    auto const beneath = platform_world.support_below({5.0F, 3.0F, 0.0F}, 0.1F);
    check(beneath && near(beneath->height, 3.0F),
          "block tops participate in the same support query as terrain");
    on_platform.request_jump();
    on_platform.update(platform_world, 0.01F, {});
    check(!on_platform.on_ground() && on_platform.position().y > 3.0F,
          "jumping from a raised platform leaves the platform surface");
    for (int frame = 0; frame < 300; ++frame)
        on_platform.update(platform_world, 0.01F, {});
    check(on_platform.on_ground() && near(on_platform.position().y, 3.0F),
          "jumping on a raised platform lands back on the platform, not world Y=0");
}

void player_controllers()
{
    auto const world = make_world();
    lc1::Character character{world, {.position = world.spawn()}, {}};
    auto apply = [&](lc1::CharacterIntent const &intent) { character.update(world, 0.1F, intent); };
    apply(lc1::PlayerController::first_person(character, {0, 1}, {1800, -1200}));
    check(near(character.position(), world.spawn() + glm::vec3{0.45F, 0, 0}),
          "first-person movement follows mouse yaw but stays horizontal at high pitch");
    check(near(character.body_heading(), {1, 0, 0}) && near(character.look_pitch(), 60),
          "mouse right turns right and mouse up looks up");
    auto const facing = character.body_heading();
    apply(lc1::PlayerController::first_person(character, {0, -1}, {}));
    check(near(character.position(), world.spawn()) && near(character.body_heading(), facing),
          "walking backward does not turn the character around");
    apply(lc1::PlayerController::first_person(character, {1, 0}, {}));
    check(near(character.position(), world.spawn() + glm::vec3{0, 0, 0.45F}),
          "first-person strafe uses the horizontal right vector");
    auto const before = character.position();
    apply(lc1::PlayerController::third_person({0, 1}, {0, -2, -4}));
    check(near(character.position(), before + glm::vec3{0, 0, -0.45F}) &&
              near(character.body_heading(), {0, 0, -1}),
          "switching controllers moves the same character using the new camera basis");
    check(near(character.look_pitch(), 0), "third-person movement restores a level look target");
    auto const stopped = character.position();
    apply(lc1::PlayerController::first_person(character, {}, {}));
    apply(lc1::PlayerController::third_person({}, {0, 0, -1}));
    check(near(character.position(), stopped),
          "neutral input from the UI/focus gate does not move");
}

void intent_rules()
{
    auto const world = make_world();
    auto make_character = [&] { return lc1::Character{world, {.position = world.spawn()}, {}}; };

    // Character, not the controller, chooses the speed that belongs to a sprint request.
    auto sprinter = make_character();
    sprinter.update(world, 0.1F, {.move = {0, -1}, .sprint = true});
    check(near(sprinter.position(), world.spawn() + glm::vec3{0, 0, -1.8F}),
          "sprint intent uses the character's run speed");

    // The same jump request is accepted only by the grounded state rule in Character.
    auto jumper = make_character();
    auto control = make_character();
    jumper.update(world, 0.01F, {.jump = true});
    control.update(world, 0.01F, {.jump = true});
    check(!jumper.on_ground(), "grounded character accepts a jump intent");
    jumper.update(world, 0.01F, {.jump = true});
    control.update(world, 0.01F, {});
    check(near(jumper.position(), control.position()) && !jumper.on_ground(),
          "airborne character rejects a second jump intent");

    // Ambient dialog requests are a character rule, so players and AI share it.
    auto idle = make_character();
    check(idle.accepts_ambient_requests({}), "grounded idle character accepts ambient requests");
    check(!idle.accepts_ambient_requests({.move = {0, 1}}),
          "moving character defers ambient requests");
    check(!idle.accepts_ambient_requests({.sprint = true}),
          "sprinting character defers ambient requests");
    check(!idle.accepts_ambient_requests({.jump = true}),
          "jumping character defers ambient requests");
    idle.update(world, 0.01F, {.jump = true});
    check(!idle.accepts_ambient_requests({}), "airborne character defers ambient requests");

    auto intact = make_character();
    auto const before = intact.position();
    rejects(
        [&] { intact.update(world, 0.1F, {.move = {std::numeric_limits<float>::quiet_NaN(), 0}}); },
        "nonfinite intent is rejected");
    check(intact.position() == before, "invalid intent is rejected before mutating the character");
}

void controller_intent_boundary()
{
    auto const world = make_world();
    lc1::Character npc{world, {.position = world.spawn()}, {}};
    lc1::AiCharacterController controller{std::vector<lc1::Waypoint>{
        {.position = world.spawn()}, {.position = world.spawn() + glm::vec3{4, 0, 0}}}};
    auto const before = npc.position();
    auto const intent = controller.update(0.1F, npc);
    check(npc.position() == before, "an AI decision does not mutate the character");
    check(intent.move != glm::vec2{} && intent.face_movement,
          "the patrol asks Character to move and turn toward its target");
    npc.update(world, 0.1F, intent);
    check(npc.position().x > before.x, "Character applies the AI's movement intent");
}

std::vector<lc1::Vertex> triangle()
{
    return {
        {.position = {0, 0, 0}, .normal = {0, 0, 1}, .uv = {0, 0}},
        {.position = {1, 0, 0}, .normal = {0, 0, 1}, .uv = {1, 0}},
        {.position = {0, 1, 0}, .normal = {0, 0, 1}, .uv = {0, 1}},
    };
}

void model_correction()
{
    lc1::scene::Model plain{triangle(), {0, 1, 2}};
    plain.generate_tangents();
    check(lc1::valid_tangent_frame(plain.vertices()[0]), "model exposes tangent preprocessing");
    auto with_tangents = triangle();
    std::vector<std::uint32_t> tangent_indices{0, 1, 2};
    lc1::generate_tangents(with_tangents, tangent_indices);
    lc1::scene::Model const tangent_model{
        with_tangents, tangent_indices, {.rotation = {-90, 0, 0}, .scale = {-2, 3, 4}}};
    for (auto const &vertex : tangent_model.vertices()) {
        check(lc1::valid_tangent_frame(vertex), "baked tangent remains valid");
        check(near(glm::vec3{vertex.tangent}, {-1, 0, 0}) && near(vertex.tangent.w, -1),
              "baked reflection transforms tangent and handedness");
        check(near(vertex.normal, {0, 1, 0}), "baked normal uses the inverse transpose");
    }
    auto diagonal_uv = triangle();
    for (auto &vertex : diagonal_uv)
        vertex.uv = {vertex.position.x + vertex.position.y, vertex.position.y - vertex.position.x};
    lc1::generate_tangents(diagonal_uv, tangent_indices);
    lc1::scene::Model const diagonal_model{
        diagonal_uv, tangent_indices, {.rotation = {-90, 0, 0}, .scale = {-2, 3, 4}}};
    check(
        near(glm::vec3{diagonal_model.vertices()[0].tangent}, glm::normalize(glm::vec3{-2, 0, -3})),
        "non-axis-aligned tangents use the linear transform, not its inverse transpose");
    lc1::scene::Model const corrected{
        triangle(),
        {0, 1, 2},
        {.position = {1, 2, 3}, .rotation = {-90, 0, 0}, .scale = {2, 3, 4}}};
    check(near(corrected.vertices()[0].position, {1, 2, 3}) &&
              near(corrected.vertices()[1].position, {3, 2, 3}) &&
              near(corrected.vertices()[2].position, {1, 2, 0}),
          "model-level unit, axis and origin correction is baked into the shared vertices");
    check(near(corrected.vertices()[0].normal, {0, 1, 0}), "axis correction rotates normals");
    check(corrected.vertices()[1].uv == glm::vec2{1, 0}, "model correction preserves UVs");

    auto tilted = triangle();
    tilted[2].position = {0, 1, 1};
    for (auto &vertex : tilted)
        vertex.normal = glm::normalize(glm::vec3{0, -1, 1});
    lc1::scene::Model const mirrored{tilted, {0, 1, 2}, {.scale = {-2, 3, 4}}};
    auto const points = mirrored.vertices();
    auto const indices = mirrored.indices();
    auto const face_normal =
        glm::normalize(glm::cross(points[indices[1]].position - points[indices[0]].position,
                                  points[indices[2]].position - points[indices[0]].position));
    check(near(face_normal, points[0].normal),
          "nonuniform mirrored correction preserves winding and inverse-transpose normals");
    check(near(points[0].normal, {0, -0.8F, 0.6F}), "nonuniform scale keeps normals perpendicular");
    rejects([] { lc1::scene::Model invalid{triangle(), {0, 1, 2}, {.scale = {1, 0, 1}}}; },
            "singular model correction is rejected");
    rejects([] { lc1::scene::Model invalid{triangle(), {0, 1, 9}}; },
            "invalid model indices are rejected");
}

} // namespace

int main()
{
    try {
        blacksmith_forging();
        dialog_script_validation();
        recruitment_dialog();
        companion_orders();
        jumping();
        height_and_support();
        pose_and_movement();
        player_controllers();
        intent_rules();
        controller_intent_boundary();
        model_correction();
        std::cout << "character tests passed\n";
        return EXIT_SUCCESS;
    }
    catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
