#include "lc1/game/ui/game-ui.hpp"
#include "lc1/game/actions.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, char const *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void panel_input_gate()
{
    lc1::GameUi ui;
    ui.toggle_player_info();
    ui.start_game();
    check(ui.gameplay_enabled(), "main menu cannot open the character panel");
    ui.toggle_player_info();
    check(!ui.gameplay_enabled(), "character panel suspends gameplay input");
    ui.toggle_player_info();
    check(ui.gameplay_enabled(), "closing the character panel restores gameplay input");
    ui.open_save("test");
    ui.toggle_player_info();
    ui.saved();
    check(ui.gameplay_enabled(), "save dialog prevents hidden character panel toggles");
    ui.open_load();
    check(!ui.gameplay_enabled(), "load dialog still blocks gameplay input");
}

void menu_overlay()
{
    lc1::GameUi ui;
    // The game starts on the menu; Esc there closes the overlay instead of quitting.
    check(!ui.gameplay_enabled(), "the main menu suspends gameplay input");
    ui.toggle_menu();
    check(ui.gameplay_enabled(), "Esc over the main menu resumes the game");
    ui.toggle_menu();
    check(!ui.gameplay_enabled(), "Esc during play opens the overlay and releases the mouse");
    ui.toggle_menu();
    check(ui.gameplay_enabled(), "Esc closes the overlay again");
    // A panel opened during play does not survive into the overlay.
    ui.toggle_player_info();
    ui.toggle_menu();
    check(!ui.gameplay_enabled(), "the overlay keeps gameplay suspended");
    ui.toggle_menu();
    check(ui.gameplay_enabled(), "the overlay closes without leaving stale panels behind");
}

void panel_binding()
{
    lc1::GameInputRouter router{lc1::default_game_bindings(), lc1::GameInputContext::Gameplay};
    lc1::InputState input;
    input.on_key(lc1::Key::B, true);
    router.update(input);
    check(router.pressed(lc1::GameAction::TogglePlayerInfo), "B opens the character panel");
    input.begin_frame();
    router.update(input);
    check(!router.pressed(lc1::GameAction::TogglePlayerInfo), "holding B does not repeat toggles");
    router.push(lc1::GameInputContext::Ui);
    input.on_key(lc1::Key::B, false);
    input.on_key(lc1::Key::B, true);
    router.update(input);
    check(!router.pressed(lc1::GameAction::TogglePlayerInfo),
          "UI context suppresses gameplay bindings");
}
} // namespace

int main()
{
    try {
        panel_input_gate();
        menu_overlay();
        panel_binding();
    }
    catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
