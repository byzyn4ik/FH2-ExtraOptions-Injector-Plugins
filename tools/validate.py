"""Validate the structure of the FH2 Extra Options injector plugins.

Usage: py tools/validate.py
Prints one line per check (ok / FAIL) and exits 1 when any check fails:
  - the repository's required files exist;
  - every plugins/<folder>/ has build.py, src/, tests/, README.md and a
    mod/mod.json with an id (unique), a name (unique: it is the mods folder),
    an x.y.z version and a description;
  - "games" lists the plugin's games, as known game ids (forza_injector's
    forza_profile.h: fh2, fh2ff, fh3, fm5, fm6); stock Extra Options ignores it;
  - games.json: each game's package lists only plugins made for that game,
    and every plugin is in a package (Xtra Features and Exit Game, carried
    by Xtra Options in FH2's, ship only on their own; so does Nitrous until
    it has been tried in the game, and Azimuth Graphics Options, which
    Azimuth Settings replaces in the packages);
  - every plugin with rules/: each rules file's achievement IDs and rules
    are consistent (each rule names a listed achievement and an event).
"""
# SPDX-License-Identifier: GPL-3.0-or-later

import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
REQUIRED = ["LICENSE", "README.md", "VERSION", "build.py", "games.json", ".gitattributes",
            ".github/workflows/build.yml", "tools/validate.py"]
GAMES = {"fh2", "fh2ff", "fh3", "fm5", "fm6"}  # forza_injector sdk/include/forza_profile.h, kForzaGames
VERSION = re.compile(r"^\d+\.\d+\.\d+$")
failures = 0


def check(condition, message):
    global failures
    print(("  ok    " if condition else "  FAIL  ") + message)
    if not condition:
        failures += 1
    return condition


def check_rules(name, path):
    data = json.loads(path.read_text(encoding="utf-8"))
    ids, rules = data.get("Achievements"), data.get("Rules")
    if not check(isinstance(ids, list) and isinstance(rules, list) and rules,
                 f"{name}: {path.name} has Achievements and Rules"):
        return
    check(len(set(ids)) == len(ids), f"{name}: {path.name} achievement IDs unique ({len(ids)})")
    bad = [r for r in rules if r.get("Achievement") not in set(ids) or not r.get("Event")]
    check(not bad, f"{name}: {path.name} every rule names a listed achievement and an event ({len(rules)} rules)")


def check_packages(plugin_games):
    """games.json: one package per game; each lists plugins made for that game, each plugin is in one."""
    path = ROOT / "games.json"
    if not check(path.is_file(), "games.json"):
        return
    packages = json.loads(path.read_text(encoding="utf-8"))
    check(set(packages) <= GAMES, f"games.json: packages {sorted(packages)} are known game ids")
    packaged = set()
    for game, package in packages.items():
        plugins = package.get("plugins", [])
        check(bool(package.get("name")) and plugins and len(set(plugins)) == len(plugins),
              f"games.json {game}: a name and plugins, each once")
        wrong = [p for p in plugins if game not in plugin_games.get(p, set())]
        check(not wrong, f"games.json {game}: every plugin exists and lists {game} in its games ({len(plugins)})")
        packaged.update(plugins)
    # Carried by Xtra Options in the FH2 package (games.json note); Nitrous: not yet tried in the game;
    # Azimuth Graphics Options: replaced by Azimuth Settings in the packages (the two clash).
    standalone = {"xtra-features", "exit-game", "nitrous", "azimuth-graphics-options"}
    missing = sorted(set(plugin_games) - packaged - standalone)
    check(not missing, f"games.json: every plugin is in a game's package (missing: {missing or 'none'})")


def check_xtra_runtime():
    plugin = ROOT / "plugins" / "xtra-options"
    runtime = plugin / "runtime"
    for relative in ("ownership.json", "NOTICE", "src/dllmain.c", "src/build.py", "src/patches.json", "tests/run_tests.py"):
        check((runtime / relative).is_file(), "xtra runtime: " + relative)
    data = json.loads((runtime / "ownership.json").read_text(encoding="utf-8"))
    groups = set(re.findall(r"GROUP_([A-Z_]+)", (runtime / "src/extra_options.h").read_text(encoding="utf-8")))
    coupled, independent = set(data["runtime_groups"]), set(data["individual_plugin_groups"])
    check(not coupled & independent and coupled | independent == groups,
          "xtra runtime: every original group has exactly one owner category")
    owned = []
    for path in (plugin / "features").glob("*/feature.json"):
        owned.extend(json.loads(path.read_text(encoding="utf-8"))["groups"])
    check(len(owned) == len(set(owned)) and set(owned) == independent,
          "xtra runtime: every independent group has exactly one feature DLL")
    source = (runtime / "src/patch_engine.c").read_text(encoding="utf-8")
    check(source.count("if (!patch_group_owned(patch->group)) continue;") == 4,
          "xtra runtime: validation, page planning and writes enforce ownership")
    provider = (runtime / "src/dllmain.c").read_text(encoding="utf-8")
    check("ForzaGameRuntimeInitialize" in provider and "ForzaPluginEarlyInitialize" not in provider,
          "xtra runtime: provider entry without a recursive early entry")
    check((runtime / "legacy-plugins/weather-control/weather_control.c").is_file() and
          (runtime / "legacy-plugins/garage-engine-rev/EO_GarageEngineRev.c").is_file(),
          "xtra runtime: original bundled plugin sources preserved")


def main():
    print("plugins repository structure")
    for relative in REQUIRED:
        check((ROOT / relative).is_file(), relative)
    check(VERSION.match((ROOT / "VERSION").read_text().strip() if (ROOT / "VERSION").is_file() else "") is not None,
          "VERSION is x.y.z")
    ids, names, plugin_games = set(), set(), {}
    for folder in sorted(p for p in (ROOT / "plugins").iterdir() if p.is_dir()):
        name = f"plugins/{folder.name}"
        for part in ("build.py", "README.md"):
            check((folder / part).is_file(), f"{name}: {part}")
        for part in ("src", "tests"):
            check((folder / part).is_dir(), f"{name}: {part}/")
        mod_json = folder / "mod" / "mod.json"
        if not check(mod_json.is_file(), f"{name}: mod/mod.json"):
            continue
        mod = json.loads(mod_json.read_text(encoding="utf-8"))
        check(bool(mod.get("id")) and mod["id"] not in ids, f"{name}: id {mod.get('id')!r} unique")
        check(bool(mod.get("name")) and mod["name"] not in names, f"{name}: name {mod.get('name')!r} unique")
        ids.add(mod.get("id"))
        names.add(mod.get("name"))
        check(VERSION.match(mod.get("version", "")) is not None and bool(mod.get("description")),
              f"{name}: version {mod.get('version')} and a description")
        games = mod.get("games")
        check(isinstance(games, list) and games and set(games) <= GAMES, f"{name}: games {games} are known game ids")
        plugin_games[folder.name] = set(games) if isinstance(games, list) else set()
        for rules in sorted((folder / "rules").glob("*.json")) if (folder / "rules").is_dir() else []:
            check_rules(name, rules)
    check_packages(plugin_games)
    check_xtra_runtime()
    print(f"{failures} failed" if failures else "all checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
