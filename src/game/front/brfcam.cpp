// Map camera waypoint ring shared by the briefing and the debriefing.
#include "game/front/brfcam.h"

#include "engine/ticker.h"
#include "game/front/common.h"

namespace st::game::front::brfcam {

namespace {

struct Waypoint {        // BrfCamWaypoint, 16 bytes
    s16 flag = -1;       // +0 (-1 empty)
    s16 keep = 0;        // +2
    s16 duration = 0;    // +4
    s32 end = -1;        // +6 (-1 not started)
    s16 height = 0;      // +A
    const Vec3* target = nullptr;  // +C
};

Waypoint g_ring[8];
int g_head = 0, g_count = 0;
hooks::View g_dummy;
hooks::View* g_view = &g_dummy;

} // namespace

void attach(hooks::View* view) { g_view = view ? view : &g_dummy; }

bool push(const Vec3* target, int height, int duration, int flag, int keep) {
    if (g_count >= 8) return false;
    Waypoint& w = g_ring[(g_count + g_head) % 8];
    w.flag = s16(flag);
    w.keep = s16(keep);
    w.duration = s16(duration);
    w.end = -1;
    w.height = s16(height);
    w.target = target;
    ++g_count;
    if (g_count == 1) g_view->pos.y = s32(w.height) << 8;
    return true;
}

bool remove(int i) {
    if (i >= 8) return false;
    Waypoint& w = g_ring[i];
    if (g_count != 0) {
        if (g_count == 1 && w.keep != 0) return false;  // the last waypoint stays
        --g_count;
    }
    w.flag = -1;
    if (i == g_head) g_head = g_count == 0 ? 0 : (g_head + 1) % 8;
    return true;
}

void clear() {
    while (g_count > 0)
        if (!remove(g_head)) break;
}

void skip() {
    if (g_count > 1 && g_ring[g_head].end != -1) remove(g_head);
}

void tick() {
    Waypoint& w = g_ring[g_head];
    if (w.flag == -1) return;
    if (w.end == -1) w.end = now() + w.duration;
    else if (w.end < now()) remove(g_head);
}

void reset(int height) {
    // Only head and count are reset; stale slots keep their flags (their
    // targets are static positions of the briefing / debriefing).
    g_head = g_count = 0;
    g_view->pos.y = s32(height) << 8;
}

// x/z halve the distance per 256 ticks, the height moves 600 units per 256
// ticks; both snap within 0x3C00.
void update() {
    const Waypoint& w = g_ring[g_head];
    if (w.flag == -1 || !w.target) return;
    const s32 dt = engine::ticker().frameDt();
    auto approach = [dt](s32& cam, s32 target) {
        const s32 d = cam - target;
        if ((d < 0 ? -d : d) < 0x3c00) cam = target;
        else cam += ((-d) >> 1) * dt >> 8;
    };
    approach(g_view->pos.x, w.target->x);
    approach(g_view->pos.z, w.target->z);
    const s32 d = g_view->pos.y - (s32(w.height) << 8);
    if ((d < 0 ? -d : d) < 0x3c00) g_view->pos.y = s32(w.height) << 8;
    else g_view->pos.y += (d >= 0 ? -600 : 600) * dt;
}

int count() { return g_count; }

} // namespace st::game::front::brfcam
