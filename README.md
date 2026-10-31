# FH2 Extra Options injector plugins

Mods for **Forza Horizon 2**, **Forza Horizon 2 Presents Fast & Furious**,
**Forza Motorsport 5** and **Forza Motorsport 6** on Azimuth, loaded by
[forza-injector](https://github.com/byzyn4ik/forza-injector). Most Forza
Horizon 2 mods also load under a stock
[Extra Options](https://github.com/RokkuDayo/ForzaHorizon2_ExtraOptions) 0.4.4
(see [Loaders](#loaders)). Every plugin lives in this one repository and is
released both on its own and in one package per game.

Each plugin is a mod folder (`mods\<Mod>\`) with its DLL(s), INI and
`mod.json`. `mod.json` lists the games it is for (`"games"`), so the injector
loads it only there; stock Extra Options ignores `"games"`.

## Install

The easy way: **Forza Builder** builds the game folder from your copy of the
game and installs Azimuth, the loader and the mods you tick.

By hand:

1. Install [forza-injector](https://github.com/byzyn4ik/forza-injector/releases)
   (its `winmm.dll` beside the game's Azimuth EXE).
2. Download your game's package from
   [Releases](https://github.com/byzyn4ik/FH2-ExtraOptions-Injector-Plugins/releases)
   and unpack it into the game's folder, so the mods land in `mods\`.
3. Start the game. Each mod's settings are in its INI; the injector's log is
   `forza-injector.log` in the game folder.

To add or remove a single mod, use its own `<folder>-<version>.zip` from the
same release; delete a mod's folder to uninstall it.

## Packages

[`games.json`](games.json) names the mods in each game's package:

| Game | Package | Mods |
|---|---|---|
| Forza Horizon 2 | `fh2-plugins-<version>.zip` | Xtra Options (with Xtra Features and the Exit Game tile), OpenBox Achievements, Keyboard & Mouse, Performance, Azimuth Settings, Custom Radio, Language Support, Photo Export, Saves in Documents (mirror) |
| Forza Horizon 2 Presents Fast & Furious | `fh2ff-plugins-<version>.zip` | Xtra Options (Fast & Furious), OpenBox Achievements (Fast & Furious), Keyboard & Mouse, Performance, Exit Game, Azimuth Settings, Xtra Features, Custom Radio, Language Support, Photo Export, Saves in Documents (mirror) |
| Forza Motorsport 5 | `fm5-plugins-<version>.zip` | Keyboard & Mouse (keys as a pad), Performance (the counter; no Video page row) |
| Forza Motorsport 6 | `fm6-plugins-<version>.zip` | Keyboard & Mouse (keys as a pad), Performance (the counter; no Video page row) |

Released only on their own: Nitrous; Azimuth Graphics Options (replaced by
Azimuth Settings, install only one of the two); and, for Forza Horizon 2 under
a stock Extra Options, Xtra Features and Exit Game (in the injector's FH2
package, Xtra Options already carries them).

## Plugins

FH2 = Forza Horizon 2, F&F = Fast & Furious, FM5/FM6 = Forza Motorsport 5/6.

| Folder | Mod | Games | What it does |
|---|---|---|---|
| [xtra-options](plugins/xtra-options) | Xtra Options | FH2 | Extra Options' features for forza-injector: an optional game runtime (`XtraRuntime.dll`) plus independent DLLs for intros, unlocks, online menus, photo camera, parental locks and aero tuning; carries Xtra Features |
| [xtra-options-ff](plugins/xtra-options-ff) | Xtra Options (Fast & Furious) | F&F | The Extra Options features that work in Fast & Furious: skip the opening videos, free photo mode camera, local content privilege checks |
| [xtra-features](plugins/xtra-features) | Xtra Features | FH2, F&F | Pause map zoom-out beyond the game's limit, sharper garage thumbnails (F8 re-renders them), photo mode car scan, and Exit Game and WORLD (time of day, weather) tiles on the pause menu |
| [exit-game](plugins/exit-game) | Exit Game | FH2, F&F | An Exit Game tile on the pause menu's Home tab that closes the game |
| [azimuth-settings](plugins/azimuth-settings) | Azimuth Settings | FH2, F&F | All of Azimuth's per-game settings on the game's own Video page (frame rate, V-sync, resolution, aspect ratio, display mode, HDR, graphics preset and its nine details, anti-aliasing) and Audio page (audio output, volume, speech and text language); frame rate, display mode and volume apply at once |
| [azimuth-graphics-options](plugins/azimuth-graphics-options) | Azimuth Graphics Options | FH2 | The earlier, FH2-only version of Azimuth Settings |
| [custom-radio](plugins/custom-radio) | Custom Radio | FH2, F&F | Your own music (MP3 and FLAC) as an extra station on the game's radio, with its own logo |
| [keyboard-mouse](plugins/keyboard-mouse) | Keyboard & Mouse | FH2, F&F, FM5, FM6 | Play with keyboard and mouse without a virtual controller: rebindable keys, mouse look, a menu pointer and a KEYBOARD & MOUSE settings page (FH2; elsewhere keys as a pad) |
| [performance](plugins/performance) | Performance | FH2, F&F, FM5, FM6 | An FPS counter (FPS, GPU load, video memory; Detailed adds frame times and the 1% low), a Video page row for it and an optional CSV performance log |
| [nitrous](plugins/nitrous) | Nitrous | FH2 | Fast & Furious' nitrous in Forza Horizon 2: hold left stick click (or Left Shift) for a boost from a refilling tank, with flames, rumble and sounds; Performance draws the gauge |
| [lang-support](plugins/lang-support) | Language Support | FH2, F&F | Language packs as mod folders (a language the game does not ship, in one of its slots) and subtitles for spoken lines that have none |
| [photo-export](plugins/photo-export) | Photo Export | FH2, F&F | Every in-game photo, copied once as a JPG into `Documents\My Games\<game>\Photos` |
| [documents-saves](plugins/documents-saves) | Saves in Documents (mirror) | FH2, F&F | The game's saves in `Documents\My Games\<game>\Saves`: loaded from there under forza-injector, otherwise kept there as a verified copy |
| [achievements-fh2](plugins/achievements-fh2) | OpenBox Achievements | FH2 | Forza Horizon 2 achievements for the OpenBox Achievements app (the rules run in game) |
| [achievements-fh2ff](plugins/achievements-fh2ff) | OpenBox Achievements (Fast & Furious) | F&F | The 25 Fast & Furious achievements for OpenBox |

Each plugin's folder has a README with its settings and details.

## Loaders

Stock Extra Options exists for Forza Horizon 2 only, so under it only the FH2
side of a mod applies:

| Mod | forza-injector | Stock Extra Options 0.4.4 (FH2) |
|---|---|---|
| Xtra Options | FH2 | – (it is Extra Options' features, for the injector) |
| Xtra Options (Fast & Furious) | F&F | – |
| Xtra Features | FH2 (inside Xtra Options), F&F | FH2 |
| Exit Game | FH2 (inside Xtra Options), F&F | FH2 |
| Azimuth Settings | FH2, F&F | FH2 |
| Azimuth Graphics Options | FH2 | FH2 |
| Custom Radio | FH2, F&F | FH2 |
| Keyboard & Mouse | FH2, F&F, FM5, FM6 | FH2 |
| Performance | FH2, F&F, FM5, FM6 | FH2 |
| Nitrous | FH2 | FH2 |
| Language Support | FH2, F&F | FH2 |
| Photo Export | FH2, F&F | FH2 |
| Saves in Documents (mirror) | FH2, F&F | FH2 (as a verified copy) |
| OpenBox Achievements | FH2 | FH2 |
| OpenBox Achievements (Fast & Furious) | F&F | – |

Under stock Extra Options, unpack the single mods' zips into the game folder.

## How it fits together

forza-injector is game-agnostic: it loads `mods\*` and gives plugins the
host's addresses. **Xtra Options** owns the Extra Options game runtime that
used to be compiled into the injector: coupled menu and native callback
modules live in its optional `XtraRuntime.dll`, the other features are
independent DLLs. These hooks need FH2; other games get their own validated
features (Xtra Options (Fast & Furious)). Plugins written for stock Extra
Options keep its v1 API.

OpenBox Achievements itself (the app, its MSI and its manifests, IDs only) is
a separate project; the achievement rules live here, with the plugins.

The FH2 plugins started in FH2-ExtraOptions-Plugins (now archived); this
repository is their home. They build against `common/`.

## Build

```text
py build.py --zig <zig.exe> [--game fh2|fh2ff|fm5|fm6 ...] [--plugin <folder> ...] [--test] [--version <x.y.z>] [--output out]
```

Needs Zig 0.15.2 and Python 3. Xtra Options builds against the injector's SDK
(`FORZA_SDK=<forza-injector>/sdk/include`). [`tools/validate.py`](tools/validate.py)
checks the structure first: every plugin lists its games, and each package
lists only plugins for its game. Output: `out/mods/<Mod>/`,
`out/games/<game>/mods/` (each package as installed) and `out/release/*.zip`
(one per plugin, one per game).

## Releases

CI builds and tests every push. Pushing a tag `v<x.y.z>` publishes a GitHub
release with every plugin's zip and each game's package (the package version
is the tag's; [`VERSION`](VERSION) otherwise). `v0.x` tags and tags with a
suffix are pre-releases.

## License

GPL-3.0-or-later (see [LICENSE](LICENSE)).
