// In-mission message queue (1000:7B06..840B, docs/re/seg_1000.md 10): the
// logic part. The HUD phase draws the queue (msg_draw / msg_draw_queue) from
// messages(); the simulation only adds, expires and clears entries.
//
// Texts are std::string copies (the original keeps a 64-byte copy and the far
// pointer; texts of 63+ characters become "Dlg Str Overflow").
#pragma once

#include "game/types.h"

#include <array>
#include <functional>
#include <string>

namespace st::game::mission {

struct Message {
    s16 style = -1;          // +0x00 -1 free, 0/1 dialog, 2 HUD line, 3 hand-signal icon line, 6 special
    s16 sticky = 0;          // +0x02
    s16 duration = 0;        // +0x04 ticks (0 = shown with the previous entry)
    Ticks expiry = -1;       // +0x06 -1 = not started
    s16 kind = 0;            // +0x0A 0..2 dialog font, 3 HUD overlay, 4 medal picture
    std::string text;        // +0x10
    s16 icon = 0;            // +0x50 hand-signal number (style 3)
};

struct MessageQueue {
    std::array<Message, 8> entries;  // g_msg_queue DS:3716
    int head = 0;                    // DS:ED22
    int count = 0;                   // DS:ED24
    bool enabled = false;            // DS:39A6
};

MessageQueue& messages();

// Observer called for every message added (the sim log uses it).
void setMessageObserver(std::function<void(const Message&)> fn);

bool msgQueueAdd(const std::string& text, int kind, int duration, int style, int sticky);  // 1000:7DF2
bool msgQueueRemove(int i);   // 1000:7EE4
void msgQueueClear();         // 1000:7F58
int msgQueueCount();          // 1000:7F6D
void msgQueueSkip();          // 1000:7F71
void msgQueueTick();          // 1000:7FD1 (every loop iteration)
void msgQueueReset();         // 1000:840B

// 1000:7D4B msg_show: time compression off, HUD line (kind 3, style 2).
void msgShow(const std::string& text, int duration);
// Same with the text read from st.exe at DS:offset.
void msgShowDs(u16 dsOffset, int duration);
// 1000:7D6F msg_hand_signal(unit, n).
void msgHandSignal(const Unit* u, int signal);
// 1000:7B06 msg_say(name, text): 'name: "text"' dialog lines; returns strlen(text).
int msgSay(const std::string& name, const std::string& text);
// 1000:672A radio_call(text, team): radio needed for friendly teams or beyond
// 600 units; may fail (damaged radio, busy/unreachable craft: rng(100) < 5).
bool radioCall(const std::string& text, int teamIndex);

// 1000:0194 tc_off / 1000:01B0 tc_on. The loop hooks resync the raw clock and
// request full redraws (set by the mission loop; no-ops headless).
void tcOff();
void tcOn();
void setTimeCompressionHooks(std::function<void()> onOff);

// Last name of a unit (SE +0x0C), "" without one.
std::string unitName(const Unit* u);

} // namespace st::game::mission
