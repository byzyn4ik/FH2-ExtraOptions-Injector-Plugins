"""Make features/<feature>/ from forza_injector's core/patches.json.

Usage: py plugins/xtra-options/tools/extract_feature.py <feature> <Name> <Dll.dll>
           GROUP=Section.Key=default [GROUP=Section.Key=default ...] [--core <forza-injector/core>]

Copies every patch of the named Extra Options patch groups (runtime_group),
field for field, into features/<feature>/patches.json, and writes
feature.json (the DLL, its name and each group's INI key and default, as
Extra Options reads them in core/settings.c). Refuses a group that has no
patches, or hooks (runtime_hooks) that the feature's code does not implement:
--code names its C files in features/<feature>/ (they define feature_prepare
and feature_extra, see src/patch_feature.c) and --hooks the hook ids they
implement. Then add the keys to mod/XtraOptions.ini and leave the
groups out of the core's patch_group_enabled.
"""
# SPDX-License-Identifier: GPL-3.0-or-later

import argparse
import json
import os
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("feature")
    parser.add_argument("name")
    parser.add_argument("dll")
    parser.add_argument("groups", nargs="+", help="GROUP=Section.Key=default")
    parser.add_argument("--core", type=Path, default=Path(os.environ.get("FORZA_CORE") or
                                                          HERE / "runtime" / "src"),
                        help="the upstream reference manifest folder (default runtime/src)")
    parser.add_argument("--game", default="fh2")
    parser.add_argument("--code", nargs="*", default=[],
                        help="the feature's C files (in features/<feature>/), which implement its hooks")
    parser.add_argument("--hooks", nargs="*", default=[],
                        help="the hook ids (runtime_hooks) the feature's code implements")
    options = parser.parse_args()
    data = json.loads((options.core / "patches.json").read_text(encoding="utf-8"))
    groups = {}
    for spec in options.groups:
        group, key, default = spec.split("=")
        section, name = key.split(".")
        groups[group] = {"section": section, "key": name, "default": int(default)}
    patches = []
    for group in groups:
        members = [{k: p[k] for k in ("id", "runtime_group", "rva", "original", "accepted", "patched")}
                   for p in data["patches"] if p["runtime_group"] == group]
        hooks = [h["id"] for h in data["runtime_hooks"] if h["runtime_group"] == group]
        if not members:
            sys.exit(f"{group}: no patches in core/patches.json")
        missing = [h for h in hooks if h not in options.hooks]
        if missing:
            sys.exit(f"{group}: has hooks {missing}; it needs the feature's code (--code, --hooks)")
        if any("patched" not in p for p in data["patches"] if p["runtime_group"] == group):
            sys.exit(f"{group}: a patch with variants (targets); not a plain byte patch")
        patches += members
    folder = HERE / "features" / options.feature
    folder.mkdir(parents=True, exist_ok=True)
    feature = {"name": options.name, "dll": options.dll, "game": options.game,
               "source": "forza-injector core/patches.json (Extra Options 0.4.4 d66c85d)", "groups": groups}
    if options.code:
        feature["code"] = options.code
        feature["hooks"] = options.hooks
    (folder / "feature.json").write_text(json.dumps(feature, indent=2) + "\n", encoding="utf-8", newline="\n")
    (folder / "patches.json").write_text(json.dumps(patches, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(f"features/{options.feature}: {len(patches)} patches in {sorted(groups)}")


if __name__ == "__main__":
    main()
