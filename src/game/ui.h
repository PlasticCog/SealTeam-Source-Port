// Front-end UI toolkit (segment 365e, docs/re/seg_365e_a.md 9.2) and the
// mouse cursor (19ac:2C97-37E9, docs/re/seg_19ac.md 9).
#pragma once

#include "core/common.h"

#include <string>
#include <vector>

namespace st::game {

// 16-byte button record of the original (flags: 0x01 clickable, 0x02
// disabled, 0x04 label drawn elsewhere, 0x10 always highlighted, 0x20 extra
// outer frame, 0x40 invisible hot-spot).
struct Button {
    s16 x = 0, y = 0, w = 0, h = 0;
    u16 key = 0;         // key code sent when activated
    s8 hotIndex = -1;    // underlined letter (-1 none)
    u8 flags = 0;
    std::string label;
};
using ButtonList = std::vector<Button>;

namespace btn {
constexpr u8 Clickable = 0x01, Disabled = 0x02, NoLabel = 0x04, Highlighted = 0x10, Framed = 0x20,
             Hidden = 0x40;
}

// Reads a button list from st.exe: records at far segment `seg`:0000 up to the
// record with key 0, labels from the near pointer table at DS:`labelTable`.
ButtonList loadButtonList(u16 seg, u16 labelTable);

struct UiState {
    int focus = -1;         // g_ui_focus (DS:EEBE)
    bool pressed = false;   // g_ui_pressed
    int releaseCount = 0;   // g_extra_redraws: 3-frame release countdown
    int redrawFrames = 0;   // g_redraw_frames: full redraws still pending
    int cursorX = 160, cursorY = 100;  // g_cursor_x / g_cursor_y (DS:EC70 / EC6C)
};
UiState& ui();

void uiCursorMove(int dx, int dy);                       // 365e:30ED (original clamp quirk kept)
int uiButtonHitTest(const ButtonList& list);             // 365e:3637
bool uiPointerUpdate(int dx, int dy, const ButtonList& list);  // 365e:3712
bool uiButtonPress(int key);                             // 365e:324A
void uiButtonRelease(int key);                           // 365e:2AF6
void uiCursorToButton(const Button& b);                  // 365e:327A
void uiDrawButtons(const ButtonList& list);              // 365e:32BA
void uiDrawTitleTab(const std::string& caption, int x, int y);  // 365e:2FB7

// Cursor with save-under per video page.
void cursorLoadAll();       // 19ac:2D7F
void cursorDraw();          // 19ac:36E0
void cursorErase();         // 19ac:37E9
void cursorReset();
void cursorSetWait(bool on);
void cursorSetSight(bool on);

} // namespace st::game
