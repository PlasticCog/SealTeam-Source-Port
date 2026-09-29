# Segment 2dbd (part 4) — support craft, extraction, ordnance, ambient flyers

Companion to the other `seg_2dbd*.md` notes. Time: 256 ticks = 1 s
(`now` DS:eca6, `dt` DS:ecaa). Positions are int32 in 1/256 world unit;
`1000:321f` distances are whole units. Headings/pitches in 1/8° normalised by
`2255:6205`. `move(dist, pitch, heading, pos)` = `4e87:0006`; dt-scaled moves
use `speed·dt` (effectively a sign-extended 24-bit value). Pitch 0x2d0 moves
straight up, 0x870 straight down. `ELAPSED(start, dur)`: when
`now - start ≥ dur` (signed) set `start = now` and return `now` (so it reads
as false when `now == 0`).

**3D render object ("obj3d", created by `2255:0234`)**: `+0` model id, `+2`
flags (bit 0 visible/active), `+6` x, `+0xa` altitude, `+0xe` y (int32 each),
`+0x12` heading, `+0x14` pitch, `+0x16` roll. A unit's body is one of these.

## Support craft — `evt_update_support_craft()` 297d

Runs first in the mission tick for every team of type 1 (boat),
2 (helicopter) or 3 (aircraft). Craft orders: 2 Extraction, 3 Emergency
Extraction, 4 Loiter, 5 Attack, 6 Cease Attack (0/1 behave like 2/3, −1 none).
Craft mover: `+0` speed, `+2` target speed, `+8` accel, `+0xa` decel, `+0xc`
heading, `+0xe` desired heading, `+0x14` turn rate, `+0x18` altitude, `+0x1a`
target altitude, `+0x1c` high, `+0x1e` low altitude, `+0x20` climb, `+0x22`
descent, `+0x24` move mode, `+0x25` altitude mode, `+0x26` flags (bit 4 boat on
water near its destination), `+0x28` destination. Altitude modes (`2841`):
0 high, 1 `(high >> 1) + low`, 2 low.

Per member, with `dist` = distance to the destination:
1. **Boat water check**: over water (feature type 7 or 9) sets "on water" and,
   within 0x78 of the destination, flag 4. Otherwise flag 4 is cleared, low
   altitude, and the boat turns in place before driving (mode 2 only once the
   heading matches).
2. **Craft collision** (`1000:3e4c`): take the other craft's heading; two
   aircraft separate vertically by 0x24/s (climb if the other is not higher,
   else descend while above 0x24); boat followers stop.
3. **Loitering boat on water**: destination = the water feature; heads for the
   extraction point (DS:0eac) if it is the insertion craft team, else its
   destination, aligning with the river direction (feature heading or its
   reverse, whichever is within 90° of the goal).
4. **Otherwise**: during an Attack pass (`dist < 180`) the heading is kept;
   else head for the destination. Boats far (> 0x384) from it on water align
   with the river; off water they head for the nearest water.
5. **Orders** (leader only):
   * 0–3: `dist < 30` low + stop; `< 180` medium + slow; else high + fast.
     When the whole SEAL team is aboard, the destination moves 3000 units east
     (fly away).
   * 4 Loiter: boat within 0x12c stops low; helicopter within 180 hovers;
     aircraft keeps circling; otherwise high + fast.
   * 5 Attack: beyond 180 high + fast, else medium + slow.
   * 6 Cease: within 1200 turn 180° and fly away fast, else switch to Loiter.
6. **Out of ammo** (Attack only): both weapons with no rounds and no reloads →
   "Support Team Out of Ammo." (0x200), order 6, destination moved by a random
   offset between 540 and 1200 on each axis (`1000:38e2`, adjusted off water).
7. Speed approach (to zero at decel, else at accel), heading approach, movement
   (backwards when negative), altitude approach (body altitude = alt << 8).
8. Attitude: boat pitch `(random(max(speed>>5,1)) + min(speed>>2,4)) << 3`;
   helicopter nose-down pitch `-(min(speed>>2, 12))°` and bank up to 12°
   toward the turn; aircraft bank up to 8°.

Constants: DS:0a68 = 1200, DS:0a6a = 180, DS:0a6c = 30.

## Extraction pickup — `evt_update_extraction_pickup()` 3232

1. When every SEAL is aboard (`365e:1cda`), the squad is carried at the
   extraction craft's position (team index DS:ec92).
2. A boat/helicopter team with order 2 or 3 qualifies when its leader is within
   42 units of the player and less than 0x3c00 above. It then turns to the
   player's heading, and for the SEAL team and rescued friendlies (type 7) every
   member that is dead, seriously wounded or within 60 units boards: placed on
   the craft, flagged aboard (0x40), stopped and hidden together with any buddy
   (`19ac:61a7`). The player's team resets its split/selection (`365e:17b1`) and
   order. Each boarding team's formation and order are reset.
3. Then `1000:00b0` runs once: "Extraction" (0x1d00 ticks), mission-end timer
   `now + 0x1d00`, music switches to the ending track (9 point man dead, 7 via
   `365e:3fae`, 10 early, 8 more than one casualty, else 6).

(The leader's "near" step moves by `(1 >> 2) << 8 = 0` — an original no-op.)

**`evt_team_set_mover_flags(team, bits)` 31ec** ORs bits into every member's
mover flags (used with 0x40 "aboard").

## Engine sounds

**`evt_play_craft_engine_sounds()` 352c** (mission start and every 0x3c00
ticks, timer DS:0a6e): helicopter sfx 23, aircraft sfx 22, a moving boat sfx 21
(fast) or 20, each with duration 0x3c00 attached to the leader.
**`evt_force_craft_engine_sound()` 3517** backdates the timer so they restart
on the next frame (after returning from the map).

## Ordnance

Pool of 32 projectiles at DS:2670 (0x50 bytes each, plus a 0x48-byte flight
record):

| Offset | Meaning |
|-------:|---------|
| +00 | base model id (restored after effects; 0x832e = 40 mm) |
| +02 | obj3d |
| +06 | flight record: +0 horizontal speed, +0x18 downward speed, +0x20 bounces, +0x22 gravity |
| +0a | ordnance class (weapon table +4) |
| +0c | weapon index (0xa M203, 0xc DEMO) |
| +0e | weapon mode mask: 1 single, 2 burst, 4 auto, 8 launcher, 0x10 thrown, 0x20 shotgun, 0x40 illumination |
| +10 | launch time |
| +14 | muzzle/hand phase: 0x40, shotgun 0x20, rockets/mortar 0x180 |
| +18 | timer base |
| +1c | impact time |
| +20 | lifetime (0x600 default, smoke/gas 0x3c00, flares 0xf000 + random(0x3c)·256, demo 0x4000/0x5000; raised by 4088) |
| +24 | detonation time (launch + 0x400; demo launch + 0x3c00) |
| +28 | hit flags: 1 unit, 2 obstacle, 4 landed; 0x10/0x20/0x40/0x80 puff kind |
| +29 | state: 1 resolved, 2 expired, 0x10 dud, 0x80 in use |
| +2a | intended target |
| +2e | last collision (unit or feature) |
| +32 | owner |
| +36 | weapon item |
| +40/+44/+48/+4c | muzzle flash / explosion / cloud / impact-puff effects |

Weapon classes: 1 SKS/AK47, 2 SVD, 3 M16/CAR15, 4 M79/Mk18, 5 M203 (class 3
unless in grenade mode), 6 Greasegun/S&W 76, 7 Stoner/K50, 8 M60, 9 shotgun,
10 frag/DEMO, 12 smoke/illumination, 13 WP/stun, 14 tear gas, 15 LAAW/RPG,
16 support rocket, 17 minigun, 18 Hushpuppy, 19 Swedish K, 20 mortar.

**`evt_calc_launch_velocity(ord, range)` 4088** — returns the initial vertical
speed that makes a lobbed round land at height 0: range ≤ 0 becomes 6; thrown
items cap the speed at the range; above 0xf0 the speed grows by range >> 5; the
range is reduced by a quarter twice; `t = (range << 8) / speed` (0x100 if speed
≤ 0); `v = (30·t) >> 8 − (launchHeight << 8) / t`; lifetime = max(lifetime, 2t,
or 4t when speed > 0x12c); returns −v. Gravity is 0x3c·dt/256 per frame.

**`evt_update_ordnance(ord)` 4429** (in-use slots each frame):
* **Muzzle/hand phase**: muzzle flash at the owner (forward offset by posture
  DS:0ad2 = {15, 15, 21}; reversed for rockets/mortar) until the phase ends;
  rockets are hidden for their first second; the round rides with the carrier.
* **In flight**: flash hidden; horizontal then vertical movement. Timed
  detonation (40 mm grenades and thrown items at the detonation time): dud →
  stops; smoke/gas → cloud (`1000:60fa`); else explosion (`1000:5f4e`, sfx 15,
  or 16 for LAAW/mortar) and it lands. Very slow rounds stop. Gravity applies
  above ground. Bullets falling above 0x9600 are culled. Lifetime expiry marks
  "expired" ("Dud." 0x100 for the player's dud), and after twice the lifetime
  "resolved".
* **Ground contact**: non-bouncers land (explode if rocket/M79/mortar/M203
  grenade and not a dud, else an impact puff, sfx 10); bouncers get a random
  heading change (±0x18°, more when slow), vertical speed −¼ and half speed.
* **Collision probe** (`1000:318e`, ±0x600 box): hitting the intended target
  (not the owner; launched/thrown rounds pass through) stops the round on it,
  with a puff for craft and a 1-in-3 puff for people, and records the hit
  direction in the victim's mover `+0x34`. Hitting a solid feature stops
  non-bouncers (puff) or ricochets bouncers (reverse and halve speed; satchel
  charges excepted).
* **Final**: when expired and resolved, the effects are released and the slot
  freed (`1000:5068`).
Damage is applied elsewhere (`19ac:8556`, combat records at 53ba:28ba, stride
0x3c), which resolves rounds with hit/expired flags. Quirk: the M203 mask 0x0f
includes 0x08, so its rifle bullets also skip direct hits.

## Ambient flyers — `evt_update_ambient_flyer(obj)` 4c74

Eight obj3d at DS:3100, spawned four at a time around the camera at altitude
0xc00 when detail > 1 (`1000:770b`, model choice `1000:3b57`). Heading bit 3
marks the flapping variant.
* **Low** (altitude < 0x12c00, birds): speed 0x30 (flapper, k = 8) or 0x48
  (k = 3); random rise `random(k)·3` and sink `random(k−2)·3` per second;
  below 0x1e00 the roll alternates ±0xf0 each frame, flappers otherwise roll
  randomly; never below 0x300; removed beyond 0x384 from the camera.
* **High** (aircraft): level flight at 1560 units/s, removed beyond 0x7530.

## Globals

| Offset | Name | Meaning |
|-------:|------|---------|
| 0a68 / 0a6a / 0a6c | g_craft_far/near/arrive_dist | 1200 / 180 / 30 |
| 0a6e | g_craft_sound_time | Last engine-sound refresh |
| 0ad2 | g_muzzle_fwd_by_posture | {15, 15, 21} |
| 0eac | g_extraction_point | Extraction waypoint |
| 2670 | g_ordnance | Projectile pool [32] |
| 3100 | g_ambient_flyers | Flyer pool [8] |
| d8ae | g_camera_pos | Camera position record |
| ec8d | g_insertion_craft_team | Insertion craft team index |
| ec92 | g_extract_craft_team | Extraction craft team index |
| f450 | g_obj_slot_seg | Segment of the 3D object slot table (0x18-byte slots) |

## Open questions

* Craft orders 0 and 1 behave like 2 and 3; their menu meaning is unknown.
* Flyer models are loaded at runtime (table segment DS:cd88); "birds / high
  aircraft" is inferred from behaviour.
* Ordnance classes 0, 11 and 0x15 have no weapon-table source.
* A direct hit on a person without a puff keeps the round alive until its
  lifetime ends; confirm that 19ac's combat resolution handles it.
