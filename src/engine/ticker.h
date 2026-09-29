// The game's 256 Hz timer service (original 4a1c, installed as an AIL timer),
// game time, the frame limiter and screen shake. See docs/re/seg_libs.md 10.
//
// On DOS the tick handler ran from the timer interrupt. Here the platform
// delivers every due tick, in order, whenever the game pumps events or waits,
// so the handler never runs concurrently with game code.
#pragma once

#include "core/common.h"

namespace st::engine {

constexpr int kTickHz = 256;             // game time unit: 1/256 s
constexpr u32 kTickPitDivisor = 4661;    // AIL: 3906 us period

class Ticker {
public:
    void install();

    u32 ticks() const { return ticks_; }            // g_ticks (DS:ECB0)
    int frameTicks() const { return frameTicks_; }  // ticks since frame_limit_reset

    // Once per frame (clk_update, segment 1000): snapshot time and compute the delta (>= 1).
    void updateGameTime();
    // clk_reset: game time and raw ticks back to 0, frame delta 1.
    void resetClock();
    // clk_wait(n): keep updating the clock until n ticks have passed.
    void wait(int n);
    s32 time() const { return time_; }
    int frameDt() const { return frameDt_; }

    // Frame limiter: at most one frame per 5 ticks (51.2 fps) when enabled.
    void setFrameLimit(bool on) { frameLimitOn_ = on; }
    void frameLimitWait();
    void frameLimitReset() { frameTicks_ = 0; }

    // Screen shake: n ticks of 4-pixel horizontal / one-line vertical jitter.
    void shakeHorizontal(int n);
    void shakeVertical(int n);
    void shakeStop();
    // Extra CRTC start offset in linear pixels for the current shake phase.
    u32 shakeOffset() const { return u32(shakeH_ * 4 + (shakeV_ ? kShakeLine : 0)); }

private:
    void onTick();
    void shakeTick();

    static constexpr int kShakeLine = 320;
    u32 ticks_ = 0;
    int frameTicks_ = 0;
    s32 time_ = 0, timePrev_ = 0;
    int frameDt_ = 1;
    bool frameLimitOn_ = true;
    int shakeH_ = 0, shakeV_ = 0;
    s32 shakeHEnd_ = 0, shakeVEnd_ = 0;
};

Ticker& ticker();

} // namespace st::engine
