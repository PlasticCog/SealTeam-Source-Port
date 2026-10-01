#include "game/ui.h"

#include "data/exeimage.h"
#include "engine/controller.h"
#include "engine/input_layer.h"
#include "engine/palette_fade.h"
#include "engine/ticker.h"
#include "game/campaign.h"
#include "game/front/common.h"
#include "game/screens.h"
#include "gfx/font.h"
#include "gfx/gfx.h"
#include "gfx/image.h"
#include "platform/system.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace st::game {

namespace {

constexpr int kButtonRecord = 16;

u16 colour(u8 index) { return u16(0xff00 | index); }

int charWidth(char c) {
    const Font* f = gfx().font();
    return f ? f->glyphWidth(u8(c)) : 0;
}

bool rectHasPoint(int rx, int ry, int rw, int rh, int x, int y) {
    return rx <= x && x < rx + rw && ry <= y && y < ry + rh;
}

// Cursor pictures (16x14) with their masks converted to the driver's
// per-plane layout, and one 20x19 save-under area per video page.
struct CursorShape {
    Bitmap pic;
    std::vector<u8> mask;  // empty: draw unmasked
};
CursorShape g_arrow, g_wait, g_sight;
Bitmap g_saveUnder[2];
int g_savedX[2] = {-1, -1}, g_savedY[2] = {-1, -1};
int g_cursorPage = 0;
bool g_cursorWait = false, g_cursorSight = false;

u8 rev4(u8 n) { return u8(((n & 1) << 3) | ((n & 2) << 1) | ((n & 4) >> 1) | ((n & 8) >> 3)); }

void loadCursorShape(const char* pic, const char* msk, CursorShape& out) {
    Image img;
    if (!loadPicture(pic, img)) fatal("Couldn't load %s", pic);
    gfx().createBitmap(out.pic, img.w, img.h);
    for (int y = 0; y < img.h; ++y) std::memcpy(out.pic.row(y), img.row(y), size_t(img.w));
    Mask m;
    out.mask.clear();
    if (loadMask(msk, img.w, img.h, m)) {
        // .msk rows are MSB-first; mode 6 remaps each nibble so bit p = plane p.
        for (u8 b : m.bits) out.mask.push_back(u8((rev4(b >> 4) << 4) | rev4(b & 0xf)));
    }
}

} // namespace

UiState& ui() {
    static UiState instance;
    return instance;
}

ButtonList loadButtonList(u16 seg, u16 labelTable, u16 off) {
    ButtonList list;
    for (int i = 0;; ++i) {
        const u8* r = exe().at(seg, u16(off + i * kButtonRecord));
        if (!r) fatal("button table %04x missing from st.exe", seg);
        Button b;
        b.key = rd16(r + 8);
        if (b.key == 0) break;
        b.x = rds16(r);
        b.y = rds16(r + 2);
        b.w = rds16(r + 4);
        b.h = rds16(r + 6);
        b.hotIndex = s8(r[10]);
        b.flags = r[11];
        if (labelTable != 0) b.label = exe().dgStringPtr(u16(labelTable + 2 * i));
        list.push_back(std::move(b));
    }
    return list;
}

// Clamp to x 4..308 and y 5..186. The original tests the lower y bound with
// "dy < 5" instead of "dy <= 0", so small downward moves are applied twice
// (and can pass 186 by a few pixels); kept for fidelity.
void uiCursorMove(int dx, int dy) {
    UiState& s = ui();
    if (dx >= 0) s.cursorX = (s.cursorX + dx < 308) ? s.cursorX + dx : 308;
    if (dx <= 0) s.cursorX = (s.cursorX + dx < 5) ? 4 : s.cursorX + dx;
    if (dy >= 0) s.cursorY = (s.cursorY + dy < 186) ? s.cursorY + dy : 186;
    if (dy < 5) s.cursorY = (s.cursorY + dy < 6) ? 5 : s.cursorY + dy;
}

// Index of the enabled clickable button under the cursor, -1 if none. The
// cursor turns into the gun sight while it is over a button. During the
// release countdown the focus is kept.
int uiButtonHitTest(const ButtonList& list) {
    UiState& s = ui();
    cursorSetSight(true);
    if (s.releaseCount != 0) return s.focus;
    for (size_t i = 0; i < list.size(); ++i) {
        const Button& b = list[i];
        if ((b.flags & btn::Disabled) || !(b.flags & btn::Clickable)) continue;
        if (rectHasPoint(b.x, b.y, b.w, b.h - 2, s.cursorX, s.cursorY)) return int(i);
    }
    cursorSetSight(false);
    return -1;
}

bool uiPointerUpdate(int dx, int dy, const ButtonList& list) {
    UiState& s = ui();
    uiCursorMove(dx, dy);
    const int hit = uiButtonHitTest(list);
    const bool leftPressed = s.focus != -1 && s.pressed && hit == -1;
    s.focus = hit;
    return leftPressed;
}

// Called with the focused button's own key; returns true on the first press.
bool uiButtonPress(int key) {
    UiState& s = ui();
    if (s.focus == -1 || key == 0x0d) return false;
    const bool first = !s.pressed;
    s.pressed = true;
    return first;
}

// Called with every input key: any non-Enter input releases a pressed
// button and starts the 3-frame countdown after which it activates.
void uiButtonRelease(int key) {
    UiState& s = ui();
    if (s.focus == -1) {
        s.pressed = false;
        s.releaseCount = 0;
    } else if (key != 0x0d) {
        if (s.pressed) s.releaseCount = 3;
        s.pressed = false;
    }
}

void uiCursorToButton(const Button& b) {
    UiState& s = ui();
    s.cursorX = b.x + (b.w >> 1);
    s.cursorY = b.y + b.h - 4;
    s.releaseCount = 0;
    s.pressed = false;
    uiCursorMove(0, 0);
    cursorSetSight(true);
}

void uiDrawButtons(const ButtonList& list) {
    Gfx& gx = gfx();
    const UiState& s = ui();
    fontSelect(FontId::Title);
    for (size_t i = 0; i < list.size(); ++i) {
        const Button& b = list[i];
        if (b.flags & btn::Hidden) continue;
        const int x = b.x, y = b.y, w = b.w, h = b.h;
        const int off = std::max(0, (w - textWidth(b.label)) >> 1);
        const int cw = b.label.empty() ? 0 : charWidth(b.label[0]);
        if (b.flags & btn::Framed) {
            gx.rect(x - 1, y - 1, w + 2, h + 2, colour(0x16));
            gx.rect(x - 2, y - 1, w + 4, h + 2, colour(0x16));
            gx.line(x - 2, y - 2, x + w + 1, y - 2, colour(0x10));
            gx.line(x - 2, y + h + 1, x + w + 1, y + h + 1, colour(0x1a));
        }
        const bool down = (s.focus == int(i) && (s.pressed || s.releaseCount != 0)) || (b.flags & btn::Highlighted);
        if (down) {
            gx.rect(x, y, w, h, colour(0x18));
            gx.rect(x, y, w, h, colour(0x18));
            gx.rect(x + 1, y + 1, w - 2, h - 2, colour(0x18));
            gx.fillRect(x + 2, y + 2, w - 4, h - 4, colour(0x16));
            gx.line(x + 1, y + h - 2, x + w - 2, y + h - 2, colour(0x12));
            gx.line(x, y + h - 1, x + w - 1, y + h - 1, colour(0x10));
            drawTextShadow(x + off + 2, y + 4, b.label, 0x0f, 0x00);
            if (b.hotIndex != -1) {
                gx.line(x + off + 3, y + 12, x + off + cw + 1, y + 12, colour(0x0f));
                gx.line(x + off + 2, y + 11, x + off + cw, y + 11, colour(0x00));
            }
        } else {
            gx.rect(x, y, w, h, colour(0x18));
            gx.rect(x + 1, y + 1, w - 2, h - 2, colour(0x16));
            gx.fillRect(x + 2, y + 2, w - 4, h - 4, colour(0x14));
            gx.line(x + 1, y + 1, x + w - 2, y + 1, colour(0x12));
            gx.line(x, y, x + w - 1, y, colour(0x10));
            drawTextShadow(x + off + 2, y + 3, b.label, 0x0f, 0x00);
            if (b.hotIndex != -1) {
                gx.line(x + off + 3, y + 13, x + off + cw + 1, y + 13, colour(0x0f));
                gx.line(x + off + 2, y + 12, x + off + cw, y + 12, colour(0x00));
            }
            if (b.flags & btn::Disabled) gx.fillRect(x + 2, y + 2, w - 4, h - 4, 0x5a14);
        }
    }
}

void uiDrawTitleTab(const std::string& caption, int x, int y) {
    Gfx& gx = gfx();
    fontSelect(FontId::Title);
    const int w = textWidth(caption);
    gx.line(x - 48, y + 6, x + w + 48, y + 6, colour(0x16));
    gx.line(x - 48, y + 7, x + w + 48, y + 7, colour(0x14));
    gx.line(x - 47, y + 7, x + w + 46, y + 7, colour(0x12));
    gx.line(x - 48, y + 8, x + w + 48, y + 8, colour(0x10));
    gx.rect(x, y, w, 16, colour(0x14));
    gx.rect(x + 1, y + 1, w - 2, 14, colour(0x16));
    gx.fillRect(x + 2, y + 2, w - 4, 12, colour(0x14));
    gx.line(x + 1, y + 14, x + w - 2, y + 14, colour(0x12));
    gx.line(x, y + 15, x + w - 1, y + 15, colour(0x10));
    drawTextShadow(x + 2, y + 4, caption, 0x00, 0x0f);
}

bool uiMenuArrowKeys(int key, const ButtonList& list) {
    int dx = 0, dy = 0;
    switch (key) {
    case engine::key::Up: dy = -5; break;
    case engine::key::Left: dx = -8; break;
    case engine::key::Right: dx = 8; break;
    case engine::key::Down: dy = 5; break;
    default: return false;
    }
    uiPointerUpdate(dx, dy, list);
    return true;
}

// ---------------------------------------------------------------- panels and dialogs

void uiDrawTextPanel(const char* text, int x, int y, int w, int h) {
    Gfx& gx = gfx();
    fontSelect(FontId::Dialog);
    gx.rect(x - 4, y - 3, w + 8, h + 6, colour(0x18));
    gx.rect(x - 3, y - 2, w + 6, h + 4, colour(0x16));
    gx.line(x - 3, y - 2, x + w + 2, y - 2, colour(0x12));
    gx.line(x - 4, y - 3, x + w + 3, y - 3, colour(0x10));
    if (text) {
        gx.fillRect(x - 2, y - 1, w + 4, h + 2, colour(0x14));
        drawTextShadow(x + 6, y + 4, text, 0x00, 0x0f);  // white text, black shadow
    } else {
        gx.rect(x - 2, y - 1, w + 4, h + 2, colour(0x14));
    }
}

int pollBiosKey() {
    sys().pump();
    if (const int k = front::scriptedBiosKey()) return k;
    engine::controller().pump(engine::input().mode());  // pad buttons into the BIOS queue
    Input& in = sys().input();
    if (!in.keyAvailable()) return 0;
    const u16 k = in.readKey();
    return (k & 0xff) ? (k & 0xff) : k;
}

namespace {

// The dialog's cursor glyph is the string at DS:3A67 ("_"); DS:3A66 is an
// empty string that clears the edit buffer.
constexpr u16 kDialogCursor = 0x3a67;

void finishEdit(int cursorX, int editX, int editY, int lineH) {
    engine::ticker().frameLimitWait();
    gfx().fillRect(cursorX + editX + 6, editY + 2, 8, lineH - 4, colour(0x16));
    present();
}

} // namespace

void uiDialogPrompt(const std::string& prompt, std::string& buf, int x, int y, int maxLen, bool yesNo,
                    bool needText) {
    Gfx& gx = gfx();
    auto& clock = engine::ticker();
    // Game controller: A / B answer the prompt (y / n or Enter / Esc), other
    // buttons are muted so they cannot type into the edit box.
    engine::Controller::DialogScope padDialog(yesNo ? engine::Controller::Dialog::YesNo
                                                    : engine::Controller::Dialog::Enter);
    gx.setClip(0, 0, 320, 200);
    fontSelect(FontId::Dialog);
    const int promptW = textWidth(prompt);
    const int editX = promptW + x + 8;
    const int editY = y + 1;
    const int boxW = maxLen != 0 ? (maxLen + 1) * 6 : 0;
    buf.clear();
    int cursorX = textWidth(buf);
    const std::string cursorGlyph = exe().dgString(kDialogCursor);
    const Font* f = gfx().font();
    const int lineH = (f ? f->height() : 9) + 6;
    int redraws = boxW == 0 ? 1 : 2;
    int count = 0;
    if (boxW != 0) cursorErase();
    gx.copyPage(gx.displayPage(), gx.drawPage());  // page_copy_full
    for (;;) {
        int key = pollBiosKey();
        if ((key == engine::key::Enter || key == engine::key::Esc) && !yesNo &&
            (count != 0 || boxW == 0 || key == engine::key::Esc || !needText) && redraws == 0) {
            if (boxW != 0) {
                finishEdit(cursorX, editX, editY, lineH);
                if (key == engine::key::Esc) buf = std::string(1, char(1));
            } else if (key == engine::key::Esc) {
                buf = "n";
            }
            return;
        }
        if (redraws != 0) {
            clock.frameLimitWait();
            if (redraws == 1 && boxW == 0) cursorErase();
            if (redraws == 2 && boxW != 0) cursorErase();
            uiDrawTextPanel(prompt.c_str(), x, y, promptW + boxW + 8, lineH + 2);
            if (boxW == 0) {
                present();
            } else {
                gx.rect(editX, editY, boxW, lineH, colour(0x16));
                gx.rect(editX + 1, editY + 1, boxW - 2, lineH - 2, colour(0x18));
                gx.line(editX + 1, editY + lineH - 2, editX + boxW - 2, editY + lineH - 2, colour(0x14));
                gx.line(editX, editY + lineH - 1, editX + boxW - 1, editY + lineH - 1, colour(0x12));
                gx.fillRect(editX + 2, editY + 2, boxW - 4, lineH - 4, colour(0x16));
                drawTextShadow(editX + 6, editY + 3, buf, 0x0f, 0x00);  // black text, white shadow
            }
            --redraws;
        }
        if (yesNo) {
            if (key == engine::key::Esc) {
                buf = "n";
                return;
            }
            key |= 0x60;
            if (key == 'y' || key == 'n') {
                buf = std::string(1, char(key));
                return;
            }
        }
        if (key == 8) {
            if (count > 0) --count;
            buf.resize(size_t(count));
            cursorX = textWidth(buf);
            redraws = 2;
        }
        if (boxW != 0 && key > 0 && key < 0x80 &&
            (std::isalnum(key) || key == ' ' || key == '.' || key == 0x27 || key == '-')) {
            if (count < boxW / 6 - 3) {
                buf.resize(size_t(count));
                buf.push_back(char(key));
                ++count;
            }
            buf.resize(size_t(count));
            cursorX = textWidth(buf);
            redraws = 2;
        }
        if (boxW != 0) {
            clock.frameLimitWait();
            if (clock.time() & 0x40) drawTextShadow(cursorX + editX + 6, editY + 3, cursorGlyph, 0x0f, 0x00);
            else gx.fillRect(cursorX + editX + 6, editY + 2, 8, lineH - 4, colour(0x16));
            present();
        } else {
            sys().idle();
        }
        clock.updateGameTime();
    }
}

namespace {

// "<a><campaign name><b>" centred on x = cx; the width is measured in the
// font that happens to be selected, as in the original.
bool confirmCampaign(u16 a, u16 b, int slot) {
    const std::string prompt = exe().dgString(a) + campaign::slotName(slot) + exe().dgString(b);
    const int x = 184 - textWidth(prompt) / 2;
    std::string buf;
    uiDialogPrompt(prompt, buf, x, 60, 0, true, false);
    return !buf.empty() && buf[0] == 'y';
}

bool confirmPlain(u16 text, int cx) {
    const std::string prompt = exe().dgString(text);
    const int x = cx - textWidth(prompt) / 2;
    std::string buf;
    uiDialogPrompt(prompt, buf, x, 64, 0, true, false);
    return !buf.empty() && buf[0] == 'y';
}

} // namespace

bool uiConfirmLoadCampaign(int slot) { return confirmCampaign(0x3cf2, 0x3d02, slot); }
bool uiConfirmReplaceCampaign(int slot) { return confirmCampaign(0x3d0d, 0x3d20, slot); }
bool uiConfirmEndMission() { return confirmPlain(0x3d2b, 160); }
bool uiConfirmExitDos() { return confirmPlain(0x3d44, 154); }

// ---------------------------------------------------------------- key reference (port)

namespace {

OverlayHooks g_overlayHooks;

// One row of the reference in the 4x6 font: the keys, then the description
// 17 characters (68 pixels) to the right. A key starting with '*' is a
// heading; an empty key continues the previous description.
struct KeyRow {
    const char* key;
    const char* desc;
};

const KeyRow kFieldKeys[] = {
    {"*IN THE FIELD", ""},
    {"Mouse, arrows", "turn, faster/slower"},
    {"Home / PgUp", "faster + turn L / R"},
    {"End / PgDn", "slower + turn L / R"},
    {"Keypad 5", "stop"},
    {"Ctrl+Left/Right", "turn quickly"},
    {"Enter, mouse 1", "fire"},
    {"Tab", "next target"},
    {"n / Alt+N", "next weapon/grenade"},
    {"r", "rate of fire"},
    {"g", "throw grenade"},
    {"1 / 2 / 3", "prone/crouch/stand"},
    {"+ / -", "stand up / get down"},
    {"[ / ]", "use tool / next tool"},
    {"x", "open the hut ahead"},
    {"q", "dive (in the water)"},
    {"Space, m, mouse2", "map and orders"},
    {"F1 / F2", "first person / chase"},
    {"F3-F8 / F9 / F10", "team, target, enemy"},
    {"Mouse wheel", "zoom chase/team view"},
    {"Esc", "end the mission"},
};

const KeyRow kOrderKeys[] = {
    {"*TEAM ORDERS (hand signals)", ""},
    {"h  halt", "s  search area"},
    {"p  split team", "j  join teams"},
    {"l  column", "v  vee wedge"},
    {"d  diamond", "i  in line"},
    {"c  cease fire", "w  fire at will"},
    {"f  field of fire", "t  fire at target"},
    {"*ON THE MAP", ""},
    {"Arrows, mouse", "move the pointer"},
    {"Enter", "press / set waypoint"},
    {"Tab / Shift+Tab", "next / previous team"},
    {"1..6", "select a team"},
    {"+ - x z, wheel", "zoom in / out"},
    {"h p l s j", "halt, ASAP, stealth,"},
    {"", "search, join"},
    {"c f t w", "fire: cease, field,"},
    {"", "at target, at will"},
    {"d i v", "demolish/snipe/cover"},
    {"a b u", "air/boat/heli attack"},
    {"k / o", "cease attack, loiter"},
    {"e", "extract at pointer"},
};

const char* const kGeneralKeys[] = {
    "Alt+P pause   Alt+T time compression   Alt+I team names   Alt+X quit game",
    "Alt+S sound   Alt+M music   Alt+D detail   Alt+Enter fullscreen",
    "Ctrl+Q quit to desktop   F12 screenshot   Ctrl+H this screen",
};

template <size_t N>
void drawKeyRows(const KeyRow (&rows)[N], int x, int y) {
    for (const KeyRow& r : rows) {
        if (r.key[0] == '*') {
            front::text4x6(y, x, r.key + 1, 0x00);
        } else {
            front::text4x6(y, x, r.key, 0x0f);
            front::text4x6(y, x + 68, r.desc, 0x0f);
        }
        y += 7;
    }
}

} // namespace

void uiSetOverlayHooks(OverlayHooks hooks) { g_overlayHooks = std::move(hooks); }

void uiShowKeyReference() {
    Gfx& gx = gfx();
    auto& clock = engine::ticker();
    if (g_overlayHooks.before) g_overlayHooks.before();
    // Pad A / B close it like a key (other buttons are muted).
    engine::Controller::DialogScope padDialog(engine::Controller::Dialog::Enter);
    // A palette fade in progress (the fade-in at a mission's start, a
    // cut-scene's end) is driven by the frame loop that stops here: show the
    // card under the normal palette and let the fade go on afterwards.
    auto& fade = engine::paletteFade();
    logInfo("key card: open (fade level %d, draw page %d, target %s)", fade.level(), gx.drawPage(),
            gx.target() ? "bitmap" : "page");
    fade.suspend();
    fade.upload();
    // Always on the page, whatever the caller was drawing into.
    Bitmap* const target = gx.target();
    gx.setTarget(nullptr);
    gx.setClip(0, 0, 320, 200);
    cursorEraseAll();  // no cursor image in the frame the pages keep
    gx.copyPage(gx.displayPage(), gx.drawPage());  // page_copy_full
    uiDrawTextPanel("", 10, 9, 300, 182);
    fontSelect(FontId::Dialog);
    drawTextShadow(14, 11, "SEAL TEAM - KEYS", 0x00, 0x0f);
    const std::string hint = "any key closes";
    drawTextShadow(306 - textWidth(hint), 11, hint, 0x00, 0x0f);
    drawKeyRows(kFieldKeys, 12, 23);
    drawKeyRows(kOrderKeys, 164, 23);
    int y = 172;
    for (const char* line : kGeneralKeys) {
        front::text4x6(y, 12, line, 0x00);
        y += 7;
    }
    present();
    // Keys typed while the H that opened it is still held (its repeats) are
    // dropped; the first key after that closes it, another Ctrl+H included.
    bool hUp = false;
    for (;;) {
        clock.frameLimitWait();
        sys().pump();
        if (!hUp) {
            sys().input().flushKeys();
            if (sys().input().keyDown(sc::H)) {
                sys().idle();
                clock.updateGameTime();
                continue;
            }
            hUp = true;
        }
        if (pollBiosKey() != 0) break;
        sys().idle();
        clock.updateGameTime();
    }
    sys().input().flushKeys();  // repeats of the closing key do not reopen it
    logInfo("key card: closed");
    // Both pages back to the frame that was up (the draw page still holds it
    // after the flip), so a screen that keeps state on its pages - the intel
    // screen's zoomed area map, say - continues as if nothing had happened.
    gx.copyPage(gx.drawPage(), gx.displayPage());
    gx.setTarget(target);
    fade.resume();
    if (g_overlayHooks.after) g_overlayHooks.after();
}

// ---------------------------------------------------------------- cursor

void cursorLoadAll() {
    loadCursorShape("cursor.pic", "cursor.msk", g_arrow);
    loadCursorShape("wait.pic", "wait.msk", g_wait);
    loadCursorShape("sight.pic", "sight.msk", g_sight);
    for (auto& b : g_saveUnder) gfx().createBitmap(b, 0x14, 0x18);
    cursorReset();
}

void cursorReset() {
    g_cursorWait = g_cursorSight = false;
    g_cursorPage = 0;
    g_savedX[0] = g_savedX[1] = -1;
}

void cursorSetWait(bool on) { g_cursorWait = on; }
void cursorSetSight(bool on) { g_cursorSight = on; }

void cursorDraw() {
    Gfx& gx = gfx();
    const UiState& s = ui();
    g_savedX[g_cursorPage] = s.cursorX;
    g_savedY[g_cursorPage] = s.cursorY;
    gx.blit(gx.screen(), s.cursorX - 4, s.cursorY - 5, g_saveUnder[g_cursorPage], 0, 0, 0x14, 0x13);
    g_cursorPage ^= 1;
    const CursorShape* shape = &g_arrow;
    int x = s.cursorX, y = s.cursorY;
    if (g_cursorWait) {
        shape = &g_wait;
    } else if (g_cursorSight) {
        shape = &g_sight;
        if (!shape->mask.empty()) {
            x -= 4;
            y -= 5;
        }
    }
    if (shape->mask.empty()) gx.blit(shape->pic, 0, 0, gx.screen(), s.cursorX, s.cursorY, 0x10, 0x0e);
    else gx.blitMasked(shape->pic, 0, 0, gx.screen(), x, y, 0x10, 0x0e, shape->mask.data());
}

void cursorErase() {
    Gfx& gx = gfx();
    const int p = g_cursorPage;
    if (g_savedX[p] == -1) return;
    gx.blit(g_saveUnder[p], 0, 0, gx.screen(), g_savedX[p] - 4, g_savedY[p] - 5, 0x14, 0x13);
    g_savedX[p] = -1;
}

// The draw page's cursor as cursorErase, then the displayed page's (the one
// cursorDraw drew last, saved under the other index).
void cursorEraseAll() {
    Gfx& gx = gfx();
    cursorErase();
    const int p = g_cursorPage ^ 1;
    if (g_savedX[p] == -1) return;
    const int drawPage = gx.drawPage();
    gx.setDrawPage(gx.displayPage());
    gx.blit(g_saveUnder[p], 0, 0, gx.screen(), g_savedX[p] - 4, g_savedY[p] - 5, 0x14, 0x13);
    g_savedX[p] = -1;
    gx.setDrawPage(drawPage);
}

} // namespace st::game
