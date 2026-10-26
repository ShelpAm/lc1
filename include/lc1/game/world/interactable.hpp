#pragma once

#include "lc1/game/dialog/dialog.hpp"

namespace lc1 {

class Character;

class Interactable {
  public:
    Interactable() = default;
    Interactable(Interactable const &) = delete;
    Interactable(Interactable &&) = delete;
    Interactable &operator=(Interactable const &) = delete;
    Interactable &operator=(Interactable &&) = delete;

    virtual ~Interactable() = default;
    virtual void interact(Character &talker) = 0;
    virtual bool can_interact(Character &talker) const = 0;
};

class InteractableCharacter : public Interactable {
  public:
    InteractableCharacter(Character *character, DialogSystem *dialogs);

    InteractableCharacter(InteractableCharacter const &) = delete;
    InteractableCharacter(InteractableCharacter &&) = delete;
    InteractableCharacter &operator=(InteractableCharacter const &) = delete;
    InteractableCharacter &operator=(InteractableCharacter &&) = delete;

    ~InteractableCharacter() override = default;

    void interact(Character &talker) override;
    bool can_interact(Character &talker) const override;

  private:
    // Required non-null borrows; the character and dialog system outlive this interaction.
    Character *me_;
    DialogSystem *dialogs_;
};

} // namespace lc1
