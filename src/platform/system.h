// Platform singleton: owns SDL, the emulated VGA, input and timer, and the
// "pump" that the game's blocking loops call whenever the original would have
// polled hardware or waited for a retrace.
#pragma once

#include "platform/input.h"
#include "platform/timer.h"
#include "platform/video.h"

#include <functional>

namespace st {

class System {
public:
    bool init(const VideoConfig& vcfg);
    void shutdown();

    Video& video() { return video_; }
    Input& input() { return input_; }
    Timer& timer() { return timer_; }

    // Process host events and refresh the window (VGA memory is always
    // visible on real hardware, so any change must show up without the game
    // asking). Throws QuitRequested if the window was closed.
    void pump();

    // Block until the next vertical retrace, like polling port 0x3DA bit 3.
    void waitRetrace();
    // Block for a number of BIOS ticks / milliseconds while keeping the window alive.
    void delayMs(u32 ms);
    // One iteration of a busy-wait loop: pump, then yield the CPU briefly.
    void idle();

    // Periodic timer "interrupt" at PIT rate 1193182/divisor. Due ticks are
    // delivered from pump(), in order, so the handler never runs concurrently
    // with game code. One service, like the single AIL timer the game uses.
    void installTickService(u32 divisor, std::function<void()> handler);

    // Testing aid: after `afterSeconds`, save the displayed frame as a BMP
    // and quit (used to verify screens without interaction).
    void scheduleScreenshot(const std::string& path, double afterSeconds);
    bool saveScreenshot(const std::string& path);

private:
    void runTicks();

    std::function<void()> tickHandler_;
    double tickHz_ = 0.0;
    double tickEpoch_ = 0.0;
    u64 ticksDelivered_ = 0;
    std::string shotPath_;
    double shotAt_ = 0.0;
    Video video_;
    Input input_;
    Timer timer_;
    int logicalH_ = 240;
    u32 lastRetrace_ = 0;
};

System& sys();

} // namespace st
