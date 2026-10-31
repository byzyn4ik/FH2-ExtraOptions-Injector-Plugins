# Nitrous for Forza Horizon 2

Forza Horizon 2 Presents Fast & Furious gave the player a nitrous boost. This
plugin for the mod loader of
[Forza Horizon 2 Extra Options](https://github.com/RokkuDayo/ForzaHorizon2_ExtraOptions)
(and forza_injector's Xtra Options) brings it to Forza Horizon 2: hold the
**left stick click** (L3) or **Left Shift** with the throttle fully down, and the
engine's driving torque rises to 1.6 times over half a second, for up to 20
seconds per tank. The tank refills while you do not use it (60 seconds from
empty).

It adds no new physics: Forza Horizon 2's car physics still carries the
nitrous model Fast & Furious uses, unused. The plugin switches it on for the
player's car only, with Fast & Furious' tuning by default. The log says
when a boost starts and ends, why it ended and how full the tank is.

Fast & Furious' presentation is brought over as far as Forza Horizon 2 allows
(see [Gauge, camera and sound](#gauge-camera-and-sound)):

- **the gauge**: Fast & Furious' nitrous bar on the left of the speedometer,
  drawn by the [Performance](../performance/README.md) plugin's overlay;
- **the exhaust**: flames at every exhaust while boosting, Fast & Furious'
  own test in Forza Horizon 2's backfire code;
- **the camera**: Fast & Furious' 52 nitrous camera layers (FOV kick, shake,
  offsets, motion blur in every driving camera), from your own copy of the
  game (`tools/ff_camera.py`); without them the follow cameras' FOV kick;
- **the rumble**: Fast & Furious' nitrous pad rumble curves;
- **the sound**: Fast & Furious' NOS start, loop and stop sounds, made from
  your own copy of the game (`tools/ff_sounds.py`), else a synthesized one.

**Status: experimental (0.0.5).** The offline tests pass against the real
executable; in the game the plugin loads, arms the player's car and boosts
(0.0.1 in the game: boosts that ended after a fraction of a second at each
gear change, see [Short boosts](#short-boosts)). 0.0.3's exhaust flames,
camera layers and rumble work in the game; 0.0.4's rules and smooth ending
are tested offline. It is not in Forza Horizon
2's package (`games.json`); it builds and ships on its own
(`release/nitrous-<version>.zip`).

## Requirements

- Forza Horizon 2 1.0.0.64 (the plugin checks the game's code, byte for byte,
  and does nothing on any other build).
- Extra Options 0.4.4 (stock), or forza_injector with Xtra Options.
- On Fast & Furious it does nothing: that game has its own nitrous (Keyboard &
  Mouse binds it, Left Shift by default).

## Install

1. Close the game.
2. Copy the `mods\Nitrous` folder (from a release ZIP, or `out\mods` after
   building) into the `mods` folder beside the game's executable and
   `winmm.dll`:

   ```text
   mods\
     Nitrous\
       mod.json
       plugins\
         Nitrous.dll
         Nitrous.ini
   ```

3. Start the game. `FH2_ExtraOptions.log` shows lines starting with
   `NITROUS:`; `armed the player's car` appears once you drive.

To turn it off, set `Enabled=0` in `Nitrous.ini`, comment the folder out in
`mods\loadorder.txt` (`# Nitrous`), or delete the folder.

## Controls and settings

`mods\Nitrous\plugins\Nitrous.ini`, read when the game starts:

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | `1` | `0`: the plugin does nothing |
| `Key` | `LShift` | the keyboard key, read only while the game window has the focus: a letter or digit, `F1`..`F24`, `Space`, `Tab`, `CapsLock`, `Shift`/`LShift`/`RShift`, `Ctrl`/`LCtrl`/`RCtrl`, `Alt`/`LAlt`/`RAlt`, `Enter`, `Backspace`, `Insert`, `Delete`, `Home`, `End`, `PageUp`, `PageDown`, the arrows, `MouseLeft`/`MouseRight`/`MouseMiddle`/`Mouse4`/`Mouse5`, or a virtual-key code `0x..`; empty or `None`: no key |
| `PadButton` | `LeftStick` | the gamepad button: `LeftStick` (`L3`, not used while driving in Forza Horizon 2), `RightStick`, `A`, `B`, `X`, `Y`, `LB`, `RB`, `DPadUp`/`Down`/`Left`/`Right`, `View`, `Menu`; empty or `None`: none |
| `TorqueScale` | `1.6` | the engine's driving torque is multiplied by this while boosting (1 to 5) |
| `Duration` | `20` | seconds of boost in a full tank (0.5 to 600) |
| `BuildupTime` | `0.5` | seconds for a boost to reach the full `TorqueScale` (0.01 to 10) |
| `RechargeTime` | `60` | seconds to refill an empty tank while not boosting (0.5 to 3600); `0`: never (a new car starts full) |

The defaults are Fast & Furious' own (`entities_global.xml`, its
`NitrousPrototype`: TorqueScale 1.6, Duration 20, BuildupTime 0.5). Fast &
Furious refills the tank only when its mission flow says so (`RefillNitrous`
in its state flow; `SkillsRechargeModifier` is 0, so skills refill
nothing) and allows nitrous per mission event (`MissionEvents.Nitrous`).
Forza Horizon 2 has none of those missions, so the refill over time
(`RechargeTime`) is this plugin's stand-in.

Left Shift is Fast & Furious' nitrous key; Keyboard & Mouse 0.0.2 and later
put Forza Horizon 2's Clutch on Left Ctrl by default, as Fast & Furious does
(an older one, or an explicit `Clutch=LShift`, clashes: set `Key=` here). The boost needs the throttle at 99 % or more, as in Fast &
Furious: a keyboard throttle always is; on a pad, the trigger fully down. It
only drives positive (pulling) torque, so it does nothing on engine braking.

### `[Effects]`

| Key | Default | Meaning |
|---|---|---|
| `Fov` | `1` | the camera widens while boosting (Fast & Furious' follow cameras) |
| `FovDegrees` | `5` | degrees added at full boost (0 to 30) |
| `FovImpulseDegrees` | `5` | the squeeze when a boost starts, fading over about 1.7 s (0 to 30) |
| `FovCockpitDegrees` | `0` | the same kick for the cockpit, hood and bumper cameras (Fast & Furious: none) |
| `Sound` | `1` | the boost's sound (Fast & Furious' with `NitrousSounds\`, else a synthesized one) |
| `SoundVolume` | `0.6` | 0 to 1 |

The gauge's settings are in `Performance.ini`, `[Nitrous]` (see the
Performance plugin's README).

### For other plugins

`Nitrous.dll` exports two functions (`GetProcAddress` on the loaded module):

- `void WINAPI NitrousHold(BOOL held)`: `TRUE` asks for nitrous for the next
  250 ms (call it on every frame the key is down); `FALSE` releases it at
  once. Any thread. Keyboard & Mouse could call it for a binding of its own.
- `BOOL WINAPI NitrousTank(float *remaining, BOOL *boosting)`: the player
  car's tank (0..1) and whether it boosts now; `FALSE` before the first car.
- `BOOL WINAPI NitrousUsage(unsigned *boosts, float *seconds)`: boosts begun
  and seconds boosted since the game started (Fast & Furious'
  `NitrousTelemetryComponent` counts the same as `NitrousUsed`), for an
  achievements plugin.
- `BOOL WINAPI NitrousStatus(FH2NitrousStatus *status)`
  (`common/include/fh2_nitrous.h`): the tank, the effects' level (0..1: the
  torque's build-up, eased off after a boost), whether it boosts, is held,
  refills, a boost run is open, and whether the player drives (a local-car
  update with gameplay input in the last half second). Performance draws its
  gauge from it.

## How it works

Forza Horizon 2 1.0.0.64's car physics (`CCarDynamics`, behind the
`ForzaCar` at `+0x4c08`) has these nitrous fields (offsets in the object):

| Field | Meaning | Forza Horizon 2 | This plugin |
|---|---|---|---|
| `+0xdb0`, `+0xdb4` | engine-speed range (rad/s) | `0`, `0` | `0`, `1e6` |
| `+0xdb8` | torque scale | `1` | `TorqueScale` |
| `+0xdbc` | build-up time | (minimum) | `BuildupTime` |
| `+0xdc0` | tank, seconds | `0` | `Duration` |
| `+0x4f8` | request | never set | the key or button |
| `+0x4fc` | boosting | physics | read |
| `+0x500` | tank used, seconds | physics | lowered while idle (refill) |
| `+0x504` | this boost's time | physics | — |

- Each physics step (`0x12eb284`) boosts while the request is set, the
  throttle (`+0x1530`) is at least 0.99, the engine speed is in the range and
  the tank is not used up; it counts the tank and the build-up.
- The engine's torque (`0x12b42e0`, the check at `0x12b4332`) of a positive
  torque is scaled by `1 + (scale - 1) * min(boost time / build-up, 1)`.
- The engine-setup builder (`0x12e0a50`, the copy at `0x12e0e42`) fills the
  parameters from the car setup's nitrous block (`+0x98c`), which Forza
  Horizon 2's cars leave empty; it runs again when the game rebuilds the
  engine (`CCarDynamics` slot `0x238`), so the plugin writes them again
  whenever they change.

Fast & Furious fills the setup block from its `NitrousPrototype` entity and
rebuilds the engine (`0x5ef0d0`), copies its nitrous input (racing action 26,
held) into the request (`0x12fdf47`), and tests the raw throttle instead
(`0x1315370`).

The plugin runs in `FH2PluginVehicleUpdate`, on the local car only: the car
must be a `ForzaCar` and its dynamics a `TRefCountedObjectThreadSafe<CCarDynamics>`
(their vtables), else nothing is written. At start it compares five code
sites of the game (the step, the torque, the builder, the getters, the car's
accessor) byte for byte and checks the executable's size and time stamp; on
any difference it logs which and does nothing.

## Rules: where nitrous works, refills, crashes

`[Nitrous] Mode` sets them at once; any key written out overrides it:

| Key | Arcade (default) | Underground | What it does, how it is detected |
|---|---|---|---|
| `AutoRecharge` | `1` | `0` | the refill over time (`RechargeTime`) |
| `InRaces` | `1` | `1` | nitrous in races: Forza Horizon 2's own telemetry (`AnthemRaceStart` / `AnthemRaceEnd`, head-to-heads' `RaceEncounterStart` / `End`) |
| `FreeRoam` | `1` | `1` | nitrous in the open world (no event running) |
| `InOtherEvents` | `1` | `1` | showcases, bucket list challenges, Rivals, online road trips and rounds (their `…Start` / `…End` events) |
| `SkillRecharge` | `0` | `1` | each `SkillAchieved` event refills `[SkillRecharge]` seconds by skill group (Drift, NearMiss, Draft, Air, Pass, Speed, Wrecker, Contact, Other); each banked chain (`SkillChainCompleted`) `ChainPer1000` seconds per 1000 points |
| `CrashPenalty` | `0` | `4` | seconds of tank a crash costs; a crash is `CrashMinImpact` (25) km/h of the car's speed lost within 0.15 s, at most one a second |
| `CrashCancelsBoost` | `0` | `1` | a crash also ends the boost |

The telemetry is read through the game's import of
`EtwPlus.dll!EtxEventWrite` (the slot `0x1dd0060`, checked against the import
table; the event table at `0x2cb9280` checked by its first name); OpenBox
Achievements wraps the same slot, and either order works. A few events only
happen in free roam (arriving at a festival, a car meet, a board smashed, a
barn find, Rivals Everywhere, a road trip's end) and put the plugin back there,
in case an event ended without its end event. Where nitrous is not allowed the
button does nothing and Performance dims the gauge (`FH2_NITROUS_BLOCKED`); a
skill's refill lights the bar's tip for a moment (`FH2_NITROUS_GAINED`).
Fast & Furious' own rules (`RefillNitrous` from its missions,
`SkillsRechargeModifier` 0, `MissionEvents.Nitrous`) have nothing to attach to
in Forza Horizon 2.

## The end of a boost

Fast & Furious ends a boost at once: its physics step zeroes the build-up with
the boost flag (`0x1315400`, as Forza Horizon 2's does), so its camera input
falls to 0 and only the layers with their own decay (the follow cameras' FOV,
offset and blur, 0.5 per second) ease out; its exhaust test simply stops. The
plugin adds (on the player's car): the camera's Nitrous input and the rumble
fade from where they were over `EaseOut` seconds (1.5), the flames go on for
`ExhaustTail` seconds (0.35) and end with one pop, and the sustain sound fades
over 0.4 s under Fast & Furious' stop sample.
## Short boosts

In 0.0.1 a boost often lasted a fraction of a second (the log: "boost started
(tank 100 %)", "boost ended (tank 98 %)", again and again). The physics step
(`0x12eb284`) tests the throttle the physics uses (`+0x1530`, at least 0.99,
the constant at `0x2363580`), not the pedal: that throttle is cut by the
gearbox during every gear change (`0x12f0b13`: while `+0x540` runs) and
ramps back up with the throttle's smoothing, and the rev limiter and the
launch can cut it too. Fast & Furious tests the same throttle (the previous
step's copy at `+0x1198`, also at 0.99), so the cut itself is the game's.
What made it short: each cut resets this boost's time (`+0x504`), so the torque
fell back to 1x and built up again over `BuildupTime`, and a run of short
boosts felt like none.

Since 0.0.2 a boost **run** goes on through cuts shorter than a second while
the input is held: when the physics boosts again, the plugin puts the
build-up back (`+0x504`), so the torque returns at full scale at once; the
effects and the sound treat the run as one boost. The run ends when the input
is released, the tank is empty or a cut lasts a second, and the log says
which:

```text
NITROUS: boost started (tank 100 %, throttle 1.00).
NITROUS: boost ended after 6.2 s (tank 66 %): the input was released; 4 shorter cuts bridged, build-up kept.
NITROUS: boost ended after 0.4 s (tank 97 %): the throttle the physics uses fell below 0.99 (...) (throttle 0.62).
```

The other conditions were checked: the engine-speed range is all of it
(`0` to `1e6` rad/s); the request (`+0x4f8`) is not cleared by the game (the
plugin writes it on every update); `NitrousHold`'s 250 ms lapse only matters
to a caller that stops calling; the key is read only while the game window
has the focus, as before.

## Gauge, camera and sound

What is Fast & Furious' own and what is the plugin's:

| Part | Fast & Furious' | Ours |
|---|---|---|
| Gauge (Performance) | the bar's pixel shader (`FZMaterialPresetRadialNitrousBar`, ported from its DXBC), its colour `#2DB2FF`, its place and size beside the speedometer (`HUDSpeedometerControl.xaml`), its outline texture (`HUD_Nos_Outline_Radial_Bar`, read at run time from your Fast & Furious install's `SpireHUD.zip`) | drawing it in an overlay (Forza Horizon 2's HUD has no nitrous binding or bar material, so its XAML cannot be served as mod media); dimmed to 0.8 while refilling |
| Screen effect (Performance) | — (Fast & Furious adds motion blur, which the overlay cannot) | edges darkened and streaks while boosting |
| Exhaust flames | its backfire and burble updates (`0xfa3c40`, `0xfa4130`) test `IsNitrousActive` first: while boosting, a backfire (state 2) and a pop on every update, so every exhaust spits flames | the same test at the entry of Forza Horizon 2's `0xf7d8a0` / `0xf7dd70` (`ExhaustFlames`); the flames are Forza Horizon 2's own backfire effect, which is Fast & Furious' too (same `Particles.zip`) |
| Camera | its `CameraPhysics.xml`: 52 layers on the inputs `Nitrous` (35) and `NitrousImpulse` (17): FOV, shake (Perlin, Simplex, height-grid, sine), offsets, impulses and motion blur in the follow, cockpit, hood and bumper cameras; the inputs computed by its `0x105d860` (`0x105dfc0`, `0x105e014`) | `tools/ff_camera.py` puts its file into the mod's media with the two inputs renamed to `ShoulderYaw` / `ShoulderRoll` (parsed by Forza Horizon 2, never computed); the plugin computes them as Fast & Furious does in Forza Horizon 2's `0x1037df0` (`CameraLayers`). Without the file: the follow cameras' FOV kick only, through their FOV getter |
| Pad rumble | `ControllerRumble.xml` `<Nitrous>` curves (pad and wheel), its input `IsNitrousActive * (1 - tank left)` (`0x303d40`), added to the other inputs' sum (`0x109c4d0`) | the same curves and input, added after Forza Horizon 2's sum in `0x1076010` (`Rumble`); the impulse-trigger curve (`ControllerHaptics.xml`) is not reached |
| Sound | `HZ2_SFX_NOS_Start_V3`, `Sustain_V4`, `Stop_V3` (`HZ2_SFX_UI.bank`, `/Master/NOS/NOS`), decoded by `tools/ff_sounds.py` | the playback (start, sustain looped while boosting with a crossfaded seam, stop) and, without the files, a synthesized burst and hiss |

Forza Horizon 2's camera has no `Nitrous` / `NitrousImpulse` inputs (its
`CameraPhysics.xml` is Fast & Furious' without the 52 nitrous layers, and the
executable lacks the input names). Fast & Furious' own layers are brought in
as mod media:

```text
py plugins/nitrous/tools/ff_camera.py --mods "<game>\mods"
```

writes `mods\Nitrous\media\Camera\CameraPhysics.xml` (checked to be Forza
Horizon 2's file plus the nitrous layers). Fast & Furious computes `Nitrous`
as the speed's ramp (0 at 1 mph, 1 from 30 mph, its `CameraNitrous`
tunables) times the build-up (boost time / build-up time, at most 1), and
`NitrousImpulse` as that ramp on the first update of a boost; the plugin
does the same for the two carrier inputs. The layers' attack, decay, curves
and outputs are Fast & Furious' data, run by Forza Horizon 2's own camera
code.

Without that file the plugin chains the FOV getter (slot
`0x70`) of `CCamFollow`/`Low`/`High` and `CCamDriver`/`Hood`/`BumperHigh` after
whatever is there (Extra Options' own FOV option keeps working) and scales the
result by `(base + kick) / base`.

Both games use FMOD Studio with XMA2 samples; Forza Horizon 2 could play Fast &
Furious' bank only with a patched copy of it and its Studio system found in
memory, so the samples are decoded once instead:

```text
py plugins/nitrous/tools/ff_sounds.py --ffmpeg <ffmpeg.exe> --mods "<game>\mods"
```

writes `mods\Nitrous\plugins\NitrousSounds\nos_start.wav`, `nos_sustain.wav`
and `nos_stop.wav` (48 kHz stereo) from your Fast & Furious install (ffmpeg's
xma2 decoder). Nothing of either game is in this repository or its releases.

## Limits

- **Online**: Forza Horizon 2 on Azimuth has no online sessions, so there is
  nothing to detect; with a multiplayer mod, others would see you boost. Turn
  it off there (`Enabled=0`). AI and remote cars are never touched.
- **The gauge needs Performance** (its overlay); without it there is none.
  Its impulse-trigger rumble is not rebuilt.
- A tuning or dyno screen that draws the torque curve does not show the boost
  (the plugin leaves `HasNitrous`, `+0xdac`, off).

## Build and test

```text
py build.py --zig <zig.exe> --plugin nitrous --test
```

`tests/run_tests.py` needs no game run: the per-frame logic on a fake car,
and Forza Horizon 2's executable (when present: `~/FH2/Anthem_1.0.0.64_x64__8wekyb3d8bbwe`)
mapped in memory for the code guards, the vtables' run-time type names and
the plugin's start.
