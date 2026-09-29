#include "engine/ticker.h"

#include "engine/palette_fade.h"
#include "platform/system.h"

namespace st::engine {

Ticker& ticker() {
    static Ticker instance;
    return instance;
}

void Ticker::install() {
    sys().installTickService(kTickPitDivisor, [this] { onTick(); });
}

void Ticker::onTick() {
    ++ticks_;
    ++frameTicks_;
    if ((ticks_ & 7) == 0) {
        shakeTick();
        paletteFade().tick();
    }
}

void Ticker::updateGameTime() {
    timePrev_ = time_;
    time_ = s32(ticks_);
    frameDt_ = time_ - timePrev_;
    if (frameDt_ < 1) frameDt_ = 1;
}

void Ticker::frameLimitWait() {
    while (frameLimitOn_ && frameTicks_ < 5) sys().idle();
}

void Ticker::shakeHorizontal(int n) {
    shakeH_ = 0;
    if (time_ + n > shakeHEnd_) shakeHEnd_ = time_ + n;
}

void Ticker::shakeVertical(int n) {
    shakeV_ = 0;
    if (time_ + n > shakeVEnd_) shakeVEnd_ = time_ + n;
}

void Ticker::shakeStop() {
    shakeH_ = shakeV_ = 0;
    shakeHEnd_ = shakeVEnd_ = 0;
    sys().video().setDisplayOffset(0);
}

void Ticker::shakeTick() {
    shakeH_ = time_ < shakeHEnd_ ? (shakeH_ ^ 1) : 0;
    shakeV_ = time_ < shakeVEnd_ ? (shakeV_ ^ 1) : 0;
    sys().video().setDisplayOffset(shakeOffset());
}

} // namespace st::engine
