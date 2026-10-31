"""Build the Nitrous plugin for Forza Horizon 2 Extra Options.

Usage: py plugins/nitrous/build.py [--zig <zig.exe>] [--output out] [--test] [test options]
(the top-level build.py runs it with --output out/build/nitrous)

Writes:
  <output>/mods/Nitrous/mod.json
  <output>/mods/Nitrous/plugins/Nitrous.dll
  <output>/mods/Nitrous/plugins/Nitrous.ini
  <output>/tests/NitrousTest.dll   (the same code with test entry points)
--test also runs tests/run_tests.py on the result (it passes --game and
--ff-game through: the real executables are only read). Nothing is installed.
Zig 0.15.2: --zig, else the ZIG environment variable, else zig on PATH.
"""

import argparse
from pathlib import Path
import shutil
import subprocess
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / "common"))
import build_common as bc  # noqa: E402

SRC = HERE / "src"
MOD_NAME = "Nitrous"
DLL_NAME = "Nitrous.dll"
INI_NAME = "Nitrous.ini"
SOURCES = ["plugin.c", "nitrous.c", "effects.c", "hooks.c", "rules.c"]
LIBS = ["-luser32", "-lwinmm"]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--zig", help="zig.exe (0.15.2)")
    parser.add_argument("--output", type=Path, default=HERE / "out")
    parser.add_argument("--test", action="store_true", help="run the offline tests after building")
    parser.add_argument("--game", type=Path, help="tests: the FH2 folder holding Anthem_1.0.0.64_x64__8wekyb3d8bbwe (default ~/FH2)")
    parser.add_argument("--ff-game", type=Path, help="tests: Fast & Furious' forza_durango_release_final.exe")
    options = parser.parse_args(argv)
    zig = bc.find_zig(options.zig)
    output = options.output.resolve()
    build = output / "obj"
    mod = output / "mods" / MOD_NAME
    plugins = mod / "plugins"
    tests = output / "tests"
    for folder in (plugins, tests, build):
        folder.mkdir(parents=True, exist_ok=True)
    version = bc.mod_version(HERE / "mod" / "mod.json")
    bc.version_rc(build / "version.rc", version, DLL_NAME, "Nitrous (Extra Options plugin)",
                  "Nitrous for Forza Horizon 2")
    print(f"version {version}")

    common = [zig, "cc", "-target", "x86_64-windows-gnu", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
              "-Wno-missing-field-initializers", "-fno-sanitize=all", "-DUNICODE", "-D_UNICODE",
              f'-DNITROUS_VERSION="{version}"', "-I", build, "-I", SRC, "-I", bc.INCLUDE]
    sources = [SRC / name for name in SOURCES]
    bc.run([*common, "-shared", "-s", *sources, build / "version.rc", *LIBS,
            "-Wl,--out-implib," + str(build / "plugin.lib"), "-o", plugins / DLL_NAME])
    bc.run([*common, "-shared", "-s", "-DNITROUS_TEST", *sources, build / "version.rc", *LIBS,
            "-Wl,--out-implib," + str(build / "plugin-test.lib"), "-o", tests / "NitrousTest.dll"])
    for folder in (plugins, tests):
        for stale in list(folder.glob("*.lib")) + list(folder.glob("*.pdb")):
            stale.unlink()
    shutil.copy2(HERE / "mod" / "mod.json", mod / "mod.json")
    shutil.copy2(HERE / "mod" / INI_NAME, plugins / INI_NAME)
    for path in (mod / "mod.json", plugins / DLL_NAME, plugins / INI_NAME, tests / "NitrousTest.dll"):
        print(path)
    if options.test:
        command = [sys.executable, str(HERE / "tests" / "run_tests.py"), "--output", str(output)]
        for name in ("game", "ff_game"):
            if getattr(options, name):
                command += ["--" + name.replace("_", "-"), str(getattr(options, name))]
        subprocess.run(command, check=True)


if __name__ == "__main__":
    main()
