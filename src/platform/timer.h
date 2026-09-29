// Time base. The DOS game counted interrupts from the 8253 PIT (the BIOS rate
// is 1193182/65536 = 18.2065 Hz; games often reprogram it faster) and waited on
// the VGA vertical retrace (70 Hz in mode 13h/X). Both are derived here from
// the host's high resolution clock so behaviour matches the original speed.
#pragma once

#include "core/common.h"

namespace st {

constexpr double kPitHz = 1193181.666;
constexpr double kBiosTickHz = kPitHz / 65536.0;
constexpr double kVgaRefreshHz = 70.086;  // 25.175 MHz / (800 * 449)

class Timer {
public:
    void init();

    double seconds() const;              // since init
    u32 biosTicks() const;               // 18.2 Hz counter (0040:006C)
    u32 retraceCount() const;            // vertical retraces since init

    // Programmable tick source: the equivalent of reprogramming PIT channel 0
    // with a divisor. Ticks keep counting continuously across reprogramming.
    void setTickDivisor(u32 divisor);
    u32 ticks() const;
    double tickHz() const { return tickHz_; }

private:
    std::uint64_t start_ = 0;
    double freq_ = 1.0;
    double tickHz_ = kBiosTickHz;
    double tickEpoch_ = 0.0;
    u32 tickBase_ = 0;
};

} // namespace st
