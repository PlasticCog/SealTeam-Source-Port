# Audio: AIL drivers, XMIDI, timbres, VOC

SEAL Team plays music and FM sound effects through the Miles Design **Audio
Interface Library 2.x** (AIL/2) XMIDI drivers (`*.adv`, "Copyright (C)
1991,1992 Miles Design") and digital effects through an AIL digital driver
(`soundrv.adv` = the chosen `sbdig.adv`/`sbpdig.adv`/...). The port
re-implements the Yamaha FM drivers (`adlib.adv`/`sbfm.adv` for OPL2,
`sbp2fm.adv`/`pasopl.adv` for OPL3) on top of the ymfm chip emulator. The
game-side use (which sequence or effect when) is in `docs/re/seg_libs.md` §12.

| File | Contents |
|---|---|
| `src/audio/xmidi.*` | XMIDI parser; `XmiSequencer` = the device-independent half of the driver |
| `src/audio/ail_opl.*` | `AilOplSynth` = the Yamaha synth half (timbre cache, voices, registers); `TimbreBank` = GTL |
| `src/audio/voc.*` | Creative Voice File decoder |
| `src/audio/mixer.*` | SDL-free engine: one driver instance shared by music and FM effects, digital voices, AIL timer model |
| `src/audio/audio.*` | public thread-safe API (`st::audio`), SDL3 output |
| `src/tools/audiotest.cpp` | `sealteam_audiotest`: offline WAV rendering, tracing, driver verification |
| `tools/ail_regdump.py` | runs an original `.adv` in an x86 emulator and logs its OPL writes (differential test) |
| `third_party/ymfm` | ymfm (Aaron Giles, BSD-3-Clause), commit `81aec25` (2026-07-27): `ymfm.h`, `ymfm_fm.*`, `ymfm_opl.*` and the `adpcm`/`pcm` units `ymfm_opl.cpp` needs |

## Driver files

An `.adv` image is loaded at offset 0 of a segment and addresses its data
CS-relative. Word 0 is the offset of a function table (after the copyright
string): `(u16 function number, u16 offset)` pairs ending with 0xFFFF,
immediately followed by the driver descriptor: min API version (200), type
(2 digital, 3 XMIDI), data suffix (`"AD"`, `"OPL"`, `"MT"` — the game loads
`SAMPLE.<suffix>` as timbre library), far pointer to the device name, default
IO/IRQ/DMA/DRQ, **service rate 120 Hz**, display size.

XMIDI function numbers: 100 describe, 101 detect, 102 init, 103 serve (timer
tick), 104 shutdown, 150 state table size (0x208 bytes), 151 register
sequence, 152 release handle, 153 default timbre cache size (0xE00), 154
define cache, 155 timbre request, 156 install timbre, 157/158 (un)protect,
159 timbre status, 170 start, 171 stop, 173 resume, 174 status, 175/176
relative volume/tempo, 177/178 set relative volume/tempo (percent, ms),
179/180 beat/measure count, 181 branch, 182/183 get/set controller, 185
channel notes, 186 send channel message, 189/190 (cancel) callback, 191 lock
channel, 192/194 map/true sequence channel, 193 release channel. All
parameters are words after a leading dummy word. The OPL2 and OPL3 drivers
contain byte-identical sequencer code; only the synth part differs.

**Timer.** AIL owns INT 08h and runs the PIT at the shortest registered
period (divisor = µs × 10000 / 8380). Every interrupt adds that period (µs) to
each timer's accumulator; a timer fires when its accumulator reaches its own
period, which is then subtracted. With the game's 256 Hz tick installed (3906
µs, divisor 4661) the driver's 8333 µs service therefore runs on every 2nd or
3rd PIT tick (average 119.99 Hz, ±4 ms jitter). `Mixer` reproduces this
(`timerHz`, default 256). The XMIDI tick equals one service call at 100 %
relative tempo, so music timing is 120 ticks/s; tempo meta events only drive
the beat/measure counters.

## XMIDI files

IFF with big-endian chunk sizes, walked by the driver **without pad bytes**:
`FORM XDIR { INFO: u16 sequence count }` then `CAT XMID { FORM XMID ... }`, or
a single `FORM XMID`. Sequence *n* is the *n*-th `FORM XMID` inside the CAT.
Inside a `FORM XMID`:

* `TIMB`: u16 count, then (u8 patch, u8 bank) pairs — the timbres the
  application must install before starting (bank 127 = percussion, patch = key).
* `RBRN`: u16 count, then (u16 marker, u32 offset into the EVNT data); used by
  the branch API (only the low byte of the marker is compared).
* `EVNT`: the event stream.

Event stream: a byte < 0x80 is an interval (the next events are processed that
many ticks later; consecutive interval bytes add up). There is no running
status. `9n key vel dur` is a note-on with a VLQ duration in ticks (the driver
treats `8n` the same way); `An`/`Bn`/`En` take 2 data bytes, `Cn`/`Dn` one;
`F0`–`FE` are skipped with a VLQ length; `FF type vlq-len data` is a meta
event: 0x2F end of sequence, 0x51 tempo, 0x58 time signature, others ignored.

## Sequencer (driver-independent half)

* Up to 8 sequence handles. Per sequence: event pointer, interval countdown,
  32-entry **note queue** (channel, key, remaining ticks), 4-deep FOR stack,
  channel map (identity unless locked), saved program/pitch/controller values,
  relative volume and tempo (+ramps), beat/measure counters.
* **Service call** per playing sequence: tempo accumulator += relative tempo;
  while it holds ≥ 100: subtract 100 and run one tick — beat counter; every
  queued note's duration counts down and expired notes get a note-off; the
  interval counter is decremented and at ≤ 0 events are processed up to the
  next interval byte. Afterwards the tempo and volume ramps advance.
* **Note-on**: the note is sent immediately with the XMIDI velocity and queued
  with duration − 1; it is released when the value drops below zero, i.e.
  exactly *duration* ticks later, before that tick's events. If the queue is
  full, entry 0 is overwritten (its note never gets a note-off).
* A per-physical-channel note count is kept for channel locking.
* **Controllers handled by the sequencer**: 110 lock (≥ 64: lock a physical
  channel and remap this sequence channel to it; < 64: flush its notes, release
  the channel, unmap), 111 lock protection, 115 indirect prefix (the next
  controller's value is taken from the application's controller array at that
  index), 116 FOR (count; 0 = endless; remembers this event), 117 NEXT (only if
  value ≥ 64: innermost active loop; endless → jump back, else decrement and
  jump back unless it reached 0), 118 clear beat/measure, 119 callback (value
  passed to the application). Controller 7 is scaled by the relative volume
  (value × percent / 100, capped at 127). All other controllers, including
  112/113/114, go to the synth. Controllers 7, 1, 10, 11, 64, 114, 110, 111
  and 112 are remembered per sequence and driver-globally.
* **Channel lock**: picks the physical channel 2–9 (never 1 or the percussion
  channel 10) with the fewest queued notes among channels neither locked nor
  lock-protected (ties → higher channel), else ignoring lock protection; sends
  sustain off, flushes queued notes on it and marks it locked. Events of other
  sequences on a locked channel are dropped (their controller/program/pitch
  values are still recorded). Release: sustain off, all notes off, then the
  recorded controllers, program and pitch bend are re-sent. The game's FM
  effects (MSC01 sequences 11+) use this to borrow a music channel.
* **End of sequence / stop**: sustain off where the sequence set it, locked
  channels released, lock protection cleared, voice protection reset. Stop
  also flushes the note queue; end of sequence does not (all notes in the game
  data end before their end-of-sequence event).
* **Start** resets the state (relative volume and tempo back to 100 %, map,
  loops, queue). **Resume** re-locks channels and re-sends saved values.
* **init_driver** sends volume 127, modulation 0, pan 64, expression 127,
  sustain 0, bank 0 and 110–112 = 0 to channels 2–10 (only), pitch bend
  centre, and the Roland MT-32 power-on programs 68, 48, 95, 78, 41, 3, 110,
  122 to channels 2–9.
* Ramps: each service adds 83 to an accumulator and moves the value by one per
  (10 × ms / |delta|) units.
* Beat counter: default 4/4 at 500000 µs per quarter; per tick 16e6/120 is
  added (scaled by 2^(denominator − 2)) and compared against 16 × tempo (the
  low words are compared signed — harmless quirk, replicated).
* Quirk not replicated (never reached by the game data, whose sequences queue
  at most 18 notes): when queue entry 31 is released while other notes remain
  queued, the driver's scan loop mis-counts.

## Yamaha synth (ADLIB.ADV / SBP2FM.ADV)

* **Reset**: registers 1–0xF5 get 0x20 in reg 1 (waveform select), 0x60 in
  reg 4, AM/VIB/EG/KSR/MULT = 1, KSL/TL = 0x3F, AR/DR = 0xFF, SL/RR = 0x0F,
  and **0xBD = 0xC0 (deep tremolo, deep vibrato)**; everything else 0. OPL3
  first sets 0x105 = 1 and 0x104 = 0 and repeats the operator defaults in the
  second bank.
* **Note slots**: 16 (OPL2) / 20 (OPL3); **voices** 9 / 18. Note-ons are only
  accepted on **MIDI channels 2–10** (1-based); channel 10 is percussion. A
  note-on without a free slot is dropped.
* **Timbre cache**: 192 entries, 3584 bytes; each installed record (with its
  size word) keeps bank, patch, protect flag and an LRU stamp (updated when
  installed and at every note-on using it). A full cache evicts the least
  recently used unprotected timbre and cuts notes using it (never happens in
  this game: all music together uses 124 distinct timbres). Program change
  looks up (channel bank, patch); installing a timbre binds it to channels
  already waiting for that bank/patch.
* **Timbre record** (2-operator, 14 bytes): u16 size (0x0E), s8 transpose
  (percussion: the key to play), then modulator AVEKM, KSL/TL, AR/DR, SL/RR,
  WS, then FB/CON, then carrier AVEKM, KSL/TL, AR/DR, SL/RR, WS. SAMPLE.AD and
  SAMPLE.OPL are identical: melodic banks 55 and 100, percussion bank 127
  (keys 35–87). The OPL3 driver also accepts 25-byte 4-operator records.
* **Note-on**: channel timbre (percussion: bank 127, patch = key, cached per
  key) → first free slot; velocity → `82 + 3 × (vel >> 3)`; timbre registers
  copied; voice allocation (below); all register groups written with key on.
* **Volume**: `f(a,b) = hi(2ab) + 1` (0 stays 0); `v = f(f(ch7, ch11),
  velocity)`; carriers (and the modulator for additive CON = 1) get
  `TL = 63 − ⌊(63 − TLtimbre) × v / 127⌋`, KSL kept. Pan (10) has no effect on
  OPL2.
* **Modulation wheel**: ≥ 64 sets the VIB bit on both operators.
* **Pitch**: bend range fixed **±12 semitones** (`((bend − 8192) >> 5) × 12` in
  1/256 semitone); note + transpose − 12 folded by octaves into 0..95; result
  rounded to 1/16 semitone and wrapped by octaves; F-number from a 192-entry
  table (C at block 3 in 1/16 semitone steps, `round(261.63 × 2^(i/192) ×
  2^17 / 49716)`, entries above 1023 stored halved for the next block); block
  = octave − 1 (clamped at 0 with the F-number halved).
* **Note-off**: every key-down slot with that channel and key; with sustain ≥ 64
  it is only marked, released when sustain drops. Release writes key-off and
  frees voice and slot. (Sustain-off and all-notes-off re-issue note-offs with
  the *played* note, so sustained percussion is not released — replicated.)
* **Controllers**: 1, 7, 10, 11, 64, 112 (voice protection), 113 (timbre
  protection), 114 (bank), 121 (reset: sustain 0, modulation 0, expression 127,
  bend centre), 123 (all notes off). Pitch bend updates all slots of the
  channel.
* **Voice allocation**: round robin over the voices starting after the last
  one tried. If none is free, priorities are recomputed for all active slots:
  0x7FFF (0xFFFF on channels with voice protection ≥ 64) minus the number of
  voices the channel holds. While the best waiting slot's priority is ≥ the
  worst sounding slot's (and not 0), the worst sounding note is cut and its
  voice given to the waiting slot. Waiting slots otherwise keep waiting; they
  are only reconsidered at a later failed allocation.
* **OPL3 specifics**: C0 is written with both output bits set, **pan ≤ 27
  keeps only output B, pan ≥ 100 only output A** (hard panning; a pan change
  rewrites C0). With A = left and B = right as on the SB Pro 2, low MIDI pan
  values come out of the right speaker — AIL uses the same mirrored convention
  in the SB Pro dual-OPL2 and SB Pro digital drivers. `swapStereo` flips it.

## Digital drivers and VOC

* `sbdig.adv` (SB 1.x/2.0) stores volume and pan but only turns the speaker
  on: **no volume control**. `sbpdig.adv` (SB Pro) programs mixer register 4:
  left nibble `(T[pan] × vol) >> 10`, right nibble `(T[127 − pan] × vol) >>
  10`, `T[i] = min(2i, 127)`. `DigitalVolume` selects SB / SB Pro / linear.
* **VOC**: 20-byte "Creative Voice File\x1A", u16 header size, u16 version,
  u16 check; blocks `type, u24 length`: 1 sound (time constant, pack, data;
  rate = 1 MHz / (256 − tc)), 2 continuation, 3 silence, 4 marker, 5 text,
  6/7 repeat, 8 extended (u16 tc = 65536 − 256e6/(ch × rate), pack, mode), 9
  new format (u32 rate, bits, channels, codec). All game effects are single
  8-bit block-1 files at tc 166 (11111 Hz) or 172 (11905 Hz).

## Port specifics

* Output: FM rendered at the OPL rate 49715.9 Hz (OPL2 mono on both channels,
  OPL3 outputs A/B halved to match ymfm's OPL2 scale), digital voices resampled
  to it with linear interpolation, mixed to 16-bit stereo; SDL3 converts to
  the device rate. Music and FM effects share one driver instance, so voice
  allocation and channel locking arbitrate exactly as in AIL.
* API (`audio.h`): `init/shutdown`, `setMusicDevice`, `loadTimbreBank`,
  `preloadTimbres`, `playMusic/stopMusic/musicPlaying/setMusicVolume`,
  `playSequence/stopSequence/sequencePlaying/setSequenceVolume/
  setSequenceRelativeVolume` (FM effects; indirect controller table),
  `playSample/stopSample/samplePlaying/setSampleVolume`, `setDigitalEnabled`.
  Volumes 0–127 map to AIL relative volume `v × 100 / 127` % (the game's music
  volume 96 % = 122). `loop` restarts a finished sequence (most tracks loop by
  themselves with FOR 0/NEXT). Missing timbres are skipped with a warning
  (the DOS game would hang in its timbre request loop; MSC03 is the MT-32 setup
  sequence with bank 60 timbres).

## Verification

* `sealteam_audiotest --verify-driver`: the built-in F-number table, velocity
  curve, register reset tables and controller/program defaults equal the bytes
  in the user's `adlib.adv` and `sbp2fm.adv`.
* Differential test: `tools/ail_regdump.py` (Python + unicorn) runs the
  user's original driver in an x86 emulator (init, cache definition, timbre
  request / install loop, start, service calls) and logs its OPL port writes;
  they were compared with `sealteam_audiotest --regdump`. All 244 sequence × device combinations
  (60 s each), runs with several concurrent FM-effect sequences (channel locks,
  indirect controllers, FOR/NEXT) and relative volume, and 10-minute runs are
  byte-identical.
* Rendered WAVs: non-silent, unclipped; a synthetic XMI confirms pitch (A4 =
  440 Hz within FFT resolution), the ±12 semitone bend and the velocity curve;
  sequence end times match tick count / 119.99 Hz.

## Known gaps

* 4-operator timbres (OPL3 driver only) are not implemented; the game's
  SAMPLE.OPL contains none.
* The chip is ymfm, not a real YM3812/YMF262; analog output stages (card
  filters, levels) are not modelled; FM/digital balance is approximate
  (`musicGain`, `digitalGain`).
* Only the AdLib/SB (OPL2) and SB Pro 2/PAS16 (OPL3) music drivers are
  re-implemented (no MT-32/General MIDI, PC speaker or dual-OPL2 SB Pro 1).
