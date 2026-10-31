"""Build (and test) the forza_injector plugins of this repository, per game.

Usage: py build.py [--zig <zig.exe>] [--game <id> ...] [--plugin <folder> ...] [--test]
                   [--version <x.y.z>] [--output out]

Every folder plugins/<folder>/ with a build.py and a mod/mod.json is a plugin.
games.json names each game's package (fh2, fh2ff, fm5, fm6): the plugins installed into
that game. tools/validate.py runs first (a structure failure stops the build).
Each plugin is built by its own build.py into <output>/build/<folder>/, then:

  <output>/mods/<Mod Name>/...                       the mod folder, as installed
  <output>/release/<folder>-<mod version>.zip        mods/<Mod Name>/... of one plugin
  <output>/games/<game>/mods/<Mod Name>/...          the game's package, as installed
  <output>/release/<game>-plugins-<version>.zip      mods/... of the game's package

--game builds only those games' packages (repeatable); without --game and
--plugin, every plugin and every game's package is built. --plugin builds
just those plugins (and the packages they complete). --test runs each
plugin's offline tests (its build.py --test). Zig 0.15.2: --zig, else ZIG,
else zig on PATH (passed on to the plugins as ZIG).
"""
# SPDX-License-Identifier: GPL-3.0-or-later

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parent
PLUGINS = ROOT / "plugins"
ZIP_TIME = (2026, 1, 1, 0, 0, 0)  # fixed, so the same files give the same ZIP


def find_zig(given):
    for candidate in (given, os.environ.get("ZIG"), shutil.which("zig")):
        if candidate and Path(candidate).is_file():
            return Path(candidate).resolve()
    sys.exit("zig not found: pass --zig <zig.exe> (Zig 0.15.2) or set ZIG")


def discover():
    """{folder name: (folder, mod.json)} for every plugins/<folder>/ with build.py and mod/mod.json."""
    found = {}
    for folder in sorted(PLUGINS.iterdir()):
        mod_json = folder / "mod" / "mod.json"
        if (folder / "build.py").is_file() and mod_json.is_file():
            found[folder.name] = (folder, json.loads(mod_json.read_text(encoding="utf-8")))
    return found


def zip_tree(archive, root, members):
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zip_file:
        for member in members:
            for path in sorted(p for p in (root / member).rglob("*") if p.is_file()):
                info = zipfile.ZipInfo(path.relative_to(root).as_posix(), ZIP_TIME)
                info.compress_type = zipfile.ZIP_DEFLATED
                zip_file.writestr(info, path.read_bytes())
    print(archive)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--zig", help="zig.exe (0.15.2)")
    parser.add_argument("--game", action="append", help="build only this game's package (games.json; repeatable)")
    parser.add_argument("--plugin", action="append", help="build only this plugins/<folder> (repeatable)")
    parser.add_argument("--test", action="store_true", help="run each plugin's offline tests")
    parser.add_argument("--version", help="version of the all-in-one ZIP (default: VERSION)")
    parser.add_argument("--output", type=Path, default=ROOT / "out")
    options = parser.parse_args()
    if subprocess.run([sys.executable, str(ROOT / "tools" / "validate.py")]).returncode:
        sys.exit("structure validation failed")
    zig = find_zig(options.zig)
    environment = dict(os.environ, ZIG=str(zig))
    plugins = discover()
    packages = json.loads((ROOT / "games.json").read_text(encoding="utf-8"))
    unknown_games = [game for game in options.game or [] if game not in packages]
    if unknown_games:
        sys.exit(f"no such game: {', '.join(unknown_games)} (have {', '.join(packages)})")
    games = options.game or ([] if options.plugin else list(packages))
    chosen = list(options.plugin or [])
    for game in games:
        chosen += [name for name in packages[game]["plugins"] if name not in chosen]
    if not options.plugin and not options.game:
        chosen += [name for name in plugins if name not in chosen]
    unknown = [name for name in chosen if name not in plugins]
    if unknown:
        sys.exit(f"no such plugin: {', '.join(unknown)} (have {', '.join(plugins)})")
    output = options.output.resolve()
    mods = output / "mods"
    release = output / "release"
    release.mkdir(parents=True, exist_ok=True)
    failed = []
    for name in chosen:
        folder, mod = plugins[name]
        build = output / "build" / name
        print(f"== {name} ({mod['name']} {mod['version']})", flush=True)
        command = [sys.executable, str(folder / "build.py"), "--output", str(build)]
        if options.test:
            command.append("--test")
        if subprocess.run(command, env=environment).returncode:
            failed.append(name)
            continue
        target = mods / mod["name"]
        if target.exists():
            shutil.rmtree(target)
        shutil.copytree(build / "mods" / mod["name"], target)
        zip_tree(release / f"{name}-{mod['version']}.zip", output, [Path("mods") / mod["name"]])
    if failed:
        sys.exit(f"failed: {', '.join(failed)}")
    version = options.version or (ROOT / "VERSION").read_text().strip()
    for game, package in packages.items():
        if not all(name in chosen for name in package["plugins"]):
            continue  # a package is made only when all its plugins were built
        folder = output / "games" / game
        if folder.exists():
            shutil.rmtree(folder)
        for name in package["plugins"]:
            mod_name = plugins[name][1]["name"]
            shutil.copytree(mods / mod_name, folder / "mods" / mod_name)
        zip_tree(release / f"{game}-plugins-{version}.zip", folder, [Path("mods")])


if __name__ == "__main__":
    main()
