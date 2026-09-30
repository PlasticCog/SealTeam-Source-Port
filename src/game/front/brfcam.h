// The top-down map camera of the briefing and the debriefing (365e:1F28..225B,
// docs/re/seg_365e_a.md 9.5): a ring of 8 waypoints (far 5170:0000) that
// moves the position of a view toward a target at a given height.
#pragma once

#include "core/common.h"
#include "game/front/hooks.h"
#include "game/types.h"

namespace st::game::front::brfcam {

// The view the camera moves (the port's stand-in for descriptor DS:D86E).
void attach(hooks::View* view);
// brfcam_queue_push: fails when 8 are queued; the first waypoint queued into
// an empty ring sets the height immediately.
bool push(const Vec3* target, int height, int duration, int flag, int keep);
bool remove(int i);   // brfcam_queue_remove: keeps the last slot if `keep`
void clear();         // removes all removable waypoints
void skip();          // brfcam_queue_skip (365e:203C)
void tick();          // brfcam_queue_tick: start / expire the head slot
void reset(int height);  // brfcam_reset (365e:225B)
void update();        // brfcam_update (365e:20D2)
int count();          // queued waypoints

} // namespace st::game::front::brfcam
