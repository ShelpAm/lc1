#pragma once

#include "lc1/scene/model.hpp"

namespace lc1 {

struct CharacterBody;
// CPU-only placeholder content, shared by the game and rendering tests.
scene::Model make_character_model(CharacterBody const &body);

// Local static OBJ assets, adapted to the player's foot origin and presentation mounts.
scene::Model load_barrel_player_model(std::filesystem::path const &path, CharacterBody const &body);
scene::Model load_player_weapon_model(std::filesystem::path const &path);

} // namespace lc1
