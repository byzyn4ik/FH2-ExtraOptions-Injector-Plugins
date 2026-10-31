# Custom Radio for Forza Horizon 2 Extra Options

Your own music (MP3 and FLAC, also WAV, M4A and WMA) as an extra station on Forza Horizon 2's car radio. This is a
plugin for the mod loader of
[Forza Horizon 2 Extra Options](https://github.com/RokkuDayo/ForzaHorizon2_ExtraOptions):
the music files of a folder play, in folder order, as one more station of the
game's own radio, so the in-game radio volume, ducking, pausing and station
banner all apply. Windows decodes the files. The HUD's radio display shows the
station with a logo of its own (or your own PNG) where the game would draw a
white box.

It needs no fork of Extra Options: it is one DLL (and its logo textures) in a
mod folder, loaded by a stock Extra Options 0.4.4. It is the custom radio of
the Extra Options pull request "custom radio", made into a standalone plugin.

## Requirements

- Forza Horizon 2 on Azimuth (tested offline against the game executable that
  every Azimuth version runs, `forza_durango_release_final.exe`).
- Forza Horizon 2 Extra Options 0.4.4 or later (its mod loader).

Or Forza Horizon 2 Presents Fast & Furious (Spire 1.0.0.6,
`forza_durango_release_final.exe`, TimeDateStamp `0x55b62fde`, SizeOfImage
`0x3a89000`) on forza_injector: see [Fast & Furious](#fast--furious).

## Install

1. Close the game.
2. Copy the `mods\Custom Radio` folder (from a release ZIP, or `out\mods`
   after building) into the `mods` folder beside `Azimuth.exe` and Extra
   Options' `winmm.dll`, and put your MP3 and FLAC files in a `Music` folder beside
   `Azimuth.exe`:

   ```text
   Azimuth\
     Azimuth.exe
     winmm.dll
     Music\
       01 First song.mp3
       02 Second song.mp3
     mods\
       Custom Radio\
         mod.json
         plugins\
           CustomRadio.dll
           CustomRadio.ini
           CustomRadio\Media\UI\Textures\Anthem\HUD\   (the station logos)
   ```

3. Start the game. Extra Options adds the folder to `mods\loadorder.txt` and
   loads the plugin; `FH2_ExtraOptions.log` shows lines starting with
   `CUSTOM RADIO`. The station (`MY MUSIC` by default) is added when the game
   loads its radio stations, if the folder has playable music files.

To turn it off, set `Enabled=0` in `CustomRadio.ini`, or comment the folder
out in `mods\loadorder.txt` (`# Custom Radio`).

## Keys

| Key | Default | Meaning |
|---|---|---|
| `RadioStation` | F9 | tunes the radio to the custom station |
| `RadioNext` | F7 | next track |
| `RadioRestart` | F6 | restarts the track; within 3 seconds of its start, the previous track |

The keys work only while driving with the game window focused (Extra
Options' "gameplay input" state: not in menus, loading or the pause menu).
Each press acts once; a key held from a menu must be released first. They do
not clash with Extra Options 0.4.4's own default keys (F1-F4, Tab). Xbox
buttons can be added in the INI (none by default). The D-pad radio cycle of
the game also reaches the custom station.

## Settings

`mods\Custom Radio\plugins\CustomRadio.ini`, read when the game starts (Extra
Options' own `FH2_ExtraOptions.ini` is not used or changed). An empty or
missing value means the default; a value that is not valid is logged and its
default used.

| Section | Key | Default | Meaning |
|---|---|---|---|
| `[CustomRadio]` | `Enabled` | `1` | add the station |
| | `Folder` | `Music` | the music folder: full, or relative to the folder of `Azimuth.exe` (`..` allowed) |
| | `Name` | `MY MUSIC` | the station name shown in game (`MOJA MUZYKA` for Polish, `МОЯ МУЗИКА` with Language Support's Ukrainian pack in the `ru-RU` slot) |
| | `Volume` | `100` | station volume in percent, 0-400 |
| | `UnlockAllStations` | `0` | every station is available, whatever the progression (also with `Enabled=0`) |
| | `PcmDecode` | `1` | `1`: Windows decodes the MP3s; `0`: the game's FMOD decoder plays them (it loses the centre of joint-stereo files) |
| | `Logo` | (empty) | the station's logo on the HUD: empty for the built-in one, or a PNG file (full path, or relative to the `plugins` folder); see [Logo](#logo) |
| `[Keys]` | `RadioStationKeyboard` | `F9` | |
| | `RadioNextKeyboard` | `F7` | |
| | `RadioRestartKeyboard` | `F6` | |
| | `RadioStationXbox`, `RadioNextXbox`, `RadioRestartXbox` | `None` | |

Keyboard keys: `F1`-`F24`, `A`-`Z`, `0`-`9`, `Escape`, `Enter`, `Space`,
`Tab`, `Backspace`, `Insert`, `Delete`, `Home`, `End`, `PageUp`, `PageDown`,
`Up`, `Down`, `Left`, `Right`, `Shift`, `Ctrl`, `Alt`, a virtual-key code
(`0x78` or `120`), or `None`. Xbox buttons: `A`, `B`, `X`, `Y`, `LB`, `RB`,
`DPadUp`, `DPadDown`, `DPadLeft`, `DPadRight`, `View`, `Menu`, `LeftStick`,
`RightStick`, combined with `+` (`LB+Y`), or `None`.

A `Name` with characters outside your Windows code page needs the INI saved
as Unicode (UTF-16 LE, "Unicode" in Notepad); Windows reads other INI files
in the code page.

### Logo

The game picks a station's logo on the HUD's radio display (above the track
title, right of the speedometer) by the station's name, from a list of its own
stations; any other name gets a plain white 196x96 box. The plugin adds its
station's names to that list, with its own logos:

| Station name | Logo |
|---|---|
| `MY MUSIC` (default) | a record with an eighth note, and MY MUSIC |
| `MOJA MUZYKA` (default with Polish game text) | the same, with MOJA MUZYKA |
| `МОЯ МУЗИКА` (default with Language Support's Ukrainian pack) | the same, with МОЯ МУЗИКА |
| any other `Name` | the record and note alone |

![The built-in logos](logo/my_music.png)

The art is in `logo\` (drawn by `tools\make_logo.py`); the build makes it into
the game's texture format (`HUD_RadioIcon_CustomRadio*.swatchbin`: 196x96,
BC7 with premultiplied alpha, stored exactly as the game's own radio logos:
format `0x2e`, 64x32 blocks in the GPU's 2D tiling, the same header; an RGBA8
texture of that size is drawn as a white box) in the plugin's media folder
`plugins\CustomRadio\Media`, which `mod.json` names as the mod's media (and
in `media\` for Fast & Furious). The plugin also makes them again from
`plugins\Logo*.png` when it starts (the same bytes: BC7 mode 6 blocks written
by `src\texture.c` and `tools\logo_texture.py` alike).

Like the game's own logos, the art keeps clear of the right and bottom edges
(x 1..188, y 2..88) and the texture carries the game's soft black drop shadow
(2 px right, 3 px down, blurred, 30%; measured on the game's logos), which
keeps white art readable over a bright road or sky. Without it (0.0.4 and
earlier) the white logo washed out against Fast & Furious' sunny roads.

`Logo=` takes your own PNG instead (for any station name): transparent
background, light artwork for the dark HUD, ideally 196x96 pixels (used as
it is: keep the art inside x 1..188, y 2..88 so its shadow fits). Any other
size from 8x8 to 4096x4096 works: it is scaled with its aspect kept into that
art area (188x87), against its right edge and centred vertically, like the
game's logos. The drop shadow is added either way. When the
game starts, the plugin converts it into
`CustomRadio\Media\...\HUD_RadioIcon_CustomRadio_User.swatchbin` (written only
when it changed, removed again when `Logo=` is emptied). A file that is
missing, not a PNG, smaller than 8x8 or larger than 4096x4096, fully
transparent or that Windows cannot decode is logged (`CUSTOM RADIO:
Logo=... is not used: <why>; the built-in logo is shown.`) and the built-in
logo is shown.

### Music

Files ending in `.mp3`, `.flac`, `.wav`, `.m4a` (AAC or ALAC) or `.wma`
directly in the folder (no subfolders), mixed, in the order Windows lists them
(by name), up to 2048 files (MP3s of up to 256 MB each). Titles and artists
come from the ID3v2 (`TIT2`, `TPE1`) or ID3v1 tag of an MP3, the Vorbis
comments (`TITLE`, `ARTIST`) of a FLAC, the tags Windows reads from the other
formats (Title, Contributing artists), else the file name. A file whose length
cannot be read, or that is shorter than 3 seconds, is skipped (and logged).
The playlist wraps around at both ends.

MP3s are decoded by Windows' MP3 codec (or FMOD with `PcmDecode=0`); FLAC,
WAV, M4A and WMA by Windows' Media Foundation (`mfplat`/`mfreadwrite`, loaded
when the first such file is met; FLAC needs Windows 10 1709 or later, ALAC
1809), always to 16-bit PCM: 24-bit becomes 16-bit, mono is played on both
sides, more channels are mixed down to stereo, and rates above 48 kHz are
resampled to 44.1 kHz (multiples of it) or 48 kHz. Without Media Foundation
these files are skipped and logged (`skipped (no Media Foundation to decode
it)`). Each track is decoded whole when it is played (and the next one in the
background): about 10 MB of memory per minute of 44.1 kHz audio, for the
playing track, the next and, for 15 s, the previous; decoded tracks are kept
within 512 MB. The log names each track served (`streaming (PCM) <file>`) and
any file Windows cannot decode (`Windows could not decode <path>`).

## How it works

The game reads `media\audio\RadioInfo_<language>.xml` and builds one station
per `<RadioStation>`; each track names an FMOD Studio event and a sound key.
The plugin hooks:

| Site (RVA in the game) | Kind | What the plugin does |
|---|---|---|
| CXML4Resource read, slot `0x2ec4fd0` | virtual-table slot | adds one `<RadioStation>` with `FH2MUSIC_<n>` sound keys to RadioInfo |
| getSoundInfo call `0x10189a0` | rel32 call | answers `FH2MUSIC_<n>` with the decoded file in memory |
| getSubSound call `0x10221f2` | rel32 call | returns the stream itself for its own sounds |
| radio player update, slot `0x2e76248` | virtual-table slot | next track / restart / previous on the radio thread |
| playlist next pick, slot `0x2e76740` | virtual-table slot | folder order with wrap-around for its own playlists |
| recently-played search call `0x1009140` | rel32 call | its own station ignores the recently-played list |
| IsStationLocked calls `0x660fd1`, `0x78a092`, `0x8fc8bf` | rel32 calls | the custom station (or, with `UnlockAllStations=1`, every station) is never locked |
| ChangeStation calls `0x8fc659`, `0xc9fad5` | rel32 calls | the D-pad cycle includes the custom station |
| XAML resource call `0x16ebb37` (shared) | rel32 call | `Controls/HUDRadioControl.xaml`: one `DataTrigger` per name of its station in the station icon style, pointing at its logo |

and calls the game's `RadioModule::SetStation` (F9), its event timeline and
volume calls, its allocator and its resource object functions. All other
sounds, stations and files go to the game unchanged.

The HUD's radio display is a UserControl whose XAML (`Controls\HUDRadioControl.xaml`
in `media\UI.zip`) is loaded through UIRenderer's XAML load (its vtable slot
`0x1d8`), which reads the text through the call at `0x16ebb37`: the same call
the pause and options pages go through, so Extra Options 0.4.4 (its thunk at
`0x13fe354`) and the other plugins of this repository chain it too. For that
control only, the plugin adds its triggers right after
`<Style.Triggers>` of `A_Style_FZMaterial_StationIcon` and hands the text back
in a new resource object, as Extra Options does for its own edits; the stock
stations' triggers and everything else stay as they are. The trigger's
`/ForzaUI;component/Textures/Anthem/HUD/<name>.png` is read as
`media\UI\Textures\Anthem\HUD\<name>.swatchbin`, which Extra Options finds in
the mod's media folder (`FH2PluginMediaDirectory` names the same folder).

Every site is checked byte for byte first (`src\sites.h`: the hooked
function's first bytes, the slot's value, and the 8 bytes around each call).
A slot or call that another module has hooked already (code outside the
expected function) is chained: the plugin calls that code instead of the game
function. A group of sites is written only when all of them pass, through an
interlocked compare-and-swap of the checked value; on any mismatch the plugin
logs the site and leaves that group, or the whole station, alone. Calls are
redirected through jumps in a memory page of the plugin's own next to the
game; no bytes of the game other than the slots and the call displacements
change.

Extra Options 0.4.4 does not hook any of these sites itself but the shared
XAML resource call (its XML hook is the next slot of the same table,
`0x2ec4fe0`), and its mod-loader plugin API
gives the plugin the local car's update with the gameplay-input state, which
is where the keys are read.

The plugin starts when Extra Options loads mod plugins (on its first game
file lookup, on a worker thread). If the game had already read its radio
stations by then, the log says so and the station appears from the next
start.

## Fast & Furious

The same DLL runs on Forza Horizon 2 Presents Fast & Furious (forza_injector
loads it; `mod.json` lists `fh2ff`). The game is told apart by its image size
and TimeDateStamp; Fast & Furious' sites are in `src\ff_sites.h`, which
`tools\port_ff_sites.py` writes from `sites.h` with Keyboard & Mouse's porter
(masked instruction patterns, callers, vtable slots, RTTI) and checks: every
hooked or called function is the same instructions from entry to end as in
Forza Horizon 2, except two of RadioModule (below); the radio's own classes
(RadioSystem, RadioStation, RadioPlaylist, RadioStreamFmod: every virtual
function) are the same instructions, so every struct offset the plugin reads
is the same.

| Forza Horizon 2 | Fast & Furious | |
|---|---|---|
| CXML4Resource read slot `0x2ec4fd0` | `0x2f0d320` | |
| getSoundInfo call `0x10189a0` | `0x103e1b0` | |
| getSubSound call `0x10221f2` | `0x10479c2` | |
| radio player update slot `0x2e76248` | `0x2ebe528` | |
| playlist next pick slot `0x2e76740` | `0x2ebea20` | |
| recently-played call `0x1009140` | `0x102e990` | |
| IsStationLocked calls `0x660fd1`, `0x78a092`, `0x8fc8bf` | `0x655cc1`, `0x7c0cb2`, `0x943d0f` | Fast & Furious' IsStationLocked also counts a name set at RadioModule+`0xd8` as locked; same arguments and result |
| ChangeStation calls `0x8fc659`, `0xc9fad5` | `0x943be9`, `0xce61a5` | ChangeStation's last two RadioModule fields moved by `0x10`; the plugin reads `+0x60` and `+0x68`, which did not |
| XAML resource call `0x16ebb37` | `0x1712d47` | |
| SetStation `0x65b310` | `0x653840` | |

Differences:

- **Stations.** Fast & Furious has two stations (Spire Radio, Spire Score)
  with track playlists only and no stinger or DJ sound banks, so its custom
  station has no stinger or DJ playlists either. It is number 3.
- **Keys.** Fast & Furious has no Extra Options runtime, so nothing calls
  `FH2PluginVehicleUpdate` there. The plugin hooks the local car's update
  itself (`src\ff_tick.c`): ForzaCar's vtable `0x2c9ca30` slot `0x80`
  (`0x2c9cab0` -> `0x2af960`), compare-exchanged, chained to another module's
  hook when there is one; for the local player's car only (player manager
  `0x3515e60`, `0x225080`, `0x2e0bb0`, as the runtime finds it on Forza
  Horizon 2). The keys work while the local car updates and the game window is
  focused (Fast & Furious has no runtime to report Extra Options' "gameplay
  input" state, so a key pressed in the pause menu can act). Xbox buttons come
  from the selected controller (manager `0x36c6fd8`, controllers of `0x1480`
  bytes). If these sites do not match, the keys are off and logged; the
  station and the D-pad still work.
- **Logo: with Language Support.** Fast & Furious' radio display hides the
  icon for its own stations and has the same station icon style (the XAML call
  `0x1712d47` is found and checked). On Fast & Furious only Language Support
  serves mod media, and only a mod's `media\` folder (not `mod.json`'s
  `"media"`), so the package carries the logos twice: in
  `plugins\CustomRadio\Media` (Forza Horizon 2) and in `media\UI\Textures\
  Anthem\HUD` (Fast & Furious). The plugin adds its triggers on Fast & Furious
  when Language Support's mount is installed (CFileSys' resolver slot
  `0x2eb7208` holds code of `LangSupport.dll`) and the mod's `media\` is
  there; `Logo=` is then written into `media\`. Without it the HUD is left as
  the game has it, the log says so, and the station works without its logo.
- **Language.** The default name follows Fast & Furious' text table manager
  (`0x36cc1b0`) and Language Support's Ukrainian pack for Fast & Furious.

## Building

Python 3.10+ and [Zig 0.15.2](https://ziglang.org/download/) (C, every file
built with `-Wall -Wextra -Werror`; no Visual Studio needed). Like Extra
Options' own plugins, the DLL has no C runtime of its own: it links import
libraries made from Windows' `kernel32`, `user32` and `msvcrt`.

From the repository's root:

```text
py build.py --zig <zig-x86_64-windows-0.15.2>\zig.exe --plugin custom-radio --test
```

Output in `out\`: `mods\Custom Radio\` (the mod to install),
`release\custom-radio-<version>.zip` and
`build\custom-radio\obj\CustomRadio-test.dll` (the same code with test
exports). `--test` runs the offline tests, with the game executable and Extra
Options 0.4.4's release from below `--game` (default `%USERPROFILE%\FH2`) when
they are there. CI (GitHub Actions) builds and tests every push and pull
request; a `v*` tag publishes the release ZIPs.

## Testing

`py tests\run_tests.py` (after a build; `build.py --test` runs it) checks,
without the game:

1. **Package**: `mod.json` as Extra Options validates it (its media folder),
   the folder layout, the DLL (x64, exactly the loader's three exports, only
   Windows imports), the INI and the logos (196x96 BC7 swatchbins of
   `logo\*.png` premultiplied with the drop shadow, a small error; the art
   in the art area). With `--game-exe` and `--ff-exe` the game's own station
   logos are checked: 196x96 BC7 with our header byte for byte, their padding
   blocks empty where our block tiling puts the padding, all nine with a baked
   drop shadow, and ours with a shadow in the range of theirs (share of the
   art, alpha, offset below); and `tools\logo_texture.py` writes the game's
   RGBA8 UI textures back byte for byte.
2. **Sites**: `src\sites.h` is well formed; with `--game-exe <path>` (or
   `FH2_GAME_EXE`) every guard byte is compared with
   `forza_durango_release_final.exe` and none of them is relocated.
3. **Coexistence**: the default keys are not Extra Options 0.4.4's; with
   `--extra-options <Azimuth folder>` (or `EO_RELEASE_DIR`) its INI bindings,
   `winmm.dll` and bundled plugins are checked too (no key and no hook site
   in common but the shared XAML resource call).
4. **Config**: INI parsing (defaults, valid and bad values, unknown keys,
   relative, absolute and UNC folders, key and button names).
5. **Keys**: one action per press, only while driving, release before rearm.
6. **MP3, FLAC, WAV**: generated FLACs (`tests\flac_writer.py`: 8, 16 and
   24-bit, mono and stereo, 44.1 and 96 kHz, Vorbis comments) and a WAV
   through Media Foundation (tags, lengths, the 16-bit stereo PCM and its
   samples; a broken FLAC skipped), the user's own FLACs (`FH2_MUSIC_DIR`,
   read only) when there are some, a FLAC between the MP3s in the plugin
   scenarios (folder order); generated silent MP3s (MPEG-1 Layer III): ID3v2 and ID3v1 tags,
   Xing and constant-bit-rate lengths, and the Windows decode to 16-bit PCM.
7. **Logo**: the DLL's texture writer against `tools\logo_texture.py`;
   the drop shadow (the DLL's and the tool's byte for byte; black, 2 px right,
   3 px down, at most 30%); `Logo=` PNGs accepted (196x96 as it is; larger,
   square, RGB without alpha: fitted into the art area against its right
   edge) and refused (missing, not a PNG, too small or
   large, fully transparent, broken).
8. **Plugin**: the test DLL loaded with a fake game image built from
   `sites.h`, one child process per scenario: vanilla sites; sites already
   hooked by another module (chained: RadioInfo injection, sound and subsound
   serving, the HUD radio display's logo triggers through the XAML resource
   call (a cut-down control and, with `--game-exe`, the game's own
   `HUDRadioControl.xaml`: the triggers come first, nothing else changes, the
   result is well-formed XML, the original resource is released), the station
   lock, the D-pad and playlist hooks, and the keys through
   `FH2PluginVehicleUpdate` to the radio thread); a mismatch in the station's
   core (nothing written) and in an optional group (only that group left
   alone); `Enabled=0`; `UnlockAllStations=1` alone; a slot without code;
   another image size; `Logo=` used (its texture written) and refused (an
   earlier one removed, `Name` with `&` and `{`); the media folder missing
   (the station without its logo); `FH2PluginMediaDirectory`.

## License

GPL-3.0 (see `LICENSE` at the repository's root). The logos in `logo\` are
this project's own artwork (the wordmark set in Windows' Bahnschrift). The station code comes from
Forza Horizon 2 Extra Options (the custom radio pull request) and
`common\include\fh2_plugin.h` from Extra Options itself, both GPL-3.0.
