"""Offline tests of Xtra Options (no game is run).

Usage: py plugins/xtra-options/tests/run_tests.py [--output out] [--sdk <forza-injector/sdk/include>] [--game <folder>]

1. same bytes: every features/<feature>/patches.json entry is the injector's
   core/patches.json entry of that id, field for field (skipped without the
   injector beside the SDK).
2. real game: every site's original bytes are in the real FH2 executable
   (--game, default ~/FH2; skipped when it is missing).
3. DLLs: each built feature DLL, loaded here and called as forza_injector
   calls it (ForzaPluginEarlyInitialize) with a copy of the FH2 image mapped in
   memory: with its keys on, every group's sites hold the patched bytes; with
   one key off, that group is untouched; with one site changed beforehand,
   that group is refused and none of its sites written; on another game build
   nothing is written.
"""
# SPDX-License-Identifier: GPL-3.0-or-later

import argparse
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import shutil
import struct
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
GAME_EXE = Path("Anthem_1.0.0.64_x64__8wekyb3d8bbwe") / "Mount" / "forza_durango_release_final.exe"
failures = []


def result(kind, what):
    print(f"  {kind:<5} {what}")
    if kind == "FAIL":
        failures.append(what)


def features():
    for folder in sorted(p for p in (ROOT / "features").iterdir() if (p / "feature.json").is_file()):
        yield (folder.name, json.loads((folder / "feature.json").read_text(encoding="utf-8")),
               json.loads((folder / "patches.json").read_text(encoding="utf-8")))


# ------------------------------------------------------------ 1. same bytes

HOOKS = {}  # core/patches.json runtime_hooks by id (filled by test_same_bytes)

def test_same_bytes(sdk):
    print("same bytes as the injector's core/patches.json")
    core = ROOT / "runtime" / "src" / "patches.json"
    if not core.is_file():
        result("skip", f"{core} not found")
        return
    data = json.loads(core.read_text(encoding="utf-8"))
    upstream = {p["id"]: p for p in data["patches"]}
    HOOKS.update({h["id"]: h for h in data["runtime_hooks"]})
    for name, feature, patches in features():
        groups = set(feature["groups"])
        hooks = [h["id"] for h in data["runtime_hooks"] if h["runtime_group"] in groups]
        result("ok" if sorted(hooks) == sorted(feature.get("hooks", [])) else "FAIL",
               f"{name}: the code implements every hook of its groups ({hooks or 'none'})")
        for patch in patches:
            source = upstream.get(patch["id"])
            same = source is not None and all(source[k] == patch[k] for k in patch)
            result("ok" if same else "FAIL", f"{name}: {patch['id']} is core/patches.json's")
        groups = set(feature["groups"])
        missing = [p for p in upstream.values() if p["runtime_group"] in groups and
                   p["id"] not in {q["id"] for q in patches}]
        result("ok" if not missing else "FAIL", f"{name}: every patch of {sorted(groups)} is here" +
               (f" (missing {[p['id'] for p in missing]})" if missing else ""))


# ------------------------------------------------------- the mapped image

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
kernel32.VirtualAlloc.restype = ctypes.c_void_p
kernel32.VirtualAlloc.argtypes = [ctypes.c_void_p, ctypes.c_size_t, wintypes.DWORD, wintypes.DWORD]
kernel32.VirtualFree.argtypes = [ctypes.c_void_p, ctypes.c_size_t, wintypes.DWORD]


class Image:
    """The game EXE laid out as the loader maps it (headers and sections at their RVAs)."""

    def __init__(self, path):
        data = Path(path).read_bytes()
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        sections, optional = struct.unpack_from("<H", data, pe + 6)[0], struct.unpack_from("<H", data, pe + 20)[0]
        self.size = struct.unpack_from("<I", data, pe + 24 + 56)[0]
        headers = struct.unpack_from("<I", data, pe + 24 + 60)[0]
        self.base = kernel32.VirtualAlloc(None, self.size, 0x3000, 0x04)  # MEM_COMMIT|RESERVE, PAGE_READWRITE
        if not self.base:
            raise OSError("VirtualAlloc failed")
        ctypes.memmove(self.base, data, headers)
        for i in range(sections):
            _, vsize, va, raw_size, raw = struct.unpack_from("<8sIIII", data, pe + 24 + optional + 40 * i)
            ctypes.memmove(self.base + va, data[raw:raw + min(raw_size, vsize or raw_size)], min(raw_size, vsize or raw_size))

    def read(self, rva, size):
        return ctypes.string_at(self.base + rva, size)

    def write(self, rva, data):
        ctypes.memmove(self.base + rva, data, len(data))

    def free(self):
        kernel32.VirtualFree(self.base, 0, 0x8000)


# ------------------------------------------------------------- 2. real game

def test_real_game(image):
    print("original bytes in the real FH2 executable")
    for name, _, patches in features():
        for patch in patches:
            found = image.read(patch["rva"], len(patch["original"]) // 2).hex()
            result("ok" if found == patch["original"] else "FAIL",
                   f"{name}: {patch['id']} at {patch['rva']:#x} holds {patch['original']} (found {found})")


# ------------------------------------------------------------------ 3. DLLs

LOG = ctypes.WINFUNCTYPE(None, ctypes.c_char_p)


class V1(ctypes.Structure):
    _fields_ = [("size", ctypes.c_uint32), ("api_version", ctypes.c_uint32), ("adapter", ctypes.c_uint32),
                ("game_image_size", ctypes.c_uint32), ("game_image", ctypes.c_void_p),
                ("plugin_module", ctypes.c_void_p), ("plugins_directory", ctypes.c_wchar_p),
                ("plugin_path", ctypes.c_wchar_p), ("extra_options_version", ctypes.c_char_p), ("log_utf8", LOG)]


class V2(ctypes.Structure):
    _fields_ = [("size", ctypes.c_uint32), ("api_version", ctypes.c_uint32), ("v1", ctypes.POINTER(V1)),
                ("game", ctypes.c_uint32), ("game_id", ctypes.c_char_p), ("title_id", ctypes.c_char_p),
                ("game_timestamp", ctypes.c_uint32), ("host_image", ctypes.c_void_p),
                ("host_timestamp", ctypes.c_uint32), ("host_image_size", ctypes.c_uint32),
                ("injector_version", ctypes.c_char_p), ("subscribe", ctypes.c_void_p)]


def call_early(dll_path, image, lines):
    module = ctypes.WinDLL(str(dll_path))
    early = module.ForzaPluginEarlyInitialize
    early.argtypes, early.restype = [ctypes.POINTER(V2)], ctypes.c_int
    log = LOG(lambda text: lines.append(text.decode("utf-8", "replace").rstrip()))
    v1 = V1(ctypes.sizeof(V1), 1, 1, image.size, image.base, module._handle, str(dll_path.parent), str(dll_path),
            b"0.4.4", log)
    v2 = V2(ctypes.sizeof(V2), 2, ctypes.pointer(v1), 1, b"fh2", b"1A96545B", 0x565501ED, None, 0, 0, b"0.1.0", None)
    ok = early(ctypes.byref(v2))
    call_early.keep.append((module, log, v1, v2))  # plugins are never unloaded
    return ok


call_early.keep = []


def stage(output, name, dll, ini_text):
    folder = output / "test-runs" / name
    shutil.rmtree(folder, ignore_errors=True)
    folder.mkdir(parents=True)
    shutil.copy2(output / "mods" / "Xtra Options" / "plugins" / dll, folder / dll)
    (folder / "XtraOptions.ini").write_text(ini_text, encoding="utf-8")
    return folder / dll


def ini_for(feature, off=()):
    sections = {}
    for group, key in feature["groups"].items():
        sections.setdefault(key["section"], []).append(f"{key['key']}={0 if group in off else 1}")
    return "".join(f"[{s}]\n" + "\n".join(keys) + "\n" for s, keys in sections.items())


def test_dlls(output, game_exe):
    print("feature DLLs, called as forza_injector calls them")
    for name, feature, patches in features():
        dll = output / "mods" / "Xtra Options" / "plugins" / feature["dll"]
        if not dll.is_file():
            result("FAIL", f"{name}: {feature['dll']} was not built")
            continue
        by_group = {g: [p for p in patches if p["runtime_group"] == g] for g in feature["groups"]}
        groups = list(feature["groups"])

        # All on.
        image, lines = Image(game_exe), []
        ok = call_early(stage(output, name + "-on", feature["dll"], ini_for(feature)), image, lines)
        patched = all(image.read(p["rva"], len(p["patched"]) // 2).hex() == p["patched"] for p in patches)
        result("ok" if ok and patched else "FAIL", f"{name}: all keys on: every site patched ({len(patches)})")
        result("ok" if sum("applied" in line for line in lines) == len(groups) else "FAIL",
               f"{name}: the log reports each group applied ({len(groups)})")
        # Hooks the feature's code implements: their slot points into this DLL.
        module = call_early.keep[-1][0]._handle
        for hook in feature.get("hooks", []):
            source = HOOKS.get(hook)
            if source is None:
                result("skip", f"{name}: hook {hook}: the injector's core/patches.json is not here")
                continue
            if source["type"] == "callback_thunk_in_unused_padding":
                thunk = image.read(source["rva"], 12)
                result("ok" if thunk[:2] == b"\x48\xb8" and thunk[10:] == b"\xff\xe0" else "FAIL",
                       f"{name}: hook {hook} is an absolute jump thunk")
                target = struct.unpack("<Q", thunk[2:10])[0]
            else:
                target = struct.unpack("<Q", image.read(source["rva"], 8))[0]
            result("ok" if 0 < target - module < 0x1000000 else "FAIL",
                   f"{name}: hook {hook} at {source['rva']:#x} points into {feature['dll']} (+{target - module:#x})")
        image.free()

        # The last group off.
        off = groups[-1]
        image, lines = Image(game_exe), []
        call_early(stage(output, name + "-off", feature["dll"], ini_for(feature, off=[off])), image, lines)
        untouched = all(image.read(p["rva"], len(p["original"]) // 2).hex() == p["original"] for p in by_group[off])
        others = all(image.read(p["rva"], len(p["patched"]) // 2).hex() == p["patched"]
                     for g in groups[:-1] for p in by_group[g])
        result("ok" if untouched and others else "FAIL", f"{name}: {off} off: its sites untouched, the others patched")
        image.free()

        # The first group's last site changed beforehand: that group is refused, nothing of it written.
        first = groups[0]
        image, lines = Image(game_exe), []
        bad = by_group[first][-1]
        image.write(bad["rva"], b"\xcc")
        call_early(stage(output, name + "-bad", feature["dll"], ini_for(feature)), image, lines)
        refused = all(image.read(p["rva"], len(p["original"]) // 2).hex() == p["original"]
                      for p in by_group[first][:-1])
        result("ok" if refused and any("unexpected bytes" in line for line in lines) else "FAIL",
               f"{name}: a changed {first} site: the group is refused, none of its sites written")
        image.free()

        # Another build: nothing written.
        image, lines = Image(game_exe), []
        pe = struct.unpack_from("<I", image.read(0, 0x40), 0x3C)[0]
        image.write(pe + 8, struct.pack("<I", 0x12345678))
        call_early(stage(output, name + "-other", feature["dll"], ini_for(feature)), image, lines)
        nothing = all(image.read(p["rva"], len(p["original"]) // 2).hex() == p["original"] for p in patches)
        result("ok" if nothing and any("not the supported game build" in line for line in lines) else "FAIL",
               f"{name}: another game build: nothing written, the log says why")
        image.free()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", type=Path, default=ROOT / "out")
    parser.add_argument("--sdk", type=Path, default=ROOT.parents[2] / "forza-injector" / "sdk" / "include")
    parser.add_argument("--game", type=Path, default=Path.home() / "FH2")
    options = parser.parse_args()
    output = options.output.resolve()
    test_same_bytes(options.sdk.resolve())
    game_exe = options.game / GAME_EXE
    if not game_exe.is_file():
        print("real game and DLLs")
        result("skip", f"the FH2 executable is not at {game_exe} (--game)")
    else:
        image = Image(game_exe)
        test_real_game(image)
        image.free()
        test_dlls(output, game_exe)
    print(f"{len(failures)} failure(s)" if failures else "all tests passed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
