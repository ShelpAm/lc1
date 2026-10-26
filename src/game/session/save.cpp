#include "lc1/game/session/save.hpp"

#include "lc1/error.hpp"
#include "lc1/game/character/character.hpp"
#include "lc1/game/continent/continent.hpp"
#include "lc1/game/party/faction.hpp"
#include "lc1/game/party/party.hpp"

#include <rfl/yaml.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <spdlog/spdlog.h>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace lc1 {
namespace {

// A save file is a sequence of records. A missing or malformed file is fatal: a partial
// world would silently drop entities and leave references dangling.
template <typename Record>
std::vector<Record> read_save_file_sequence(std::filesystem::path const &directory,
                                            std::string_view name)
{
    auto const path = directory / name;
    auto stream = std::ifstream{path};
    if (!stream)
        fail("cannot open save file {}", path.string());
    auto records = rfl::yaml::read<std::vector<Record>, rfl::NoOptionals>(stream);
    if (!records)
        fail("cannot read save file {}: {}", path.string(), records.error().what());
    return *records;
}

template <typename Record>
Record read_save_file_single(std::filesystem::path const &directory, std::string_view name)
{
    auto const path = directory / name;
    auto stream = std::ifstream{path};
    if (!stream)
        fail("cannot open save file {}", path.string());
    auto records = rfl::yaml::read<Record, rfl::NoOptionals>(stream);
    if (!records)
        fail("cannot read save file {}: {}", path.string(), records.error().what());
    return *records;
}

// Version 1 predates companion commands. Keep its exact shape so all old required
// fields are still checked rather than accepting missing fields in version 2.
struct CheckpointHeader {
    std::uint32_t version;
};

struct LegacyCharacterState {
    std::uint32_t keyid;
    float x;
    float y;
    float z;
    float body_yaw;
    float look_yaw;
    float look_pitch;
    bool patrol;
};

template <typename State> struct LegacyWorldData {
    std::uint32_t version;
    SerializedSession session;
    std::vector<SerializedFaction> factions;
    std::vector<SerializedEquipment> equipments;
    std::vector<SerializedWeapon> weapons;
    std::vector<SerializedParty> parties;
    std::vector<SerializedCharacterConfig> characters;
    std::vector<State> states;
};

SerializedWorld read_checkpoint(std::filesystem::path const &directory)
{
    auto const version =
        read_save_file_single<CheckpointHeader>(directory, "checkpoint.yaml").version;
    if (version == 3)
        return read_save_file_single<SerializedWorld>(directory, "checkpoint.yaml");
    if (version == 2) {
        auto legacy = read_save_file_single<LegacyWorldData<SerializedCharacterState>>(
            directory, "checkpoint.yaml");
        return {.version = 3,
                .continent = {"present", 1},
                .session = legacy.session,
                .factions = std::move(legacy.factions),
                .equipments = std::move(legacy.equipments),
                .weapons = std::move(legacy.weapons),
                .parties = std::move(legacy.parties),
                .characters = std::move(legacy.characters),
                .states = std::move(legacy.states)};
    }
    if (version != 1)
        fail("unsupported checkpoint version {}", version);
    auto legacy =
        read_save_file_single<LegacyWorldData<LegacyCharacterState>>(directory, "checkpoint.yaml");
    SerializedWorld world{.version = 3,
                          .session = legacy.session,
                          .factions = std::move(legacy.factions),
                          .equipments = std::move(legacy.equipments),
                          .weapons = std::move(legacy.weapons),
                          .parties = std::move(legacy.parties),
                          .characters = std::move(legacy.characters),
                          .states = {}};
    for (auto const &state : legacy.states)
        world.states.push_back({.keyid = state.keyid,
                                .x = state.x,
                                .y = state.y,
                                .z = state.z,
                                .body_yaw = state.body_yaw,
                                .look_yaw = state.look_yaw,
                                .look_pitch = state.look_pitch,
                                .patrol = state.patrol,
                                .companion_order = CompanionOrder::None});
    return world;
}

// A duplicate keyid means two records claim the same identity; keeping either one
// silently would make every reference ambiguous.
template <typename Map> void insert_unique(Map &map, auto const &record, std::string_view kind)
{
    if (record.keyid == 0)
        fail("{} keyid must be nonzero", kind);
    if (!map.try_emplace(record.keyid, record).second)
        fail("duplicate {} keyid {}", kind, record.keyid);
}

template <typename Map> auto sorted_records(Map const &map)
{
    using Record = typename Map::mapped_type;
    std::vector<Record> records;
    records.reserve(map.size());
    for (auto const &[_, record] : map)
        records.push_back(record);
    std::ranges::sort(records, {}, &Record::keyid);
    return records;
}

std::string path_utf8(std::filesystem::path const &path)
{
    auto const utf8 = path.u8string();
    return {utf8.begin(), utf8.end()};
}

} // namespace

std::filesystem::path Save::slot_directory(std::filesystem::path const &root, std::string_view name)
{
    if (name.empty() || name == "." || name == ".." || name.size() > 128 || name.front() == ' ' ||
        name.back() == ' ' || name.back() == '.' ||
        name.find_first_of("/\\:<>\"|?*") != std::string_view::npos ||
        std::ranges::any_of(name, [](unsigned char c) { return c < 32 || c == 127; }))
        fail("invalid save name: use 1-128 bytes without path separators or reserved characters");
    return root / std::filesystem::path{std::u8string{name.begin(), name.end()}};
}

std::vector<std::string> Save::slots(std::filesystem::path const &root)
{
    std::vector<std::string> result;
    if (!std::filesystem::exists(root))
        return result;
    for (auto const &entry : std::filesystem::directory_iterator(root)) {
        if (!entry.is_directory())
            continue;
        auto const &path = entry.path();
        bool const seed = std::ranges::all_of(
            std::array{"factions", "equipments", "weapons", "parties", "characters", "session",
                       "continent"},
            [&](char const *name) {
                return std::filesystem::is_regular_file(path / (std::string{name} + ".yaml"));
            });
        if (std::filesystem::is_regular_file(path / "checkpoint.yaml") || seed) {
            auto const utf8 = path.filename().u8string();
            std::string name(utf8.begin(), utf8.end());
            // Exclude directories that cannot be selected through the naming interface.
            try {
                slot_directory(root, name);
                result.push_back(std::move(name));
            }
            catch (Error const &) {
            }
        }
    }
    std::ranges::sort(result);
    return result;
}

Save::Save(std::filesystem::path directory) : directory_{std::move(directory)}, session_{}
{
    if (std::filesystem::exists(directory_ / "checkpoint.yaml")) {
        read_world(read_checkpoint(directory_));
        validate_references();
        return;
    }
    read_factions();
    read_equipments();
    read_weapons();
    read_parties();
    read_characters();
    read_session();
    continent_ = read_save_file_single<ContinentIdentity>(directory_, "continent.yaml");
    validate_references();
}

Save::Save(std::filesystem::path directory, SerializedWorld const &world)
    : directory_{std::move(directory)}, session_{}
{
    read_world(world);
    validate_references();
}

void Save::read_world(SerializedWorld const &world)
{
    if (world.version != 3)
        fail("unsupported checkpoint version {}", world.version);
    session_ = world.session;
    continent_ = world.continent;
    for (auto const &record : world.factions)
        insert_unique(factions_, record, "faction");
    for (auto const &record : world.equipments)
        insert_unique(equipments_, record, "equipment");
    for (auto const &record : world.weapons)
        insert_unique(weapons_, record, "weapon");
    for (auto const &record : world.parties)
        insert_unique(parties_, record, "party");
    for (auto const &record : world.characters)
        insert_unique(characters_, record, "character");
    for (auto const &record : world.states)
        insert_unique(states_, record, "character state");
    if (states_.size() != characters_.size())
        fail("checkpoint must contain one state per character");
}

SerializedWorld Save::data() const
{
    return {.version = 3,
            .continent = continent_,
            .session = session_,
            .factions = sorted_records(factions_),
            .equipments = sorted_records(equipments_),
            .weapons = sorted_records(weapons_),
            .parties = sorted_records(parties_),
            .characters = sorted_records(characters_),
            .states = sorted_records(states_)};
}

void Save::write(std::filesystem::path const &directory, SerializedWorld const &world)
{
    Save const checked{directory, world};
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error)
        fail("cannot create save directory {}: {}", path_utf8(directory), error.message());
    auto const staging = directory / "checkpoint.yaml.tmp";
    auto stream = std::ofstream{staging, std::ios::binary | std::ios::trunc};
    if (!stream)
        fail("cannot open checkpoint staging file {}", path_utf8(staging));
    rfl::yaml::write(checked.data(), stream);
    stream.close();
    if (!stream)
        fail("cannot write checkpoint staging file {}", path_utf8(staging));
    std::filesystem::rename(staging, directory / "checkpoint.yaml", error);
    if (error)
        fail("cannot commit checkpoint in {}: {}", path_utf8(directory), error.message());
    spdlog::info("[save] checkpoint saved to {}", path_utf8(directory));
}

SerializedCharacterConfig serialize_character(Character const &character)
{
    auto const &config = character.config();
    SerializedCharacterConfig record{.keyid = config.keyid,
                                     .name = config.name,
                                     .sex = config.sex,
                                     .age = config.age,
                                     .party = config.party->keyid(),
                                     .level = config.level,
                                     .equipment = {},
                                     .weapon = {}};
    for (auto const &[slot, equipment] : config.equipment)
        record.equipment.emplace(slot, equipment->keyid());
    for (auto const &[slot, weapon] : config.weapon)
        record.weapon.emplace(slot, weapon->keyid());
    return record;
}

SerializedCharacterState serialize_character_state(Character const &character)
{
    auto const position = character.position();
    auto const order = character.companion_order();
    return {.keyid = character.config().keyid,
            .x = position.x,
            .y = position.y,
            .z = position.z,
            .body_yaw = character.body_yaw(),
            .look_yaw = character.look_yaw(),
            .look_pitch = character.look_pitch(),
            // A companion controller is a party order, not the patrol example. The order
            // itself is serialized separately and restored by Party::command.
            .patrol = order == CompanionOrder::None && character.controller() != nullptr,
            .companion_order = order};
}

SerializedParty serialize_party(Party const &party)
{
    SerializedParty record{.keyid = party.keyid(),
                           .faction = party.faction()->keyid(),
                           .leader = party.leader() ? party.leader()->config().keyid : 0,
                           .members = {},
                           .captives = {}};
    for (auto const *member : party.members())
        record.members.push_back(member->config().keyid);
    for (auto const *captive : party.captives())
        record.captives.push_back(captive->config().keyid);
    return record;
}

void Save::read_factions()
{
    for (auto const &record :
         read_save_file_sequence<SerializedFaction>(directory_, "factions.yaml")) {
        insert_unique(factions_, record, "faction");
        spdlog::info("[save] {}: faction {} {} loaded", directory_.string(), record.keyid,
                     record.name);
    }
}

void Save::read_equipments()
{
    for (auto const &record :
         read_save_file_sequence<SerializedEquipment>(directory_, "equipments.yaml")) {
        insert_unique(equipments_, record, "equipment");
        spdlog::info("[save] {}: equipment {} {} loaded", directory_.string(), record.keyid,
                     to_string(record.type));
    }
}

void Save::read_weapons()
{
    for (auto const &record :
         read_save_file_sequence<SerializedWeapon>(directory_, "weapons.yaml")) {
        insert_unique(weapons_, record, "weapon");
        spdlog::info("[save] {}: weapon {} {} loaded", directory_.string(), record.keyid,
                     to_string(record.type));
    }
}

void Save::read_parties()
{
    for (auto const &record :
         read_save_file_sequence<SerializedParty>(directory_, "parties.yaml")) {
        insert_unique(parties_, record, "party");
        spdlog::info("[save] {}: party {} of faction {} loaded", directory_.string(), record.keyid,
                     record.faction);
    }
}

void Save::read_characters()
{
    for (auto const &record :
         read_save_file_sequence<SerializedCharacterConfig>(directory_, "characters.yaml")) {
        insert_unique(characters_, record, "character");
        spdlog::info("[save] {}: character {} {} loaded", directory_.string(), record.keyid,
                     record.name);
    }
}

void Save::read_session()
{
    session_ = read_save_file_single<SerializedSession>(directory_, "session.yaml");
    if (session_.player == 0)
        fail("session.yaml must name the player character");
    spdlog::info("[save] {}: session player {} example {} loaded", directory_.string(),
                 session_.player, session_.example);
}

void Save::validate_references() const
{
    Continent::validate_identity(continent_);
    if (session_.player == 0)
        fail("session.yaml must name the player character");
    // The session goes first: a save that cannot say who the player is has no business
    // building a world, and the zero sentinel would otherwise report character 0.
    character(session_.player, "session player");
    if (session_.example != 0) {
        if (session_.example == session_.player)
            fail("session example {} is also the player", session_.example);
        character(session_.example, "session example");
    }
    for (auto const &entry : parties_) {
        auto const &party = entry.second;
        if (!factions_.contains(party.faction))
            fail("party {} refers to unknown faction {}", party.keyid, party.faction);
        if (party.leader != 0)
            character(party.leader, std::format("party {} leader", party.keyid));
        for (auto const member : party.members)
            character(member, std::format("party {} member", party.keyid));
        for (auto const captive : party.captives)
            character(captive, std::format("party {} captive", party.keyid));
    }
    for (auto const &entry : characters_) {
        auto const &character_record = entry.second;
        if (!parties_.contains(character_record.party))
            fail("character {} refers to unknown party {}", character_record.keyid,
                 character_record.party);
        for (auto const &slot : character_record.equipment)
            if (!equipments_.contains(slot.second))
                fail("character {} refers to unknown equipment {}", character_record.keyid,
                     slot.second);
        for (auto const &slot : character_record.weapon)
            if (!weapons_.contains(slot.second))
                fail("character {} refers to unknown weapon {}", character_record.keyid,
                     slot.second);
    }
    // Keep referential diagnostics above independent of membership checks below.
    for (auto const &[_, party] : parties_) {
        auto check_owner = [&](std::uint32_t id) {
            if (characters_.at(id).party != party.keyid)
                fail("party {} lists character {} belonging to another party", party.keyid, id);
        };
        if (party.leader != 0)
            check_owner(party.leader);
        std::unordered_set<std::uint32_t> members;
        for (auto id : party.members) {
            check_owner(id);
            if (id == party.leader)
                fail("party {} leader {} must not appear in members", party.keyid, id);
            if (!members.insert(id).second)
                fail("party {} has duplicate member {}", party.keyid, id);
        }
        std::unordered_set<std::uint32_t> captives;
        for (auto id : party.captives) {
            check_owner(id);
            if (!captives.insert(id).second)
                fail("party {} has duplicate captive {}", party.keyid, id);
            if (id == party.leader || members.contains(id))
                fail("party {} character {} is both a companion and captive", party.keyid, id);
        }
    }
    for (auto const &[id, record] : characters_) {
        auto const &party = parties_.at(record.party);
        if (party.leader != id && !std::ranges::contains(party.members, id) &&
            !std::ranges::contains(party.captives, id))
            fail("character {} is not listed in its party {}", id, party.keyid);
        for (auto const &[slot, id] : record.equipment)
            if (equipments_.at(id).type != slot)
                fail("character {} equipment {} does not match its slot", record.keyid, id);
        for (auto const &[slot, id] : record.weapon)
            if (weapons_.at(id).type != slot)
                fail("character {} weapon {} does not match its slot", record.keyid, id);
    }
    for (auto const &[id, state] : states_) {
        character(id, "checkpoint state");
        if (!std::isfinite(state.x) || !std::isfinite(state.y) || !std::isfinite(state.z) ||
            !std::isfinite(state.body_yaw) || !std::isfinite(state.look_yaw) ||
            !std::isfinite(state.look_pitch) || std::abs(state.look_yaw) > 85.0F ||
            std::abs(state.look_pitch) > 89.0F)
            fail("character {} has invalid saved pose", id);
        if (state.patrol && id != session_.example)
            fail("character {} is not the patrol example", id);
        if (state.companion_order != CompanionOrder::None &&
            state.companion_order != CompanionOrder::Follow &&
            state.companion_order != CompanionOrder::Wait)
            fail("character {} has an invalid companion order", id);
        if (state.companion_order != CompanionOrder::None) {
            auto const &party = parties_.at(characters_.at(id).party);
            if (state.patrol || party.leader == 0 || !std::ranges::contains(party.members, id))
                fail("character {} has an ineligible companion order", id);
        }
    }
}

SerializedCharacterConfig const &Save::character(std::uint32_t keyid, std::string_view what) const
{
    auto const record = characters_.find(keyid);
    if (record == characters_.end())
        fail("{} refers to unknown character {}", what, keyid);
    return record->second;
}

} // namespace lc1
