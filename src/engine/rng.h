// The game-wide random number generator (2255:789A rng_next, 2255:78D6
// rng_range; docs/re/seg_2255.md 5.3): an additive lagged-Fibonacci
// generator, x[n] = x[n-32] + x[n-17] mod 2^16. The original never reseeds
// it, so every run starts from the same 32-word table (read from st.exe).
// All game code must draw from this one generator, in the original call
// order, for behaviour to match the DOS game.
#pragma once

#include "core/common.h"

namespace st::engine {

class Rng {
public:
    // Load the initial state from st.exe (far 5382:0000).
    void init();
    u16 next();          // rng_next
    int range(int n);    // rng_range: (next() & 0x7FFF) % n, n > 0

private:
    u16 state_[32]{};
    int i_ = 0, j_ = 15;
};

Rng& rng();

} // namespace st::engine
