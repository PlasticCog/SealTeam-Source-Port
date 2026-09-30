#include "engine/rng.h"

#include "data/exeimage.h"

namespace st::engine {

namespace {
constexpr u16 kSeedTableSeg = 0x5382;
} // namespace

Rng& rng() {
    static Rng instance;
    return instance;
}

void Rng::init() {
    const u8* p = exe().at(kSeedTableSeg, 0);
    if (!p) fatal("random table missing from st.exe");
    for (int k = 0; k < 32; ++k) state_[k] = rd16(p + 2 * k);
    i_ = 0;
    j_ = 15;
}

u16 Rng::next() {
    state_[i_] = u16(state_[i_] + state_[j_]);
    const u16 r = state_[i_];
    i_ = (i_ + 1) & 31;
    j_ = (j_ + 1) & 31;
    return r;
}

int Rng::range(int n) {
    if (n <= 0) return 0;  // the original requires n > 0 (divide error otherwise)
    return int(next() & 0x7fff) % n;
}

} // namespace st::engine
