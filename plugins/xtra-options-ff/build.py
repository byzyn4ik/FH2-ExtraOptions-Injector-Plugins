"""Build Xtra Options (Fast & Furious): Extra Options' byte-patch features ported to Forza Horizon 2
Presents Fast & Furious, one forza_injector plugin DLL each.

Usage: py plugins/xtra-options-ff/build.py [--zig <zig.exe>] [--output out] [--sdk <forza-injector/sdk/include>]
                                           [--test] [--ff <Fast & Furious game EXE>]
(the top-level build.py runs it with --output out/build/xtra-options-ff)

  <output>/mods/Xtra Options (Fast & Furious)/mod.json
  <output>/mods/Xtra Options (Fast & Furious)/plugins/XtraOptions.ini
  <output>/mods/Xtra Options (Fast & Furious)/plugins/<Feature>.dll   one per src/features/<feature>/

The DLLs are Xtra Options' own patch_feature.c (plugins/xtra-options/src),
built with these features' tables: the same INI keys, the Fast & Furious
sites (tools/port_feature.py found them). A Fast & Furious-only feature
("native": true) may add its own C files ("code") and C tests ("tests", built
and run by --test). Nothing is installed.
"""
# SPDX-License-Identifier: GPL-3.0-or-later

import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
XTRA = ROOT / "plugins" / "xtra-options"
sys.path.insert(0, str(ROOT / "common"))
import build_common as bc  # noqa: E402

MOD_NAME = "Xtra Options (Fast & Furious)"
INI_NAME = "XtraOptions.ini"


def xtra_build():
    spec = importlib.util.spec_from_file_location("xtra_options_build", XTRA / "build.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--zig", help="zig.exe (0.15.2)")
    parser.add_argument("--output", type=Path, default=HERE / "out")
    parser.add_argument("--sdk", type=Path, default=Path(os.environ.get("FORZA_SDK") or
                                                         ROOT.parent / "forza-injector" / "sdk" / "include"))
    parser.add_argument("--test", action="store_true", help="run the offline tests after building")
    parser.add_argument("--ff", type=Path, help="tests: Fast & Furious' forza_durango_release_final.exe")
    options = parser.parse_args(argv)
    zig = bc.find_zig(options.zig)
    sdk = options.sdk.resolve()
    if not (sdk / "forza_patch.h").is_file():
        sys.exit(f"forza_injector's SDK not found at {sdk} (--sdk <forza-injector>/sdk/include)")
    xtra = xtra_build()
    output = options.output.resolve()
    mod = output / "mods" / MOD_NAME
    plugins = mod / "plugins"
    if mod.exists():
        shutil.rmtree(mod)
    plugins.mkdir(parents=True)
    version = bc.mod_version(HERE / "mod" / "mod.json")
    ini = (HERE / "mod" / INI_NAME).read_text(encoding="utf-8")
    tests = []
    for folder in sorted(p for p in (HERE / "src" / "features").iterdir() if (p / "feature.json").is_file()):
        build = output / "obj" / folder.name
        feature = xtra.feature_data(folder, build)
        if feature["game"] != "fh2ff" or (feature.get("code") and not feature.get("native")):
            sys.exit(f"src/features/{folder.name}: only Fast & Furious features belong here "
                     "(own code only in a Fast & Furious-only feature, \"native\": true)")
        code = [folder / name for name in feature.get("code", [])]  # the feature's own C files, if any
        tests += [(folder, test) for test in feature.get("tests", [])]
        for group, key in feature["groups"].items():
            if f"[{key['section']}]" not in ini or f"\n{key['key']}={int(key['default'])}" not in ini:
                sys.exit(f"mod/{INI_NAME}: no [{key['section']}] {key['key']}={int(key['default'])} ({group})")
        dll = feature["dll"]
        bc.version_rc(build / "version.rc", version, dll,
                      f"Xtra Options (Fast & Furious): {feature['name']} (forza_injector plugin)", MOD_NAME)
        bc.run([zig, "cc", "-target", "x86_64-windows-gnu", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                "-Wno-missing-field-initializers", "-fno-sanitize=all", "-DUNICODE", "-D_UNICODE",
                "-I", build, "-I", XTRA / "src", "-I", sdk, "-I", folder, "-shared", "-s",
                XTRA / "src" / "patch_feature.c", *code, build / "version.rc", "-Wl,--out-implib," + str(build / "plugin.lib"), "-o", plugins / dll])
    for stale in plugins.glob("*.lib"):
        stale.unlink()
    shutil.copy2(HERE / "mod" / "mod.json", mod / "mod.json")
    shutil.copy2(HERE / "mod" / INI_NAME, plugins / INI_NAME)
    for path in sorted(plugins.iterdir()):
        print(path)
    if options.test:
        # A feature's own C tests (feature.json "tests"): built and run before the DLL tests.
        for folder, test in tests:
            executable = output / "tests" / (folder.name + "-" + Path(test).stem + ".exe")
            executable.parent.mkdir(parents=True, exist_ok=True)
            bc.run([zig, "cc", "-target", "x86_64-windows-gnu", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-I", folder, folder / test, "-o", executable])
            bc.run([executable])
        command = [sys.executable, str(HERE / "tests" / "run_tests.py"), "--output", str(output)]
        if options.ff:
            command += ["--ff", str(options.ff)]
        subprocess.run(command, check=True)


if __name__ == "__main__":
    main()
