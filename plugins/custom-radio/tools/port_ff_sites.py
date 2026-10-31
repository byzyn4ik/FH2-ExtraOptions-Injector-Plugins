"""Find Custom Radio's Forza Horizon 2 sites again in Fast & Furious and write src/ff_sites.h.

Usage: py tools/port_ff_sites.py --fh2 <FH2 forza_durango_release_final.exe> --ff <F&F one> [--out src/ff_sites.h]

Every site of src/sites.h (functions, virtual-table slots, rel32 calls, globals and
virtual tables) is found again with Keyboard & Mouse's porter
(plugins/keyboard-mouse/tools/port_sites.py: masked instruction patterns, callers,
vtable slots, RTTI) and then checked further:

- a function: the same instructions from entry to end in both games (.pdata
  length; rip-relative and rel32 fields masked), else it is listed as differing
  (RadioModule::IsStationLocked and ChangeStation are, see FF_DIFFERENCES); its
  guard is Fast & Furious' own first bytes, as many as sites.h has for FH2, none
  of them a base-relocated pointer;
- a slot: the ported vtable slot holds the ported function;
- a call: an E8 to the ported function, inside the ported caller; its guard is
  Fast & Furious' 8 bytes on each side;
- a virtual table: the same run-time type (RTTI) in both games;
- a global: every ported reference agrees.

The sites Fast & Furious needs beyond sites.h (the local car's update, which
Forza Horizon 2's Extra Options runtime calls the plugin from, and the reads
around it) are ported and checked the same way (FF_EXTRA). Exit code 1 when a
site is not found or a check fails; nothing is written then. Needs pefile,
capstone and numpy. Both games are only read.
"""
# SPDX-License-Identifier: GPL-3.0-or-later

import argparse
from pathlib import Path
import re
import struct
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE.parents[2] / "plugins" / "keyboard-mouse" / "tools"))
from port_sites import Image, Porter  # noqa: E402

# Functions whose code differs in Fast & Furious (found by their callers and checked by hand).
FF_DIFFERENCES = {
    "FN_STATION_LOCKED": "Fast & Furious' IsStationLocked(module, name) also counts a name set at "
                         "RadioModule+0xd8 as locked; same arguments and result",
    "FF_FN_CAR_UPDATE": "ForzaCar::Update reads other car fields in Fast & Furious (a larger car); the "
                        "plugin only calls it with the same four arguments",
    "FN_CHANGE_STATION": "ChangeStation's last two RadioModule fields moved by 0x10 (+0xd8/+0xe0 -> "
                         "+0xe8/+0xf0); the plugin reads only +0x60 and +0x68, which did not move",
}

# Fast & Furious only: name -> (FH2 RVA, kind, guard length)
FF_EXTRA = {
    "FF_FN_CAR_UPDATE": (0x2A9400, "function", 16),      # ForzaCar::Update(car, dt, frame dt, frame)
    "FF_SLOT_CAR_UPDATE": (0x2C5CC00, "slot", 0),        # ForzaCar vtable 0x2c5cb80 + 0x80
    "FF_RVA_FORZA_CAR_VTABLE": (0x2C5CB80, "vtable", 0),
    "FF_FN_LOCAL_PLAYER": (0x21E0C0, "function", 7),     # mov rcx, [rcx+0x48]; test rcx, rcx (then jne)
    "FF_FN_PLAYER_CAR": (0x2DA060, "function", 16),
    "FF_RVA_PLAYER_MANAGER": (0x34ABA60, "global", 0),
    "FF_RVA_TEXT_TABLES": (0x3662990, "global", 0),       # common/include/fh2_game.h FH2_TEXT_TABLES
    "FF_RVA_CONTROLLER_MANAGER": (0x365D7C8, "global", 0),
    "FF_RVA_CONTROLLER_MANAGER_VTABLE": (0x2E7A2E8, "vtable", 0),
}


def c_bytes(text):
    return bytes(int(h, 16) for h in re.findall(r"\\x([0-9a-fA-F]{2})", text))


def load_sites(path):
    source = Path(path).read_text(encoding="utf-8")
    functions = {n: (int(r, 16), c_bytes(b)) for n, r, b in
                 re.findall(r'X\((FN_\w+),\s*(0x[0-9a-fA-F]+),\s*"([^"]*)"\)', source)}
    slots = {n: (int(r, 16), f) for n, r, f in re.findall(r"X\((SLOT_\w+),\s*(0x[0-9a-fA-F]+),\s*(FN_\w+)\)", source)}
    calls = {n: (int(r, 16), f, c_bytes(b), c_bytes(a)) for n, r, f, b, a in re.findall(
        r'X\((CALL_\w+),\s*(0x[0-9a-fA-F]+),\s*(FN_\w+),\s*"([^"]*)",\s*"([^"]*)"\)', source)}
    data = {n: int(r, 16) for n, r in re.findall(r"#define (RVA_\w+) (0x[0-9a-fA-F]+)u", source)}
    return functions, slots, calls, data


def q(image, rva):
    return int.from_bytes(image.data[rva:rva + 8], "little") - image.base


def same_function(porter, a, b):
    span, other = porter.function_of(a), porter.ff_function_of(b)
    if not span or not other or span[0] != a or other[0] != b or span[1] - span[0] != other[1] - other[0]:
        return False
    instructions = list(porter.md.disasm(porter.fh2.data[a:span[1]], a))
    return re.fullmatch(porter.pattern(instructions), porter.ff.data[b:other[1]], re.S) is not None


def c_literal(data):
    return '"' + "".join(f"\\x{b:02x}" for b in data) + '"'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--fh2", type=Path, required=True)
    parser.add_argument("--ff", type=Path, required=True)
    parser.add_argument("--out", type=Path)
    options = parser.parse_args()
    fh2, ff = Image(options.fh2), Image(options.ff)
    porter = Porter(fh2, ff)
    functions, slots, calls, data = load_sites(ROOT / "src" / "sites.h")
    bad = []
    ported = {}
    import pefile
    pe = pefile.PE(str(options.ff), fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_BASERELOC"]])
    reloc = {e.rva for block in getattr(pe, "DIRECTORY_ENTRY_BASERELOC", []) for e in block.entries if e.type == 10}

    def relocated_in(rva, size):
        return any(r in reloc for r in range(rva - 7, rva + size))

    def fail(what):
        bad.append(what)
        print("  FAIL", what)

    print(f"Fast & Furious: TimeDateStamp 0x{ff.stamp:08x}, SizeOfImage 0x{ff.size:x}")
    function_rows = []
    for name, (rva, guard) in functions.items():
        found, why, _ = porter.port(rva)
        if found is None:
            fail(f"{name} {rva:#x}: not found ({why})")
            continue
        ported[name] = found
        same = same_function(porter, rva, found)
        note = "same instructions" if same else FF_DIFFERENCES.get(name)
        if not same and not note:
            fail(f"{name} {rva:#x} -> {found:#x}: its code differs")
            continue
        size = len(guard)
        if relocated_in(found, size):
            fail(f"{name} {found:#x}: its first {size} bytes hold a relocated pointer")
            continue
        function_rows.append((name, found, ff.data[found:found + size], f"FH2 {rva:#x}; {note}"))
        print(f"{name} {rva:#x} -> {found:#x} ({note})")
    slot_rows = []
    for name, (rva, function) in slots.items():
        found, why, _ = porter.port(rva)
        if found is None or function not in ported or q(ff, found) != ported[function]:
            fail(f"{name} {rva:#x}: {why}; holds {q(ff, found) if found else 0:#x}")
            continue
        slot_rows.append((name, found, function, f"FH2 {rva:#x}"))
        print(f"{name} {rva:#x} -> {found:#x} (holds {function} {ported[function]:#x})")
    call_rows = []
    for name, (rva, function, before, after) in calls.items():
        found, why, _ = porter.port(rva)
        code = ff.data[found - 8:found + 13] if found else b""
        target = found + 5 + struct.unpack("<i", code[9:13])[0] if found else 0
        caller, ff_caller = porter.function_of(rva), porter.ff_function_of(found) if found else None
        caller_port = porter.port(caller[0])[0] if caller else None
        if found is None or code[8] != 0xE8 or function not in ported or target != ported[function] or \
                not ff_caller or found - ff_caller[0] != rva - caller[0] or                 (caller_port != ff_caller[0] and not same_function(porter, caller[0], ff_caller[0])):
            fail(f"{name} {rva:#x}: {why}")
            continue
        if relocated_in(found - 8, 21):
            fail(f"{name}: a guard byte is a relocated pointer")
            continue
        same = same_function(porter, caller[0], ff_caller[0])
        call_rows.append((name, found, function, code[:8], code[13:],
                          f"FH2 {rva:#x}, in {ff_caller[0]:#x} (FH2 {caller[0]:#x}, "
                          f"{'same instructions' if same else 'differs'})"))
        print(f"{name} {rva:#x} -> {found:#x} in {ff_caller[0]:#x} ({'same' if same else 'differs'})")
    data_rows = []
    for name, rva in data.items():
        found, why, _ = porter.port(rva)
        if found is None:
            fail(f"{name} {rva:#x}: {why}")
            continue
        if name.endswith("_VTABLE") and porter.locator(fh2, q(fh2, rva - 8)) != porter.locator(ff, q(ff, found - 8)):
            fail(f"{name} {rva:#x} -> {found:#x}: another run-time type")
            continue
        data_rows.append((name, found, f"FH2 {rva:#x}; {why}"))
        print(f"{name} {rva:#x} -> {found:#x} ({why})")
    extra_rows = []
    for name, (rva, kind, size) in FF_EXTRA.items():
        found, why, _ = porter.port(rva)
        if found is None:
            fail(f"{name} {rva:#x}: {why}")
            continue
        if kind == "vtable" and porter.locator(fh2, q(fh2, rva - 8)) != porter.locator(ff, q(ff, found - 8)):
            fail(f"{name}: another run-time type")
            continue
        if kind == "slot" and q(ff, found) != porter.port(q(fh2, rva))[0]:
            fail(f"{name}: the slot does not hold the ported function")
            continue
        if kind == "function":
            leaf = porter.function_of(rva) is None
            if not leaf and not same_function(porter, rva, found) and name not in FF_DIFFERENCES:
                fail(f"{name} {rva:#x} -> {found:#x}: its code differs")
                continue
            if relocated_in(found, size):
                fail(f"{name}: guard bytes hold a relocated pointer")
                continue
            extra_rows.append((name, found, ff.data[found:found + size], f"FH2 {rva:#x}; {why}"))
        else:
            extra_rows.append((name, found, None, f"FH2 {rva:#x}; {why}"))
        print(f"{name} {rva:#x} -> {found:#x} ({why})")
    if bad:
        print(f"{len(bad)} site(s) failed; nothing written")
        return 1
    print(f"{len(function_rows)} functions, {len(slot_rows)} slots, {len(call_rows)} calls, {len(data_rows)} data, "
          f"{len(extra_rows)} Fast & Furious sites: all found and checked")
    if not options.out:
        return 0
    lines = [
        "// Custom Radio: the game code it uses in Forza Horizon 2 Presents Fast & Furious.",
        "// SPDX-License-Identifier: GPL-3.0-or-later",
        "//",
        "// Generated by tools/port_ff_sites.py from Forza Horizon 2 1.0.0.64 (TimeDateStamp "
        f"0x{fh2.stamp:08x}) and",
        f"// Fast & Furious 1.0.0.6 (forza_durango_release_final.exe, TimeDateStamp 0x{ff.stamp:08x}, SizeOfImage "
        f"0x{ff.size:x}). Do not edit.",
        "// The same sites as sites.h (same names), found again in Fast & Furious; the guards are",
        "// Fast & Furious' own bytes. tests/run_tests.py --ff-exe checks them again.",
        "#ifndef CUSTOM_RADIO_FF_SITES_H",
        "#define CUSTOM_RADIO_FF_SITES_H",
        "",
        f"#define FF_GAME_IMAGE_SIZE 0x{ff.size:x}u",
        f"#define FF_GAME_STAMP 0x{ff.stamp:08x}u",
        f"#define FF_CODE_END 0x{(ff.text[1] + 0xFFF) & ~0xFFF:x}u",
        "",
        "#define FF_RADIO_FUNCTIONS(X) \\",
    ]
    for name, rva, guard, note in function_rows:
        lines.append(f"    /* {note} */ \\")
        lines.append(f"    X({name}, 0x{rva:x}, {c_literal(guard)}) \\")
    lines += ["", "#define FF_RADIO_SLOTS(X) \\"]
    for name, rva, function, note in slot_rows:
        lines.append(f"    /* {note} */ \\")
        lines.append(f"    X({name}, 0x{rva:x}, {function}) \\")
    lines += ["", "#define FF_RADIO_CALLS(X) \\"]
    for name, rva, function, before, after, note in call_rows:
        lines.append(f"    /* {note} */ \\")
        lines.append(f"    X({name}, 0x{rva:x}, {function}, {c_literal(before)}, {c_literal(after)}) \\")
    lines.append("")
    for name, rva, note in data_rows:
        lines.append(f"#define FF_{name} 0x{rva:x}u  // {note}")
    lines += ["", "// Fast & Furious only: the local car's update (the plugin's key tick) and the reads around it."]
    for name, rva, guard, note in extra_rows:
        lines.append(f"#define {name} 0x{rva:x}u  // {note}")
        if guard is not None:
            lines.append(f"#define {name}_GUARD {c_literal(guard)}")
    lines += ["#endif", ""]
    options.out.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(f"written {options.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
