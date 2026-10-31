"""Which struct offsets and immediates moved between Forza Horizon 2 and Fast & Furious.

Usage: py tools/diff_offsets.py --fh2 <FH2 exe> --ff <FF exe> --sites <port_sites report> [--out offsets.txt]

For every code site port_sites.py found, the FH2 function holding it and the
Fast & Furious function holding its match are disassembled (bounds from
.pdata) and aligned instruction by instruction (numbers abstracted). In each
aligned pair the numbers (displacements, immediates; not rip-relative or
branch targets) are compared, giving for each FH2 number the Fast & Furious
numbers seen in its place, with counts and the functions they came from.
The report lists every number that changed somewhere, and for the numbers
asked about (--numbers) whether they were seen unchanged, changed, or not at
all.
"""
# SPDX-License-Identifier: GPL-3.0-or-later

import argparse
from collections import defaultdict, Counter
import difflib
from pathlib import Path
import re
import sys

import capstone
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import port_sites  # noqa: E402

NUMBER = re.compile(r"0x[0-9a-f]+|\b\d+\b")


def functions(image):
    p0, p1 = image.sections[".pdata"]
    raw = np.frombuffer(image.data[p0:p1 - (p1 - p0) % 12], dtype=np.uint32).reshape(-1, 3)
    return raw[raw[:, 0] != 0]


def span(table, rva):
    k = np.searchsorted(table[:, 0], rva, side="right") - 1
    if k < 0 or not (table[k, 0] <= rva < table[k, 1]):
        return None
    return int(table[k, 0]), int(table[k, 1])


class Differ:
    def __init__(self, fh2, ff):
        self.fh2, self.ff = fh2, ff
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        self.t_fh2, self.t_ff = functions(fh2), functions(ff)
        self.seen = defaultdict(Counter)
        self.where = defaultdict(set)

    def body(self, image, begin, end):
        return list(self.md.disasm(image.data[begin:end], begin))

    @staticmethod
    def text(ins):
        s = f"{ins.mnemonic} {ins.op_str}"
        s = re.sub(r"rip [+-] 0x[0-9a-f]+", "rip+D", s)
        if ins.mnemonic.startswith("j") or ins.mnemonic.startswith("call"):
            s = re.sub(r"0x[0-9a-f]+", "T", s)
        return s

    def compare(self, fh2_rva, ff_rva):
        a, b = span(self.t_fh2, fh2_rva), span(self.t_ff, ff_rva)
        if not a or not b:
            return None
        # Align from the sites' functions' starts, same relative position.
        A = self.body(self.fh2, *a)
        B = self.body(self.ff, *b)
        ka = [NUMBER.sub("N", self.text(i)) for i in A]
        kb = [NUMBER.sub("N", self.text(i)) for i in B]
        sm = difflib.SequenceMatcher(a=ka, b=kb, autojunk=False)
        for tag, i1, i2, j1, j2 in sm.get_opcodes():
            if tag != "equal":
                continue
            for x, y in zip(A[i1:i2], B[j1:j2]):
                nx, ny = NUMBER.findall(self.text(x)), NUMBER.findall(self.text(y))
                if len(nx) != len(ny):
                    continue
                for u, v in zip(nx, ny):
                    u, v = int(u, 0), int(v, 0)
                    self.seen[u][v] += 1
                    if u != v:
                        self.where[u].add((x.address, y.address))
        return sm.ratio()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--fh2", type=Path, required=True)
    parser.add_argument("--ff", type=Path, required=True)
    parser.add_argument("--sites", type=Path, required=True, help="port_sites.py --report output")
    parser.add_argument("--numbers", nargs="*", default=[], help="hex numbers to report on")
    parser.add_argument("--out", type=Path)
    options = parser.parse_args()
    fh2, ff = port_sites.Image(options.fh2), port_sites.Image(options.ff)
    differ = Differ(fh2, ff)
    pairs = set()
    for line in options.sites.read_text(encoding="utf-8").splitlines():
        m = re.match(r"(0x[0-9a-f]+) -> (0x[0-9a-f]+)\s+\[code\]", line)
        if m:
            a, b = int(m.group(1), 16), int(m.group(2), 16)
            sa, sb = span(differ.t_fh2, a), span(differ.t_ff, b)
            if sa and sb:
                pairs.add((sa[0], sb[0]))
    lines = []
    for a, b in sorted(pairs):
        ratio = differ.compare(a, b)
        lines.append(f"function {a:#x} -> {b:#x}: similarity {ratio:.3f}")
    lines.append("")
    lines.append("numbers that changed (FH2 -> Fast & Furious {value: count}):")
    for u in sorted(differ.seen):
        c = differ.seen[u]
        if any(v != u for v in c):
            sample = ", ".join(f"{x:#x}->{y:#x}" for x, y in sorted(differ.where[u])[:3])
            lines.append(f"  {u:#x}: {{{', '.join(f'{v:#x}: {n}' for v, n in c.most_common())}}}  e.g. {sample}")
    if options.numbers:
        lines.append("")
        lines.append("asked:")
        for text in options.numbers:
            u = int(text, 16)
            c = differ.seen.get(u)
            if not c:
                lines.append(f"  {u:#x}: not seen")
            elif list(c) == [u]:
                lines.append(f"  {u:#x}: unchanged ({c[u]} times)")
            else:
                lines.append(f"  {u:#x}: {{{', '.join(f'{v:#x}: {n}' for v, n in c.most_common())}}}")
    report = "\n".join(lines) + "\n"
    if options.out:
        options.out.write_text(report, encoding="utf-8")
    else:
        print(report)


if __name__ == "__main__":
    sys.exit(main())
