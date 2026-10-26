#pragma once

#include "lc1/game/character/equipment.hpp"
#include "lc1/game/character/weapon.hpp"
#include "lc1/game/continent/identity.hpp"
#include "lc1/game/party/companion-order.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lc1 {

class Character;
class Party;

// Seed files store one record kind per file; checkpoints store the complete world together.
// The session record says which character the player controls. Records hold keyids where runtime
// objects holds a borrowed pointer, so a file never depends on address or on load order. See
// docs/save-data.md.
struct SerializedFaction {
    std::uint32_t keyid;
    std::string name;
};

struct SerializedEquipment {
    std::uint32_t keyid;
    EquipmentType type;
    float armor;
};

struct SerializedWeapon {
    std::uint32_t keyid;
    WeaponType type;
    float attack;
    float range;
};

struct SerializedParty {
    std::uint32_t keyid;
    std::uint32_t faction;
    std::uint32_t leader;               // 0 means the party has no leader yet.
    std::vector<std::uint32_t> members; // Ordinary members only; excludes the leader.
    std::vector<std::uint32_t> captives;
};

// Session state, not world data: a save names the character the player controls and,
// optionally, the character an AI demonstration drives. The rules of the world (parties,
// factions) stay in their own files.
struct SerializedSession {
    std::uint32_t player;
    std::uint32_t example; // 0 means this save has no AI demonstration.
};

struct SerializedCharacterConfig {
    std::uint32_t keyid;
    std::string name;
    bool sex;
    int age;
    std::uint32_t party;
    int level;
    // An enum cannot be a YAML mapping key, so reflect-cpp writes each map as a sequence
    // of [key, value] pairs: "- [Head, 10]".
    std::map<EquipmentType, std::uint32_t> equipment;
    std::map<WeaponType, std::uint32_t> weapon;
};

struct SerializedCharacterState {
    std::uint32_t keyid;
    float x;
    float y;
    float z;
    float body_yaw;
    float look_yaw;
    float look_pitch;
    bool patrol;
    CompanionOrder companion_order = CompanionOrder::None;
};

// One complete checkpoint: membership and character records are committed together.
struct SerializedWorld {
    std::uint32_t version = 3;
    ContinentIdentity continent{"present", 1};
    SerializedSession session;
    std::vector<SerializedFaction> factions;
    std::vector<SerializedEquipment> equipments;
    std::vector<SerializedWeapon> weapons;
    std::vector<SerializedParty> parties;
    std::vector<SerializedCharacterConfig> characters;
    std::vector<SerializedCharacterState> states;
};

// Runtime worlds must supply valid non-null party/faction and equipment/weapon links.
SerializedCharacterConfig serialize_character(Character const &character);
SerializedCharacterState serialize_character_state(Character const &character);
SerializedParty serialize_party(Party const &party);

// The saved world as data: one directory of YAML files, decoded once and checked for
// duplicate ids and dangling references. The session file names the player, so no character
// keyid is special to the game code. Save owns no runtime object, so it can be loaded
// without a device, a continent or a window, and the world that GameWorld builds from it
// borrows nothing back.
class Save {
  public:
    explicit Save(std::filesystem::path directory);

    // Names identify one child directory under the save collection.
    static std::filesystem::path slot_directory(std::filesystem::path const &root,
                                                std::string_view name);
    static std::vector<std::string> slots(std::filesystem::path const &root);

    // Validate before writing; commit a complete checkpoint by renaming a closed staging file.
    static void write(std::filesystem::path const &directory, SerializedWorld const &world);
    SerializedWorld data() const;

    std::filesystem::path const &directory() const;

    ContinentIdentity const &continent() const;

    std::uint32_t player() const;

    // 0 when the save has no AI demonstration; never a valid character keyid.
    std::uint32_t example() const;

    bool has_example() const;

    std::unordered_map<std::uint32_t, SerializedFaction> const &factions() const;

    std::unordered_map<std::uint32_t, SerializedEquipment> const &equipments() const;

    std::unordered_map<std::uint32_t, SerializedWeapon> const &weapons() const;

    std::unordered_map<std::uint32_t, SerializedParty> const &parties() const;

    std::unordered_map<std::uint32_t, SerializedCharacterConfig> const &characters() const;

    std::unordered_map<std::uint32_t, SerializedCharacterState> const &states() const;

  private:
    Save(std::filesystem::path directory, SerializedWorld const &world);
    void read_world(SerializedWorld const &world);
    void read_factions();
    void read_equipments();
    void read_weapons();
    void read_parties();
    void read_characters();
    void read_session();
    // Every keyid a record names must exist, checked before any runtime object is built:
    // a dangling reference is a data error, not a half-built world.
    void validate_references() const;
    // Shared by the session record and the world records, so a dangling keyid always
    // reports the same way no matter which file named it.
    SerializedCharacterConfig const &character(std::uint32_t keyid, std::string_view what) const;

    std::filesystem::path directory_;
    SerializedSession session_;
    ContinentIdentity continent_{"present", 1};
    std::unordered_map<std::uint32_t, SerializedFaction> factions_;
    std::unordered_map<std::uint32_t, SerializedEquipment> equipments_;
    std::unordered_map<std::uint32_t, SerializedWeapon> weapons_;
    std::unordered_map<std::uint32_t, SerializedParty> parties_;
    std::unordered_map<std::uint32_t, SerializedCharacterConfig> characters_;
    std::unordered_map<std::uint32_t, SerializedCharacterState> states_;
};

} // namespace lc1
