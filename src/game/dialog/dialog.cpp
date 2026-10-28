#include "lc1/game/dialog/dialog.hpp"

#include "lc1/error.hpp"
#include "lc1/game/character/character.hpp"
#include "lc1/game/party/party.hpp"
#include "lc1/game/resources/crafting.hpp"

#include <rfl/yaml.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <unordered_set>
#include <utility>

namespace lc1 {
namespace {

void validate_time(float seconds)
{
    if (!std::isfinite(seconds) || seconds < 0.0F)
        fail("invalid dialog update time");
}

bool valid_role(DialogRole role)
{
    return role == DialogRole::Talker || role == DialogRole::Target;
}

bool valid_condition(DialogCondition condition)
{
    return condition == DialogCondition::Always || condition == DialogCondition::CanRecruit ||
           condition == DialogCondition::CanBeRecruited ||
           condition == DialogCondition::SameParty || condition == DialogCondition::CanCommand;
}

bool can_recruit(Character const &recruiter, Character const &candidate)
{
    auto const *party = recruiter.config().party;
    return party && party->can_recruit(recruiter, candidate);
}

bool matches(DialogCondition condition, Character const &talker, Character const &target)
{
    switch (condition) {
    case DialogCondition::Always:
        return true;
    case DialogCondition::CanRecruit:
        return can_recruit(talker, target);
    case DialogCondition::CanBeRecruited:
        return can_recruit(target, talker);
    case DialogCondition::CanCommand:
        return talker.config().party && talker.config().party->can_command(talker, target);
    case DialogCondition::SameParty:
        return talker.config().party && talker.config().party == target.config().party;
    }
    return false;
}

// Substitutions are literal; names are never evaluated as code or format strings.
void validate_text(std::string_view text)
{
    if (text.empty())
        fail("dialog text must not be empty");
    while (!text.empty()) {
        auto const pos = text.find_first_of("{}");
        if (pos == std::string_view::npos)
            break;
        text.remove_prefix(pos);
        if (text.starts_with("{target_name}"))
            text.remove_prefix(std::string_view{"{target_name}"}.size());
        else if (text.starts_with("{talker_name}"))
            text.remove_prefix(std::string_view{"{talker_name}"}.size());
        else
            fail("unknown or malformed dialog placeholder: {}", text);
    }
}

template <typename T> T read_yaml(std::filesystem::path const &path)
{
    auto stream = std::ifstream{path};
    if (!stream)
        fail("cannot open dialog file {}", path.string());
    auto result = rfl::yaml::read<T, rfl::NoOptionals, rfl::NoExtraFields>(stream);
    if (!result)
        fail("cannot read dialog file {}: {}", path.string(), result.error().what());
    return std::move(*result);
}

struct ScriptFile {
    std::string id;
    std::string file;
};

struct CatalogFile {
    int version;
    DialogSettings settings;
    std::vector<ScriptFile> scripts;
    std::vector<DialogRule> rules;
};

} // namespace

DialogScript DialogScript::load(std::filesystem::path const &path)
{
    auto script = read_yaml<DialogScript>(path);
    try {
        script.validate();
    }
    catch (Error const &error) {
        throw Error(std::format("invalid dialog script {}: {}", path.string(), error.what()),
                    error.location());
    }
    return script;
}

void DialogScript::validate() const
{
    if (version != 2)
        fail("unsupported dialog script version {}", version);
    std::unordered_set<std::string> ids;
    for (auto const &node : nodes) {
        if (node.id.empty() || node.id == "end" || !ids.insert(node.id).second)
            fail("invalid or duplicate dialog node id '{}'", node.id);
    }
    if (!ids.contains(entry))
        fail("unknown dialog entry '{}'", entry);
    for (auto const &node : nodes) {
        if (!valid_role(node.speaker) || !valid_role(node.respondent))
            fail("unknown participant role in node '{}'", node.id);
        validate_text(node.text);
        if (node.choices.empty() || node.choices.size() > 4)
            fail("dialog node '{}' must have 1 to 4 choices", node.id);
        std::unordered_set<std::string> choices;
        bool has_fallback = false;
        for (auto const &choice : node.choices) {
            if (choice.id.empty() || !choices.insert(choice.id).second)
                fail("invalid or duplicate choice id '{}' in node '{}'", choice.id, node.id);
            validate_text(choice.text);
            if (choice.next != "end" && !ids.contains(choice.next))
                fail("unknown target '{}' in node '{}' choice '{}'", choice.next, node.id,
                     choice.id);
            if (!valid_condition(choice.condition))
                fail("unknown dialog condition in node '{}'", node.id);
            if (choice.action != DialogAction::None && choice.action != DialogAction::Recruit &&
                choice.action != DialogAction::Follow && choice.action != DialogAction::Wait &&
                choice.action != DialogAction::ForgeIronHelmet)
                fail("unknown dialog action in node '{}'", node.id);
            if (!valid_role(choice.recruiter))
                fail("unknown recruiter role in node '{}'", node.id);
            if (choice.condition == DialogCondition::Always && choice.action == DialogAction::None)
                has_fallback = true;
        }
        if (!has_fallback)
            fail("dialog node '{}' needs an unconditional choice without an action", node.id);
    }
}

DialogCatalog::DialogCatalog(std::filesystem::path const &path)
{
    try {
        auto file = read_yaml<CatalogFile>(path);
        if (file.version != 1)
            fail("unsupported dialog catalog version {}", file.version);
        settings_ = file.settings;
        for (auto seconds : {settings_.line_seconds, settings_.cooldown_seconds,
                             settings_.initial_delay, settings_.max_duration})
            validate_time(seconds);
        if (!std::isfinite(settings_.start_distance) || settings_.start_distance <= 0.0F ||
            !std::isfinite(settings_.end_distance) ||
            settings_.end_distance < settings_.start_distance || settings_.line_seconds <= 0.0F ||
            settings_.cooldown_seconds <= 0.0F || settings_.max_duration <= settings_.line_seconds)
            fail("invalid dialog distances or timing settings");
        for (auto const &source : file.scripts) {
            auto const relative = std::filesystem::path{source.file}.lexically_normal();
            if (source.id.empty() || scripts_.contains(source.id))
                fail("empty or duplicate dialog id '{}'", source.id);
            if (source.file.empty() || relative.is_absolute() || relative.has_root_path() ||
                relative == "." || std::ranges::find(relative, "..") != relative.end())
                fail("dialog '{}' needs a relative file within the catalog directory", source.id);
            scripts_.emplace(source.id, DialogScript::load(path.parent_path() / relative));
        }
        if (scripts_.empty())
            fail("dialog catalog must contain scripts");
        for (auto const &rule : file.rules) {
            script(rule.dialog);
            if (rule.trigger != DialogTrigger::Interaction && rule.trigger != DialogTrigger::Nearby)
                fail("unknown dialog trigger");
            if (rule.conditions.empty())
                fail("dialog rule '{}' needs conditions; use Always for a fallback", rule.dialog);
            for (auto condition : rule.conditions)
                if (!valid_condition(condition))
                    fail("unknown condition in dialog rule '{}'", rule.dialog);
        }
        rules_ = std::move(file.rules);
    }
    catch (Error const &error) {
        throw Error(std::format("invalid dialog catalog {}: {}", path.string(), error.what()),
                    error.location());
    }
}

DialogScript const &DialogCatalog::script(std::string_view id) const
{
    auto const found = scripts_.find(id);
    if (found == scripts_.end())
        fail("unknown dialog id '{}'", id);
    return found->second;
}

std::string_view DialogCatalog::select(Character const &talker, Character const &target,
                                       DialogTrigger trigger) const
{
    for (auto const &rule : rules_) {
        if (rule.trigger == trigger && (rule.talker == 0 || rule.talker == talker.config().keyid) &&
            (rule.target == 0 || rule.target == target.config().keyid) &&
            std::ranges::all_of(rule.conditions,
                                [&](auto condition) { return matches(condition, talker, target); }))
            return rule.dialog;
    }
    return {};
}

Dialog::Dialog(DialogScript const &script) : script_(&script)
{
    script_->validate();
}

bool Dialog::start(Character &talker, Character &target)
{
    if (target_ || &target == &talker)
        return false;
    target_ = &target;
    talker_ = &talker;
    enter(script_->entry);
    return true;
}

Character *Dialog::participant(DialogRole role) const
{
    return role == DialogRole::Talker ? talker_ : target_;
}

Character *Dialog::speaker() const
{
    if (!target_)
        return nullptr;
    auto const &node = script_->nodes[node_index_];
    return participant(pending_choice_ ? node.respondent : node.speaker);
}

Character *Dialog::respondent() const
{
    return target_ && !pending_choice_ ? participant(script_->nodes[node_index_].respondent)
                                       : nullptr;
}

bool Dialog::contains(Character const &character) const
{
    return talker_ == &character || target_ == &character;
}

bool Dialog::available(DialogChoice const &choice) const
{
    if (!matches(choice.condition, *talker_, *target_))
        return false;
    if (choice.action == DialogAction::ForgeIronHelmet) {
        auto const *customer = participant(choice.recruiter);
        return game::can_craft(customer->bag(), game::iron_helmet_recipe());
    }
    if (choice.action == DialogAction::Recruit) {
        auto const *recruiter = participant(choice.recruiter);
        auto const *candidate = participant(
            choice.recruiter == DialogRole::Talker ? DialogRole::Target : DialogRole::Talker);
        return can_recruit(*recruiter, *candidate);
    }
    if (choice.action == DialogAction::Follow || choice.action == DialogAction::Wait) {
        auto const *commander = participant(choice.recruiter);
        auto const *member = participant(
            choice.recruiter == DialogRole::Talker ? DialogRole::Target : DialogRole::Talker);
        return commander->config().party &&
               commander->config().party->can_command(*commander, *member);
    }
    return true;
}

void Dialog::respond(Character &character, int response_index)
{
    if (respondent() != &character || response_index < 0 ||
        static_cast<std::size_t>(response_index) >= visible_choices_.size())
        return;
    auto const choice_index = visible_choices_[response_index];
    auto const &choice = script_->nodes[node_index_].choices[choice_index];
    // Resolve the previously displayed choice before checking its current eligibility.
    if (!available(choice)) {
        refresh();
        return;
    }
    auto response = expand(choice.text);
    if (choice.action == DialogAction::Recruit) {
        auto *recruiter = participant(choice.recruiter);
        auto *candidate = participant(choice.recruiter == DialogRole::Talker ? DialogRole::Target
                                                                             : DialogRole::Talker);
        recruiter->config().party->recruit(*recruiter, *candidate);
    }
    if (choice.action == DialogAction::Follow || choice.action == DialogAction::Wait) {
        auto *commander = participant(choice.recruiter);
        auto *member = participant(choice.recruiter == DialogRole::Talker ? DialogRole::Target
                                                                          : DialogRole::Talker);
        commander->config().party->command(
            *commander, *member,
            choice.action == DialogAction::Follow ? CompanionOrder::Follow : CompanionOrder::Wait);
    }
    if (choice.action == DialogAction::ForgeIronHelmet) {
        auto *customer = participant(choice.recruiter);
        if (!game::craft(customer->bag(), game::iron_helmet_recipe())) {
            refresh();
            return;
        }
    }
    pending_choice_ = choice_index;
    current_speech_text_ = std::move(response);
    candidate_responses_.clear();
    visible_choices_.clear();
    elapsed_ = 0.0F;
}

void Dialog::update(float delta_seconds, Character const *player, float line_seconds)
{
    validate_time(delta_seconds);
    if (!std::isfinite(line_seconds) || line_seconds <= 0.0F)
        fail("dialog line duration must be positive");
    if (!target_)
        return;
    elapsed_ += delta_seconds;
    if (elapsed_ < line_seconds)
        return;
    // At most one transition per update, even after a long frame.
    if (pending_choice_) {
        auto const &next = script_->nodes[node_index_].choices[*pending_choice_].next;
        if (next == "end")
            clear();
        else
            enter(next);
        return;
    }
    if (respondent() == player)
        return;
    refresh();
    std::size_t best = 0;
    auto const &choices = script_->nodes[node_index_].choices;
    for (std::size_t i = 1; i < visible_choices_.size(); ++i)
        if (choices[visible_choices_[i]].npc_priority >
            choices[visible_choices_[best]].npc_priority)
            best = i;
    respond(*respondent(), static_cast<int>(best));
}

void Dialog::enter(std::string_view node_id)
{
    auto const node = std::ranges::find(script_->nodes, node_id, &DialogNode::id);
    node_index_ = static_cast<std::size_t>(node - script_->nodes.begin());
    pending_choice_.reset();
    refresh();
}

std::string Dialog::expand(std::string_view text) const
{
    std::string result;
    while (!text.empty()) {
        if (text.starts_with("{target_name}")) {
            result += target_->config().name;
            text.remove_prefix(std::string_view{"{target_name}"}.size());
        }
        else if (text.starts_with("{talker_name}")) {
            result += talker_->config().name;
            text.remove_prefix(std::string_view{"{talker_name}"}.size());
        }
        else {
            result += text.front();
            text.remove_prefix(1);
        }
    }
    return result;
}

void Dialog::refresh()
{
    auto const &node = script_->nodes[node_index_];
    current_speech_text_ = expand(node.text);
    candidate_responses_.clear();
    visible_choices_.clear();
    elapsed_ = 0.0F;
    for (std::size_t i = 0; i < node.choices.size(); ++i) {
        if (available(node.choices[i])) {
            visible_choices_.push_back(i);
            candidate_responses_.push_back(expand(node.choices[i].text));
        }
    }
}

std::string_view Dialog::current_node_id() const
{
    return target_ ? std::string_view{script_->nodes[node_index_].id} : std::string_view{};
}

std::string_view Dialog::response_id(std::size_t index) const
{
    if (index >= visible_choices_.size())
        return {};
    return script_->nodes[node_index_].choices[visible_choices_[index]].id;
}

void Dialog::clear()
{
    target_ = nullptr;
    talker_ = nullptr;
    node_index_ = 0;
    pending_choice_.reset();
    elapsed_ = 0.0F;
    current_speech_text_.clear();
    candidate_responses_.clear();
    visible_choices_.clear();
}

DialogSystem::DialogSystem(std::filesystem::path const &catalog_path)
    : catalog_(catalog_path), initial_delay_(catalog_.settings().initial_delay)
{
}

Dialog const *DialogSystem::find(Character const &character) const
{
    auto const found = participant_sessions_.find(&character);
    return found == participant_sessions_.end() || !found->second->dialog.contains(character)
               ? nullptr
               : &found->second->dialog;
}

Dialog *DialogSystem::find(Character const &character)
{
    auto const found = participant_sessions_.find(&character);
    return found == participant_sessions_.end() || !found->second->dialog.contains(character)
               ? nullptr
               : &found->second->dialog;
}

std::string_view DialogSystem::dialog_id(Character const &character) const
{
    auto const found = participant_sessions_.find(&character);
    return found == participant_sessions_.end() || !found->second->dialog.contains(character)
               ? std::string_view{}
               : found->second->id;
}

bool DialogSystem::cooling_down(Character const &character) const
{
    auto const found = cooldowns_.find(character.config().keyid);
    return found != cooldowns_.end() && found->second > 0.0F;
}

bool DialogSystem::can_start(Character const &talker, Character const &target,
                             DialogTrigger trigger) const
{
    return &talker != &target && !find(talker) && !find(target) &&
           glm::distance(talker.position(), target.position()) <=
               catalog_.settings().start_distance &&
           (trigger != DialogTrigger::Nearby ||
            (initial_delay_ == 0.0F && !cooling_down(talker) && !cooling_down(target))) &&
           !catalog_.select(talker, target, trigger).empty();
}

bool DialogSystem::start(Character &talker, Character &target, DialogTrigger trigger)
{
    // A borrowed Dialog can be cleared or advanced to its end before the next system tick.
    // Retire that inactive node before installing another session for its participants.
    for (auto const *character : {&talker, &target}) {
        auto const found = participant_sessions_.find(character);
        if (found != participant_sessions_.end() && !found->second->dialog.target()) {
            auto *finished = found->second;
            auto const node = std::ranges::find_if(
                sessions_, [&](auto const &session) { return &session == finished; });
            finish(*finished);
            sessions_.erase(node);
        }
    }
    if (!can_start(talker, target, trigger))
        return false;
    auto const id = catalog_.select(talker, target, trigger);
    sessions_.push_back({.id = std::string{id},
                         .talker = &talker,
                         .target = &target,
                         .dialog = Dialog{catalog_.script(id)}});
    auto &session = sessions_.back();
    try {
        participant_sessions_.emplace(&talker, &session);
        participant_sessions_.emplace(&target, &session);
        session.dialog.start(talker, target);
    }
    catch (...) {
        participant_sessions_.erase(&talker);
        participant_sessions_.erase(&target);
        sessions_.pop_back();
        throw;
    }
    return true;
}

void DialogSystem::finish(Session &session)
{
    for (auto const *character : {session.talker, session.target})
        cooldowns_[character->config().keyid] = catalog_.settings().cooldown_seconds;
    participant_sessions_.erase(session.talker);
    participant_sessions_.erase(session.target);
    session.dialog.clear();
}

bool DialogSystem::interrupt(Character const &character)
{
    for (auto session = sessions_.begin(); session != sessions_.end(); ++session) {
        if (session->dialog.contains(character)) {
            finish(*session);
            sessions_.erase(session);
            return true;
        }
    }
    return false;
}

void DialogSystem::clear() noexcept
{
    participant_sessions_.clear();
    sessions_.clear();
    cooldowns_.clear();
    ordered_.clear();
    by_x_.clear();
}

void DialogSystem::reset() noexcept
{
    clear();
    initial_delay_ = catalog_.settings().initial_delay;
}

void DialogSystem::update(float delta_seconds, std::span<Character *const> characters,
                          Character const *player, bool allow_player_requests)
{
    validate_time(delta_seconds);
    initial_delay_ = std::max(0.0F, initial_delay_ - delta_seconds);
    for (auto &[_, cooldown] : cooldowns_)
        cooldown = std::max(0.0F, cooldown - delta_seconds);
    std::erase_if(cooldowns_, [](auto const &entry) { return entry.second == 0.0F; });
    for (auto session = sessions_.begin(); session != sessions_.end();) {
        session->elapsed += delta_seconds;
        if (glm::distance(session->talker->position(), session->target->position()) >
                catalog_.settings().end_distance ||
            session->elapsed >= catalog_.settings().max_duration)
            session->dialog.clear();
        session->dialog.update(delta_seconds, player, catalog_.settings().line_seconds);
        if (!session->dialog.target()) {
            finish(*session);
            session = sessions_.erase(session);
        }
        else
            ++session;
    }
    if (initial_delay_ > 0.0F)
        return;
    // Sweep an X-sorted roster to restrict searches to a nearby slab. Exact 3D distances
    // and explicit keyid ties preserve the original deterministic selection policy.
    ordered_.assign(characters.begin(), characters.end());
    for (auto const *character : ordered_)
        if (!character)
            fail("dialog scheduler requires non-null characters");
    std::ranges::sort(ordered_, {},
                      [](auto const *character) { return character->config().keyid; });
    by_x_ = ordered_;
    std::ranges::sort(by_x_, {}, [](auto const *character) { return character->position().x; });
    float const radius = catalog_.settings().start_distance;
    for (auto *talker : ordered_) {
        if (talker == player || find(*talker) || cooling_down(*talker))
            continue;
        Character *best = nullptr;
        float best_distance = radius;
        auto candidate =
            std::ranges::lower_bound(by_x_, talker->position().x - radius, {},
                                     [](auto const *character) { return character->position().x; });
        for (; candidate != by_x_.end() &&
               (*candidate)->position().x <= talker->position().x + radius;
             ++candidate) {
            auto *target = *candidate;
            if (target == player && !allow_player_requests)
                continue;
            auto const distance = glm::distance(talker->position(), target->position());
            if (distance <= best_distance && can_start(*talker, *target, DialogTrigger::Nearby) &&
                (!best || distance < best_distance ||
                 target->config().keyid < best->config().keyid)) {
                best = target;
                best_distance = distance;
            }
        }
        if (best)
            start(*talker, *best, DialogTrigger::Nearby);
    }
    ordered_.clear();
    by_x_.clear();
}

} // namespace lc1

namespace lc1 {

DialogSettings const &DialogCatalog::settings() const
{
    return settings_;
}

Character *Dialog::talker() const
{
    return talker_;
}

Character *Dialog::target() const
{
    return target_;
}

bool Dialog::displaying_response() const
{
    return pending_choice_.has_value();
}

std::string_view Dialog::current_speech_text() const
{
    return current_speech_text_;
}

std::vector<std::string> const &Dialog::candidate_responses() const
{
    return candidate_responses_;
}

} // namespace lc1
