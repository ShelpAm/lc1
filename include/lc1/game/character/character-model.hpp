#pragma once

#include "lc1/scene/model.hpp"

namespace lc1 {

struct CharacterBody;
// CPU-only placeholder content, shared by the game and rendering tests.
scene::Model make_character_model(CharacterBody const &body);

} // namespace lc1
