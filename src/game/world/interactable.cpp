#include "lc1/game/world/interactable.hpp"

#include "lc1/error.hpp"
#include "lc1/game/character/character.hpp"

namespace lc1 {

void InteractableCharacter::interact(Character &talker)
{
    if (!can_interact(talker))
        fail("cannot interact with this character");
    dialogs_->start(talker, *me_);
}

bool InteractableCharacter::can_interact(Character &talker) const
{
    return dialogs_->can_start(talker, *me_);
}

} // namespace lc1

namespace lc1 {

InteractableCharacter::InteractableCharacter(Character *character, DialogSystem *dialogs)
    : me_(character), dialogs_(dialogs)
{
}

} // namespace lc1
