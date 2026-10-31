# Xtra Options (Fast & Furious)

The [Xtra Options](../xtra-options) byte-patch features that also work in
**Forza Horizon 2 Presents Fast & Furious** (Spire 1.0.0.6,
`forza_durango_release_final.exe`, TimeDateStamp `0x55b62fde`, SizeOfImage
`0x3a89000`), one forza_injector plugin DLL each:

| DLL | `XtraOptions.ini` | What it does | Sites |
|---|---|---|---|
| `SkipIntros.dll` | `[Frontend] SkipIntro=1` | skips the developer opening videos | 4 |
| `PhotoMode.dll` | `[PhotoMode] FreeCamera=1` | no camera collision and boundaries in photo mode | 3 |
| `ParentalLocks.dll` | `[Compatibility] DisableParentalLocks=1` | content privilege checks allowed locally | 2 |
| `PhotoModeUnlock.dll` | `[PhotoMode] UnlockEarly=0` (off by default) | photo mode before the game unlocks it (Fast & Furious only); not needed in normal play: photo mode in free roam is a long press of D-pad up | 1 |
| `MapTeleport.dll` | `[Map] Teleport=1`, `TeleportButton=X`, `TeleportHold=500` | hold X on the world map to travel to the map cursor, with the game's own fast travel, free (Fast & Furious only) | 2 slots, 20 guards |
| `TreasureMap.dll` | `[Map] TreasureMap=0` (off by default) | the world map shows every discount board and barn-find spot, as Forza Horizon 2's Treasure Map does (Fast & Furious only: it sells no Treasure Map, but the code is FH2's; see `src/features/treasure-map/treasure_map.c`) | 9 + 1 slot |

The DLLs are Xtra Options' own `src/patch_feature.c`, built with these
features' tables (`src/features/<feature>/`): the same ids, bytes and INI keys
as the FH2 features, at Fast & Furious' sites. `tools/port_feature.py` found
each site from the FH2 one by the code around it (relative displacements
masked; exactly one match; the same original bytes there). Each DLL checks
the game image and every site's bytes before it writes anything; on another
game or build it changes nothing and logs why.

`PhotoModeUnlock.dll` has no FH2 counterpart. WaitForPause asks
`0x9a7250` whether photo mode may open; besides the game's context checks it
needs a flag, byte `+0x3d` of the record `0xa9c4b0` looks up, which keeps
photo mode locked until the game sets it. The DLL turns that one flag load
(`movzx edi, byte [rax+0x3d]` at `0x9a7265`) into `mov dil, 1`; nothing else
reads it there, and the other conditions stay. Its tests check the code
around the site and that it exists once in the executable.

### Map Teleport

`MapTeleport.dll` has no FH2 counterpart and carries its own code
(`src/features/map-teleport/`). On the world map (MapScene, vtable
`0x2e48a70`), X is the game's fast travel (`0xda2a20`, bound to UI key
`0x43`): to a snapped festival site or house, or anywhere on the map, at a
price and after a confirmation, once every fast-travel board is smashed. Y
sets or clears the GPS waypoint (`0xd7a1f0`).

- **Hold, anywhere on the map.** MapScene's update (slot 153, `0xda0080`)
  polls the button every frame through its axis (`0x1095250`, UI context 7,
  action `0x8e` for X / `0x91` for Y, on the controller the map reads:
  `0xd43e60` -> slot `0x140`). Not the state query (`0x10948e0`): the game's
  key translation consumes the press (consume mask `+0x1e8`, read by
  `0x1095100`), so the state reads "up" right after the press, and Keyboard &
  Mouse answers a consumed key as up too; the axis is not consumed. Held for
  `TeleportHold` ms: travel.
- **A tap stays the game's.** When the game's handler for the press comes
  (the closure invoke slot, X: vtable `0x2e52f30` +0x10, Y: `0x2e52ff0`
  +0x10; it only comes where the game has an X action, e.g. over the house),
  it is held back: released before `TeleportHold`, it runs; held, it never
  runs. `TeleportHold=0` travels on the press.
- **Legend.** X's entry of the map's button bar (`0xd44610(page)`, slot 2 =
  XDescription, `0xba1e90`, as the map sets its other buttons each frame)
  reads "Teleport (hold)" in the game's language (Language Support's pack
  counted; a CUserString, vtable `0x2ef91c0`, made a UI string by
  `0x1431f40`), in free roam, everywhere on the map; over a house it replaces
  the game's "Fast travel" text (a tap still fast travels there). Keyboard &
  Mouse draws its keycap over the icon. With `TeleportButton=Y` there is no
  entry (Y's text is the map's waypoint text).
- **Log.** One line per map, per press (pressed, released after N ms, the
  game's handler run) and per travel or refusal.
- **Where.** The world point under the map cursor that the controller
  (MapViewControllerInteractive, page `+0x428`) picks every frame, `+0x3d0`
  (a snapped map item's position on one); it is the point the game's own
  fast travel uses. If it is unusable, the cursor's map point (`+0x2c0`,
  `+0x2c8`).
- **How.** The game's fast travel to a position, FastTravelModule
  `0x561d90(module, &position, cost 0)`; the module comes from `0x90e9b0` on
  the entity root `0x352b3b0`. It posts FastTravelInitiated and the game does
  the rest as for any fast travel: it moves the car to the nearest road node
  (`0x55be40` -> `0x55ee10`: `0x193ee60`, facing along the road, `0xf97820`)
  and waits for streaming (WaitForFastTravelStreaming). The map closes as
  the game's fast-travel confirmation closes it (`0xd6bd90`).
- **Only in free roam.** The event context (`0x219530` on the root, `+0x60`,
  2 = free roam); in events, races and missions the button stays the game's.
- **Guards.** 20 sites (`patches.json`, the code around each address the DLL
  uses or relies on) are checked byte for byte and left unchanged; the two
  slots must hold the game's own function. On any mismatch or another build
  nothing is written and the log says why. Keyboard & Mouse answers the
  state query from the key bound to MenuX (X) or MenuY (Y).

Not ported: Unlocks (Fast & Furious has no DLC or VIP, and several of its
sites differ), the online menu tabs (no match), Aero Tuning (its own code
reads FH2 structures) and the Extra Options runtime (`XtraRuntime.dll`).

## Install

`mods\Xtra Options (Fast & Furious)\` beside `azimuth.exe` with
forza_injector's `winmm.dll`. `mod.json` limits the mod to `fh2ff`.

## Build and test

```text
py build.py --zig <zig.exe> --plugin xtra-options-ff --test
```

The tests compare every site with the FH2 feature, read the real Fast &
Furious executable (`--ff`) and call each DLL as forza_injector does on a
mapped copy of it (all on, one key off, a changed site, another build).
Map Teleport adds: its game facts from the executable (RTTI of the three
vtables, the slots' functions, the X/Y bindings and UI actions, the fast
travel calls, the event context), its slots per setting (X, Y, a bad value,
off, a slot another module hooked, another build), and a C test of its
game-free logic (settings, the target from made-up map controller data, the
hold of the button).
