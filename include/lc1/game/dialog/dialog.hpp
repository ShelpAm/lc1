#pragma once

#include <cstdint>
#include <filesystem>
#include <list>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lc1 {

class Character;

enum class DialogRole { Talker, Target };
enum class DialogCondition { Always, CanRecruit, CanBeRecruited, SameParty, CanCommand };
enum class DialogAction { None, Recruit, Follow, Wait, ForgeIronHelmet };
enum class DialogTrigger { Interaction, Nearby };

struct DialogChoice {
    std::string id;
    std::string text;
    DialogCondition condition;
    DialogAction action;
    DialogRole recruiter;
    int npc_priority;
    std::string next; // Reserved target "end" closes the conversation.
};

struct DialogNode {
    std::string id;
    DialogRole speaker;
    DialogRole respondent;
    std::string text;
    std::vector<DialogChoice> choices;
};

struct DialogScript {
    int version;
    std::string entry;
    std::vector<DialogNode> nodes;

    static DialogScript load(std::filesystem::path const &path);
    void validate() const;
};

struct DialogRule {
    DialogTrigger trigger;
    std::uint32_t talker; // Zero matches any character.
    std::uint32_t target;
    std::vector<DialogCondition> conditions; // All must match; first matching rule wins.
    std::string dialog;
};

struct DialogSettings {
    float start_distance;
    float end_distance;
    float line_seconds;
    float cooldown_seconds;
    float initial_delay;
    float max_duration;
};

// Immutable content. IDs, rather than filenames or node IDs, identify conversations.
class DialogCatalog {
  public:
    explicit DialogCatalog(std::filesystem::path const &path);
    DialogScript const &script(std::string_view id) const;
    std::string_view select(Character const &talker, Character const &target,
                            DialogTrigger trigger) const;

    DialogSettings const &settings() const;

  private:
    std::map<std::string, DialogScript, std::less<>> scripts_;
    std::vector<DialogRule> rules_;
    DialogSettings settings_;
};

// A single conversation. The script and participants must outlive it.
class Dialog {
  public:
    explicit Dialog(DialogScript const &script);
    Dialog(DialogScript &&) = delete;

    bool start(Character &talker, Character &target);
    // Only the current respondent may choose. A chosen response is displayed before advancing.
    void respond(Character &character, int response_index);
    void update(float delta_seconds, Character const *player, float line_seconds);

    Character *talker() const;

    Character *target() const;

    Character *speaker() const;
    Character *respondent() const;
    bool contains(Character const &character) const;

    bool displaying_response() const;

    std::string_view current_speech_text() const;

    std::vector<std::string> const &candidate_responses() const;

    std::string_view current_node_id() const;
    std::string_view response_id(std::size_t index) const;
    void clear();

  private:
    Character *participant(DialogRole role) const;
    bool available(DialogChoice const &choice) const;
    void enter(std::string_view node_id);
    void refresh();
    std::string expand(std::string_view text) const;

    DialogScript const *script_; // Required non-null borrow; immutable for this lifetime.
    std::size_t node_index_{};
    std::vector<std::size_t> visible_choices_;
    std::optional<std::size_t> pending_choice_;
    float elapsed_{};
    // Non-owning participants; both must outlive the active conversation.
    Character *target_{};
    Character *talker_{};
    std::string current_speech_text_;
    std::vector<std::string> candidate_responses_;
};

// Local gameplay coordination, independent of rendering, input and window APIs.
// Participants must outlive active use; teardown does not dereference them. Session pointers are
// borrowed until the next update() or interruption of that session; a character can participate in
// at most one active session.
class DialogSystem {
  public:
    explicit DialogSystem(std::filesystem::path const &catalog_path);
    DialogSystem(DialogSystem const &) = delete;
    DialogSystem &operator=(DialogSystem const &) = delete;
    DialogSystem(DialogSystem &&) = delete;
    DialogSystem &operator=(DialogSystem &&) = delete;
    ~DialogSystem() = default;

    Dialog *find(Character const &character);
    Dialog const *find(Character const &character) const;
    std::string_view dialog_id(Character const &character) const;
    bool can_start(Character const &talker, Character const &target,
                   DialogTrigger trigger = DialogTrigger::Interaction) const;
    bool start(Character &talker, Character &target,
               DialogTrigger trigger = DialogTrigger::Interaction);
    bool interrupt(Character const &character);
    // Release all participant borrows before their world is destroyed.
    void clear() noexcept;
    // Clear runtime state and restart the ambient-conversation delay after loading a world.
    void reset() noexcept;
    // Non-null, unique registered participants. Player can be null for an NPC-only world.
    // Player requests can be deferred while moving, using UI, or otherwise occupied.
    void update(std::span<Character *const> characters, Character const *player,
                float delta_seconds, bool allow_player_requests);

  private:
    struct Session {
        std::string id;
        Character *talker;
        Character *target;
        Dialog dialog;
        float elapsed{};
    };

    bool cooling_down(Character const &character) const;
    void finish(Session &session);

    DialogCatalog catalog_; // Precedes sessions borrowing its immutable scripts.
    std::list<Session> sessions_;
    // Stable list nodes; remove both entries before erasing their session.
    std::unordered_map<Character const *, Session *> participant_sessions_;
    std::unordered_map<std::uint32_t, float> cooldowns_;
    float initial_delay_;
    // Scratch buffers only: roster borrows are released before update returns.
    std::vector<Character *> ordered_;
    std::vector<Character *> by_x_;
};

} // namespace lc1
