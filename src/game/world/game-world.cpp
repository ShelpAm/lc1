#include "lc1/game/world/game-world.hpp"

#include "lc1/error.hpp"
#include "lc1/game/character/ai-character-controller.hpp"
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
    : GameWorld(save,
                WorldSetup{.continent = Continent::load(save.continent()),
                           .example_patrol = {},
                           .initial_player_resources = {}},
                dialog_catalog)
{
}

GameWorld::GameWorld(Save const &save, WorldSetup setup,
                     std::filesystem::path const &dialog_catalog)
    : continent_(std::move(setup.continent)), example_patrol_(std::move(setup.example_patrol)),
      initial_player_resources_(std::move(setup.initial_player_resources)), dialogs_(dialog_catalog)
{
    load(save);
}

void GameWorld::load(Save const &save)
{
    if (save.continent() != continent_.identity())
        fail("cannot load a different continent into an existing world");
    if (save.has_example() && !save.states().empty() && save.states().at(save.example()).patrol &&
        !example_patrol_)
        fail("saved patrol requires a scenario route");
    // Release participant and roster borrows before destroying their owners.
    dialogs_.reset();
    interaction_target_ = nullptr;
    player_ = nullptr;
    example_ = nullptr;
    dialog_participants_.clear();
    visible_characters_.clear();
    characters_.clear();
    resources_.clear();
    parties_.clear();
    weapons_.clear();
    equipments_.clear();
    factions_.clear();

    build_world(save);
    player_ = character_by_id(save.player(), "player");
    if (save.states().empty())
        for (auto const &stack : initial_player_resources_)
            if (!resources_.contains(stack.keyid) ||
                !player_->bag().add(resources_.at(stack.keyid), stack.quantity))
                fail("invalid initial player resource {}", stack.keyid);
    if (save.has_example()) {
        example_ = character_by_id(save.example(), "AI example");
        if (example_patrol_ && (save.states().empty() || save.states().at(save.example()).patrol)) {
            auto waypoints = example_patrol_->route;
            if (waypoints.empty())
                fail("example patrol needs a nonempty route");
            if (example_patrol_->resume_nearest) {
                // Resume from the closest authored point, retaining the controller's target
                // index 1.
                auto const nearest =
                    std::ranges::min_element(waypoints, {}, [&](Waypoint const &point) {
                        return glm::distance(point.position, example_->position());
                    });
                auto const index = static_cast<std::size_t>(nearest - waypoints.begin());
                std::rotate(waypoints.begin(),
                            waypoints.begin() + (index + waypoints.size() - 1) % waypoints.size(),
                            waypoints.end());
            }
            example_->set_controller(std::make_unique<AiCharacterController>(
                std::move(waypoints), example_patrol_->arrival_radius));
        }
    }

    for (auto const &[id, state] : save.states()) {
        if (state.companion_order != CompanionOrder::None) {
            auto *character = character_by_id(id, "companion order");
            auto *party = character->config().party;
            party->command(*party->leader(), *character, state.companion_order);
        }
    }

    for (auto const &[_, character] : characters_)
        if (character.get() != player_)
            character->set_interactable(
                std::make_unique<InteractableCharacter>(character.get(), &dialogs_));
}

void GameWorld::build_world(Save const &save)
{
    for (auto const &resource : game::resources)
        insert_unique(resources_, resource.keyid, resource, "resource");

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
        insert_unique(weapons_, record.keyid,
                      std::make_unique<Weapon>(
                          record.keyid, record.type,
                          WeaponProperty{.attack = record.attack, .range = record.range}),
                      "weapon");
    }

    // Parties precede characters because a character record names the party it belongs to.
    for (auto const &entry : save.parties()) {
        auto const &record = entry.second;
        insert_unique(parties_, record.keyid,
                      std::make_unique<Party>(record.keyid, factions_.at(record.faction).get()),
                      "party");
    }

    // Seed records use the prototype pose; checkpoints restore explicit character poses.
    scene::Transform const prototype{.position = continent_.spawn()};
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
        actual.position = continent_.spawn() + offset;
        // Place the randomized seed body on the terrain at
        // its XZ position so a nearby slope cannot leave it buried below the surface;
        // the Character constructor itself still permits an airborne spawn.
        if (auto const ground = continent_.ground_height({actual.position.x, actual.position.z}))
            actual.position.y = *ground;
        if (save.states().empty() && record.keyid == save.example() && example_patrol_ &&
            example_patrol_->spawn)
            actual.position = *example_patrol_->spawn;
        auto const state = save.states().find(record.keyid);
        if (state != save.states().end()) {
            auto const &pose = state->second;
            actual.position = {pose.x, pose.y, pose.z};
            actual.rotation.y = pose.body_yaw;
        }
        auto character = std::make_unique<Character>(continent_, actual, std::move(config));
        if (state != save.states().end()) {
            character->set_look_angles(state->second.look_yaw, state->second.look_pitch);
            character->bag() = game::Bag{state->second.bag_capacity};
            for (auto const &stack : state->second.bag)
                if (!character->bag().add(resources_.at(stack.keyid), stack.quantity))
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
    SerializedWorld world{.version = 4,
                          .continent = continent_.identity(),
                          .session = {.player = player_->config().keyid,
                                      .example = example_ ? example_->config().keyid : 0},
                          .factions = {},
                          .equipments = {},
                          .weapons = {},
                          .parties = {},
                          .characters = {},
                          .states = {}};
    for (auto const &[id, faction] : factions_)
        world.factions.push_back({.keyid = id, .name = faction->name()});
    for (auto const &[id, equipment] : equipments_)
        world.equipments.push_back(
            {.keyid = id, .type = equipment->type(), .armor = equipment->property().armor});
    for (auto const &[id, weapon] : weapons_)
        world.weapons.push_back({.keyid = id,
                                 .type = weapon->type(),
                                 .attack = weapon->property().attack,
                                 .range = weapon->property().range});
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

Character *GameWorld::find_interaction_target(glm::vec3 forward) const
{
    constexpr float min_alignment = 0.70710678F;

    Character *best = nullptr;
    float best_alignment = -1.0F;
    float best_distance_squared = 0.0F;

    for (auto const &[id, character] : characters_) {
        auto *candidate = character.get();

        if (candidate == player_)
            continue;

        auto const *interaction = candidate->interactable();

        if (!interaction || !interaction->can_interact(*player_))
            continue;

        auto const offset = candidate->position() - player_->position();

        // Use full distance to avoid preferring distant characters on other floors.
        float const distance_squared = glm::dot(offset, offset);

        // Facing is horizontal, independent of height difference.
        glm::vec3 const horizontal_offset{offset.x, 0.0F, offset.z};
        float const horizontal_distance_squared = glm::dot(horizontal_offset, horizontal_offset);

        if (horizontal_distance_squared < 1e-6F)
            continue;

        auto const direction = horizontal_offset / std::sqrt(horizontal_distance_squared);

        float const alignment = glm::dot(forward, direction);

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

void GameWorld::update(float delta_seconds, WorldInteraction const &interaction)
{
    if (!std::isfinite(delta_seconds) || delta_seconds < 0.0F)
        fail("invalid world update time");

    auto update_character = [&](Character &character) {
        CharacterIntent intent;
        if (&character == player_) {
            intent = interaction.player_intent;
        }
        else {
            auto const *dialog = dialogs_.find(character);
            if (dialog) {
                // Conversation pauses the controller but keeps gravity and collision active.
                auto const *partner =
                    dialog->talker() == &character ? dialog->target() : dialog->talker();
                intent.body_direction = partner->position() - character.position();
            }
            else if (auto *controller = character.controller()) {
                intent = controller->update(delta_seconds, character);
            }
        }
        character.update(delta_seconds, continent_, intent);
    };
    // Leaders move before followers, with stable keyid order inside each phase. The player
    // participates in the first phase, so its intent is applied before followers decide.
    for (auto *character : dialog_participants_)
        if (character->companion_order() != CompanionOrder::Follow)
            update_character(*character);
    for (auto *character : dialog_participants_)
        if (character->companion_order() == CompanionOrder::Follow)
            update_character(*character);

    // Interaction
    interaction_target_ = find_interaction_target(
        interaction.player_intent.interaction_direction.value_or(player_->body_heading()));
    if (auto *dialog = dialogs_.find(*player_)) {
        if (interaction.player_intent.interact)
            dialogs_.interrupt(*player_);
        else
            dialog->respond(*player_, interaction.response_index);
    }
    else if (interaction.player_intent.interact && interaction_target_) {
        interaction_target_->interactable()->interact(*player_);
    }
    dialogs_.update(delta_seconds, dialog_participants_, player_,
                    interaction.player_input_enabled &&
                        player_->accepts_ambient_requests(interaction.player_intent));
}

} // namespace lc1

namespace lc1 {

Continent const &GameWorld::continent() const
{
    return continent_;
}

Character &GameWorld::player()
{
    return *player_;
}

Character const &GameWorld::player() const
{
    return *player_;
}

std::vector<Character const *> const &GameWorld::characters() const
{
    return visible_characters_;
}

DialogSystem const &GameWorld::dialogs() const
{
    return dialogs_;
}

Character const *GameWorld::interaction_target() const
{
    return interaction_target_;
}

} // namespace lc1
