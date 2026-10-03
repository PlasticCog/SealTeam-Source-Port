// Helpers shared by the front-end screens: RLE sprite sets (348e:15F0 /
// spr_get_frame_image), the message queue of the dialog text bar
// (1000:7B06..840B, docs/re/seg_1000.md 10), the 4x6 text and emboss helpers
// (365e:94DE / 9505, 4dcf:000E), the recruit-screen voice player
// (365e:7E14..7F74) and the common render prologue/epilogue.
#pragma once

#include "core/common.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace st::game::front {

// ---------------------------------------------------------------- time

s32 now();  // g_timer (DS:ECA6), the frame-latched 256 Hz clock
// The recurring "now - last >= n" test used by the auto-advance timers.
inline bool elapsedSince(s32 last, s32 n) { return now() - last >= n; }

// ---------------------------------------------------------------- sprites

// A sprite set: RLE images <NAME>F<f+1>R<r+1>.RLE, fetched with 0-based
// frame set f and rotation r. Images start with u16 width, height.
class SpriteSet {
public:
    explicit SpriteSet(std::string name = {}) : name_(std::move(name)) {}
    void setName(const std::string& name);
    const u8* frame(int f, int r);   // nullptr if missing
    void free() { cache_.clear(); }

    static int width(const u8* img) { return img ? rd16(img) : 0; }
    static int height(const u8* img) { return img ? rd16(img + 2) : 0; }

private:
    std::string name_;
    std::map<int, std::vector<u8>> cache_;
};

// gfx_sprite_scaled at the image's own size (the screens pass the size
// through math_mul_8_8(size, 0x100), an identity).
void drawSprite(const u8* img, int x, int y);

// ---------------------------------------------------------------- messages

namespace msg {
// Kinds: 0..2 dialog line in font DS:EEF4[kind] (prop, propbold, memo),
// 3 HUD line, 4 medal picture (frame style % 8 of the mdl set).
void reset();                                           // msg_queue_reset (840B)
bool add(const std::string& text, int kind, int duration, int style, int sticky);  // 7DF2
bool remove(int i);                                     // 7EE4
void clear();                                           // 7F58
int count();                                            // 7F6D
void skip();                                            // 7F71
void tick();                                            // 7FD1
void drawQueue();                                       // 8339 (msg_draw_queue)
void draw();                                            // 80F1 (msg_draw, head entry)
int say(const std::string& name, const std::string& text);  // msg_say (7B06)
// Medal pictures for kind 4 (sprite set "mdl", far 53BA:2140).
void setMedalSprites(SpriteSet* set);
} // namespace msg

// ---------------------------------------------------------------- text

// text4x6_draw_string (4dcf:000E): x is aligned down to a multiple of 4 like
// the planar writer; colour = current 4x6 text colour.
void text4x6(int y, int x, const std::string& s, u8 color = 0x0f);
// text4x6_draw_centered (365e:94DE): x = (clip_cx - 2*len + 1) & ~3.
void text4x6Centered(int y, const std::string& s, u8 color = 0x0f);
// font_draw_text_far_centered (365e:9438): x = clip_cx - width/2.
void drawTextCentered(int y, const std::string& s, u8 color);
// font_draw_text_emboss (365e:9505).
void drawTextEmboss(int x, int y, const std::string& s, u8 a, u8 b, u8 c);
// str_itoa_pad(v, width, '0').
std::string padNumber(int v, int width);

// ---------------------------------------------------------------- mission data

// Area name of a world (<world>.wd +1, loaded to DS:ECB9 by the world
// loader); world names from DS:1CD0.
std::string areaName(int world);
// Per-area tables DS:3140 (region / map frame) and DS:3178 (camp frame).
int areaRegion(int world);
int areaCampFrame(int world);

// Team types of the craft groups the world builder (365e:129A) creates for
// the current MCI - used where the original reads g_groups[...]+0x26 while
// the world is not available in the port yet.
int insertionTeamType();   // g_insertion_group (EC8C)
int extractionTeamType();  // g_extraction_group (EC8D)
int fireSupportTeamType(); // g_fire_support_group (EC90)
int breakContactTeamType();  // g_break_contact_group (EC91), -1 none

// Time of day (g_tod_hour / g_tod_minute, 1000:1964..19EE); the front end
// keeps its own copy for the Patrol Order, debriefing and camp captions.
namespace tod {
void set(int hour, int minute);  // tod_set
void add(int hours, int minutes);  // tod_add (8-bit arithmetic)
void update();                   // tod_update: +1 minute per 0x3C00 ticks
int hour();
int minute();
// tod_palette_index (1000:1A88): palette for the time of day (0 day, 1 night,
// 2 dawn 5-8 h / dusk 18-20 h). skyShade receives the value the original
// also stores to DS:0530/0532 for the sky renderer ((h-4)*32 + m/2 at dawn,
// 0x80 day and dusk, 0 night).
int paletteIndex(int* skyShade = nullptr);
} // namespace tod

// ---------------------------------------------------------------- voice

// Two-part recruit voice sample newNa.voc / newNb.voc (N = year + 1).
namespace voice {
void load(int year);   // voice_load (7E5A)
void free();           // voice_free (7E14)
void play();           // voice_play (7EFE)
void stop();           // voice_stop (7F54)
void tick(int dt);     // voice_tick (7F74)
} // namespace voice

// ---------------------------------------------------------------- frames

// Render prologue of every screen: while g_redraw_frames > 0 restore the
// background picture and return true (full redraw); otherwise wait for the
// frame limiter and erase the cursor.
bool renderBegin();
// Epilogue: full clip, frame limiter, cursor.
void renderEnd();
// Leave pattern: wait cursor + one last render done by the caller.

// mouse_set_pos(105, 105): drop pending pointer motion.
void mouseRecenter();

// ---------------------------------------------------------------- input

// input_get_key (19ac:2647) for the front end: returns a due scripted key
// (developer commands) before asking the input layer.
int getKey();
// A due scripted key in raw BIOS form for pollBiosKey (dialog prompts), 0 if none.
int scriptedBiosKey();

// Scripted key presses for the developer commands (screenshot tests): a
// comma separated list of keys - single characters or Enter, Esc, Space,
// Backspace, Up, Down, Left, Right, F10, AltX, AltE, AltS, AltM, Plus, Minus,
// Comma - each optionally followed by "@seconds" (time since the script was
// set); keys without a time follow the previous one after 0.4 s (the first
// at 1 s). Game controller events: PadA, PadB, PadX, PadY, PadLB, PadRB,
// PadLS, PadRS, PadStart, PadBack, PadUp/Down/Left/Right (d-pad), PadLT,
// PadRT (triggers), PadLX+/-, PadLY+/-, PadRX+/-, PadRY+/- (sticks, full
// deflection); "Pad~A" releases; CtrlH (a real Ctrl+H key press), WheelUp /
// WheelDown (a mouse wheel notch), MiddleClick (a middle button press). They are pushed as synthetic SDL gamepad
// events and go through the controller mapping layer (docs/controller.md).
void setKeyScript(const std::string& spec);
// A key time may be written "anchor+seconds" (e.g. Esc@mission+4): it counts
// from rebaseKeyScript(anchor), which the mission loop calls with "mission"
// when a mission starts, so mission keys do not depend on the front-end timing.
void rebaseKeyScript(const std::string& anchor);

} // namespace st::game::front
