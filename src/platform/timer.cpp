#include "platform/timer.h"

#include <SDL3/SDL.h>

namespace st {

void Timer::init() {
    start_ = SDL_GetPerformanceCounter();
    freq_ = double(SDL_GetPerformanceFrequency());
    tickHz_ = kBiosTickHz;
    tickEpoch_ = 0.0;
    tickBase_ = 0;
}

double Timer::seconds() const {
    return double(SDL_GetPerformanceCounter() - start_) / freq_;
}

u32 Timer::biosTicks() const { return u32(seconds() * kBiosTickHz); }

u32 Timer::retraceCount() const { return u32(seconds() * kVgaRefreshHz); }

void Timer::setTickDivisor(u32 divisor) {
    const u32 now = ticks();
    if (divisor == 0) divisor = 65536;  // PIT treats 0 as 65536
    tickBase_ = now;
    tickEpoch_ = seconds();
    tickHz_ = kPitHz / double(divisor);
}

u32 Timer::ticks() const {
    return tickBase_ + u32((seconds() - tickEpoch_) * tickHz_);
}

} // namespace st
