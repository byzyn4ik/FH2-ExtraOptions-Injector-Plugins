"""Port an Xtra Options byte-patch feature from Forza Horizon 2 to Fast & Furious.

Usage: py tools/port_feature.py <xtra-options feature folder> <output feature folder> --fh2 <FH2 exe> --ff <FF exe>

Each FH2 site is found again in Fast & Furious' .text by the code around it:
the instructions from about 24 bytes before to 40 after the site (wider when
that is not unique), with rip-relative displacements and rel32 branch/call
targets masked (they move between the builds). A site is ported only when the
pattern matches exactly once and the bytes at the Fast & Furious site are the
FH2 site's original bytes; otherwise the feature is not written. Needs pefile
and capstone (py -m pip install pefile capstone).
"""
# SPDX-License-Identifier: GPL-3.0-or-later

import argparse
import json
from pathlib import Path
import re
import sys

import capstone
from capstone import x86
import pefile

WINDOWS = ((24, 40), (48, 64), (96, 96), (160, 160))


def load(path):
    pe = pefile.PE(str(path), fast_load=True)
    image = bytes(pe.get_memory_mapped_image())
    text = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
    return image, text.VirtualAddress, text.VirtualAddress + text.Misc_VirtualSize


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source", type=Path)
    parser.add_argument("target", type=Path)
    parser.add_argument("--fh2", type=Path, required=True)
    parser.add_argument("--ff", type=Path, required=True)
    options = parser.parse_args()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    fh2, _, _ = load(options.fh2)
    ff, f0, f1 = load(options.ff)
    ff_text = ff[f0:f1]

    def around(rva, before, after):
        for k in range(before, before + 16):
            found = list(md.disasm(fh2[rva - k:rva + after], rva - k))
            if rva in [i.address for i in found]:
                return found
        return None

    def pattern(instructions):
        parts = []
        for ins in instructions:
            keep = [True] * len(ins.bytes)
            if ins.disp_size and any(op.type == x86.X86_OP_MEM and op.mem.base == x86.X86_REG_RIP
                                     for op in ins.operands):
                keep[ins.disp_offset:ins.disp_offset + ins.disp_size] = [False] * ins.disp_size
            if (ins.group(capstone.CS_GRP_JUMP) or ins.group(capstone.CS_GRP_CALL)) and ins.imm_size >= 4:
                keep[ins.imm_offset:ins.imm_offset + ins.imm_size] = [False] * ins.imm_size
            parts += [re.escape(bytes([b])) if k else b"." for b, k in zip(ins.bytes, keep)]
        return b"".join(parts)

    def find(rva):
        for before, after in WINDOWS:
            instructions = around(rva, before, after)
            if not instructions:
                return None, "no instruction starts at the site"
            hits = [m.start() for m in re.finditer(pattern(instructions), ff_text, re.DOTALL)]
            if len(hits) == 1:
                return f0 + hits[0] + rva - instructions[0].address, f"unique with {before}/{after} bytes"
        return None, f"{len(hits)} matches"

    feature = json.loads((options.source / "feature.json").read_text(encoding="utf-8"))
    patches = json.loads((options.source / "patches.json").read_text(encoding="utf-8"))
    ported = []
    for patch in patches:
        rva, why = find(patch["rva"])
        original = bytes.fromhex(patch["original"])
        if rva is None or ff[rva:rva + len(original)] != original:
            sys.exit(f"{patch['id']}: not ported ({why if rva is None else 'other bytes at ' + hex(rva)})")
        print(f"  {patch['id']}: FH2 {patch['rva']:#x} -> Fast & Furious {rva:#x} ({why})")
        ported.append(dict(patch, rva=rva, fh2_rva=patch["rva"]))
    feature = dict(feature, game="fh2ff",
                   source=f"ported from xtra-options/features/{options.source.name} by tools/port_feature.py "
                          f"(FH2 1.0.0.64 -> Fast & Furious 1.0.0.6; same bytes at every site)")
    options.target.mkdir(parents=True, exist_ok=True)
    (options.target / "feature.json").write_text(json.dumps(feature, indent=2) + "\n", encoding="utf-8")
    (options.target / "patches.json").write_text(json.dumps(ported, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
