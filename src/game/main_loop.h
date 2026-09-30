// The top-level game loop (main_game_loop, 19ac:016D; docs/re/seg_19ac.md
// 2.2): title, main menu and the campaign / practice / demo step machines.
#pragma once

namespace st::game {

// Runs until a screen asks to quit (or a demo mission ends). Returns the
// process exit code (0).
int mainLoop();

} // namespace st::game
