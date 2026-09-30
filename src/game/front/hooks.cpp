// Placeholder implementations of the 3D hooks (see hooks.h): world set-up
// is skipped and the 3D viewports stay black.
// TODO(render): connect to src/render and src/game/mission when their APIs
// are available (original addresses in hooks.h).
#include "game/front/hooks.h"

#include "gfx/gfx.h"

namespace st::game::front::hooks {

namespace {
void blackViewport(const View& v) { gfx().fillRect(v.x, v.y, v.w, v.h, u16(Gfx::kSolid | 0x00)); }
} // namespace

void briefingWorldEnter() {}  // TODO(render): 1000:4580 mis_load_world, 365e:129A, 19ac:2B60, 365e:6DC4
void briefingWorldLeave() {}  // TODO(render): 1000:45A6
void drawBriefingView(const View& view) { blackViewport(view); }  // TODO(render): 365e:6E0C 3D part

void debriefWorldEnter(int, View&, Vec3&) {}  // TODO(render): 1000:4580, 365e:CC6E, 365e:B5CD
void debriefWorldLeave() {}  // TODO(render): 1000:45A6
void drawDebriefView(const View& view) { blackViewport(view); }  // TODO(render): 365e:CCA0 3D part

void campSceneLoad(int) {}    // TODO(render): 19ac:64D4 camp_load_scene
void campScenePlace(bool) {}  // TODO(render): 19ac:6945 / 19ac:6B07, view_set_mode(5/6), 1000:6A9C mis_load_sprites
void campSceneLeave() {}      // TODO(render): mis_free_sprites, mis_free_world
void campSceneTick() {}       // TODO(render): view_update_camera, spr_advance_anim_clocks, 2dbd:3F4C
void drawCampView(const View& view, int, int) { blackViewport(view); }  // TODO(render): 365e:D5B2 3D part

} // namespace st::game::front::hooks
