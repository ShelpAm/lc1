#include "lc1/game/world/game-world.hpp"

#include "lc1/error.hpp"
#include "lc1/game/character/ai-character-controller.hpp"
#include "lc1/game/debug/world-setup.hpp"
#include "lc1/game/party/party.hpp"
#include "lc1/game/resources/catalog.hpp"
#include "lc1/game/world/interactable.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <utility>

namespace lc1 {
namespace {
template <typename Map, typename Value>
void insert_unique(Map &map, std::uint32_t keyid, Value value, std::string_view kind)
{
    if (!map.try_emplace(keyid, std::move(value)).second)
        fail("duplicate {} keyid {}", kind, keyid);
}

} // namespace

GameWorld::GameWorld(Save const &save, std::filesystem::path const &dialog_catalog)
    : setup_(demo::world_setup(save.continent())), dialogs_(dialog_catalog)
{
    load(save);
}

void GameWorld::load(Save const &save)
{
    if (save.continent() != setup_.continent.identity())
        fail("cannot load a different continent into an existing world");
    if (save.has_example() && !save.states().empty() && save.states().at(save.example()).patrol &&
        !setup_.example_patrol)
        fail("saved patrol requires a scenario route");
    // Release participant and roster borrows before destroying their owners.
    dialogs_.reset();
    players_.clear();
    example_ = nullptr;
    dialog_participants_.clear();
    visible_characters_.clear();
    characters_.clear();
    parties_.clear();
    weapons_.clear();
    equipments_.clear();
    factions_.clear();

    build_world(save);
    std::vector<Character *> controlled;
    for (auto const &binding : save.players()) {
        auto *character = character_by_id(binding.character, "player binding");
        auto &player =
            players_.emplace(PlayerId{binding.keyid}, WorldPlayer{character, {}}).first->second;
        controlled.push_back(character);
        character->set_controller(std::make_unique<PlayerCharacterController>(player.controls));
        if (save.states().empty())
            for (auto const &stack : setup_.initial_player_resources) {
                auto const *resource = game::find_resource(stack.keyid);
                if (!resource || !character->bag().add(*resource, stack.quantity))
                    fail("invalid initial player resource {}", stack.keyid);
            }
    }
    if (save.has_example()) {
        example_ = character_by_id(save.example(), "AI example");
        if (setup_.example_patrol &&
            (save.states().empty() || save.states().at(save.example()).patrol)) {
            auto waypoints = setup_.example_patrol->route;
            if (waypoints.empty())
                fail("example patrol needs a nonempty route");
            if (setup_.example_patrol->resume_nearest) {
                // Resume from the closest authored point, retaining the controller's target
                // index 1.
                auto const nearest =
                    std::ranges::min_element(waypoints, {}, [&](Waypoint const &point) {
                        return glm::distance(point.position, example_->position());
                    });
                auto const index = static_cast<std::size_t>(nearest - waypoints.begin());
                std::ranges::rotate(waypoints, waypoints.begin() + (index + waypoints.size() - 1) %
                                                                       waypoints.size());
            }
            example_->set_controller(std::make_unique<AiCharacterController>(
                dialogs_, [this](CharacterId id) -> Character & { return character(id); },
                controlled, std::move(waypoints), setup_.example_patrol->arrival_radius));
        }
    }

    for (auto const &[_, character] : characters_) {
        if (!character->controller())
            character->set_controller(std::make_unique<AiCharacterController>(
                dialogs_, [this](CharacterId id) -> Character & { return this->character(id); }));
        character->set_interactable(
            std::make_unique<InteractableCharacter>(character.get(), &dialogs_));
    }
    for (auto const &[id, state] : save.states()) {
        if (state.companion_order != CompanionOrder::None) {
            auto *character = character_by_id(id, "companion order");
            auto *party = character->config().party;
            party->command(this->character(*party->leader()), *character, state.companion_order);
        }
    }
}

void GameWorld::build_world(Save const &save)
{
    // Save already proved every reference resolves, so the lookups below cannot miss: a
    // dangling keyid fails the load before this runs.
    for (auto const &entry : save.factions()) {
        auto const &record = entry.second;
        insert_unique(factions_, record.keyid, std::make_unique<Faction>(record.keyid, record.name),
                      "faction");
    }
    for (auto const &entry : save.equipments()) {
        auto const &record = entry.second;
        insert_unique(equipments_, record.keyid,
                      std::make_unique<Equipment>(record.keyid, record.type,
                                                  EquipmentProperty{.armor = record.armor}),
                      "equipment");
    }
    for (auto const &entry : save.weapons()) {
        auto const &record = entry.second;
        insert_unique(
            weapons_, record.keyid,
            std::make_unique<Weapon>(record.keyid, record.type,
                                     WeaponProperty{.attack = record.attack, .range = record.range},
                                     record.model.value()),
            "weapon");
    }

    // Parties precede characters because a character record names the party it belongs to.
    for (auto const &entry : save.parties()) {
        auto const &record = entry.second;
        insert_unique(parties_, record.keyid, std::make_unique<Party>(record.keyid, record.faction),
                      "party");
    }

    // Seed records use the prototype pose; checkpoints restore explicit character poses.
    scene::Transform const prototype{.position = setup_.continent.spawn()};
    for (auto const &entry : save.characters()) {
        auto const &record = entry.second;
        CharacterConfig config{
            .keyid = record.keyid,
            .name = record.name,
            .sex = record.sex,
            .age = record.age,
            .party = parties_.at(record.party).get(),
            .level = record.level,
            .equipment = {},
            .weapon = {},
            .model = record.model.value(),
        };
        for (auto const &slot : record.equipment)
            config.equipment[slot.first] = equipments_.at(slot.second).get();
        for (auto const &slot : record.weapon)
            config.weapon[slot.first] = weapons_.at(slot.second).get();
        auto actual = prototype;
        // Randomize in x,z [-5,5]
        std::uint32_t const seed = record.keyid * 0x9E3779B9U;
        std::mt19937_64 rng{seed};
        std::uniform_real_distribution<float> dist{-5.0F, 5.0F};
        auto const offset = glm::vec3{dist(rng), 0.0F, dist(rng)};
        actual.position = setup_.continent.spawn() + offset;
        // Place the randomized seed body on the terrain at
        // its XZ position so a nearby slope cannot leave it buried below the surface;
        // the Character constructor itself still permits an airborne spawn.
        if (auto const ground =
                setup_.continent.ground_height({actual.position.x, actual.position.z}))
            actual.position.y = *ground;
        if (save.states().empty() && record.keyid == save.example() && setup_.example_patrol &&
            setup_.example_patrol->spawn)
            actual.position = *setup_.example_patrol->spawn;
        auto const state = save.states().find(record.keyid);
        if (state != save.states().end()) {
            auto const &pose = state->second;
            actual.position = {pose.x, pose.y, pose.z};
            actual.rotation.y = pose.body_yaw;
        }
        auto character = std::make_unique<Character>(setup_.continent, actual, std::move(config));
        if (state != save.states().end()) {
            character->set_look_angles(state->second.look_yaw, state->second.look_pitch);
            character->bag() = game::Bag{state->second.bag_capacity};
            for (auto const &stack : state->second.bag)
                if (!character->bag().add(*game::find_resource(stack.keyid), stack.quantity))
                    fail("cannot restore character {} bag", record.keyid);
        }
        insert_unique(characters_, record.keyid, std::move(character), "character");
    }

    link_party_members(save);
    dialog_participants_.reserve(characters_.size());
    for (auto const &[_, character] : characters_)
        dialog_participants_.push_back(character.get());
    std::ranges::sort(dialog_participants_, {},
                      [](Character const *character) { return character->config().keyid; });
    visible_characters_.assign(dialog_participants_.begin(), dialog_participants_.end());
}

SerializedWorld GameWorld::snapshot() const
{
    SerializedWorld world{
        .version = 5,
        .continent = setup_.continent.identity(),
        .session = {.players = {}, .example = example_ ? example_->config().keyid : 0},
        .factions = {},
        .equipments = {},
        .weapons = {},
        .parties = {},
        .characters = {},
        .states = {}};
    for (auto const &[id, player] : players_)
        world.session.players.push_back({id.value, player.character->config().keyid});
    for (auto const &[id, faction] : factions_)
        world.factions.push_back({.keyid = id, .name = faction->name()});
    for (auto const &[id, equipment] : equipments_)
        world.equipments.push_back(
            {.keyid = id, .type = equipment->type(), .armor = equipment->property().armor});
    for (auto const &[id, weapon] : weapons_)
        world.weapons.push_back({.keyid = id,
                                 .type = weapon->type(),
                                 .attack = weapon->property().attack,
                                 .range = weapon->property().range,
                                 .model = weapon->model_config()});
    for (auto const &[_, party] : parties_)
        world.parties.push_back(serialize_party(*party));
    for (auto const &[_, character] : characters_) {
        world.characters.push_back(serialize_character(*character));
        world.states.push_back(serialize_character_state(*character));
    }
    return world;
}

void GameWorld::link_party_members(Save const &save)
{
    for (auto const &entry : save.parties()) {
        auto const &record = entry.second;
        Party *const party = parties_.at(record.keyid).get();
        std::vector<Character *> members;
        std::vector<Character *> captives;
        for (auto const member : record.members)
            members.push_back(character_by_id(member, "party member"));
        for (auto const captive : record.captives)
            captives.push_back(character_by_id(captive, "party captive"));
        party->restore_roster(record.leader == 0 ? nullptr
                                                 : character_by_id(record.leader, "party leader"),
                              std::move(members), std::move(captives));
    }
}

Character *GameWorld::character_by_id(std::uint32_t keyid, std::string_view what) const
{
    auto const character = characters_.find(keyid);
    if (character == characters_.end())
        fail("{} refers to unknown character {}", what, keyid);
    return character->second.get();
}

Character *GameWorld::find_interaction_target(Character &actor) const
{
    constexpr float min_alignment = 0.70710678F;

    Character *best = nullptr;
    float best_alignment = -1.0F;
    float best_distance_squared = 0.0F;

    for (auto const &[id, character] : characters_) {
        auto *candidate = character.get();

        if (candidate == &actor)
            continue;

        auto const *interaction = candidate->interactable();

        if (!interaction || !interaction->can_interact(actor))
            continue;

        auto const offset = candidate->position() - actor.position();

        // Use full distance to avoid preferring distant characters on other floors.
        float const distance_squared = glm::dot(offset, offset);

        // Facing is horizontal, independent of height difference.
        glm::vec3 const horizontal_offset{offset.x, 0.0F, offset.z};
        float const horizontal_distance_squared = glm::dot(horizontal_offset, horizontal_offset);

        if (horizontal_distance_squared < 1e-6F)
            continue;

        auto const direction = horizontal_offset / std::sqrt(horizontal_distance_squared);

        float const alignment = glm::dot(actor.body_heading(), direction);

        if (alignment < min_alignment)
            continue;

        bool const better =
            best == nullptr || alignment > best_alignment ||
            (alignment == best_alignment && distance_squared < best_distance_squared) ||
            (alignment == best_alignment && distance_squared == best_distance_squared &&
             id < best->config().keyid);

        if (better) {
            best = candidate;
            best_alignment = alignment;
            best_distance_squared = distance_squared;
        }
    }

    return best;
}

void GameWorld::update(float delta_seconds, std::map<PlayerId, PlayerControls> const &inputs)
{
    for (auto const &[id, input] : inputs)
        players_.at(id).controls = input;
    // Leaders move before followers, with stable keyid order inside each phase. The player
    // participates in the first phase, so its intent is applied before followers decide.
    for (auto *character : dialog_participants_)
        if (character->companion_order() != CompanionOrder::Follow)
            character->update(delta_seconds, setup_.continent);
    for (auto *character : dialog_participants_)
        if (character->companion_order() == CompanionOrder::Follow)
            character->update(delta_seconds, setup_.continent);

    // Resolve each player's actions in stable player-id order, then advance dialogue once.
    std::vector<DialogPlayer> dialog_players;
    dialog_players.reserve(players_.size());
    for (auto &[_, player] : players_) {
        auto &actor = *player.character;
        auto &input = player.controls;
        player.interaction_target = find_interaction_target(actor);
        if (auto *dialog = dialogs_.find(actor)) {
            if (input.interact)
                dialogs_.interrupt(actor);
            else
                dialog->respond(actor, input.response_index);
        }
        else if (input.interact && player.interaction_target) {
            player.interaction_target->interactable()->interact(actor);
        }
        dialog_players.push_back(
            {&actor, input.view.keyboard_enabled && actor.accepts_ambient_requests()});
        input.jump = false;
        input.swing_weapon = false;
        input.nod = false;
        input.interact = false;
        input.response_index = -1;
        input.view.look_delta = {};
        input.view.view_changed = false;
    }
    dialogs_.update(delta_seconds, dialog_participants_, dialog_players);
}

} // namespace lc1

namespace lc1 {

Continent const &GameWorld::continent() const
{
    return setup_.continent;
}

Character &GameWorld::player(PlayerId id)
{
    return *players_.at(id).character;
}

Character const &GameWorld::player(PlayerId id) const
{
    return *players_.at(id).character;
}

std::map<PlayerId, WorldPlayer> const &GameWorld::players() const
{
    return players_;
}

std::span<Character *const> GameWorld::characters()
{
    return dialog_participants_;
}

std::unordered_map<std::uint32_t, std::unique_ptr<Party>> &GameWorld::parties()
{
    return parties_;
}

std::unordered_map<std::uint32_t, std::unique_ptr<Party>> const &GameWorld::parties() const
{
    return parties_;
}

std::vector<Character const *> const &GameWorld::characters() const
{
    return visible_characters_;
}

DialogSystem const &GameWorld::dialogs() const
{
    return dialogs_;
}

Character const *GameWorld::interaction_target(PlayerId id) const
{
    return players_.at(id).interaction_target;
}

} // namespace lc1

namespace lc1 {
Character &GameWorld::character(CharacterId id)
{
    return *character_by_id(id.value, "character lookup");
}

Character const &GameWorld::character(CharacterId id) const
{
    return *character_by_id(id.value, "character lookup");
}

Faction const &GameWorld::faction(std::uint32_t id) const
{
    return *factions_.at(id);
}
} // namespace lc1
