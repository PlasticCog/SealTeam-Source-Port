# Game controller support

`src/platform/gamepad.*`, `src/engine/controller.*`, `src/game/launcher_pad.cpp`
and the joystick step of `src/engine/input_layer.cpp` add game controller
play to the port. The original supported a game-port joystick (docs/re/
seg_19ac.md 3.2-3.3): its two buttons became Enter and Space, its Y axis the
Up / Down keys, and both axes fed the motion vector that turns the point
man and moves the pointer. The port keeps that path and puts an SDL3 gamepad
behind it; every other pad button is typed as the keyboard key the game
already understands, so no game logic changed.

## How it works

* **Platform** (`Gamepad`): `SDL_INIT_GAMEPAD`, hot-plugging through
  `SDL_EVENT_GAMEPAD_ADDED` / `REMOVED` (the first connected pad is opened;
  when it goes away the next one is taken), buttons, sticks and triggers
  mirrored from the events. Any pad the OS knows through SDL's mapping
  database works; the names below are the Xbox ones.
* **Mapping layer** (`engine::Controller`): every game action has one
  binding: a button, trigger, stick direction or whole stick, on one *layer*
  (base, or while the Shift / Orders modifier is held). Two channels:
  * *stick channel* = the original joystick: the Select / Fire actions are
    joystick button 1 (Enter every 0x50 ticks while held), "Cam pan / map"
    is button 2 (Space every 0x50 ticks in menus, 0x100 elsewhere, and every
    frame in the views >= 2 where the game resets that timer), the Move
    stick is the joystick axes: motion `dx = clamp(x, +-100) >> 3`,
    `dy = clamp(y, +-100) >> 4` in the field views (>> 2 in menus and on the
    map) and the Down / Up keys every 0x60 ticks when the Y axis passes
    0x30. The Camera stick adds to the same motion vector (turning the point
    man in view 0, the camera in the team / support views, zoom with Y) but
    never produces the axis keys. The mouse is used only while the sticks
    give (0, 0), as in the original.
  * *key channel*: every other action is typed into the BIOS key queue on
    the press edge, so `InputLayer::getKey` reads it through the keyboard
    path with the keyboard's throttle (one key per 0x50 ticks, keys inside
    the window are lost). The arrow and posture actions repeat while held
    like a typematic key (0.5 s delay, then the throttle rate); everything
    else (views, orders, toggles) is one key per press.
* Sticks: radial dead zone (Options page, default 20 %), quadratic response
  so small deflections turn slowly, full deflection = the original's full
  joystick travel (+-128 scale), scaled by the "Stick speed" option.
  Triggers and stick directions count as pressed past half travel.
* Modal prompts (`ui_dialog_prompt`: pause, "End Your Mission? (Y/N)",
  "Exit to DOS?", name entry) poll the BIOS queue directly; there the
  Select / Fire buttons type Enter (or `y` in a Y/N question), Cancel types
  Esc (or `n`) and every other button is muted so it cannot end up in a
  text box.
* The "press any key" logo screen accepts any pad button.

## Default layout (Xbox names)

Hold **LB** for the *Shift* layer, **RB** for the *Orders* layer.

| Input | Base | Shift (LB +) | Orders (RB +) |
|---|---|---|---|
| Left stick | move / turn (speed with Y, as the joystick) | | |
| Right stick | camera (turn, pan in the team view, zoom) | | |
| A | Enter: fire, select, skip insertion, map waypoint | F1 point man view | `w` fire at will |
| B | Esc: cancel, "End Your Mission?" | F2 team view | `c` cease fire |
| X | `n` next weapon | `]` next tool | `t` fire at target |
| Y | `-` posture down (map: zoom in) | `+` posture up (map: expand) | `f` field of fire |
| RT | Enter: fire | `[` use tool | `i` in line |
| LT | `g` grenade (aim, again to throw) | Alt-N next grenade | `l` column |
| LS click | Tab next target (map: next group) | F9 target view | `d` diamond |
| RS click | Space: joystick button 2 (map from view 0, camera pan with the stick in the team view, posture with the stick in view 0; leaves the map) | `r` rate of fire | `v` vee wedge |
| D-pad up | Up: speed up (support views: zoom in) | F3 support view 1 | `h` halt |
| D-pad down | Down: slow down / back | F4 support view 2 | `s` search |
| D-pad left | Left: turn left (menus / map: pointer) | F7 split team A | `p` split team |
| D-pad right | Right: turn right | F8 split team B | `j` join team |
| Start | Alt-P pause | Alt-T time compression | `x` expose trap |
| Back | `m` map screen | Alt-U auto-target | `q` dive |

Unbound by default (bind them on the Controller page): support views 3 / 4
(F5 / F6), Alt-I team info names, Alt-X quit.

Front-end screens: the left stick moves the pointer, A selects, B backs
out, the D-pad nudges the pointer like the arrow keys. Map screen: the
stick moves the map pointer, A sets a waypoint or presses the focused
button, LS click cycles the groups, Y / Shift+Y zoom, Back or RS click
leave the map, orders through the Orders layer or by clicking the panel.

## Remapping

Start menu -> **Controller**. The pages list every action with its
binding; click an action (or move the pointer onto it and press A / Enter)
and then press the button, pull the trigger or push the stick you want.
Holding LB or RB while pressing records a chord on that layer. Esc cancels,
Del / Backspace clears the binding. A binding taken from another action
clears that action. "Xbox defaults" restores the table above; the Options
page has the on / off switch, the dead zone and the stick speed.
While a capture is waiting the pad produces no keys, so the button you
press cannot activate anything on the page.

Bindings are stored in `sealteam.cfg` as `bind_<action> = <input>`
(inputs: `a b x y lb rb lt rt ls rs start back guide dpad_up dpad_down
dpad_left dpad_right lx- lx+ ly- ly+ rx- rx+ ry- ry+ left_stick right_stick
none`, chords `shift+...` / `orders+...`), with `pad_enabled`,
`pad_dead_zone` and `pad_sensitivity`. A missing line means the default.

## Verification

* `sealteam --test-pad`: pushes synthetic SDL gamepad events
  (`SDL_PushEvent`) and runs the game's per-frame input reads
  (`InputLayer::getKey` / `getMotion`) in the action, menu and map modes,
  printing the key codes and motion produced and PASS / FAIL against the
  default layout: Enter repeat while A is held, single presses of every
  key-channel button, Space with the 0x100 timer, the LB / RB chords,
  triggers, typematic repeat of a held D-pad key, the stick's Up / Down axis
  keys and motion values (12 at full deflection, curve, dead zone), the
  right stick, menu / map pointer motion (25), the modal prompt answers,
  capture and chord capture, rebinding with conflict clearing, the cfg
  names, and the off switch. Exit code = number of failures.
* `sealteam --controller-screen [1..5]` opens the remapping page directly
  (with a page number for screenshots).
* The key script of the developer commands (`--keys`, front/common.h)
  accepts pad tokens (`PadA`, `Pad~A` release, `PadLB`, `PadUp` d-pad,
  `PadLT`, `PadLX-` ...) that are pushed as synthetic SDL events. End to
  end through the real mission loop (`re/scratch_pad/mis_*.png`):
  * `--original --play-mission 1 --keys PadA@mission+4,Pad~A@mission+4.3,
    PadBack@mission+8,Pad~Back@mission+8.3 --shot map.bmp --shot-after 14`:
    A skips the insertion, Back (`m`) opens the map screen with its panel.
  * `... PadLB@mission+8,PadB@mission+8.2,Pad~B@mission+8.5,Pad~LB@mission+8.6`:
    the LB+B chord gives F2, the team view.
  * `... PadStart@mission+8,Pad~Start@mission+8.3 --shot-after 12`: Start
    gives Alt-P, the "Game Paused. Press Enter key to Continue." prompt
    (which A then answers through the dialog path).
* The five pages of the Controller screen and the start menu entry were
  checked by screenshot (`re/scratch_pad/pad_p*.png`, `main_page.png`).

## Limitations

* One pad at a time (the first connected; the next one after it is
  removed). Rumble, gyros and touchpads are not used.
* The layer is global: the same binding means the same key on every
  screen, like the joystick of the original. Base-layer letters can
  therefore act as hot keys on front-end screens (e.g. X = `n`).
* Keys typed by the pad go through the original throttle: rapid taps
  faster than one per 0x50 ticks (0.31 s) are dropped like on the keyboard.
* Chords use the layer at the moment of the press; releasing the modifier
  first and the button later is fine, pressing the modifier after the
  button re-types the button on the new layer.
* The original's joystick calibration screen is not reproduced (the dead
  zone option replaces it).
