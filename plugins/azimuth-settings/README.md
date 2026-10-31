# Azimuth Settings

All of Azimuth's per-game settings as rows of the game's own settings pages:
graphics on the **Video** page, sound and language on the **Audio** page. The
rows only pass the settings through: they read and write Azimuth's
`data\azimuth.ini` (the running game's `[title-<id>]` section, and
`[launcher] display`), apply at once what can already be applied while the
game runs (frame rate, display mode, shadow distance and redraw, volume), and
leave everything else to Azimuth, which reads the INI at the next start.

One DLL in a mod folder, loaded through the plugin API of
[Forza Horizon 2 Extra Options](https://github.com/RokkuDayo/ForzaHorizon2_ExtraOptions)
(`FH2PluginInitialize`), which forza_injector calls the same way. Games:

| Game | Title (`[title-...]`) | Pages |
|---|---|---|
| Forza Horizon 2 1.0.0.64 | `1a96545b` | Video and Audio |
| Forza Horizon 2 Presents Fast & Furious 1.0.0.6 | `5a9771a4` | Video and Audio |
| Forza Motorsport 5 / 6 (or any other image) | | none: the log says so and nothing is installed |

**Do not install it together with
[Azimuth Graphics Options](../azimuth-graphics-options/README.md)**: both add
rows for the same keys to the same pages. Azimuth Settings replaces it in the
game packages; when `AzimuthGraphicsOptions.dll` is loaded in the process it
installs nothing and logs why (and if the other DLL loads later, its rows are
left alone and these are not added).

## Rows

Every key and value is the one Azimuth 1.0.4's INI reader takes (`azimuth.exe`
`0xabe70`; its defaults `0x9d70`, its writer `0xb5980`; 1.0.2 reads the same).
"At once" needs the live site to be as inspected (see the log); otherwise the
row's description says a restart is needed.

| Page | Row | Choices | Key (Azimuth's default) | Applies |
|---|---|---|---|---|
| Video | Frame rate | Azimuth's launcher list (below) | `frame_rate` 30..1000 (the title's own, 30) | at once |
| | V-sync | Off, On | `vsync` = `off`, `on` (on) | restart |
| | Base resolution | 960 x 540 ... 3840 x 2160 | `resolution` = lines; Azimuth takes 360..4320 (1080) | restart |
| | Aspect ratio | 16:9, As the display | `aspect` = `16:9`, `display` (16:9) | restart |
| | Display mode | Windowed, Fullscreen | `[launcher] display` | at once (as Alt+Enter) |
| | HDR | Off, On | `hdr` (off) | restart |
| | Graphics | Low, Default, High, Ultra, Custom | `graphics`, and all nine details below | shadow distance and redraw at once, the rest restart |
| | Shadow detail | Low, Default, High, Ultra | `shadow_detail` (default) | restart |
| | Shadow distance | Default, High, Ultra | `shadow_distance` (default) | at once |
| | Shadow redraw | Default, High, Ultra | `shadow_redraw` (default) | at once |
| | Reflections | Low, Default, High, Ultra | `reflections` (default) | restart |
| | Scenery detail | Low, Default, High, Ultra | `scenery_detail` (default) | restart |
| | Car detail | Low, Default, High, Ultra | `car_detail` (default) | restart |
| | Particles | Low, Default | `particles` (default) | restart |
| | Motion blur | Off, Default, High, Ultra | `motion_blur` = `off`, `default`, `high`, `ultra` (default) | restart |
| | Texture filtering | Default, 16x | `texture_filtering` = `default`, `16x` (default) | restart |
| | Anti-aliasing | Off, 2x MSAA, 4x MSAA, Console (coverage) | `antialiasing` = `off`, `msaax2`, `msaax4`, `coverage` (msaax4) | restart |
| Audio | Audio output (Azimuth) | Stereo, System (Windows), Multichannel | `audio` = `stereo`, `system`, `multichannel` (stereo) | restart |
| | Volume (Azimuth) | slider 0 ... 100 % in 5 % steps | `volume` 0..100 (100) | at once |
| | Speech language (Azimuth) | Same as text, 12 languages | `speech` (absent: as the text) | restart |
| | Text language (Azimuth) | Windows language, 12 languages | `language` (absent: Windows' language) | restart |

- **Graphics** is a preset of the nine details: Azimuth fills them from the
  preset and then lets each detail key present override it, and its launcher
  writes all nine with the preset. So does this row; a preset level a detail
  does not have moves towards Default, as in Azimuth (`0x4140`): Low gives
  shadow distance and redraw Default and motion blur Off; High and Ultra give
  particles Default and texture filtering 16x. Changing a detail writes
  `graphics` as the preset the details now match, else `custom`; Graphics
  shows Custom when they match none (or `graphics=custom`).
- The levels each detail offers are Azimuth's own detail tables (`0x3ef0`).
  Azimuth 1.x has no `2x`/`4x`/`8x` texture filtering and no `low` shadow
  distance/redraw (Azimuth Graphics Options wrote those; Azimuth ignores them).
- **Languages**: en-US, en-GB, fr-FR, de-DE, it-IT, es-ES, es-MX, pl-PL,
  pt-BR, ru-RU, ja-JP, zh-TW (codes Azimuth knows, all in both games'
  AppxManifest). A saved code that is none of these is kept as a choice of
  its own. Azimuth itself checks that the game has speech in the chosen
  language. With Language Support's Ukrainian pack in the `ru-RU` slot, the
  text language for it is Russian (`ru-RU`).
- **Frame rate choices** are the ones Azimuth's launcher offers (1.0.4
  `0x104c0`, 1.0.2 `0x10590`): 30, then 60, 90, 120, 144, 165 and 240 up to the
  refresh rate of the display the game is on, then that refresh rate when
  higher. A saved rate between two choices shows as the lower one. Without
  `frame_rate` the row shows the title's own rate, read from Azimuth (1.0.4
  `0x7d8550`, 1.0.2 `0x8e7544`; 30 for both games; 30 when unreadable).
- Not rows: `[launcher]`'s HDR calibration (`hdr_peak`, `hdr_paper_white`,
  `hdr_black`), wheel, player and folder settings: Azimuth's launcher keeps
  those for every game. `frame_pacing` is not read by Azimuth 1.x.
- The section is the running game's title (from its executable), not
  `[launcher] game`; the log notes when they differ.

Captions are in English; in Polish when the game runs in Polish; in Ukrainian
while [Language Support](../lang-support/README.md)'s Ukrainian pack fills the
game's `ru-RU` slot (`common/include/fh2_game.h`). `[Captions] Language` can
force one.

## Requirements

- Azimuth **1.0.4** or 1.0.2 (the release's `azimuth.exe`, or the Builder's
  branded executable of that build). Other builds: nothing is installed.
- Forza Horizon 2 1.0.0.64 or Fast & Furious 1.0.0.6 as Azimuth runs them.
- A mod loader with the Extra Options plugin API: forza_injector (FH2 and
  F&F), or Extra Options 0.4.4 / 0.3.2 (FH2).
- Not Azimuth Graphics Options in the same `mods` folder.

## Install

1. Close the game.
2. Copy `mods\Azimuth Settings` (from `out\mods` after building, or from a
   release ZIP) into the `mods` folder beside the Azimuth executable:

   ```text
   mods\
     Azimuth Settings\
       mod.json
       plugins\
         AzimuthSettings.dll
         AzimuthSettings.ini
   ```

3. Start the game. The loader's log (and `AzimuthSettings.log` beside the
   DLL) show lines starting with `AZIMUTH SETTINGS:`.

## Settings

`AzimuthSettings.ini` beside the DLL, read when the game starts:

| Section | Key | Default | Meaning |
|---|---|---|---|
| `[Video]` | `Enabled` | `1` | the rows on the Video page |
| `[Audio]` | `Enabled` | `1` | the rows on the Audio page |
| `[Azimuth]` | `LiveApply` | `1` | apply frame rate, display mode, shadow distance/redraw and volume at once; `0`: everything after a restart |
| | `Ini` | `<launcher>\data\azimuth.ini` | Azimuth's settings file (full, or relative to the launcher's folder) |
| `[Captions]` | `Language` | `auto` | `auto`, or always `en`, `pl` or `uk` |
| `[Logging]` | `Enabled` | `1` | `AzimuthSettings.log` beside the plugin |

## How it works

- **Settings pages.** The game reads every XAML resource through one call
  (FH2 `call 0xaabec0` at `0x16ebb37`; F&F `call 0xc795c0` at `0x1712d47`).
  The plugin keeps whatever the call goes to (the game's reader, Extra
  Options' thunk `0x13fe354` on FH2, or another module's hook), points the call
  at a jump within 2 GB (only the 4-byte displacement is swapped, atomically)
  and calls the previous target first. It then adds its rows to the Video and
  Audio pages that come back (lists made scrollable: Video 440 px, Audio
  550 px). Pages marked `<!-- EO:StandaloneOptionsPage -->` and the pages
  Extra Options' fork builds from the Video page are left alone. The rows bind
  to properties (`AzsFrameRate`, `AzsVolume`, ...) registered in the page
  type's registry with the game's own int, float and collection property
  descriptors, as Extra Options does for its rows.
- **Frame rate at once**: the target rate Azimuth's pacing reads every frame
  (1.0.4 `0x7d8554`, 1.0.2 `0x8e7540`).
- **Display mode at once**: Azimuth's Alt+Enter, posted to its window when the
  mode (1.0.4 `0x7fcbc1`, 1.0.2 `0x909d61`) differs.
- **Volume at once**: Azimuth's own volume setter (1.0.4 `0xc37f0`, 1.0.2
  `0xbaea0`).
- **Shadow distance at once**: the cascade distances (FH2 `0x2207288`, F&F
  `0x22332d8`) Azimuth overwrites at startup for High and Ultra (its patch
  records for both games hold the same values).
- **Shadow redraw at once**: the shadow renderer's maps per frame
  (`[renderer+0x21bc]`: 4 from High, 1 at Default; renderer FH2
  `[0x36609a0]` vtable `0x2e82b88`, F&F `[0x36ca1b0]` vtable `0x2ecaea8`). With
  Realtime shadows on (Extra Options or Xtra Options patch the read at FH2
  `0x1131c02` / F&F `0x1157a12`) this is left to it.
- **Audio output, languages and the rest after a restart**: Azimuth reads them
  when it starts the game.

### Fast & Furious

Same UI engine and the same Video page XAML; its Audio page lacks FH2's Radio
DJ row, which changes nothing here. Every site was found again in the F&F
executable (`tests/run_tests.py` finds them again each run):

| What | FH2 | F&F | Found by |
|---|---|---|---|
| Video page set-up / registry / lea | `0xeca710` / `0x35470d0` / `0xeca808` | `0xee4ca0` / `0x35adf70` / `0xee4d98` | keyboard-mouse's ff_sites.inc; set-up + 0xf8 |
| Audio page set-up / registry / lea | `0xc803c0` / `0x35107b0` / `0xc804b8` | `0xca5f70` / `0x357a460` / `0xca6068` | its first property, "AudioPresetOptions" |
| Int / float / collection property | `0xed81f0` / `0xed6e90` / `0xed77d0` | `0xef8ac0` / `0xef6b00` / `0xef7aa0` | the names they copy: "Subtitles", "Gamma", "OnOffOptions" (leas `0xef8b2b`, `0xef6b6b`, `0xef7afe`) |
| BoxInt / BoxFloat | `0x4702d0` / `0x3f5990` | `0x479fa0` / `0x402860` | the box call in the Subtitles / Gamma getters |
| Other natives, resource call, collection vtables, text tables | (settings_pages.cpp) | (settings_pages.cpp) | keyboard-mouse's ff_sites.inc; masked code |
| Shadow cascades | `0x2207288` | `0x22332d8` | its console values; Azimuth 1.0.4's patch records |
| Shadow renderer store / pointer / vtable / read | `0x112d760` / `0x36609a0` / `0x2e82b88` / `0x1131c02` | `0x1153570` / `0x36ca1b0` / `0x2ecaea8` / `0x1157a12` | the code around each site |

### Guards

Every site is checked byte for byte before use; on a mismatch nothing (or only
that live part) is installed and the log says why.

| What | Guard |
|---|---|
| host | `TimeDateStamp` 1.0.4 `0x6ae393eb` or 1.0.2 `0x6ae05dd1`; the launcher's frame rate table |
| game | image size and PE `TimeDateStamp` (FH2 `0x3a18000`/`0x565501ed`, F&F `0x3a89000`/`0x55b62fde`); the first 12 bytes of each of the 20 native functions; the three property templates' names; both page registrations; the resource call and the code after it |
| `data\azimuth.ini` | has `[launcher]` and the game's `[title-<id>]` |
| live sites | as Azimuth Graphics Options: rate setter, Alt+Enter, volume setter, cascades holding one of Azimuth's three levels, the renderer store (`mov [rip],rcx; ret`), its vtable and a value of 1 or 4 |

## Build and test

From the repository's root:

```text
py build.py --zig <zig 0.15.2>\zig.exe --plugin azimuth-settings --test
```

writes `out\mods\Azimuth Settings\...`, the release ZIP, a test build of the
DLL with extra entry points and `plugin_test.exe`, then runs
`tests\run_tests.py`. The tests never run a game and only read its files:
FH2 (`--game`, default `%USERPROFILE%\FH2`, else `D:\Games\Forza Horizon 2`),
Fast & Furious (`--ff-game`, default `D:\Games\Forza Horizon 2 Presents Fast &
Furious`: its executable, `UI.zip` and a copy of its UTF-16 `azimuth.ini`),
Azimuth's release ZIPs, and Extra Options 0.4.4's release and sources.
Missing inputs skip the tests that need them; capstone (optional) adds the
masked code comparisons of the F&F sites.

## Not verified in a game

Everything above is checked offline against the real executables, pages and
INI files; the rows have not been tried in a running game yet (FH2 or F&F).

## License

GPL-3.0-or-later (see `LICENSE` at the repository's root).
