#!/usr/bin/env python3
"""Run an original AIL/2 XMIDI driver (.ADV) in a 16-bit x86 emulator and log
its OPL register writes, one line per write: "<service call> <bank> <reg> <value>"
(hex register/value). The format matches `sealteam_audiotest --regdump`, so the
port's driver re-implementation can be compared byte for byte:

    python tools/ail_regdump.py Game/adlib.adv MSC01.XMI SAMPLE.AD --seq 0 \
        --ticks 7200 --out emu.txt
    sealteam_audiotest --xmi MSC01.XMI --seq 0 --seconds 60 --regdump port.txt
    cmp emu.txt port.txt

Emulates what the game does: init_driver, define the default timbre cache,
register the sequence, answer timbre requests from the Global Timbre Library,
start it, then call the driver's service function once per tick. --fx starts
further sequences of the same XMI (FM effects) with the controller table
(repeat, 0x7F); --volume sets the relative volume to volume*100/127 %.
OPL3 drivers (sbp2fm.adv, pasopl.adv) need --io 0x220 (second bank at +2).
Needs the `unicorn` package. The driver is read from the user's game copy.
"""
import argparse
import struct
import sys

from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INSN
from unicorn.x86_const import (UC_X86_INS_IN, UC_X86_INS_OUT, UC_X86_REG_AX, UC_X86_REG_CS,
                               UC_X86_REG_DS, UC_X86_REG_DX, UC_X86_REG_EFLAGS, UC_X86_REG_ES,
                               UC_X86_REG_SP, UC_X86_REG_SS)

SERVICE_HZ = 1193181.666 / 9943.0  # AIL's PIT divisor for the 120 Hz service rate

# Paragraphs of the emulated memory layout.
DRV, XMI, STATE, CACHE, TIMB, CTRL, STUB, STK = 0x1000, 0x2000, 0x3000, 0x3200, 0x3400, 0x3500, 0x5000, 0x6000

# AIL/2 XMIDI driver function numbers.
F_INIT, F_SERVE, F_STATE_SIZE, F_REGISTER = 102, 103, 150, 151
F_CACHE_SIZE, F_DEFINE_CACHE, F_TIMBRE_REQUEST, F_INSTALL_TIMBRE = 153, 154, 155, 156
F_START, F_SET_VOLUME = 170, 177


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('driver')
    ap.add_argument('xmi')
    ap.add_argument('gtl')
    ap.add_argument('--seq', type=int, default=0)
    ap.add_argument('--ticks', type=int, default=1200, help='service calls to run')
    ap.add_argument('--io', type=lambda x: int(x, 0), default=0x388)
    ap.add_argument('--volume', type=int, default=127)
    ap.add_argument('--fx', action='append', default=[], help='SEQ@SECONDS[:REPEAT]')
    ap.add_argument('--out', required=True)
    a = ap.parse_args()

    drv = open(a.driver, 'rb').read()
    xmi = open(a.xmi, 'rb').read()
    gtl = open(a.gtl, 'rb').read()

    mu = Uc(UC_ARCH_X86, UC_MODE_16)
    mu.mem_map(0, 0x100000)
    mu.mem_write(DRV * 16, drv)
    mu.mem_write(XMI * 16, xmi)
    mu.mem_write(STUB * 16, b'\xf4')  # return target

    funcs = {}
    i = struct.unpack_from('<H', drv, 0)[0]
    while True:
        n, o = struct.unpack_from('<HH', drv, i)
        i += 4
        if n == 0xffff:
            break
        funcs[n] = o

    log = open(a.out, 'w')
    state = {'tick': 0}
    latch = {}

    def hook_out(uc, port, size, value, user):
        if port in (a.io, a.io + 2):
            latch[port] = value & 0xff
        elif port in (a.io + 1, a.io + 3):
            bank = 1 if port == a.io + 3 else 0
            log.write('%d %d %02x %02x\n' % (state['tick'], bank, latch.get(port - 1, 0), value & 0xff))

    def hook_in(uc, port, size, user):
        return 0

    mu.hook_add(UC_HOOK_INSN, hook_out, None, 1, 0, UC_X86_INS_OUT)
    mu.hook_add(UC_HOOK_INSN, hook_in, None, 1, 0, UC_X86_INS_IN)

    def call(fn, *args):
        """Far call of driver function `fn` with word arguments after the dummy word."""
        sp = 0xfff0
        for w in reversed([0] + list(args)):
            sp -= 2
            mu.mem_write(STK * 16 + sp, struct.pack('<H', w & 0xffff))
        for w in (STUB, 0):  # far return address
            sp -= 2
            mu.mem_write(STK * 16 + sp, struct.pack('<H', w))
        mu.reg_write(UC_X86_REG_SS, STK)
        mu.reg_write(UC_X86_REG_SP, sp)
        mu.reg_write(UC_X86_REG_DS, XMI)
        mu.reg_write(UC_X86_REG_ES, XMI)
        mu.reg_write(UC_X86_REG_CS, DRV)
        mu.reg_write(UC_X86_REG_EFLAGS, 0x202)
        mu.emu_start(DRV * 16 + funcs[fn], STUB * 16, count=50000000)
        return mu.reg_read(UC_X86_REG_AX), mu.reg_read(UC_X86_REG_DX)

    def gtl_find(bank, patch):
        p = 0
        while p + 6 <= len(gtl):
            pa, ba, off = struct.unpack_from('<BBI', gtl, p)
            p += 6
            if ba == 0xff:
                return None
            if ba == bank and pa == patch:
                size = struct.unpack_from('<H', gtl, off)[0] or 2
                return gtl[off:off + size]
        return None

    call(F_INIT, a.io, 0xffff, 0xffff, 0xffff)
    size, _ = call(F_CACHE_SIZE)
    call(F_DEFINE_CACHE, 0, CACHE, size)
    registered = [0]

    def register(seq, ctrl):
        n = registered[0]
        registered[0] += 1
        mu.mem_write(CTRL * 16 + (n + 1) * 16, bytes(ctrl) + bytes(16 - len(ctrl)))
        h, _ = call(F_REGISTER, 0, XMI, seq, 0, STATE + n * 0x40, (n + 1) * 16, CTRL)
        if h == 0xffff:
            return None
        while True:  # the application's timbre request loop
            req, _ = call(F_TIMBRE_REQUEST, h)
            if req == 0xffff:
                break
            rec = gtl_find(req >> 8, req & 0xff)
            if rec is None:  # the game would loop forever here
                print('timbre bank %d patch %d missing' % (req >> 8, req & 0xff), file=sys.stderr)
                break
            mu.mem_write(TIMB * 16, rec)
            call(F_INSTALL_TIMBRE, req >> 8, req & 0xff, 0, TIMB)
        call(F_START, h)
        return h

    h = register(a.seq, [])
    if h is None:
        sys.exit('sequence %d not found' % a.seq)
    if a.volume != 127:
        call(F_SET_VOLUME, h, a.volume * 100 // 127, 0)
    fx = []
    for f in a.fx:
        s, rest = f.split('@')
        t, r = (rest.split(':') + ['1'])[:2]
        fx.append([int(s), float(t), int(r), False])
    for tick in range(1, a.ticks + 1):
        state['tick'] = tick
        for f in fx:
            if not f[3] and tick >= f[1] * SERVICE_HZ:
                f[3] = True
                register(f[0], [f[2], 0x7f])
        call(F_SERVE)
    log.close()


if __name__ == '__main__':
    main()
