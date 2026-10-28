#include "lc1/game/dialog/dialog.hpp"
#include "lc1/error.hpp"
#include "lc1/game/character/ai-character-controller.hpp"
#include "lc1/game/character/character.hpp"
#include "lc1/game/continent/continent.hpp"
#include "lc1/game/party/faction.hpp"
#include "lc1/game/party/party.hpp"
#include "lc1/game/world/interactable.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void check(bool value, std::string_view message)
{
    if (!value)
        throw std::runtime_error(std::string{message});
}

template <typename Function> void rejects(Function function, std::string_view message)
{
    try {
        function();
    }
    catch (lc1::Error const &) {
        return;
    }
    throw std::runtime_error(std::string{message});
}

lc1::Continent make_world()
{
    return lc1::Continent{{.origin = {-16.0F, -16.0F},
                           .tile_size = 16.0F,
                           .tiles = std::vector(2, std::vector<lc1::TileData>(2)),
                           .spawn = {0.0F, 0.0F, 0.0F},
                           .patches = {},
                           .blocks = {},
                           .regions = {{lc1::RegionId{1}, "Plain", lc1::RegionType::Plain}},
                           .locations = {},
                           .roads = {},
                           .height_samples = {}}};
}

lc1::CharacterConfig config(std::uint32_t id, std::string name, lc1::Party &party)
{
    return {.keyid = id,
            .name = std::move(name),
            .sex = false,
            .age = 18,
            .party = &party,
            .level = 1,
            .equipment = {},
            .weapon = {}};
}

struct World {
    lc1::Continent continent = make_world();
    lc1::Faction faction{1, "Faction"};
    lc1::Party party{1, &faction};
    lc1::Party other{2, &faction};
    lc1::Character player{continent, {.position = continent.spawn()}, config(1, "Player", party)};
    lc1::Character captive{continent, {.position = continent.spawn()}, config(2, "Captive", party)};
    lc1::Character leader{continent, {.position = continent.spawn()}, config(3, "Leader", other)};
    lc1::Character worker{continent, {.position = continent.spawn()}, config(4, "Worker", other)};
    lc1::Character passerby{
        continent, {.position = continent.spawn()}, config(5, "Passerby", other)};

    World()
    {
        party.set_leader(&player);
        party.add_captive(&captive);
        other.set_leader(&leader);
        other.add_member(&worker);
        other.add_member(&passerby);
    }

    auto participants()
    {
        // Deliberately unordered: the scheduler must make stable decisions.
        return std::array{&worker, &player, &passerby, &leader, &captive};
    }
};

void tick(lc1::DialogSystem &system, World const &world, float seconds = 1.5F)
{
    // Empty scheduling roster advances existing sessions without initiating new ones.
    system.update(seconds, {}, &world.player, false);
}

void rules_and_lifecycle(std::filesystem::path const &path)
{
    World world;
    lc1::DialogCatalog catalog{path};
    check(catalog.select(world.player, world.captive, lc1::DialogTrigger::Interaction) == "recruit",
          "recruitment takes precedence over same-party dialogue");
    check(catalog.select(world.player, world.leader, lc1::DialogTrigger::Interaction) == "greeting",
          "an unrelated leader uses the fallback");
    rejects([&] { catalog.script("missing"); }, "unknown script ids fail");

    lc1::DialogSystem system{path};
    lc1::InteractableCharacter interaction{&world.captive, &system};
    check(interaction.can_interact(world.player),
          "interactive characters query shared availability");
    interaction.interact(world.player);
    auto *dialog = system.find(world.player);
    check(dialog && dialog == system.find(world.captive) &&
              system.dialog_id(world.player) == "recruit",
          "both participants share the selected session");
    check(!system.start(world.player, world.leader) && !system.start(world.worker, world.captive) &&
              !system.start(world.captive, world.player) &&
              !system.start(world.worker, world.worker),
          "busy talkers, busy targets and self conversations are rejected");
    check(system.start(world.leader, world.worker), "disjoint pairs can talk concurrently");
    world.party.recruit(world.player, world.captive);
    check(catalog.select(world.player, world.captive, lc1::DialogTrigger::Interaction) ==
                  "companion" &&
              system.dialog_id(world.player) == "recruit",
          "state changes affect the next entry without replacing an active conversation");
    dialog->respond(world.player, 0);
    check(!dialog->displaying_response() && dialog->response_id(0) == "goodbye" &&
              dialog->current_node_id() == "greeting",
          "a stale invitation refreshes without shifting the selection to goodbye");
    check(system.interrupt(world.captive) && !system.find(world.player) &&
              system.find(world.worker),
          "either participant can interrupt without affecting a different pair");
    check(system.start(world.player, world.captive) &&
              system.dialog_id(world.player) == "companion",
          "manual retries ignore ambient cooldown and reselect from current state");
    check(system.interrupt(world.player) && !system.interrupt(world.player),
          "interruption is safe to repeat");
    world.leader.move(world.continent, {6.0F, 0.0F, 0.0F});
    tick(system, world, 0.0F);
    check(!system.find(world.worker), "separation ends NPC conversations too");
    check(!system.can_start(world.player, world.leader), "starting range is enforced");
}

void npc_initiates_and_player_decides(std::filesystem::path const &path)
{
    World world;
    lc1::DialogSystem system{path};
    check(!system.start(world.captive, world.player, lc1::DialogTrigger::Nearby),
          "ambient conversations observe the initial quiet period");
    tick(system, world, 8.0F);
    check(system.start(world.captive, world.player, lc1::DialogTrigger::Nearby),
          "NPCs can initiate a recruitment request to the player");
    auto *dialog = system.find(world.player);
    check(system.dialog_id(world.player) == "request_recruitment" &&
              dialog->speaker() == &world.captive && dialog->respondent() == &world.player &&
              dialog->current_speech_text() == "Player, may I join your party?",
          "roles describe actual speakers and decision makers, not who initiated");
    tick(system, world, 10.0F);
    check(!dialog->displaying_response() && world.party.members().empty(),
          "NPC automation never answers on behalf of the player");
    dialog->respond(world.captive, 0);
    dialog->respond(world.leader, 0);
    check(world.party.members().empty(), "other participants cannot submit the player's response");
    dialog->respond(world.player, 1);
    check(dialog->speaker() == &world.player && !dialog->respondent() &&
              dialog->current_speech_text() == "Not now." && dialog->candidate_responses().empty(),
          "a refusal is displayed as the player's speech before closing");
    tick(system, world);
    check(!system.find(world.player) && world.party.captives().size() == 1 &&
              !system.can_start(world.captive, world.player, lc1::DialogTrigger::Nearby),
          "refusal preserves membership and imposes an ambient cooldown");
    tick(system, world, 29.0F);
    check(!system.can_start(world.captive, world.player, lc1::DialogTrigger::Nearby),
          "ambient cooldown lasts its configured duration");
    tick(system, world, 1.0F);
    check(system.start(world.captive, world.player, lc1::DialogTrigger::Nearby),
          "ambient requests become eligible after the cooldown");
    dialog = system.find(world.player);
    dialog->respond(world.player, 0);
    check(world.party.members().size() == 1 && world.party.captives().empty() &&
              world.captive.config().party == &world.party && dialog->displaying_response(),
          "explicit Target recruiter roles let the player recruit an initiating NPC");
    dialog->respond(world.player, 0);
    check(world.party.members().size() == 1,
          "repeated input cannot repeat an action during a reply");
    tick(system, world);
    check(dialog->current_node_id() == "joined" && dialog->speaker() == &world.captive,
          "the initiating NPC speaks the acknowledgement");
    dialog->respond(world.player, 0);
    tick(system, world);
    check(!system.find(world.player), "the final response releases both participants");
}

void npc_policy_and_limits(std::filesystem::path const &path)
{
    World world;
    lc1::DialogSystem system{path};
    check(system.start(world.leader, world.worker),
          "NPC-only conversations can use interaction rules");
    auto *dialog = system.find(world.leader);
    check(dialog->speaker() == &world.worker && dialog->respondent() == &world.leader,
          "node speaker and respondent are independent");
    tick(system, world, 0.0F);
    check(!dialog->displaying_response(),
          "NPC replies wait instead of skipping the displayed line");
    tick(system, world);
    check(dialog->displaying_response() && dialog->speaker() == &world.leader &&
              dialog->current_speech_text() == "How are you doing?",
          "NPCs choose the highest-priority available response");
    tick(system, world);
    check(dialog->current_node_id() == "reply" && dialog->speaker() == &world.worker,
          "NPC answers advance to the next authored turn");
    tick(system, world);
    tick(system, world);
    check(!system.find(world.leader) && !system.find(world.worker),
          "NPC-only dialogue completes without player input");

    check(system.start(world.player, world.captive),
          "the player can start while other NPCs cool down");
    tick(system, world, 90.0F);
    check(!system.find(world.player), "unanswered or looping sessions have a bounded lifetime");
    rejects([&] { tick(system, world, -1.0F); }, "negative time is rejected");
    rejects([&] { tick(system, world, std::numeric_limits<float>::quiet_NaN()); },
            "non-finite time is rejected");
}

void ambient_scheduling_and_patrol(std::filesystem::path const &path)
{
    World world;
    lc1::DialogSystem system{path};
    auto const participants = world.participants();
    system.update(7.0F, participants, &world.player, false);
    check(!system.find(world.captive), "NPCs do not initiate immediately on load");
    system.update(1.0F, participants, &world.player, false);
    check(!system.find(world.player) && system.find(world.captive) == system.find(world.leader) &&
              system.find(world.worker) == system.find(world.passerby) &&
              system.find(world.worker) != system.find(world.captive),
          "deferred player requests allow stable disjoint NPC pairs in an unordered roster");
    auto const *first = system.find(world.captive);
    check(first->talker() == &world.captive && first->target() == &world.leader,
          "equal-distance scheduling uses character ids as a stable tie break");

    world.captive.set_controller(
        std::make_unique<lc1::AiCharacterController>(std::vector<lc1::Waypoint>{
            {.position = world.continent.spawn()}, {.position = {4.0F, 0.0F, 0.0F}}}));
    auto const original = world.captive.position();
    auto step = [&](bool controller_enabled) {
        auto intent = lc1::CharacterIntent{};
        if (controller_enabled && world.captive.controller())
            intent = world.captive.controller()->update(0.1F, world.captive);
        world.captive.update(0.1F, world.continent, intent);
    };
    step(!system.find(world.captive));
    check(world.captive.position() == original && world.captive.controller(),
          "conversation pauses patrol without deleting its controller");
    system.interrupt(world.captive);
    step(!system.find(world.captive));
    check(world.captive.position().x > original.x && world.captive.controller(),
          "interruption resumes the same patrol controller");
    world.captive.request_jump();
    step(false);
    check(!world.captive.on_ground(), "paused controllers do not pause physical simulation");

    World another;
    lc1::DialogSystem requests{path};
    auto const roster = another.participants();
    requests.update(8.0F, roster, &another.player, true);
    auto const *request = requests.find(another.player);
    check(request && request->talker() == &another.captive &&
              request->respondent() == &another.player,
          "idle players can receive a nearby NPC's request");
    check(requests.find(another.worker) && requests.find(another.leader),
          "NPC dialogue continues alongside the player's conversation");
}

void nearby_search_boundaries(std::filesystem::path const &path)
{
    World world;
    // Lower id wins an equal-distance tie even when X sorting visits it last.
    world.captive.move(world.continent, {1, 0, 0});
    world.leader.move(world.continent, {-1, 0, 0});
    world.worker.move(world.continent, {0, 0, 8});
    world.passerby.move(world.continent, {0, 0, -8});
    lc1::DialogSystem system{path};
    auto const roster = world.participants();
    system.update(8.0F, roster, nullptr, true);
    auto const *dialog = system.find(world.player);
    check(dialog && dialog->target() == &world.captive,
          "equal-distance targets use keyid rather than X traversal order");
    system.clear();
    check(!system.find(world.player) && system.dialog_id(world.captive).empty(),
          "clear removes participant indices");
    world.captive.move(world.continent, {3, 0, 0});
    world.leader.move(world.continent, {-7, 0, 0});
    system.update(0.0F, roster, nullptr, true);
    check(system.find(world.player) && system.find(world.player)->target() == &world.captive,
          "targets exactly on the positive X radius boundary are included");
    system.interrupt(world.player);
    check(!system.find(world.player) && !system.find(world.captive),
          "interrupt removes both participant indices");
    check(system.start(world.player, world.captive),
          "manual interaction remains immediate after interruption");
    system.find(world.player)->clear();
    check(!system.find(world.captive) && system.dialog_id(world.player).empty(),
          "directly cleared borrowed conversations are no longer visible");
    check(system.start(world.player, world.captive),
          "a directly cleared conversation can be replaced before the next update");
    tick(system, world, 90.0F);
    check(!system.find(world.player) && system.dialog_id(world.captive).empty(),
          "timeout removes participant indices");
    system.clear();
    world.captive.move(world.continent, {-8, 0, 0});
    system.update(0.0F, roster, nullptr, true);
    check(system.find(world.player) && system.find(world.player)->target() == &world.captive,
          "targets exactly on the negative X radius boundary are included");
}

void invalid_catalogs(std::filesystem::path const &path)
{
    struct TemporaryDirectory {
        std::filesystem::path path =
            std::filesystem::temp_directory_path() /
            ("lc1-dialog-catalog-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

        TemporaryDirectory() { std::filesystem::create_directories(path); }

        ~TemporaryDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } directory;

    std::filesystem::copy(path.parent_path(), directory.path,
                          std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::overwrite_existing);
    auto input = std::ifstream{path};
    std::string const original{std::istreambuf_iterator<char>{input},
                               std::istreambuf_iterator<char>{}};
    auto replaced = [&](std::string_view from, std::string_view to) {
        auto text = original;
        auto const pos = text.find(from);
        check(pos != std::string::npos, "catalog test replacement exists");
        text.replace(pos, from.size(), to);
        return text;
    };
    auto malformed = [&](std::string const &text, std::string_view message) {
        {
            auto output = std::ofstream{directory.path / "catalog.yaml"};
            output << text;
        }
        rejects([&] { lc1::DialogCatalog catalog{directory.path / "catalog.yaml"}; }, message);
    };
    malformed(replaced("version: 1", "version: 2"), "unsupported catalog versions fail");
    malformed(replaced("file: recruit.yaml", "file: ../recruit.yaml"), "lexical file escapes fail");
    malformed(replaced("file: recruit.yaml", "file: /tmp/recruit.yaml"),
              "absolute script paths fail");
    malformed(replaced("id: greeting", "id: recruit"), "duplicate script ids fail");
    malformed(replaced("dialog: recruit", "dialog: missing"), "dangling rule references fail");
    malformed(replaced("conditions: [CanRecruit]", "conditions: []"), "empty rule conditions fail");
    malformed(replaced("trigger: Interaction", "trigger: Unknown"), "unknown triggers fail");
    malformed(replaced("line_seconds: 1.5", "line_seconds: 0"), "zero line duration fails");
    malformed(replaced("cooldown_seconds: 30.0", "cooldown_seconds: -1"),
              "negative cooldown fails");
    malformed(replaced("end_distance: 5.0", "end_distance: 1.0"), "inverted distance limits fail");
    malformed(original + "typo: true\n", "unknown catalog fields fail");

    // Participant filters and AND conditions use the same resolver as default content.
    auto custom = replaced("talker: 0, target: 0, conditions: [CanRecruit]",
                           "talker: 1, target: 2, conditions: [SameParty, CanRecruit]");
    {
        auto output = std::ofstream{directory.path / "catalog.yaml"};
        output << custom;
    }
    lc1::DialogCatalog catalog{directory.path / "catalog.yaml"};
    World world;
    check(catalog.select(world.player, world.captive, lc1::DialogTrigger::Interaction) == "recruit",
          "character filters can specialize a rule");
    check(catalog.select(world.leader, world.worker, lc1::DialogTrigger::Interaction) ==
              "companion",
          "nonmatching character filters fall through to later rules");
    check(catalog.select(world.player, world.worker, lc1::DialogTrigger::Interaction) == "greeting",
          "a true recruit condition alone cannot override mismatching participant filters");
    world.party.recruit(world.player, world.captive);
    check(catalog.select(world.player, world.captive, lc1::DialogTrigger::Interaction) ==
              "companion",
          "all conditions must still hold after a state change");
}

void benchmark(std::filesystem::path const &path)
{
    auto continent =
        lc1::Continent{{.origin = {-4096.0F, -4096.0F},
                        .tile_size = 4096.0F,
                        .tiles = std::vector(2, std::vector<lc1::TileData>(2)),
                        .spawn = {0.0F, 0.0F, 0.0F},
                        .patches = {},
                        .blocks = {},
                        .regions = {{lc1::RegionId{1}, "Plain", lc1::RegionType::Plain}},
                        .locations = {},
                        .roads = {},
                        .height_samples = {}}};
    lc1::Faction faction{1, "Faction"};
    lc1::Party party{1, &faction};
    std::vector<std::unique_ptr<lc1::Character>> characters;
    std::vector<lc1::Character *> roster;
    for (std::uint32_t i = 0; i < 512; ++i) {
        characters.push_back(std::make_unique<lc1::Character>(
            continent,
            lc1::scene::Transform{.position = {static_cast<float>(i % 32) * 10.0F, 0.0F,
                                               static_cast<float>(i / 32) * 10.0F}},
            config(i + 1, "NPC", party)));
        roster.push_back(characters.back().get());
    }
    lc1::DialogSystem system{path};
    system.update(8.0F, {}, nullptr, false);
    auto const start = std::chrono::steady_clock::now();
    for (int i = 0; i < 120; ++i)
        system.update(1.0F / 60.0F, roster, nullptr, false);
    auto const seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << "512 separated NPCs, 120 updates: " << seconds * 1000.0 << " ms total, "
              << seconds * 1000.0 / 120.0 << " ms/update\n";
}

} // namespace

int main(int argc, char **argv)
{
    try {
        if (argc != 2 && argc != 3)
            throw std::runtime_error("usage: lc1_dialog_tests <catalog> [--validate]");
        auto const path = std::filesystem::path{argv[1]};
        if (argc == 3) {
            if (std::string_view{argv[2]} == "--benchmark") {
                benchmark(path);
                return EXIT_SUCCESS;
            }
            check(std::string_view{argv[2]} == "--validate", "unknown argument");
            lc1::DialogCatalog catalog{path};
        }
        else {
            rules_and_lifecycle(path);
            npc_initiates_and_player_decides(path);
            npc_policy_and_limits(path);
            ambient_scheduling_and_patrol(path);
            nearby_search_boundaries(path);
            invalid_catalogs(path);
        }
        std::cout << "dialog tests passed\n";
        return EXIT_SUCCESS;
    }
    catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
