// Developer entry points of the renderer:
//   --view-world <mission 1..80> [x y z heading pitch] [enhanced [native|N [dist%|max]]]
//                [fill|4:3] [size WxH] [detail D] [hour H] [chase]
//   --view-model <table index 0..96> [enhanced [native|N]]
//   --bench-view <mission 1..80> [the same options] [frames N]
// The viewers render until Esc/Enter (arrows turn/move, PgUp/PgDn height,
// Home/End pitch, +/- speed, 1..6 detail level). Combine with --shot FILE
// --shot-after S. `size WxH` renders native-resolution frames at that size
// whatever the window (a 4K frame on a smaller monitor). --bench-view prints
// the average and worst frame times over a full turn of the camera.
#include "core/settings.h"
#include "data/ealib.h"
#include "engine/palette_fade.h"
#include "engine/ticker.h"
#include "game/devtools.h"
#include "gfx/font.h"
#include "gfx/gfx.h"
#include "gfx/image.h"
#include "platform/system.h"
#include "render/model.h"
#include "render/r3d.h"
#include "render/r3dmath.h"
#include "render/sky.h"
#include "render/sprites.h"
#include "render/veg.h"
#include "render/world.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace st::render {

namespace {

struct ViewOpts {
    bool gradient = true;  // tod_draw_sky_ground (off: flat world colours)
    bool rawCam = false;   // camraw x y z yaw pitch (world units, 1/8 degree)
    s32 raw[5]{};
    bool small = false;    // small camera viewport (0, 24, 320, 139) instead of the main one
    bool chase = false;    // chase camera of view mode 2 behind the insertion point
    int chaseAngle = -1;   // orbit angle (1/8 degree), default: behind, facing the objective
    int detail = 5;
    int hour = -1, minute = 0;
    bool hud = true;
    int frames = 180;      // --bench-view: frames of one full turn
};

bool parseInt(const std::string& s, int& v) {
    char* end = nullptr;
    const long r = std::strtol(s.c_str(), &end, 10);
    if (!end || *end || s.empty()) return false;
    v = int(r);
    return true;
}

// Parse trailing keyword options; returns the index of the first unparsed argument.
void parseOptions(const game::DevArgs& a, size_t i, ViewOpts& o) {
    settings().preset = Preset::Original;  // the saved preset does not apply to the viewer
    while (i < a.size()) {
        const std::string& k = a[i++];
        int v = 0;
        if (k == "enhanced") {
            settings().preset = Preset::Enhanced;
            if (i < a.size() && (a[i] == "native" || parseInt(a[i], v))) {
                settings().renderScale = a[i] == "native" ? kRenderScaleNative : v;
                ++i;
                if (i < a.size() && (a[i] == "max" || parseInt(a[i], v))) {
                    settings().drawDistancePct = a[i] == "max" ? kDrawDistanceMax : v;
                    ++i;
                }
            }
        } else if (k == "original") {
            settings().preset = Preset::Original;
        } else if (k == "fill") {
            settings().wideView = true;
        } else if (k == "4:3") {
            settings().wideView = false;
        } else if (k == "size" && i < a.size()) {
            int w = 0, h = 0;
            if (std::sscanf(a[i].c_str(), "%dx%d", &w, &h) == 2 && w >= 320 && h >= 200)
                sys().video().setOutputSizeOverride(w, h);
            ++i;
        } else if (k == "frames" && i < a.size() && parseInt(a[i], v)) {
            o.frames = std::max(1, v);
            ++i;
        } else if (k == "detail" && i < a.size() && parseInt(a[i], v)) {
            o.detail = v;
            ++i;
        } else if (k == "hour" && i < a.size() && parseInt(a[i], v)) {
            o.hour = v;
            ++i;
        } else if (k == "nohud") {
            o.hud = false;
        } else if (k == "small") {
            o.small = true;
        } else if (k == "chase") {
            o.chase = true;
            o.small = true;
            if (i < a.size() && parseInt(a[i], v)) {
                o.chaseAngle = angleWrap(v);
                ++i;
            }
        } else if (k == "camraw" && i + 5 <= a.size()) {
            o.rawCam = true;
            for (int j = 0; j < 5; ++j) o.raw[j] = s32(std::strtol(a[i + size_t(j)].c_str(), nullptr, 0));
            i += 5;
        } else if (k.rfind("--", 0) == 0) {
            break;  // next port option (--shot ...)
        }
    }
}

bool initRenderer() {
    if (!modelsLoad() || !skyLoad()) return false;
    spritesLoad();
    return true;
}

void setPalette(int hour, int minute) {
    const int idx = todPaletteIndex(hour, minute);
    Palette pal{};
    if (loadPalette(todPaletteName(idx), pal)) {
        engine::paletteFade().setPalette(pal);
        engine::paletteFade().setLevel(0);
    }
}

void presentFrame() {
    Gfx& gx = gfx();
    gx.clipFull();
    gx.flip(true, false);
    engine::ticker().frameLimitReset();
    sys().pump();
}

// Interactive render loop. Returns when Esc/Enter is pressed.
// A stand-in unit for the human model so the soldier sprite path can be
// inspected without the mission code: --view-model 51 [stand|walk|run|crouch|prone|dead]
game::Unit g_fakeUnit{};
game::Anim g_fakeAnim{};
game::Mover g_fakeMover{};
game::Status g_fakeStatus{};
game::Team g_fakeTeam{};
game::Obj3D* g_fakeBody = nullptr;

game::Unit* fakeLookup(const game::Obj3D* body) { return body == g_fakeBody ? &g_fakeUnit : nullptr; }

void setupFakeUnit(game::Obj3D* body, const std::string& pose) {
    g_fakeBody = body;
    g_fakeUnit = game::Unit{};
    g_fakeUnit.body = body;
    g_fakeUnit.anim = &g_fakeAnim;
    g_fakeUnit.mover = &g_fakeMover;
    g_fakeUnit.status = &g_fakeStatus;
    g_fakeUnit.team = &g_fakeTeam;
    g_fakeTeam.type = game::TeamType::VietCong;
    g_fakeStatus.size = 60;
    g_fakeAnim = game::Anim{};
    g_fakeMover = game::Mover{};
    game::AnimState st = game::AnimState::Upright;
    int posture = 0, mode = 0;
    if (pose == "walk") mode = 1;
    else if (pose == "run") mode = 2;
    else if (pose == "crouch") st = game::AnimState::Crouch, posture = 1;
    else if (pose == "crawl") st = game::AnimState::Prone, posture = 2, mode = 1;
    else if (pose == "prone") st = game::AnimState::Prone, posture = 2;
    else if (pose == "dead") st = game::AnimState::Dead, posture = 3;
    g_fakeAnim.state = g_fakeAnim.return_state = st;
    g_fakeAnim.posture = g_fakeAnim.posture_copy = s16(posture);
    g_fakeMover.move_mode = game::MoveMode(mode);
    g_fakeMover.posture = game::Posture(posture > 2 ? 2 : posture);
    setUnitLookup(fakeLookup);
}

void runView(game::Camera& cam, ViewOpts& o, const std::string& title, Obj3D* spin = nullptr) {
    Font f4;
    const bool haveFont = f4.load("4x6.fnt");
    Gfx& gx = gfx();
    gx.setDrawPage(1);
    gx.setDisplayPage(0);
    auto& clock = engine::ticker();
    clock.resetClock();
    sys().input().flushKeys();
    int speed = 64;
    RenderContext& ctx = renderContext();
    for (;;) {
        clock.updateGameTime();
        ctx.frameTicks = clock.frameDt();
        ctx.time = clock.time();
        ctx.detail = o.detail;
        Input& in = sys().input();
        bool quit = false;
        while (in.keyAvailable()) {
            const u16 k = in.readKey();
            const u8 ascii = u8(k & 0xFF);
            if (ascii == 0x1B || ascii == 0x0D || ascii == 'q') quit = true;
            else if (ascii == '+' || ascii == '=') speed = std::min(speed * 2, 0x4000);
            else if (ascii == '-') speed = std::max(speed / 2, 4);
            else if (ascii >= '1' && ascii <= '6') o.detail = ascii - '1';
            else if (ascii == 'h') o.hud = !o.hud;
            else if (ascii == 'r' && spin) spin->heading = s16(angleWrap(spin->heading + 8 * 15));
            else if (ascii == 'e') {
                settings().preset = settings().original() ? Preset::Enhanced : Preset::Original;
            }
        }
        if (quit) break;
        const int step = speed * clock.frameDt() / 4 + 1;
        if (in.keyDown(sc::Left)) cam.yaw = s16(angleWrap(cam.yaw + 4 * clock.frameDt()));
        if (in.keyDown(sc::Right)) cam.yaw = s16(angleWrap(cam.yaw - 4 * clock.frameDt()));
        if (in.keyDown(sc::Up) || in.keyDown(sc::Down)) {
            const int dir = in.keyDown(sc::Up) ? 1 : -1;
            // heading 0 looks along +z, 720 along -x
            cam.pos.x -= dir * ((s32(mathSin(cam.yaw)) * step) >> 6);
            cam.pos.z += dir * ((s32(mathCos(cam.yaw)) * step) >> 6);
        }
        if (in.keyDown(sc::PgUp)) cam.pos.y += step * 16;
        if (in.keyDown(sc::PgDn)) cam.pos.y = std::max<s32>(0, cam.pos.y - step * 16);
        if (in.keyDown(sc::Home)) cam.pitch = s16(angleWrap(cam.pitch + 2 * clock.frameDt()));
        if (in.keyDown(sc::End)) cam.pitch = s16(angleWrap(cam.pitch - 2 * clock.frameDt()));

        gx.clipFull();
        gx.clear(0);
        gx.setClip(cam.rect_x, cam.rect_y, cam.rect_w, cam.rect_h);
        vegUpdate(cam, false);
        if (o.gradient) todDrawSkyGround(cam, o.detail, clock.time(), o.hour);
        g_fakeAnim.clock += clock.frameDt();
        const int r = renderView(cam, true);
        if (r) std::fprintf(stderr, "renderView status %d\n", r);
        if (o.hud && haveFont) {
            gx.clipFull();
            gx.setTextColors(15, 0);
            const RenderStats& st = renderStats();
            char line[160];
            std::snprintf(line, sizeof line, "%s  %s %s %s", title.c_str(),
                          settings().original() ? "ORIGINAL" : "ENHANCED",
                          settings().original() ? "" : renderScaleName(settings().effectiveRenderScale()),
                          settings().original() ? "" : drawDistanceName(settings().effectiveDrawDistancePct()));
            gx.draw4x6String(f4, line, 2, 1);
            std::snprintf(line, sizeof line, "X %d Y %d Z %d H %d P %d  OBJ %d/%d  DET %d",
                          int(cam.pos.x >> 8), int(cam.pos.y >> 8), int(cam.pos.z >> 8), cam.yaw >> 3,
                          cam.pitch >> 3, st.drawn, st.tested, o.detail);
            gx.draw4x6String(f4, line, 2, 181);
        }
        presentFrame();
    }
}

// Load mission `mission`'s world and set the camera up; returns the world
// index or -1.
int setupWorldView(const game::DevArgs& a, int mission, ViewOpts& o, game::Camera& cam) {
    size_t i = 1;
    int user[5];
    bool haveCam = false;
    if (a.size() >= 6) {
        haveCam = true;
        for (int k = 0; k < 5; ++k)
            if (!parseInt(a[1 + size_t(k)], user[k])) haveCam = false;
        if (haveCam) i = 6;
    }
    parseOptions(a, i, o);
    if (!initRenderer()) return -1;

    const int y = (mission - 1) / 20 + 1, n = (mission - 1) % 20 + 1;
    char name[32];
    std::snprintf(name, sizeof name, "c%dm%02d.mci", y, n);
    std::vector<u8> mci;
    if (!resources().read(name, mci) || mci.size() < 0x176) {
        std::fprintf(stderr, "cannot read %s\n", name);
        return -1;
    }
    const int hour = rd16(&mci[0]), minute = rd16(&mci[2]), worldIdx = rd16(&mci[4]);
    const s32 insX = s32(rd32(&mci[0x0A])), insZ = s32(rd32(&mci[0x12]));
    const s32 objX = s32(rd32(&mci[0x78])), objZ = s32(rd32(&mci[0x80]));
    if (o.hour < 0) {
        o.hour = hour;
        o.minute = minute;
    }
    renderContext().todHour = o.hour;

    worldBegin();
    if (!wldLoad(worldIdx)) return -1;
    vegCreatePools();
    worldEnd();
    logInfo("view-world: mission %d %s, world %d %s (%s), %d objects", mission, name, worldIdx,
            worldName(worldIdx).c_str(), worldDesc().area_name, objCount());

    setPalette(o.hour, o.minute);
    modelsSetTimeOfDay(o.hour);

    cam = game::Camera{};
    cam.rect_x = 0;
    cam.rect_y = 8;
    cam.rect_w = 320;
    cam.rect_h = 171;
    cam.zoom = 8;
    if (o.small) {
        cam.rect_y = 24;
        cam.rect_h = 139;
    }
    if (o.rawCam) {
        cam.pos = game::Vec3{o.raw[0], o.raw[1], o.raw[2]};
        cam.yaw = s16(o.raw[3]);
        cam.pitch = s16(o.raw[4]);
    } else if (o.chase) {
        // View mode 2 with the team camera defaults (distance 0x60, angle 0):
        // 0x60 units north of the leader, looking at him, 0x1800 up.
        // Orbit angle a: the camera sits at leader + d*(-sin a, cos a).
        const int a = o.chaseAngle >= 0 ? o.chaseAngle : angleWrap(mathHeading(insX, insZ, objX, objZ) + 1440);
        const s32 d = 0x60;
        const s32 sfx = s32(mathSin(a)) << 2, cfx = s32(mathCos(a)) << 2;  // math_sin_fx / cos_fx (16.16)
        cam.pos = game::Vec3{s32(u32(insX) - u32(s32((long long)d * sfx >> 8))), 0x1800,
                             s32(u32(insZ) + u32(s32((long long)d * cfx >> 8)))};
        cam.yaw = s16((mathHeading(cam.pos.x, cam.pos.z, insX, insZ) >> 3) << 3);
        cam.pitch = 0;
    } else if (haveCam) {
        cam.pos = game::Vec3{s32(u32(user[0]) << 8), s32(u32(user[1]) << 8), s32(u32(user[2]) << 8)};
        cam.yaw = s16(angleWrap(user[3] * 8));
        cam.pitch = s16(angleWrap(user[4] * 8));
    } else {
        cam.pos = game::Vec3{insX, 0x1800, insZ};
        cam.yaw = s16(mathHeading(insX, insZ, objX, objZ));
        cam.pitch = 0;
    }
    renderContext().reinsertPoint = game::Vec3{insX, 0, insZ};
    vegReset();
    return worldIdx;
}

int viewWorld(const game::DevArgs& a) {
    int mission = 1;
    if (a.empty() || !parseInt(a[0], mission) || mission < 1 || mission > 80) {
        std::fprintf(stderr, "usage: --view-world <mission 1..80> [x y z heading pitch] [enhanced [native|N [dist%%|max]]]\n");
        return 2;
    }
    ViewOpts o;
    game::Camera cam{};
    const int worldIdx = setupWorldView(a, mission, o, cam);
    if (worldIdx < 0) return 1;
    runView(cam, o, worldName(worldIdx));
    worldFree();
    wldFree();
    sys().video().dropHiResLayers();
    return 0;
}

// --bench-view: render `frames` frames while the camera makes a full turn,
// without the frame limiter, and print the frame times.
int benchView(const game::DevArgs& a) {
    int mission = 3;
    if (a.empty() || !parseInt(a[0], mission) || mission < 1 || mission > 80) {
        std::fprintf(stderr, "usage: --bench-view <mission 1..80> [enhanced [native|N [dist%%|max]]] [size WxH] [frames N]\n");
        return 2;
    }
    ViewOpts o;
    game::Camera cam{};
    const int worldIdx = setupWorldView(a, mission, o, cam);
    if (worldIdx < 0) return 1;
    Gfx& gx = gfx();
    gx.setDrawPage(1);
    gx.setDisplayPage(0);
    auto& clock = engine::ticker();
    clock.setFrameLimit(false);
    clock.resetClock();
    RenderContext& ctx = renderContext();
    ctx.detail = o.detail;
    const Timer& timer = sys().timer();
    double renderSum = 0, renderMax = 0, presentSum = 0, presentMax = 0;
    int drawnSum = 0, drawnMax = 0;
    const s16 yaw0 = cam.yaw;
    // Warm-up frame (layer allocation, view list) is not counted.
    for (int f = -1; f < o.frames; ++f) {
        if (f == 0) sys().video().resetPresentStats();
        clock.updateGameTime();
        ctx.frameTicks = clock.frameDt();
        ctx.time = clock.time();
        cam.yaw = s16(angleWrap(yaw0 + (f < 0 ? 0 : kAngleFull * f / o.frames)));
        const double t0 = timer.seconds();
        gx.clipFull();
        gx.clear(0);
        gx.setClip(cam.rect_x, cam.rect_y, cam.rect_w, cam.rect_h);
        vegUpdate(cam, false);
        if (o.gradient) todDrawSkyGround(cam, o.detail, clock.time(), o.hour);
        renderView(cam, false);
        const double t1 = timer.seconds();
        presentFrame();
        const double t2 = timer.seconds();
        if (f < 0) continue;
        renderSum += t1 - t0;
        renderMax = std::max(renderMax, t1 - t0);
        presentSum += t2 - t1;
        presentMax = std::max(presentMax, t2 - t1);
        drawnSum += renderStats().drawn;
        drawnMax = std::max(drawnMax, renderStats().drawn);
    }
    int W = 320, H = 200;
    if (const HiResLayer* l = sys().video().hiResLayer(gx.displayPage())) {
        W = l->w;
        H = l->h;
    }
    const double n = o.frames;
    std::printf("bench-view: mission %d (%s), %s %s, distance %s, %dx%d, %d frames\n", mission,
                worldName(worldIdx).c_str(), settings().original() ? "Original" : "Enhanced",
                settings().original() ? "" : renderScaleName(settings().effectiveRenderScale()),
                drawDistanceName(settings().effectiveDrawDistancePct()), W, H, o.frames);
    std::printf("  objects drawn: avg %d, max %d\n", int(drawnSum / n), drawnMax);
    std::printf("  render:  avg %.2f ms, max %.2f ms\n", 1000 * renderSum / n, 1000 * renderMax);
    std::printf("  present: avg %.2f ms, max %.2f ms\n", 1000 * presentSum / n, 1000 * presentMax);
    const Video::PresentStats& ps = sys().video().presentStats();
    if (ps.frames > 0)
        std::printf("    lock: avg %.2f ms, max %.2f ms; compose+convert: avg %.2f ms, max %.2f ms; "
                    "upload+present: avg %.2f ms, max %.2f ms\n",
                    1000 * ps.lock / ps.frames, 1000 * ps.lockMax, 1000 * ps.compose / ps.frames,
                    1000 * ps.composeMax, 1000 * ps.upload / ps.frames, 1000 * ps.uploadMax);
    std::printf("  frame:   avg %.2f ms (%.1f fps), max %.2f ms\n", 1000 * (renderSum + presentSum) / n,
                n / (renderSum + presentSum), 1000 * (renderMax + presentMax));
    std::fflush(stdout);
    worldFree();
    wldFree();
    sys().video().dropHiResLayers();
    return 0;
}

int viewModel(const game::DevArgs& a) {
    int id = 0;
    if (a.empty() || !parseInt(a[0], id) || id < 0 || id >= kModelCount) {
        std::fprintf(stderr, "usage: --view-model <0..96> [pose] [enhanced [scale]]\n");
        return 2;
    }
    ViewOpts o;
    o.hour = 12;
    size_t i = 1;
    std::string pose = "stand";
    if (i < a.size() && a[i].rfind("--", 0) != 0 && a[i] != "enhanced" && a[i] != "original") pose = a[i++];
    parseOptions(a, i, o);
    if (!initRenderer()) return 1;
    setPalette(12, 0);
    worldBegin();
    const game::ModelDesc* d = modelByIndex(id);
    Obj3D* obj = worldAddObject(d, 12000, 12000, 0x0001);
    obj->heading = 0;
    worldEnd();
    if (modelDsOffset(d) == 0x862A) setupFakeUnit(obj, pose);
    world().groundColor = 0x2A;
    world().skyColor = 0x88;
    game::Camera cam{};
    cam.rect_x = 0;
    cam.rect_y = 8;
    cam.rect_w = 320;
    cam.rect_h = 171;
    cam.zoom = 8;
    // Size from the largest vertex coordinate (2^W model units); the
    // bounding radii of the descriptors are generous.
    const ModelInfo* mi = modelInfo(d);
    const int w = mi && mi->lod[0].window >= 0 ? mi->lod[0].window + 1 : 4;
    const s32 r = std::max<s32>(s32(1) << std::max(0, w + 8 + d->scale_shift), 0x800);
    const s32 dist = r * 3;
    cam.pos = game::Vec3{obj->pos.x, obj->pos.y + r, obj->pos.z - dist};
    cam.yaw = 0;
    cam.pitch = s16(angleWrap(-mathAtan2(dist >> 8, r >> 8)));
    o.gradient = false;
    renderContext().viewMode = 2;
    runView(cam, o, std::string("model ") + std::to_string(id) + " " + modelName(d), obj);
    setUnitLookup(nullptr);
    worldFree();
    sys().video().dropHiResLayers();
    return 0;
}

}  // namespace

const game::DevCommand kViewWorld("--view-world",
                                  "render a mission's world: <1..80> [x y z heading pitch] "
                                  "[enhanced [native|N [dist%|max]]] [fill|4:3] [size WxH] [detail D] [hour H]",
                                  viewWorld);
const game::DevCommand kViewModel("--view-model", "render one model of the table: <0..96> [enhanced [native|N]]",
                                  viewModel);
const game::DevCommand kBenchView("--bench-view",
                                  "frame times of a mission's world: <1..80> [enhanced [native|N [dist%|max]]] "
                                  "[fill|4:3] [size WxH] [frames N]",
                                  benchView);

}  // namespace st::render
