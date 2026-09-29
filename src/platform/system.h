// Platform singleton: owns SDL, the emulated VGA, input and timer, and the
// "pump" that the game's blocking loops call whenever the original would have
// polled hardware or waited for a retrace.
#pragma once

#include "platform/input.h"
#include "platform/timer.h"
#include "platform/video.h"

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

    // Testing aid: after `afterSeconds`, save the displayed frame as a BMP
    // and quit (used to verify screens without interaction).
    void scheduleScreenshot(const std::string& path, double afterSeconds);
    bool saveScreenshot(const std::string& path);

private:
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
