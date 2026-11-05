#include "lc1/game/world/world-snapshot.hpp"
#include "lc1/game/world/game-world.hpp"

#include "lc1/error.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace lc1 {
CharacterSnapshot const &WorldSnapshot::character(CharacterId id) const
{
    auto const found =
        std::ranges::lower_bound(characters, id.value, {}, &CharacterSnapshot::keyid);
    if (found == characters.end() || found->keyid != id.value)
        fail("unknown snapshot character {}", id.value);
    return *found;
}

CharacterSnapshot const &WorldSnapshot::player(PlayerId id) const
{
    return character(players.at(id).character_id);
}

WorldSnapshot interpolate_world(WorldSnapshot const &previous, WorldSnapshot const &current,
                                float alpha)
{
    assert(alpha >= 0 && alpha <= 1);
    assert(previous.characters.size() == current.characters.size());
    auto result = current;
    for (std::size_t i = 0; i < result.characters.size(); ++i) {
        assert(previous.characters[i].keyid == current.characters[i].keyid);
        auto const &from = previous.characters[i].pose;
        auto const &to = current.characters[i].pose;
        auto &pose = result.characters[i].pose;
        pose.world_transform.position = glm::mix(from.position(), to.position(), alpha);
        // Upright gameplay transforms rotate only around Y. Cross the yaw seam by
        // the shortest arc; relative look angles have bounded, non-wrapping ranges.
        pose.world_transform.rotation.y =
            from.body_yaw() + std::remainder(to.body_yaw() - from.body_yaw(), 360.0F) * alpha;
        pose.relative_look_yaw = glm::mix(from.look_yaw(), to.look_yaw(), alpha);
        pose.relative_look_pitch = glm::mix(from.look_pitch(), to.look_pitch(), alpha);
    }
    return result;
}

WorldSnapshot capture_world(GameWorld const &world)
{
    WorldSnapshot snapshot;
    snapshot.characters.reserve(world.characters().size());
    for (auto const *character : world.characters()) {
        std::optional<ModelConfig> weapon_model;
        auto const slot = character->config().weapon.find(WeaponType::MeleeWeapon);
        if (slot != character->config().weapon.end())
            weapon_model = slot->second->model_config();
        std::string speech;
        auto const *dialog = world.dialogs().find(*character);
        if (dialog && dialog->speaker() == character)
            speech = dialog->current_speech_text();
        snapshot.characters.push_back(
            {.keyid = character->config().keyid,
             .name = character->config().name,
             .faction = world.faction(character->faction()).name(),
             .pose = *character,
             .model = character->config().model,
             .weapon_model = std::move(weapon_model),
             .attack_sequence = character->attack_sequence(),
             .nod_sequence = character->nod_sequence(),
             .speech = std::move(speech),
             .dialog_partner =
                 dialog ? std::optional{(dialog->talker() == character ? dialog->target()
                                                                       : dialog->talker())
                                            ->config()
                                            .keyid}
                        : std::nullopt,
             .age = character->config().age,
             .level = character->config().level,
             .party_id = character->config().party->keyid(),
             .companion_order = character->companion_order(),
             .equipment = {},
             .weapon = {},
             .bag = character->bag()});
        auto &view = snapshot.characters.back();
        for (auto const &[slot, equipment] : character->config().equipment)
            view.equipment.emplace(slot, *equipment);
        for (auto const &[slot, weapon] : character->config().weapon)
            view.weapon.emplace(slot, *weapon);
    }
    snapshot.parties.reserve(world.parties().size());
    for (auto const &[id, party] : world.parties())
        snapshot.parties.push_back(*party);
    std::ranges::sort(snapshot.parties, {}, &Party::keyid);
    for (auto const &[id, player] : world.players()) {
        auto const &actor = *player.character;
        auto &view =
            snapshot.players.emplace(id, PlayerSnapshot{.character_id = actor.id()}).first->second;
        view.bag = actor.bag();
        view.companion_count = actor.config().party->members().size();
        if (player.interaction_target)
            view.interaction_target = player.interaction_target->config().name;
        if (auto const *dialog = world.dialogs().find(actor))
            view.dialog = DialogSnapshot{.speaker = dialog->speaker()->config().name,
                                         .speech = std::string{dialog->current_speech_text()},
                                         .responses = dialog->respondent() == &actor
                                                          ? dialog->candidate_responses()
                                                          : std::vector<std::string>{}};
    }
    return snapshot;
}
} // namespace lc1
