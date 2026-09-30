#include "game/mission/msg.h"

#include <cstring>

#include "engine/rng.h"
#include "game/mission/exedata.h"
#include "game/mission/geo.h"
#include "game/mission/sfx.h"
#include "game/mission/state.h"

namespace st::game::mission {

namespace {

// g_msg_queue DS:3716 is zero in the executable image (style 0, not -1) and
// messages are disabled (DS:39A6 = 0) until the first msg_queue_reset.
MessageQueue zeroQueue() {
    MessageQueue q;
    for (Message& m : q.entries) {
        m.style = 0;
        m.expiry = 0;
    }
    return q;
}
MessageQueue g_queue = zeroQueue();
std::function<void(const Message&)> g_observer;
std::function<void()> g_tcOffHook;

constexpr u16 kHandSignalNames = 0x1CB0;  // char* [16]
constexpr u16 kSayPrefix = 0x370A;        // ': "'
constexpr u16 kSayQuoteOnly = 0x370E;     // '"'
constexpr u16 kSayEndSplit = 0x3710;      // '"'
constexpr u16 kSayEnd = 0x3712;           // '"'
constexpr u16 kSaySpacer = 0x3714;        // ' '
constexpr u16 kSignalBlank = 0x39A7;      // ' '
constexpr u16 kOverflow = 0x39A9;         // "Dlg Str Overflow"
constexpr u16 kRadioDamaged = 0x3428;     // "Radio damaged."
constexpr u16 kRadioPrefix = 0x3437;      // "Radio: "
constexpr u16 kNoPrefix = 0x343F;         // ""

} // namespace

MessageQueue& messages() { return g_queue; }

void setMessageObserver(std::function<void(const Message&)> fn) { g_observer = std::move(fn); }
void setTimeCompressionHooks(std::function<void()> onOff) { g_tcOffHook = std::move(onOff); }

bool msgQueueAdd(const std::string& text, int kind, int duration, int style, int sticky) {
    MessageQueue& q = g_queue;
    if (!q.enabled || q.count > 7) return false;
    Message& m = q.entries[size_t((q.count + q.head) % 8)];
    q.count++;
    if (kind != 4 && style == 3) {
        m.icon = s16(duration);
        duration = duration == -1 ? 0 : 0x300;
    }
    m.duration = s16(duration);
    m.style = s16(style);
    m.sticky = s16(sticky);
    m.expiry = -1;
    m.kind = s16(kind);
    m.text = text.size() < 0x3F ? text : dsText(kOverflow);
    if (g_observer) g_observer(m);
    return true;
}

bool msgQueueRemove(int i) {
    MessageQueue& q = g_queue;
    if (i >= 8) return false;
    Message& m = q.entries[size_t(i)];
    if (q.count != 0) {
        if (q.count == 1 && m.sticky != 0) return false;
        q.count--;
    }
    m.style = -1;
    m.text.clear();
    if (i == q.head) q.head = q.count == 0 ? 0 : (q.head + 1) % 8;
    return true;
}

void msgQueueClear() {
    while (g_queue.count >= 1) {
        if (!msgQueueRemove(g_queue.head)) return;
    }
}

int msgQueueCount() { return g_queue.count; }

void msgQueueSkip() {
    MessageQueue& q = g_queue;
    if (q.count > 1 && q.entries[size_t(q.head)].expiry != -1) {
        msgQueueRemove(q.head);
        if (q.count > 0) {
            const Message& h = q.entries[size_t(q.head)];
            if (h.style != -1 && h.duration == 0) msgQueueRemove(q.head);
        }
    }
}

void msgQueueTick() {
    MessageQueue& q = g_queue;
    Message& m = q.entries[size_t(q.head)];
    if (m.style == -1) return;
    const Ticks now = ms().time;
    if (m.expiry == -1) {
        m.expiry = wrapAdd(now, m.duration);
    } else if (m.expiry < now) {
        msgQueueRemove(q.head);
        const Message& h = q.entries[size_t(q.head)];
        if (h.style != -1 && h.duration == 0) msgQueueRemove(q.head);
    }
}

void msgQueueReset() {
    g_queue.head = 0;
    g_queue.count = 0;
    g_queue.enabled = true;  // only head, count and the flag: old entries stay in place
}

void tcOff() {
    MissionState& S = ms();
    if (S.tcState != 0) {
        S.tcState = 0;
        msgQueueClear();
        if (g_tcOffHook) g_tcOffHook();  // clk_resync_raw, g_full_redraw = 2
    }
}

void tcOn() {
    MissionState& S = ms();
    if (S.tcState == 0) {
        S.tcState = 3;
        msgQueueClear();
        msgQueueAdd(dsText(0x025D), 3, 0x7800, 2, 0);
    }
}

void msgShow(const std::string& text, int duration) {
    tcOff();
    msgQueueAdd(text, 3, duration, 2, 0);
}

void msgShowDs(u16 dsOffset, int duration) { msgShow(dsText(dsOffset), duration); }

void msgHandSignal(const Unit* u, int signal) {
    tcOff();
    const std::string name = dsTextPtr(u16(kHandSignalNames + signal * 2));
    if (!ms().handSignalGfx) {
        msgQueueAdd(name, 3, 0x100, 2, 0);
        return;
    }
    const std::string who = (u == pointMan()) ? dsText(kSignalBlank) : unitName(u);
    msgQueueAdd(who, 3, signal, 3, 0);
    msgQueueAdd(name, 3, -1, 3, 0);
}

int msgSay(const std::string& name, const std::string& text) {
    std::string buf = name;
    // Original quirk (1000:7B06): with an empty name the quote word (DS:370E)
    // is copied into the buffer but the prefix length stays 0; that 0 is used
    // by the length tests, the split cap and the truncation (1000:7C3F), so
    // the first line of a split loses one character before the split point.
    int prefixLen = 0;
    if (buf.empty()) {
        buf = dsText(kSayQuoteOnly);
    } else {
        buf += dsText(kSayPrefix);
        prefixLen = int(buf.size());
    }
    const int textLen = int(text.size());
    if (textLen != 0 && prefixLen + textLen < 0xA0) {
        if (prefixLen + textLen < 0x37) {
            msgQueueAdd(buf + text + dsText(kSayEnd), 1, 0x700, 0, 0);
            msgQueueAdd(dsText(kSaySpacer), 1, 0, 0, 0);
        } else {
            int s = textLen / 2 - 8;
            if (0x36 - prefixLen < s) s = 0x36 - prefixLen;
            // Port guard: s cannot go negative with the game's texts (a split
            // needs prefix + text >= 0x37); keeps the indexing defined.
            if (s < 0) s = 0;
            while (s < textLen && text[size_t(s)] != ' ' && text[size_t(s)] != '\0') ++s;
            msgQueueAdd((buf + text).substr(0, size_t(prefixLen + s)), 1, 0x700, 0, 0);
            const std::string rest = s + 1 <= textLen ? text.substr(size_t(s) + 1) : std::string();
            msgQueueAdd(rest + dsText(kSayEndSplit), 1, 0, 0, 0);
        }
    }
    return textLen;
}

bool radioCall(const std::string& text, int teamIndex) {
    MissionState& S = ms();
    Team* t = team(teamIndex);
    Unit* pm = pointMan();
    if (!t || !t->members[0] || !pm) return false;
    const int dist = geoDistance(t->members[0]->body->pos, pm->body->pos);
    std::string prefix;
    if (teamIndex < S.firstMtmGroup || dist > 600) {
        if (S.radioDamaged) {
            msgShowDs(kRadioDamaged, 0x200);
            return false;
        }
        if (t->type == TeamType::Boat || t->type == TeamType::Helicopter) {
            Mover* mv = t->members[0]->mover;
            if (S.time <= mv->posture_until) return false;  // radio busy
            if (engine::rng().range(100) < 5) {
                mv->posture_until = wrapAdd(S.time, 0x1E00);
                return false;
            }
        }
        prefix = dsText(kRadioPrefix);
        sfxPlay(0x1D, 0x200, &pm->body->pos, 1, pm);
        sfxPlay(0x1D, 0x500, &pm->body->pos, 1, pm);
    } else {
        prefix = dsText(kNoPrefix);
    }
    msgQueueClear();
    msgShow(prefix + text, 0x200);
    return true;
}

std::string unitName(const Unit* u) {
    if (!u || !u->se) return std::string();
    const char* n = u->se->last_name;
    return std::string(n, strnlen(n, sizeof u->se->last_name));
}

} // namespace st::game::mission
