// Entry points of a mission as the front-end flow sees them (original
// 1000:01DE mission run and 1000:0790 unload, docs/re/seg_1000.md 6).
#pragma once

namespace st::game::mission {

// Play the current mission (campaign/practice/demo state already set up by
// the front end). Returns 1 if the player quit the game (Alt-X), else 0.
int run();

// Release the mission world after run() (1000:0790).
void unload();

} // namespace st::game::mission
