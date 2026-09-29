#include "game/ui.h"

#include "data/exeimage.h"
#include "game/screens.h"
#include "gfx/font.h"
#include "gfx/gfx.h"
#include "gfx/image.h"

#include <algorithm>
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

ButtonList loadButtonList(u16 seg, u16 labelTable) {
    ButtonList list;
    for (int i = 0;; ++i) {
        const u8* r = exe().at(seg, u16(i * kButtonRecord));
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
        b.label = exe().dgStringPtr(u16(labelTable + 2 * i));
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

} // namespace st::game
