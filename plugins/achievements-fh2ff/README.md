# OpenBox Achievements (Fast & Furious)

Forza Horizon 2 Presents Fast & Furious achievements for the
[OpenBox Achievements](https://github.com/byzyn4ik/OpenBoxAchievements) app. The
plugin evaluates the achievement rules in game and sends unlocks and progress
to OpenBox, which shows them (its `games\fh2ff` page: 25 achievements).

It is a copy of the FH2 plugin ([achievements-fh2](../achievements-fh2)), with
Fast & Furious' addresses, title, saves folder and rules. It uses only the
Extra Options plugin API (version 1), so forza_injector loads it.

## Install

`mods\OpenBox Achievements (Fast & Furious)\` beside `azimuth.exe`, with
forza_injector's `winmm.dll`. The OpenBox Achievements app must be installed.
Settings: `plugins\OpenBoxAchievements.ini` (the same keys as the FH2 plugin;
`Game=fh2ff`).

## What it hooks

Each hook is checked byte for byte against the supported game, Spire 1.0.0.6
(`forza_durango_release_final.exe`, SHA-256 `29eec8b9…68cc49`, TimeDateStamp
`0x55b62fde`, SizeOfImage `0x3a89000`). On any difference the hook is left out
and the log says why.

| Hook | Fast & Furious | (FH2) |
|---|---|---|
| `EtwPlus!EtxEventWrite` import slot: the game's telemetry events go to the rules | `0x1df7068` | `0x1dd0060` |
| ETX event table (names and field types of the events) | `0x2d7fe40`, 77 events | `0x2cb9280`, 131 |
| Pause menu's Achievements button (PopupShowAchievements, vtable slot 145) opens OpenBox | slot `0x2e7e520`, method `0xe52f40` | `0x2e360c8`, `0xe38770` |

71 of the 77 events are FH2's with the same fields. Only `PromoCarPhotographed`
has one more field. 6 are new: `NitrousUsed`, `GarageCarReveal`,
`ReachOutChallenge`, `IEDroveToFirstEvent` and two demo events.

Saves are read from `<launcher>\data\titles\5A9771A4\saves`. One rule uses
a save stat: 16 also counts `Main/NumUltimateAirs` (the save keeps no count of
the other Air grades).

Text fields (ETX type 0) are UTF-16 of any length. A short one is still text:
`Air` with its NUL is 8 bytes, which the records once wrote as a 64-bit number,
so the Air rule never matched.

## Rules

`rules\fh2ff_achievement_rules.json` is built into the DLL (`[Achievements] Rules=`
can point to another file). The values come from the game's own data:
- car ordinals: `gamedbRC.slt`, the `FastAndFuriousCars` table;
- skills: the `SkillTypes` table;
- the Nice destination: `DestinationHubs`;
- the Cargo Plane showcase: `tracks\test04\ShowcaseEvents.xml`.

| Achievement | Rule | Confidence |
|---|---|---|
| 1–10 Acquired the … | `CarAddedToGarage`, field 3 = the car (2280, 2287, 2277, 2281, 1667, 2276, 2284, 2279, 2286, 2288) | same event and field as FH2's car rules |
| 11 Restored the Plymouth Roadrunner | `BarnFindRestored`, field 3 = 2278 | same as FH2 |
| 13 Got to Nice | `ArrivedAtDestination`, field 3 = 4 (Nice) | same as FH2; **to confirm**: not yet seen in play (the opening drive may send only `IEDroveToFirstEvent`/`IEFinished`) |
| 14–21 skills | `SkillAchieved`, field 3 = Burnout ×10, Drift/EDrift ×20, Air ×5, Destruction ("Wreckage") ×10, CleanSpeed ("Speed") ×5, NearMiss ×25, TradingPaint ×5, Draft ("Drafting") ×5 | same event as FH2; display names mapped to the game's skill names (`Skills.xml`: every grade of a skill is one type, e.g. `Air` for all four Air grades); seen in play except Air (the text fix above) |
| 16 (also) | `SaveStat` `Main/NumUltimateAirs` ≥ 5 | Ultimate Airs are Air skills: a floor from the save |
| 22, 24 Skill chain 7,000 / 50,000 | `SkillChainCompleted`, highest field 3 | field as FH2's island skill-chain rule |
| 12 Cargo Plane Showcase | `ShowcaseRaceEnd`, field 3 = 560 | **to confirm**: is field 3 the event id? Does a lost race also count? |
| 26 Used all your Nitrous in the first half | `NitrousUsed`, field 8 = 1 and field 6 ≤ 0.5 | fields from the game's own debug text: 3 MissionId, 4 MissionEventId, 5 MissionEventType, 6 EventProgress, 7 NitrousUsed, 8 NitrousNowEmpty; sent when the nitrous button is let go in an event. **To confirm**: that EventProgress runs 0–1 (the game shows it ×100 as a percentage) |
| 27 Selfie! (1970 Dodge Charger R/T) | `PromoCarPhotographed`, field 4 or 5 = 2280 | unlocked in play; which of the two fields is the car is not known |

To confirm 12, 13 and 26, record one session with `[Logging] Telemetry=1`:
win (and once lose) the Cargo Plane showcase, use all nitrous early in a race and
once late, and reach Nice in a new profile. Then read `OpenBoxAchievements.telemetry.jsonl`.
`tools\ach-replay.exe` replays a recording against the rules.

## Build and tests

```text
py build.py [--zig <zig.exe>] [--output out] [--test]
```

The offline tests (`tests\run_tests.py`) do the following:
- **game:** check every address above against the real executable (`--game`;
  skipped when it is missing);
- **package:** check the package;
- **rules:** check the rules' IDs against OpenBox's `fh2ff` manifest;
- **replay:** replay `tests\data\session.jsonl`;
- **plugin:** load the DLL with a fake game image, a fake OpenBox pipe and a
  fake app.

## License

GPL-3.0-or-later (see [LICENSE](LICENSE)).
