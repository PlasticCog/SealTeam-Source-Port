// Palette upload and fades (original segment 49f8, see docs/re/seg_libs.md 9).
//
// A signed fade level L is applied on every upload: L >= 0 darkens towards
// black, L < 0 tints towards red. The timer moves the current level 5 units
// towards the target every 8 ticks (32 Hz).
#pragma once

#include "core/common.h"
#include "gfx/image.h"

namespace st::engine {

class PaletteFade {
public:
    // Source palette (6-bit DAC values) and number of colours to upload.
    void setPalette(const Palette& pal, int count = 256);
    const Palette& palette() const { return base_; }

    // Set target and current level at once and upload (0x100 = black, 0 = normal).
    void setLevel(int level);
    int level() const { return current_; }

    // Called every frame: v >= 0x600 requests a fade to black once it has been
    // requested for 0x100 ticks, v <= -0x400 the same towards red; anything
    // else cancels and returns to normal.
    void request(int v, int frameDt);

    // Timer step (every 8th tick).
    void tick();

    void suspend();
    void resume();

    // Write base palette, adjusted by the current level, to the DAC.
    void upload() const;

private:
    Palette base_{};
    int count_ = 256;
    int current_ = 0, target_ = 0;
    int blackAcc_ = 0, redAcc_ = 0;
    int savedCurrent_ = 0, savedTarget_ = 0;
};

PaletteFade& paletteFade();

} // namespace st::engine
