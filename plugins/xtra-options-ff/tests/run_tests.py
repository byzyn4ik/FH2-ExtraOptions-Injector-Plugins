"""Offline tests of Xtra Options (Fast & Furious) (no game is run).

Usage: py plugins/xtra-options-ff/tests/run_tests.py [--output out] [--ff <Fast & Furious game EXE>]

1. ported: every feature is its Xtra Options (FH2) feature, field for field,
   except the site (rva) and the game; the same ids, groups and INI keys.
2. real game: every site's original bytes are in Fast & Furious' executable
   (--ff, default D:\\Games\\Forza Horizon 2 Fast and Furious\\game\\Mount\\...;
   skipped when it is missing).
3. DLLs: each built DLL called as forza_injector calls it
   (ForzaPluginEarlyInitialize) with a mapped copy of Fast & Furious: all keys
   on patches every site; a key off leaves its group; a changed site refuses
   its group; another build (FH2's stamp) writes nothing.
"""
# SPDX-License-Identifier: GPL-3.0-or-later

import argparse
import ctypes
import importlib.util
import json
import re
from pathlib import Path
import shutil
import struct
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
XTRA = ROOT.parent / "xtra-options"
FF_EXE = Path(r"D:\Games\Forza Horizon 2 Fast and Furious\game\Mount\forza_durango_release_final.exe")
MOD_NAME = "Xtra Options (Fast & Furious)"

spec = importlib.util.spec_from_file_location("xtra_options_tests", XTRA / "tests" / "run_tests.py")
xo = importlib.util.module_from_spec(spec)
spec.loader.exec_module(xo)
result, failures = xo.result, xo.failures


def features(root=ROOT):
    for folder in sorted(p for p in (root / "src" / "features").iterdir() if (p / "feature.json").is_file()):
        yield (folder.name, json.loads((folder / "feature.json").read_text(encoding="utf-8")),
               json.loads((folder / "patches.json").read_text(encoding="utf-8")))


def test_ported():
    print("ported from Xtra Options (FH2)")
    for name, feature, patches in features():
        if feature.get("native"):
            result("ok" if not (XTRA / "features" / name).exists() and all("fh2_rva" not in p for p in patches)
                   else "FAIL", f"{name}: Fast & Furious only, no FH2 feature to port")
            continue
        source = XTRA / "features" / name
        fh2_feature = json.loads((source / "feature.json").read_text(encoding="utf-8"))
        fh2_patches = {p["id"]: p for p in json.loads((source / "patches.json").read_text(encoding="utf-8"))}
        same = all(feature[k] == fh2_feature[k] for k in ("name", "dll", "groups")) and feature["game"] == "fh2ff"
        result("ok" if same else "FAIL", f"{name}: the FH2 feature's name, DLL and INI keys, for fh2ff")
        result("ok" if sorted(fh2_patches) == sorted(p["id"] for p in patches) else "FAIL",
               f"{name}: every FH2 site has its Fast & Furious site ({len(patches)})")
        for patch in patches:
            fh2 = fh2_patches.get(patch["id"], {})
            fields = all(patch[k] == fh2.get(k) for k in patch if k not in ("rva", "fh2_rva"))
            result("ok" if fields and patch.get("fh2_rva") == fh2.get("rva") else "FAIL",
                   f"{name}: {patch['id']}: FH2 {fh2.get('rva', 0):#x} -> {patch['rva']:#x}, same bytes")


def test_real_game(ff_exe):
    print("original bytes in the real Fast & Furious executable")
    image = xo.Image(ff_exe)
    for name, _, patches in features():
        for patch in patches:
            found = image.read(patch["rva"], len(patch["original"]) // 2).hex()
            result("ok" if found == patch["original"] else "FAIL",
                   f"{name}: {patch['id']} at {patch['rva']:#x} holds {patch['original']} (found {found})")
    image.free()


def test_photo_unlock(ff_exe):
    """PhotoModeUnlock's one site is the flag load inside WaitForPause's photo-mode check (0x9a7250):
    the check's own code around it, and the only copy of that code in the executable."""
    print("photo mode unlock site")
    image = xo.Image(ff_exe)
    head = image.read(0x9A7250, 0x60)
    target = 0x9A725D + 5 + struct.unpack_from("<i", head, 0xE)[0]
    result("ok" if head.startswith(bytes.fromhex("48895c2408574883ec20488bd9e8")) and target == 0xA9C4B0 else "FAIL",
           f"the check 0x9a7250 first calls the record lookup 0xa9c4b0 (target {target:#x})")
    result("ok" if head[0x15:0x19].hex() == "0fb6783d" else "FAIL",
           "then loads the flag at 0x9a7265 (movzx edi, byte [rax+0x3d])")
    result("ok" if head[0x58:0x5f].hex() == "4084ff740db001" else "FAIL",
           f"and allows photo mode only with it set (test dil, dil; je; mov al, 1 at 0x9a72a8: {head[0x58:0x5f].hex()})")
    image.free()
    # The check's first 0x20 bytes, call target masked: one copy in .text (no other use of this flag load).
    data = Path(ff_exe).read_bytes()
    pattern = re.escape(head[:0xE]) + b"...." + re.escape(head[0x12:0x20])
    count = len(re.findall(pattern, data, re.DOTALL))
    result("ok" if count == 1 else "FAIL", f"the check's code exists once in the executable ({count})")


MT_SLOTS = {  # MapTeleport's vtable slots: slot RVA -> the game's function there
    "update": (0x2E48F38, 0xDA0080),  # MapScene (0x2e48a70) slot 153, its update
    "x": (0x2E52F40, 0xEFF220),       # the X closure (0x2e52f30) invoke
    "y": (0x2E53000, 0xDB8270),       # the Y closure (0x2e52ff0) invoke
}


def relocate_slots(image, ff_exe):
    """Apply the executable's DIR64 relocations on the pages of MapTeleport's slots, as the loader does
    (Image maps the sections without relocating them; a slot then holds the preferred-base address)."""
    data = Path(ff_exe).read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    preferred = struct.unpack_from("<Q", data, pe + 24 + 24)[0]
    reloc_rva, reloc_size = struct.unpack_from("<II", data, pe + 24 + 112 + 5 * 8)
    sections, optional = struct.unpack_from("<H", data, pe + 6)[0], struct.unpack_from("<H", data, pe + 20)[0]
    raw = None
    for i in range(sections):
        _, vsize, va, raw_size, raw_at = struct.unpack_from("<8sIIII", data, pe + 24 + optional + 40 * i)
        if va <= reloc_rva < va + max(vsize, raw_size):
            raw = raw_at + reloc_rva - va
    pages = {slot & ~0xFFF for slot, _ in MT_SLOTS.values()}
    at, end, delta = raw, raw + reloc_size, image.base - preferred
    while at < end:
        page, size = struct.unpack_from("<II", data, at)
        if not size:
            break
        if page in pages:
            for k in range((size - 8) // 2):
                entry = struct.unpack_from("<H", data, at + 8 + 2 * k)[0]
                if entry >> 12 == 10:  # IMAGE_REL_BASED_DIR64
                    rva = page + (entry & 0xFFF)
                    value = struct.unpack("<Q", image.read(rva, 8))[0]
                    image.write(rva, struct.pack("<Q", (value + delta) & 0xFFFFFFFFFFFFFFFF))
        at += size
    return image


def ff_image(ff_exe):
    return relocate_slots(xo.Image(ff_exe), ff_exe)


def slot_value(image, slot):
    return struct.unpack("<Q", image.read(slot, 8))[0]


def module_range(module):
    handle = module._handle
    pe = ctypes.c_uint32.from_address(handle + 0x3C).value
    return handle, handle + ctypes.c_uint32.from_address(handle + pe + 24 + 56).value


def rtti_name(image, preferred, vtable):
    col = struct.unpack("<Q", image.read(vtable - 8, 8))[0] - preferred
    descriptor = struct.unpack("<I", image.read(col + 12, 4))[0]
    return image.read(descriptor + 0x10, 64).split(b"\0")[0].decode("ascii", "replace")


def test_map_teleport_sites(ff_exe):
    """MapTeleport's game facts, read from the real executable (beyond each guard's own bytes)."""
    print("map teleport sites")
    image = xo.Image(ff_exe)
    data = Path(ff_exe).read_bytes()
    preferred = struct.unpack_from("<Q", data, struct.unpack_from("<I", data, 0x3C)[0] + 24 + 24)[0]
    for vtable, name in ((0x2E48A70, ".?AVMapScene@@"), (0x2DE6510, ".?AVMapViewControllerInteractive@@"),
                         (0x2CD88D8, ".?AVFastTravelModule@EntityModel@@")):
        found = rtti_name(image, preferred, vtable)
        result("ok" if found == name else "FAIL", f"vtable {vtable:#x} is {name} (RTTI: {found})")
    for what, (slot, function) in MT_SLOTS.items():
        found = slot_value(image, slot) - preferred
        result("ok" if found == function else "FAIL",
               f"slot {slot:#x} ({what}) holds {function:#x} (found {found:#x})")

    def rel(at):  # the target of the rel32 at `at`
        return at + 4 + struct.unpack("<i", image.read(at, 4))[0]

    def u32(at):
        return struct.unpack("<I", image.read(at, 4))[0]
    # The map binds UI key 0x43 (X) to 0xda2a20 through closure vtable 0x2e52f30, and 0x44 (Y) through 0x2e52ff0.
    x = rel(0xD7E3CC), rel(0xD7E3F4), u32(0xD7E412)
    result("ok" if x == (0xDA2A20, 0x2E52F30, 0x43) else "FAIL",
           f"the map binds X (key {x[2]:#x}) to {x[0]:#x} through closure vtable {x[1]:#x}")
    y = rel(0xD7E48D), u32(0xD7E4A6)
    result("ok" if y == (0x2E52FF0, 0x44) else "FAIL",
           f"the map binds Y (key {y[1]:#x}) through closure vtable {y[0]:#x}")
    # The pad-to-UI table: X's press/release actions 0x8e/0x8f raise key 0x43, Y's 0x91/0x92 raise 0x44.
    table = struct.unpack("<12I", image.read(0x2280270, 48))
    result("ok" if table[6:12] == (0x8E, 0x8F, 0x43, 0x91, 0x92, 0x44) else "FAIL",
           f"UI actions 0x8e/0x8f are X (0x43), 0x91/0x92 are Y (0x44): {[hex(v) for v in table[6:12]]}")
    # The game's fast-travel confirmation (0xd6bd90) sends controller +0x3d0 (page +0x428) to 0x561d10.
    confirm = image.read(0xD6BD90, 0x30)
    result("ok" if confirm[0x10:0x17] == bytes.fromhex("488b9028040000") and
           confirm[0x17:0x1F] == bytes.fromhex("c5f82882d0030000") and rel(0xD6BDBB) == 0x561D10 else "FAIL",
           "the game's fast-travel confirmation (0xd6bd90) sends controller +0x3d0 to 0x561d10")
    # 0x561d90(module, &position, cost): destination kind 1 (a position) at +0xb0, the position at +0x100.
    travel = image.read(0x561D90, 0x30)
    result("ok" if travel[6:16] == bytes.fromhex("c781b000000001000000") and
           travel[0x21:0x28] == bytes.fromhex("44898120010000") and
           travel[0x28:0x30] == bytes.fromhex("c5f8298100010000") else "FAIL",
           "0x561d90 stores a position destination (+0xb0 = 1, +0x120 the cost, +0x100 the position)")
    # It posts FastTravelInitiated on the entity root the map reads (0x352b3b0).
    root = rel(0x561DD0)
    result("ok" if root == 0x352B3B0 else "FAIL", f"0x561d90 posts on the entity root 0x352b3b0 ({root:#x})")
    # The free-roam check reads the event context (+0x60) through 0x219530 on the same root.
    lookup = rel(0x98A514), rel(0x98A519)
    result("ok" if lookup == (0x352B3B0, 0x219530) and image.read(0x875370, 4) == bytes.fromhex("8b4160c3")
           else "FAIL", "the event context: 0x219530 on the root 0x352b3b0, its value at +0x60 (0x875370)")
    # The legend: help-bar slot 2 is XDescription (0xb68300's table), the map sets its buttons through
    # 0xd44610 / 0x1431f40 / 0xba1e90, and CUserString (0x2ef91c0) is the literal text the DLL hands it.
    entry = u32(0xB683CC + 2 * 4)
    xdesc = rel(entry + 3)
    result("ok" if entry == 0xB6832F and xdesc == 0x354EB78 else "FAIL",
           f"help-bar slot 2 is the XDescription property ({entry:#x} -> {xdesc:#x})")
    result("ok" if rel(0xDA00D9) == 0xD44610 and rel(0xDA0155) == 0x1431F40 and rel(0xDA0177) == 0xBA1E90 and
           image.read(0xDA016E, 5) == bytes.fromhex("ba06000000") else "FAIL",
           "the map's button bar: 0xd44610(page), 0x1431f40 (UI string), 0xba1e90(bar, slot, text)")
    user_string = rtti_name(image, preferred, 0x2EF91C0)
    slots = [slot_value(image, 0x2EF91C0 + 8 * i) - preferred for i in (2, 3, 7)]
    result("ok" if user_string == ".?AVCUserString@@" and slots == [0x75E3E0, 0x758780, 0x1E0830] and
           image.read(0x1E0830, 5) == bytes.fromhex("488b4108c3") else "FAIL",
           f"CUserString ({user_string}): add reference, release, text at +8 {[hex(s) for s in slots]}")
    image.free()
    # Every guard but the short, generic ones exists once in the executable.
    generic = {"map_scene_update_entry", "map_x_closure_invoke", "event_context_value", "ui_string_from_loc"}
    patches = json.loads((ROOT / "src" / "features" / "map-teleport" / "patches.json").read_text(encoding="utf-8"))
    for patch in patches:
        if patch["id"] not in generic:
            count = data.count(bytes.fromhex(patch["original"]))
            result("ok" if count == 1 else "FAIL", f"guard {patch['id']} exists once in the executable ({count})")


def test_map_teleport_dll(output, ff_exe):
    """MapTeleport.dll as forza_injector calls it: the slots it swaps, per setting."""
    print("map teleport DLL")

    def run(name, ini, prepare=None):
        image, lines = ff_image(ff_exe), []
        if prepare:
            prepare(image)
        xo.call_early(stage(output, "map-teleport-dll-" + name, "MapTeleport.dll", ini), image, lines)
        lo, hi = module_range(xo.call_early.keep[-1][0])
        values = {what: slot_value(image, slot) for what, (slot, _) in MT_SLOTS.items()}
        hooked = {what: lo <= values[what] < hi for what in MT_SLOTS}
        untouched = {what: values[what] == image.base + function for what, (_, function) in MT_SLOTS.items()}
        image.free()
        return lines, hooked, untouched

    lines, hooked, untouched = run("on", "[Map]\nTeleport=1\n")
    result("ok" if hooked["update"] and hooked["x"] and untouched["y"] else "FAIL",
           "on (defaults): MapScene's update and the X closure point into the DLL, Y untouched")
    result("ok" if any("button X, hold (ms) 500" in line for line in lines) else "FAIL",
           f"the log names button X and 500 ms ({lines})")
    lines, hooked, untouched = run("y", "[Map]\nTeleport=1\nTeleportButton=Y\nTeleportHold=0\n")
    result("ok" if hooked["update"] and hooked["y"] and untouched["x"] else "FAIL",
           "TeleportButton=Y: the Y closure is hooked, X untouched")
    result("ok" if any("button Y, hold (ms) 0" in line for line in lines) else "FAIL",
           "the log names button Y and 0 ms")
    lines, hooked, untouched = run("bad", "[Map]\nTeleport=1\nTeleportButton=B\nTeleportHold=99999\n")
    result("ok" if hooked["x"] and any("not X or Y" in line for line in lines) and
           any("clamped" in line for line in lines) and any("hold (ms) 5000" in line for line in lines) else "FAIL",
           "TeleportButton=B, TeleportHold=99999: X and 5000 ms, both logged")
    lines, hooked, untouched = run("off", "[Map]\nTeleport=0\n")
    result("ok" if all(untouched.values()) and any("off in XtraOptions.ini" in line for line in lines) else "FAIL",
           "Teleport=0: no slot changed")

    def foreign(image):  # another module already hooked MapScene's update
        image.write(MT_SLOTS["update"][0], struct.pack("<Q", 0x7FF612340000))
    lines, hooked, untouched = run("foreign", "[Map]\nTeleport=1\n", foreign)
    result("ok" if not hooked["update"] and untouched["x"] and any("unexpected bytes" in line for line in lines)
           else "FAIL", "a slot that holds another module's hook: refused, nothing written")

    def other_build(image):
        pe = struct.unpack_from("<I", image.read(0, 0x40), 0x3C)[0]
        image.write(pe + 8, struct.pack("<I", 0x565501ED))
    lines, hooked, untouched = run("other", "[Map]\nTeleport=1\n", other_build)
    result("ok" if all(untouched.values()) and any("not the supported game build" in line for line in lines)
           else "FAIL", "another game build: no slot changed, the log says why")


def stage(output, name, dll, ini_text):
    folder = output / "test-runs" / name
    shutil.rmtree(folder, ignore_errors=True)
    folder.mkdir(parents=True)
    shutil.copy2(output / "mods" / MOD_NAME / "plugins" / dll, folder / dll)
    (folder / "XtraOptions.ini").write_text(ini_text, encoding="utf-8")
    return folder / dll


def test_dlls(output, ff_exe):
    print("feature DLLs, called as forza_injector calls them, on Fast & Furious")
    for name, feature, patches in features():
        by_group = {g: [p for p in patches if p["runtime_group"] == g] for g in feature["groups"]}
        groups = list(feature["groups"])
        image, lines = ff_image(ff_exe), []
        ok = xo.call_early(stage(output, name + "-on", feature["dll"], xo.ini_for(feature)), image, lines)
        patched = all(image.read(p["rva"], len(p["patched"]) // 2).hex() == p["patched"] for p in patches)
        result("ok" if ok and patched else "FAIL", f"{name}: all keys on: every site patched ({len(patches)})")
        result("ok" if sum("applied" in line for line in lines) == len(groups) else "FAIL",
               f"{name}: the log reports each group applied ({lines})")
        image.free()

        off = groups[-1]
        image, lines = ff_image(ff_exe), []
        xo.call_early(stage(output, name + "-off", feature["dll"], xo.ini_for(feature, off=[off])), image, lines)
        untouched = all(image.read(p["rva"], len(p["original"]) // 2).hex() == p["original"] for p in by_group[off])
        result("ok" if untouched else "FAIL", f"{name}: {off} off: its sites untouched")
        image.free()

        first = groups[0]
        image, lines = ff_image(ff_exe), []
        bad = by_group[first][-1]
        image.write(bad["rva"], b"\xcc")
        xo.call_early(stage(output, name + "-bad", feature["dll"], xo.ini_for(feature)), image, lines)
        refused = all(image.read(p["rva"], len(p["original"]) // 2).hex() == p["original"]
                      for p in by_group[first][:-1])
        result("ok" if refused and any("unexpected bytes" in line for line in lines) else "FAIL",
               f"{name}: a changed {first} site: the group is refused, none of its sites written")
        image.free()

        image, lines = ff_image(ff_exe), []
        pe = struct.unpack_from("<I", image.read(0, 0x40), 0x3C)[0]
        image.write(pe + 8, struct.pack("<I", 0x565501ED))
        xo.call_early(stage(output, name + "-other", feature["dll"], xo.ini_for(feature)), image, lines)
        nothing = all(image.read(p["rva"], len(p["original"]) // 2).hex() == p["original"] for p in patches)
        result("ok" if nothing and any("not the supported game build" in line for line in lines) else "FAIL",
               f"{name}: another game build: nothing written, the log says why")
        image.free()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", type=Path, default=ROOT / "out")
    parser.add_argument("--ff", type=Path, default=FF_EXE)
    options = parser.parse_args()
    output = options.output.resolve()
    test_ported()
    if not options.ff.is_file():
        print("real game and DLLs")
        result("skip", f"the Fast & Furious executable is not at {options.ff} (--ff)")
    else:
        test_real_game(options.ff)
        test_photo_unlock(options.ff)
        test_map_teleport_sites(options.ff)
        test_dlls(output, options.ff)
        test_map_teleport_dll(output, options.ff)
    print(f"{len(failures)} failure(s)" if failures else "all tests passed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
