// Port-only start menu shown before the original game: choose the Original
// (1:1) or Enhanced preset and change display, enhancement and sound
// settings. Drawn with the game's own fonts, buttons and cursor.
#pragma once

namespace st::game {

// Returns false if the player chose to quit.
bool runLauncher();

} // namespace st::game
