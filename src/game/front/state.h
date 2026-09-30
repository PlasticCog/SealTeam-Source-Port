// Front-end globals shared by several screens.
#pragma once

namespace st::game::front {

// g_recruit_slot (DS:3F7C): recruit screen year*4 + recruit (0..15); on the
// campaign screen the portrait slot of the point man (SE id - 51D9:0140[id]).
int& recruitSlot();

// Portrait slot of an SE id (51D9:0140 table).
int portraitSlotOf(int seId);

} // namespace st::game::front
