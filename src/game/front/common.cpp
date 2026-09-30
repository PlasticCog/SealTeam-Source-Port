#include "game/front/common.h"

#include "audio/audio.h"
#include "data/ealib.h"
#include "data/exeimage.h"
#include "engine/input_layer.h"
#include "engine/ticker.h"
#include "game/campaign.h"
#include "game/globals.h"
#include "game/screens.h"
#include "game/ui.h"
#include "gfx/font.h"
#include "gfx/gfx.h"
#include "platform/system.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace st::game::front {

s32 now() { return engine::ticker().time(); }

// ---------------------------------------------------------------- sprites

void SpriteSet::setName(const std::string& name) {
    name_ = name;
    cache_.clear();
}

const u8* SpriteSet::frame(int f, int r) {
    const int key = f * 64 + r;
    auto it = cache_.find(key);
    if (it == cache_.end()) {
        std::string up = name_;
        std::transform(up.begin(), up.end(), up.begin(), [](unsigned char c) { return char(std::toupper(c)); });
        char file[40];
        std::snprintf(file, sizeof file, "%sF%dR%d.RLE", up.c_str(), f + 1, r + 1);
        std::vector<u8> d;
        if (!resources().read(file, d) || d.size() < 4) d.clear();
        it = cache_.emplace(key, std::move(d)).first;
    }
    return it->second.empty() ? nullptr : it->second.data();
}

void drawSprite(const u8* img, int x, int y) {
    if (!img) return;
    const int w = SpriteSet::width(img), h = SpriteSet::height(img);
    if (w <= 0 || h <= 0) return;
    gfx().spriteScaled(x, y, w, h, img);
}

// ---------------------------------------------------------------- messages

namespace msg {

namespace {

constexpr int kQueue = 8;
constexpr u16 kOverflowText = 0x39a9;  // "Dlg Str Overflow"
constexpr u16 kSayPrefix = 0x370a;     // ": \""
constexpr u16 kSayNoName = 0x370e;     // "\""
constexpr u16 kSayEnd2 = 0x3710;       // "\""
constexpr u16 kSayEnd1 = 0x3712;       // "\""
constexpr u16 kSayBlank = 0x3714;      // " "

struct Entry {
    int style = -1;
    int sticky = 0;
    int duration = 0;
    s32 expiry = -1;
    int kind = 0;
    std::string text;
    int icon = 0;
};

Entry g_q[kQueue];
int g_head = 0, g_count = 0;
bool g_enabled = false;  // DS:39A6, initially 0
SpriteSet* g_medals = nullptr;

// Fonts DS:EEF4[kind]: prop, propbold, memo.
FontId kindFont(int kind) {
    switch (kind) {
    case 1: return FontId::Title;
    case 2: return FontId::Clipboard;
    default: return FontId::Dialog;
    }
}

int fontHeight() {
    const Font* f = gfx().font();
    return f ? f->height() : 0;
}

// msg_draw_signal_picture (1000:805E): the large medal picture (frame set 1
// of mdl) centred in a panel; forces a full redraw.
void drawPicture(int style) {
    if (!g_medals) return;
    const u8* img = g_medals->frame(1, style % 8);
    if (!img) return;
    const int w = SpriteSet::width(img), h = SpriteSet::height(img);
    const int x = 160 - (w >> 1);
    const int y = 100 - (h >> 1) + 16;
    uiDrawTextPanel("", x, y, w, h);
    gfx().spriteScaled(x, y, w, h, img);
    ui().redrawFrames = 2;
}

} // namespace

void setMedalSprites(SpriteSet* set) { g_medals = set; }

void reset() {
    g_head = g_count = 0;
    g_enabled = true;
}

bool add(const std::string& text, int kind, int duration, int style, int sticky) {
    if (!g_enabled || g_count >= kQueue) return false;
    Entry& e = g_q[(g_count + g_head) % kQueue];
    ++g_count;
    if (kind != 4 && style == 3) {
        e.icon = duration;
        e.duration = duration == -1 ? 0 : 0x300;
    } else {
        e.duration = duration;
    }
    e.style = style;
    e.sticky = sticky;
    e.expiry = -1;
    e.kind = kind;
    e.text = text.size() < 0x3f ? text : exe().dgString(kOverflowText);
    return true;
}

bool remove(int i) {
    if (i >= kQueue) return false;
    Entry& e = g_q[i];
    if (g_count != 0) {
        if (g_count == 1 && e.sticky) return false;
        --g_count;
    }
    e.style = -1;
    e.text.clear();
    if (i == g_head) g_head = g_count == 0 ? 0 : (g_head + 1) % kQueue;
    return true;
}

void clear() {
    while (g_count > 0)
        if (!remove(g_head)) break;
}

int count() { return g_count; }

void skip() {
    if (g_count <= 1) return;
    if (g_q[g_head].expiry == -1) return;
    remove(g_head);
    if (g_count > 0 && g_q[g_head].style != -1 && g_q[g_head].duration == 0) remove(g_head);
}

void tick() {
    Entry& e = g_q[g_head];
    if (e.style == -1) return;
    if (e.expiry == -1) {
        e.expiry = now() + e.duration;
        return;
    }
    if (e.expiry >= now()) return;
    remove(g_head);
    if (g_q[g_head].style != -1 && g_q[g_head].duration == 0) remove(g_head);
}

void draw() {
    const Entry& e = g_q[g_head];
    if (e.style == -1) return;
    Gfx& gx = gfx();
    if (e.kind == 3) {
        // HUD overlay line (the front end has no view mode: y as in view 0).
        const int y = 0x96;
        if (e.style == 3) {
            text4x6(y - 0x3c, 0x22 - 2 * int(e.text.size()), e.text);
        } else {
            text4x6Centered(y, e.text);
        }
        if (g_count <= 1 || e.style != 3) return;
        const Entry& n = g_q[(g_head + 1) % kQueue];
        if (n.style == -1) return;
        if (n.style == 3) text4x6(y - 0x11, (0x11 - int(n.text.size())) * 2, n.text);
        else text4x6Centered(y + 7, n.text);
        return;
    }
    if (e.kind == 4) {
        drawPicture(e.style);
        return;
    }
    fontSelect(kindFont(e.kind));
    const int y = e.style == 6 ? 0x11 : (0x62 - fontHeight()) * 2;
    if (e.style == 6) drawText(0xdc, y, e.text, 0x0f);
    else drawTextCentered(y, e.text, 0x0f);
    if (g_count <= 1) return;
    const Entry& n = g_q[(g_head + 1) % kQueue];
    if (n.kind == 4) {
        drawPicture(n.style);
        return;
    }
    if (n.style == 6) drawText(0xdc, y + fontHeight() + 1, n.text, 0x0f);
    else if (n.style != -1) drawTextCentered(y + fontHeight() + 1, n.text, 0x0f);
    (void)gx;
}

void drawQueue() {
    const Entry& e = g_q[g_head];
    if (e.style == -1) return;
    if (e.expiry > now()) draw();
    if (g_count <= 1 || e.style == 3) return;
    const int saved = g_head;
    for (int n = g_count - 1; n > 0; --n) {
        g_head = (g_head + 1) % kQueue;
        Entry& m = g_q[g_head];
        if (m.style != 3) continue;
        if (m.expiry == -1) m.expiry = now() + m.duration;
        else if (m.expiry > now()) draw();
        break;
    }
    g_head = saved;
}

int say(const std::string& name, const std::string& text) {
    std::string buf = name;
    if (!buf.empty()) buf += exe().dgString(kSayPrefix);
    else buf = exe().dgString(kSayNoName);
    const int plen = int(buf.size());
    const int tlen = int(text.size());
    if (tlen == 0 || plen + tlen >= 0xa0) return tlen;
    if (plen + tlen > 0x36) {
        int s = tlen / 2 - 8;
        if (s > 0x36 - plen) s = 0x36 - plen;
        if (s < 0) s = 0;
        while (s < tlen && text[size_t(s)] != ' ') ++s;
        add(buf + text.substr(0, size_t(s)), 1, 0x700, 0, 0);
        const std::string rest = size_t(s + 1) <= text.size() ? text.substr(size_t(s + 1)) : std::string();
        add(rest + exe().dgString(kSayEnd2), 1, 0, 0, 0);
    } else {
        add(buf + text + exe().dgString(kSayEnd1), 1, 0x700, 0, 0);
        add(exe().dgString(kSayBlank), 1, 0, 0, 0);
    }
    return tlen;
}

} // namespace msg

// ---------------------------------------------------------------- text

void text4x6(int y, int x, const std::string& s, u8 color) {
    Gfx& gx = gfx();
    gx.setTextColors(color, 0);
    gx.setTextOpaque(false);
    gx.draw4x6String(font4x6(), s, x & ~3, y);
}

void text4x6Centered(int y, const std::string& s, u8 color) {
    const int x = (gfx().clipCx() - 2 * int(s.size()) + 1) & ~3;
    text4x6(y, x, s, color);
}

void drawTextCentered(int y, const std::string& s, u8 color) {
    drawText(gfx().clipCx() - textWidth(s) / 2, y, s, color);
}

void drawTextEmboss(int x, int y, const std::string& s, u8 a, u8 b, u8 c) {
    // Two shadow passes (365e:9505): a at (x, y), b at (x+1, y+1), c at (x+2, y+2).
    drawTextShadow(x + 1, y + 1, s, c, a);
    drawTextShadow(x, y, s, b, a);
}

std::string padNumber(int v, int width) {
    std::string s = std::to_string(v < 0 ? -v : v);
    while (int(s.size()) < width) s.insert(s.begin(), '0');
    if (v < 0) s.insert(s.begin(), '-');
    return s;
}

// ---------------------------------------------------------------- mission data

std::string areaName(int world) {
    const std::string name = exe().dgStringPtr(u16(0x1cd0 + 2 * world));
    std::vector<u8> d;
    if (name.empty() || !resources().read(name + ".wd", d) || d.size() < 2) return {};
    const char* p = reinterpret_cast<const char*>(d.data() + 1);
    return std::string(p, strnlen(p, d.size() - 1));
}

int areaRegion(int world) { return exe().dgShort(u16(0x3140 + 2 * world)); }
int areaCampFrame(int world) { return exe().dgShort(u16(0x3178 + 2 * world)); }

namespace {
// ent_spawn_insertion_craft: method 8 = helicopter team (2), boats (1).
int craftType(int method) { return method == 8 ? 2 : 1; }
// ent_spawn_support_craft: 1 OV-10 (3), 8 UH-1 (2), boats (1).
int supportType(int kind) { return kind == 1 ? 3 : kind == 8 ? 2 : 1; }
} // namespace

int insertionTeamType() { return craftType(int(u16(campaign::mci().insertion_method))); }
// The second craft (or the first when both methods match) becomes the
// extraction group.
int extractionTeamType() { return craftType(int(u16(campaign::mci().extraction_method))); }
// The fire support unit, or the emergency helicopter group (always present).
int fireSupportTeamType() {
    const int fs = int(u16(campaign::mci().fire_support));
    return fs != 0 ? supportType(fs) : 2;
}
int breakContactTeamType() {
    const MciHeader& m = campaign::mci();
    int craft = m.insertion_method == m.extraction_method ? 1 : 2;
    if (u16(m.fire_support) != 0) ++craft;
    if (craft < 3 && u16(m.break_contact) != 0) return supportType(int(u16(m.break_contact)));
    return -1;
}

namespace tod {
namespace {
int g_hour = 0, g_minute = 0;
s32 g_minuteStart = 0;
} // namespace
void set(int hour, int minute) {
    g_hour = hour;
    g_minute = minute;
    g_minuteStart = now();
}
void add(int hours, int minutes) {
    s8 m = s8(g_minute + minutes);
    s8 h = s8(g_hour);
    if (m >= 60) {
        ++h;
        m = s8(m - 60);
    }
    if (m < 0) {
        --h;
        m = s8(m + 60);
    }
    h = s8(h + hours);
    if (h >= 24) h = s8(h - 24);
    if (h < 0) h = s8(h + 24);
    g_hour = h;
    g_minute = m;
}
void update() {
    if (now() - g_minuteStart < 0x3c00) return;
    g_minuteStart = now();
    ++g_minute;
    if (g_minute % 60 == 0) {
        ++g_hour;
        g_minute = 0;
        if (g_hour % 24 == 0) g_hour = 0;
    }
}
int hour() { return g_hour; }
int minute() { return g_minute; }
int paletteIndex(int* skyShade) {
    int pal = 1, shade = 0;
    if (g_hour > 4) {
        if (g_hour <= 8) {
            pal = 2;
            shade = (g_hour - 4) * 32 + (s8(g_minute) >> 1);
        } else if (g_hour < 18) {
            pal = 0;
            shade = 0x80;
        } else if (g_hour < 21) {
            pal = 2;
            shade = 0x80;
        }
    }
    if (skyShade) *skyShade = shade;
    return pal;
}
} // namespace tod

// ---------------------------------------------------------------- voice

namespace voice {

namespace {
constexpr u16 kNameTmpl = 0x3f8e;  // "new1a"
constexpr u16 kExt = 0x3f94;       // ".voc"
std::shared_ptr<const audio::PcmSound> g_a, g_b;
bool g_aPlaying = false, g_bStarted = false;
s32 g_timer = 0;
audio::SampleHandle g_handle = 0;

bool digitalOk() { return g().digitalAllowed; }

std::shared_ptr<const audio::PcmSound> loadOne(const std::string& name) {
    std::vector<u8> d;
    if (!resources().read(name, d)) return nullptr;
    return audio::loadVoc(d);
}
} // namespace

void free() {
    if (!digitalOk()) return;
    g_a.reset();
    g_b.reset();
}

void load(int year) {
    if (!digitalOk()) return;
    free();
    std::string name = exe().dgString(kNameTmpl);
    if (name.size() < 5) return;
    name[3] = char('1' + year);
    const std::string ext = exe().dgString(kExt);
    g_a = loadOne(name + ext);
    name[4] = 'b';
    g_b = loadOne(name + ext);
}

void play() {
    if (!digitalOk() || !g().sfxOn || !g_a) return;
    g_aPlaying = true;
    g_timer = 0;
    audio::stopSample(g_handle);
    g_handle = audio::playSample(g_a, 0x7f);
}

void stop() {
    if (!digitalOk()) return;
    g_aPlaying = false;
    if (g_a) audio::stopSample(g_handle);
}

void tick(int dt) {
    if (!g_aPlaying && !g_bStarted) return;
    g_timer += dt;
    if (!g_aPlaying) return;
    if (audio::samplePlaying(g_handle)) return;  // AIL status 3 = done
    g_aPlaying = false;
    g_bStarted = true;
    if (g_a) audio::stopSample(g_handle);
    if (g_b) g_handle = audio::playSample(g_b, 0x7f);
}

} // namespace voice

// ---------------------------------------------------------------- frames

bool renderBegin() {
    UiState& s = ui();
    if (s.redrawFrames == 0) {
        engine::ticker().frameLimitWait();
        cursorErase();
        return false;
    }
    --s.redrawFrames;
    picBlitToScreen();
    return true;
}

void renderEnd() {
    gfx().clipFull();
    engine::ticker().frameLimitWait();
    cursorDraw();
}

void mouseRecenter() {
    int dx, dy;
    engine::input().getMotion(dx, dy);
}

// ---------------------------------------------------------------- testing

namespace {

struct ScriptKey {
    double at = 0;
    int code = 0;  // BIOS form: ASCII or scan << 8
    std::string anchor;  // "": from the script start; else relative to rebaseKeyScript(anchor)
};
std::vector<ScriptKey> g_script;
size_t g_scriptPos = 0;
std::chrono::steady_clock::time_point g_scriptStart;
std::map<std::string, std::chrono::steady_clock::time_point> g_anchors;  // fired anchors

// Game controller tokens of the key script: PadA, PadLB, PadUp (d-pad),
// PadLT (trigger), PadLX- / PadRY+ (stick full deflection); "Pad~X"
// releases. Replayed as synthetic SDL events, so the mapping layer sees
// them like a real pad.
struct PadName { const char* name; int sdl; bool axis; int value; };
const PadName kPad[] = {
    {"A", SDL_GAMEPAD_BUTTON_SOUTH, false, 0}, {"B", SDL_GAMEPAD_BUTTON_EAST, false, 0},
    {"X", SDL_GAMEPAD_BUTTON_WEST, false, 0}, {"Y", SDL_GAMEPAD_BUTTON_NORTH, false, 0},
    {"LB", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, false, 0}, {"RB", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false, 0},
    {"LS", SDL_GAMEPAD_BUTTON_LEFT_STICK, false, 0}, {"RS", SDL_GAMEPAD_BUTTON_RIGHT_STICK, false, 0},
    {"Start", SDL_GAMEPAD_BUTTON_START, false, 0}, {"Back", SDL_GAMEPAD_BUTTON_BACK, false, 0},
    {"Up", SDL_GAMEPAD_BUTTON_DPAD_UP, false, 0}, {"Down", SDL_GAMEPAD_BUTTON_DPAD_DOWN, false, 0},
    {"Left", SDL_GAMEPAD_BUTTON_DPAD_LEFT, false, 0}, {"Right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT, false, 0},
    {"LT", SDL_GAMEPAD_AXIS_LEFT_TRIGGER, true, 32767}, {"RT", SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, true, 32767},
    {"LX+", SDL_GAMEPAD_AXIS_LEFTX, true, 32767}, {"LX-", SDL_GAMEPAD_AXIS_LEFTX, true, -32767},
    {"LY+", SDL_GAMEPAD_AXIS_LEFTY, true, 32767}, {"LY-", SDL_GAMEPAD_AXIS_LEFTY, true, -32767},
    {"RX+", SDL_GAMEPAD_AXIS_RIGHTX, true, 32767}, {"RX-", SDL_GAMEPAD_AXIS_RIGHTX, true, -32767},
    {"RY+", SDL_GAMEPAD_AXIS_RIGHTY, true, 32767}, {"RY-", SDL_GAMEPAD_AXIS_RIGHTY, true, -32767},
};

int scriptedPadIndex(const std::string& name) {
    for (size_t i = 0; i < sizeof(kPad) / sizeof(kPad[0]); ++i)
        if (name == kPad[i].name) return int(i);
    return -1;
}

void pushScriptedPadEvent(int code) {
    const PadName& p = kPad[code & 0xff];
    const bool release = (code & 0x100) != 0;
    sys().gamepad().setVirtual(true);
    SDL_Event ev{};
    if (p.axis) {
        ev.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
        ev.gaxis.axis = u8(p.sdl);
        ev.gaxis.value = s16(release ? 0 : p.value);
    } else {
        ev.type = release ? SDL_EVENT_GAMEPAD_BUTTON_UP : SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        ev.gbutton.button = u8(p.sdl);
        ev.gbutton.down = !release;
    }
    SDL_PushEvent(&ev);
}

int parseKey(const std::string& name) {
    struct Named { const char* name; int code; };
    static const Named kNamed[] = {
        {"Enter", 0x0d}, {"Esc", 0x1b}, {"Space", 0x20}, {"Backspace", 0x08}, {"Up", 0x4800},
        {"Down", 0x5000}, {"Left", 0x4b00}, {"Right", 0x4d00}, {"F10", 0x4400}, {"AltX", 0x2d00},
        {"AltE", 0x1200}, {"AltS", 0x1f00}, {"AltM", 0x3200}, {"Plus", '+'}, {"Minus", '-'}, {"Comma", ','},
        // Mission keys (docs/re/seg_19ac.md 5-6).
        {"F1", 0x3b00}, {"F2", 0x3c00}, {"F3", 0x3d00}, {"F4", 0x3e00}, {"F5", 0x3f00}, {"F6", 0x4000},
        {"F7", 0x4100}, {"F8", 0x4200}, {"F9", 0x4300}, {"Tab", 0x09}, {"ShiftTab", 0x0f00},
        {"Home", 0x4700}, {"End", 0x4f00}, {"PgUp", 0x4900}, {"PgDn", 0x5100}, {"Center", 0x4c00},
        {"CtrlLeft", 0x7300}, {"CtrlRight", 0x7400}, {"CtrlPgUp", 0x8400}, {"CtrlPgDn", 0x7600},
        {"AltT", 0x1400}, {"AltU", 0x1600}, {"AltI", 0x1700}, {"AltP", 0x1900}, {"AltD", 0x2000},
        {"AltN", 0x3100}, {"Equals", '='}, {"LBracket", '['}, {"RBracket", ']'},
    };
    for (const Named& n : kNamed)
        if (name == n.name) return n.code;
    if (name.rfind("Pad", 0) == 0) {  // PadA / Pad~A: game controller press / release (see kPad)
        std::string rest = name.substr(3);
        const bool release = !rest.empty() && rest[0] == '~';
        if (release) rest = rest.substr(1);
        const int i = scriptedPadIndex(rest);
        return i < 0 ? 0 : 0x20000 | (release ? 0x100 : 0) | i;
    }
    if (name.rfind("Move", 0) == 0) {  // MoveDX_DY: relative pointer motion in pixels
        const size_t us = name.find('_');
        if (us != std::string::npos) {
            const int dx = std::atoi(name.c_str() + 4), dy = std::atoi(name.c_str() + us + 1);
            return 0x10000 | (u8(s8(dx)) << 8) | u8(s8(dy));
        }
    }
    if (name.size() == 1) return u8(name[0]);
    return 0;
}

// Returns the next due scripted key (0 if none).
int dueKey() {
    if (g_scriptPos >= g_script.size()) return 0;
    const ScriptKey& k = g_script[g_scriptPos];
    auto base = g_scriptStart;
    if (!k.anchor.empty()) {
        const auto it = g_anchors.find(k.anchor);
        if (it == g_anchors.end()) return 0;  // anchor not reached yet
        base = it->second;
    }
    const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - base).count();
    if (k.at > t) return 0;
    const int code = g_script[g_scriptPos++].code;
    if (code & 0x20000) {  // scripted game controller event (SDL_PushEvent), not a key
        pushScriptedPadEvent(code);
        return 0;
    }
    if (code & 0x10000) {  // scripted pointer motion, not a key
        sys().input().addMotion(s8(u8(code >> 8)), s8(u8(code)));
        return 0;
    }
    return code;
}

} // namespace

void setKeyScript(const std::string& spec) {
    g_script.clear();
    g_scriptPos = 0;
    g_scriptStart = std::chrono::steady_clock::now();
    g_anchors.clear();
    double t = 0.6;
    size_t pos = 0;
    while (pos < spec.size()) {
        size_t end = spec.find(',', pos);
        if (end == std::string::npos) end = spec.size();
        std::string tok = spec.substr(pos, end - pos);
        pos = end + 1;
        if (tok.empty()) continue;
        double at = t + 0.4;
        const size_t atPos = tok.find('@', 1);
        std::string anchor;
        if (atPos != std::string::npos) {
            std::string when = tok.substr(atPos + 1);
            const size_t plus = when.find('+');
            if (plus != std::string::npos && !std::isdigit(u8(when[0]))) {
                anchor = when.substr(0, plus);  // e.g. mission+4
                when = when.substr(plus + 1);
            }
            at = std::atof(when.c_str());
            tok = tok.substr(0, atPos);
        }
        const int code = parseKey(tok);
        if (!code) {
            logWarn("key script: unknown key '%s'", tok.c_str());
            continue;
        }
        t = at;
        g_script.push_back(ScriptKey{at, code, anchor});
    }
}

int scriptedBiosKey() { return dueKey(); }

void rebaseKeyScript(const std::string& anchor) { g_anchors[anchor] = std::chrono::steady_clock::now(); }

int getKey() {
    int k = dueKey();
    if (k == 0) return engine::input().getKey();
    if (k >= 'A' && k <= 'Z') k |= 0x60;  // input_get_key folds letters to lower case
    return k;
}

} // namespace st::game::front
