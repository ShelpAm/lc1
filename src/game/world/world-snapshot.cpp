#include "lc1/game/world/world-snapshot.hpp"
#include "lc1/game/world/game-world.hpp"

namespace lc1 {
CharacterSnapshot const &WorldSnapshot::player() const
{
    return characters[player_index];
}

WorldSnapshot capture_world(GameWorld const &world)
{
    WorldSnapshot snapshot;
    snapshot.characters.reserve(world.characters().size());
    for (auto const *character : world.characters()) {
        if (character == &world.player())
            snapshot.player_index = snapshot.characters.size();
        std::optional<ModelConfig> weapon_model;
        auto const slot = character->config().weapon.find(WeaponType::MeleeWeapon);
        if (slot != character->config().weapon.end())
            weapon_model = slot->second->model_config();
        std::string speech;
        auto const *dialog = world.dialogs().find(*character);
        if (dialog && !dialog->contains(world.player()) && dialog->speaker() == character)
            speech = dialog->current_speech_text();
        snapshot.characters.push_back({.keyid = character->config().keyid,
                                       .name = character->config().name,
                                       .faction = character->faction()->name(),
                                       .pose = *character,
                                       .model = character->config().model,
                                       .weapon_model = std::move(weapon_model),
                                       .attack_sequence = character->attack_sequence(),
                                       .speech = std::move(speech)});
    }
    snapshot.bag = world.player().bag();
    snapshot.companion_count = world.player().config().party->members().size();
    if (auto const *target = world.interaction_target())
        snapshot.interaction_target = target->config().name;
    if (auto const *dialog = world.dialogs().find(world.player())) {
        snapshot.dialog = DialogSnapshot{.speaker = dialog->speaker()->config().name,
                                         .speech = std::string{dialog->current_speech_text()},
                                         .responses = dialog->respondent() == &world.player()
                                                          ? dialog->candidate_responses()
                                                          : std::vector<std::string>{}};
    }
    return snapshot;
}
} // namespace lc1
