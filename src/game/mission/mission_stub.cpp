// Temporary stand-in for the mission loop until the renderer and simulation
// are integrated (phase 2b). Lets the front-end flow run end to end: the
// mission "ends" immediately as if the team had been extracted.
#include "game/mission/mission.h"

#include "core/common.h"

namespace st::game::mission {

int run() {
    logWarn("mission loop not ported yet: skipping the mission");
    return 0;
}

void unload() {}

} // namespace st::game::mission
