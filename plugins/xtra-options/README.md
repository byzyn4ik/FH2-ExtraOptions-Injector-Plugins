# Xtra Options

Extra Options' game code lives here as an optional FH2 runtime provider and
independent feature DLLs. `forza-injector` handles the Azimuth host and generic
plugin loading; it no longer compiles FH2 gameplay, settings, databases or hooks.

```text
mods/Xtra Options/
  mod.json
  plugins/XtraRuntime.dll       FH2 runtime provider; remaining coupled modules
  plugins/XtraRuntime.ini       runtime options for a new installation
  plugins/XtraOptions.ini       independent feature options
  plugins/SkipIntros.dll
  plugins/OnlineMenus.dll
  plugins/Unlocks.dll
  plugins/PhotoMode.dll
  plugins/ParentalLocks.dll
  plugins/AeroTuning.dll
  plugins/XtraFeatures.dll
  plugins/XtraFeatures.ini
```

| DLL | Owned groups | Fixed sites | Extra hooks |
|---|---|---:|---:|
| SkipIntros.dll | INTRO | 4 | 0 |
| OnlineMenus.dll | SOCIAL, CLUBS, RIVALS, MARKETPLACE | 20 | 0 |
| Unlocks.dll | DLC, VIP, ISLAND_CARS, TREASURE_MAP | 22 | 1 |
| PhotoMode.dll | PHOTO_CAMERA | 3 | 0 |
| ParentalLocks.dll | PARENTAL | 2 | 0 |
| AeroTuning.dll | AERO_TUNING | 4 | 2 |

Deleting an independent DLL removes its feature under the injector. Aero Tuning
defaults off; enabling it can change PI and handling even at default tuning.

The remaining features share menu state, resources and native callbacks. Their
implementations stay together in `runtime/src/features/`, in `XtraRuntime.dll`.
Further independent ports can move out of this provider.

## Both loaders

Every shipped DLL retains the original `FH2PluginInitialize` API. Existing
custom plugins continue to load under stock Extra Options and the injector.
Stock Extra Options already implements the built-in Xtra Options features, so
these replacement DLLs leave its own implementation in charge. Their ordinary
v1/v2 entries do not install duplicate hooks.

With injector 0.2.0+, the host first calls `ForzaGameRuntimeInitialize` on
runtime providers. XtraRuntime claims only the supported FH2 package and PE
build, after checking the real executable. It then supplies FH2's native file,
database and vehicle hooks and the original deferred plugin/event lifecycle.
The host does not also initialize the same mod tree. Independent patch DLLs run
once through the runtime's early phase, before game code executes.

An unknown game is declined without writes. Generic injection into other
Azimuth games does **not** make these FH2 gameplay hooks compatible with them.
Fast & Furious uses separate achievement rules; its gameplay ports are separate
work and no FH2 RVAs are assumed to work there.

## Existing settings

An existing launcher-folder `FH2_ExtraOptions.ini` takes precedence for the
coupled runtime, preserving the original keys and defaults. Without that file,
the runtime reads `plugins/XtraRuntime.ini` beside its DLL. The runtime does not
move or rewrite either file. Binary menu choices (`FH2_ExtraOptions.dat`) and
`Gamerpic.png` retain their launcher-folder paths.

Independent DLLs read `XtraOptions.ini`; these keys keep Extra Options' section
and option names. Stock Extra Options uses its own settings for its built-ins.
Xtra Features retains its own INI.

## Ownership and further ports

`runtime/ownership.json` assigns every original patch group to exactly one
runtime or independent feature. Runtime validation, page planning and writes
all skip independent groups. Changing a runtime menu option therefore cannot
restore original bytes over another DLL's patches.

The original Extra Options 0.4.4 reference manifest is
`runtime/src/patches.json`. `tools/extract_feature.py` copies a feature's fixed
patches from there. Native hooks must be ported with the same accepted bytes
and included in the feature's transaction; copying byte patches alone is not
sufficient. The source keeps its GPL attribution and native validation.

## Build and tests

```text
py plugins/xtra-options/build.py --zig <zig.exe> --sdk <forza-injector>/sdk/include --test
```

Tests compare feature patches and hook ownership with the reference manifest,
check the real FH2 executable, and call the built DLLs on mapped image copies.
They cover enabled/disabled groups, corrupt sites, foreign games, provider
failure, settings precedence, idempotence and a runtime reapply after individual
DLL patches. No game instructions are executed. In-game behavior still needs a
manual launch check after deployment.
