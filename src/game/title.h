// EA logo and title screen with scrolling credits (19ac:05A4, 19ac:0667).
#pragma once

namespace st::game {

void titleShowLogo();
// Returns 0 to quit the game (Alt-X), 1 otherwise.
int titleScreen();

// input_toggle_keys (19ac:0B2A): Alt-S / Alt-D / Alt-M. Returns true if handled.
bool inputToggleKeys(int key);

} // namespace st::game
