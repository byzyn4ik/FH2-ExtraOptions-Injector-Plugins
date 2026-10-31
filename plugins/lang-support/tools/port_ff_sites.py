"""Find Language Support's Forza Horizon 2 sites again in Fast & Furious, and check them.

Usage: py tools/port_ff_sites.py --fh2 <FH2 forza_durango_release_final.exe> --ff <F&F one>

Uses Keyboard & Mouse's porter (plugins/keyboard-mouse/tools/port_sites.py: masked
instruction patterns, callers, vtable slots, RTTI) for every site src/voice_subtitles.c
and src/ff_media.c use, then checks each pair further: a function must be the same
instructions from entry to end (.pdata length; rip-relative and rel32 fields masked),
and a vtable must have Forza Horizon 2's type and, slot by slot, the ported functions.
Prints the table src/ff_media.c and src/voice_subtitles.c hold; exit code 1 when a
site differs from it. Needs pefile, capstone and numpy. Both games are only read.
"""
# SPDX-License-Identifier: GPL-3.0-or-later

import argparse
from pathlib import Path
import re
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / "keyboard-mouse" / "tools"))
from port_sites import Image, Porter  # noqa: E402

# name: (FH2 RVA, F&F RVA as the sources hold it, kind)
SITES = {
    "PlaySpeech parameter constructor": (0xF50EE0, 0xF771E0, "function"),
    "PreparePlayVO parameter constructor": (0xF51100, 0xF77400, "function"),
    "PreparePlay3DVO parameter constructor": (0xF512A0, 0xF775A0, "function"),
    "SubtitleDataSource::Update": (0xC3DF10, 0xC6EA40, "function"),
    "DialogueScript parser": (0x85AFA0, 0x891630, "function"),
    "ICinematicSubtitlesDisplay::SetText": (0xC3A4B0, 0xC6A950, "function"),
    "CFileSys::Resolve": (0x11F49B0, 0x121A7A0, "function"),
    "operator new": (0x3229C0, 0x3286D0, "function"),
    "CFileVolume::CFileVolume": (0x11E05A0, 0x1206420, "function"),
    "open flags -> mode": (0x11E1290, 0x1207110, "leaf"),
    "CFileStream::CFileStream": (0x11E04A0, 0x1206320, "function"),
    "CFileSys vtable": (0x2E6ED18, 0x2EB6FF8, "vtable 0x218"),
    "CFileVolume vtable": (0x2EAB918, 0x2EF3C38, "vtable 0x1f0"),
    "CFileStream vtable": (0x2EAB340, 0x2EF3660, "vtable 0x290"),
    "CFileSys::Resolve slot": (0x2E6EF28, 0x2EB7208, "slot"),
}


def q(image, rva):
    return int.from_bytes(image.data[rva:rva + 8], "little") - image.base


def same_function(porter, a, b, leaf_size=0x40):
    fh2, ff = porter.fh2, porter.ff
    span, other = porter.function_of(a), porter.ff_function_of(b)
    if span and other:
        if span[1] - span[0] != other[1] - other[0]:
            return False
        size = span[1] - span[0]
    else:
        size = leaf_size
    instructions = []
    for ins in porter.md.disasm(fh2.data[a:a + size], a):
        instructions.append(ins)
        if not (span and other) and ins.mnemonic == "ret":
            break
    pattern = porter.pattern(instructions)
    return re.match(pattern, ff.data[b:b + size + 16], re.S) is not None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--fh2", type=Path, required=True)
    parser.add_argument("--ff", type=Path, required=True)
    options = parser.parse_args()
    fh2, ff = Image(options.fh2), Image(options.ff)
    porter = Porter(fh2, ff)
    print(f"Fast & Furious: TimeDateStamp 0x{ff.stamp:08x}, SizeOfImage 0x{ff.size:x}")
    bad = 0
    for name, (a, b, kind) in SITES.items():
        if kind.startswith("vtable"):
            size = int(kind.split()[1], 16)
            type_ok = porter.locator(fh2, q(fh2, a - 8)) == porter.locator(ff, q(ff, b - 8))
            # Each slot: the ported function, or (a short function the porter finds several times) the same
            # instructions.
            slots_ok = all(porter.port_code(q(fh2, a + o))[0] == q(ff, b + o) or
                           same_function(porter, q(fh2, a + o), q(ff, b + o)) for o in range(0, size, 8))
            ok, how = type_ok and slots_ok, f"type {porter.locator(ff, q(ff, b - 8))[0].decode()}, {size // 8} slots"
        elif kind == "slot":
            ok = q(fh2, a) == SITES["CFileSys::Resolve"][0] and q(ff, b) == SITES["CFileSys::Resolve"][1]
            how = "holds the ported resolver"
        else:
            found, why, _ = porter.port(a)
            ok = found == b and same_function(porter, a, b)
            how = f"{why}; same instructions" if ok else f"ported to {found and hex(found)} ({why})"
        bad += not ok
        print(f"{'ok  ' if ok else 'FAIL'} {name}: FH2 0x{a:x} -> F&F 0x{b:x} ({how})")
    print("all sites agree" if not bad else f"{bad} site(s) differ")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
