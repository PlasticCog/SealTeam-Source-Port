// Billboard sprites drawn by the type-4 primitive callback of the human,
// bush, jungle, muzzle and exgr models: segment 348e (soldier sprites) and
// the scenery/effect helpers of segment 1000 (docs/re/seg_2dbd.md 4-5,
// seg_1000.md 16).
#pragma once

#include "core/common.h"
#include "game/types.h"

#include <vector>

namespace st::render {

// Load the sprite data the callback needs: the scenery textures
// (Jungle.RLE, Bush.RLE, Grass.RLE, impcf1r1.RLE, mis_load_sprites) and the
// soldier/headgear sets (spr_load_soldier_sprites 348e:19B0), and register
// the callback with the renderer. Idempotent.
bool spritesLoad();
void spritesFree();

// One RLE image of a sprite set (.RLE bytes) and its .RLX record.
struct SpriteImage {
    std::vector<u8> rle;           // empty = missing
    game::RlxRecord rlx{};
    bool hasRlx = false;
    int w() const { return rle.size() >= 4 ? int(rle[0] | (rle[1] << 8)) : 0; }
    int h() const { return rle.size() >= 4 ? int(rle[2] | (rle[3] << 8)) : 0; }
};
// Frame f (0-based), rotation r (0..7) of a soldier set by name ("us", "cs"...)
// or headgear set ("band"...), nullptr if not loaded.
const SpriteImage* spriteFrame(const char* set, int f, int r);

// Hooks the mission code installs so the callback can find the unit that
// owns a body (19ac:622C) and draw effects that belong to projectiles
// (1000:461A prj_find_by_effect and the explosion/smoke/impact painters).
// grp_from_object_id (19ac:622C): the unit whose body is `body`, or nullptr.
using UnitLookup = game::Unit* (*)(const game::Obj3D* body);
// Effect objects owned by a projectile (muzzle flash, explosion, smoke,
// impact: 1000:461A and 1000:6DD5/6F4D); return true if `obj` was handled.
using EffectPainter = bool (*)(const game::Obj3D* obj, int x, int y, int scale, int rotation);
// spr_update_anim (348e:0302), the animation state machine owned by the
// mission code: 1 while a timed animation runs. Without it the renderer uses
// a read-only test (state != return_state and clock - start < duration).
using AnimUpdate = int (*)(game::Unit* unit);
void setUnitLookup(UnitLookup fn);
void setEffectPainter(EffectPainter fn);
void setAnimUpdate(AnimUpdate fn);
// The Point Man (team 0 member 0) and his team, for the muzzle-flash and
// headgear rules; set by the mission code.
void setPointMan(const game::Unit* pointMan);

// g_spr_rotation (DS:1284) of the last billboard (0..7, 0 = facing the viewer).
int spriteRotation();

// Scaled RLE helpers of segment 1000 (all (x, y) in current target pixels,
// scale 8.8 with 0x100 = 1:1).
void sprDrawCentered(int x, int y, int scale, const u8* rle);        // 1000:6C80
void sprDrawBottomCentered(int x, int y, int scale, const u8* rle);  // 1000:6CFD (detail > 0)
void sprDrawClippedBottom(int x, int y, int scale, const u8* rle);   // 1000:6D56 (detail >= 4)

}  // namespace st::render
