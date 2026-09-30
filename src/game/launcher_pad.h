// "Controller" page of the start menu: lists every game action with its
// pad binding, rebinds an action by pressing a button / stick, resets to
// the Xbox default layout and holds the pad options (docs/controller.md).
#pragma once

namespace st::game {

// Returns false if the player chose to quit the program (Alt-X).
bool runControllerPage();

} // namespace st::game
