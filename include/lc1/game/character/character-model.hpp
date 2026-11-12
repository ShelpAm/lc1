#pragma once

#include "lc1/scene/model.hpp"

namespace lc1 {

struct CharacterBody;
// CPU-only placeholder content, shared by the game and rendering tests.
scene::Model make_character_model(CharacterBody const &body);
scene::Model make_flashlight_model();
scene::Model make_torch_model();
scene::Model make_helmet_model();
// Visual-only rigid recoil (0.3 s) and forward fall (0.6 s, final pose held).
glm::mat4 character_reaction_transform(bool dead, float elapsed, CharacterBody const &body);

} // namespace lc1
