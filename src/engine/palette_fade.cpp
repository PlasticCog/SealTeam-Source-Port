#include "engine/palette_fade.h"

#include "platform/system.h"

namespace st::engine {

PaletteFade& paletteFade() {
    static PaletteFade instance;
    return instance;
}

void PaletteFade::setPalette(const Palette& pal, int count) {
    base_ = pal;
    count_ = count;
    upload();
}

void PaletteFade::setLevel(int level) {
    current_ = target_ = level;
    upload();
}

void PaletteFade::request(int v, int frameDt) {
    if (v >= 0x600) {
        if (blackAcc_ < 0x100) blackAcc_ += frameDt;
        target_ = blackAcc_ >= 0x100 ? 0x100 : 0;
    } else if (v <= -0x400) {
        if (redAcc_ < 0x100) redAcc_ += frameDt;
        target_ = redAcc_ >= 0x100 ? -0x100 : 0;
    } else {
        blackAcc_ = redAcc_ = 0;
        target_ = 0;
    }
}

void PaletteFade::tick() {
    if (current_ == target_) return;
    constexpr int kStep = 5;
    if (current_ < target_) current_ = (target_ - current_ < kStep) ? target_ : current_ + kStep;
    else current_ = (current_ - target_ < kStep) ? target_ : current_ - kStep;
    upload();
}

void PaletteFade::suspend() {
    savedCurrent_ = current_;
    savedTarget_ = target_;
    current_ = target_ = 0;
}

void PaletteFade::resume() {
    current_ = savedCurrent_;
    target_ = savedTarget_;
    upload();
}

void PaletteFade::upload() const {
    Palette out = base_;
    const int n = count_ * 3;
    if (current_ >= 0) {
        const int mul = 256 - current_;
        for (int i = 0; i < n; ++i) out[size_t(i)] = u8((base_[size_t(i)] * mul) >> 8);
    } else {
        const int k = -current_;
        for (int i = 0; i < n; i += 3) {
            const int r = base_[size_t(i)], g = base_[size_t(i + 1)], b = base_[size_t(i + 2)];
            out[size_t(i)] = u8(r + (((63 - r) * k) >> 8));
            out[size_t(i + 1)] = u8(g - ((g * k) >> 8));
            out[size_t(i + 2)] = u8(b - ((b * k) >> 8));
        }
    }
    sys().video().setPalette(out.data(), 0, count_);
}

} // namespace st::engine
