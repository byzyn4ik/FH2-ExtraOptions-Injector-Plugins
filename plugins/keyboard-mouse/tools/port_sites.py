"""Find Keyboard & Mouse's Forza Horizon 2 sites again in Fast & Furious.

Usage: py tools/port_sites.py --fh2 <FH2 exe> --ff <FF exe> [--out src/ff_sites.inc] [--report report.txt]

Every game RVA in the game-side sources (a hex literal of six digits or more in
the files listed in SOURCES) is looked for in the Fast & Furious executable:

- code (.text): the instructions around the site, with rip-relative
  displacements and rel32 branch/call targets masked (they move between the
  builds), must match exactly once in Fast & Furious' .text (wider windows are
  tried when a narrow one is not unique);
- data (.rdata/.data): the code that references it (rip-relative operands) is
  found again as above and its operand read in Fast & Furious; all ported
  references must agree. A pointer-holding .rdata entry (a vtable slot) is
  also found by its content: the ported function's address, once.
  Run-time type names (.?AV...@@) are found by name.

The output (src/ff_sites.inc) is a sorted C table {FH2 RVA, Fast & Furious RVA,
mask offset, mask length} plus the masks: for a code site, which of the 256
bytes from the site hold a relocated field (rip displacement or rel32), so a
guard can compare the FH2 bytes with Fast & Furious' everywhere else. A site
that is not found is left out (the plugin then refuses what needs it and says
so in the log). Needs pefile, capstone and numpy.
"""
# SPDX-License-Identifier: GPL-3.0-or-later

import argparse
from collections import Counter
from pathlib import Path
import re
import struct
import sys

import capstone
from capstone import x86
import numpy as np
import pefile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
SOURCES = ["kbm_input.c", "settings_page.c", "kbm_menu.c", "kbm_pitch.c", "controller_pause.c", "plugin.c",
           "common.h", "kbm_text.c"]
# Hex literals in those files that are not game addresses.
NOT_SITES = {0x100000, 0x1d00000, 0x3a18000, 0xffffff}
FAMILIES = (((0, 32), (0, 48), (0, 80), (0, 128), (0, 192), (0, 256)),
            ((24, 40), (48, 64), (96, 96), (160, 160)),
            ((48, 8), (96, 8), (160, 8), (256, 8)))
MASK_SPAN = 256
LITERAL = re.compile(r"\b0x([0-9a-fA-F]{6,8})u?\b")
DEFINE = re.compile(r"^\s*#define\s+(\w+)\s+0x([0-9a-fA-F]+)u?\b")


# Sites found by hand (the reason says how); used when the search finds nothing.
MANUAL = {
    # World prompt handler (FH2 0x78b6e0, F&F 0x78f290): the four action queries' return addresses.
    0x78b740: (0x78f2f0, "the world prompt handler's queries (0x78f2f6..0x78f336), by hand"),
    0x78b790: (0x78f340, "end of the world prompt handler's queries, by hand"),
    # WaitForPause's controller check (cmp byte [rbx+4],0; jne; lea rdx,[rdi+0x28]; mov rcx,rdi; call onPause).
    0x9648ed: (0x9a86f8, "the only copy of the check's 13 bytes; its call (0x9a8705) goes to the ported onPause"),
    # End of the impulse query function (F&F 0x10954c0..0x109563e in .pdata).
    0x106f3ca: (0x109563e, "end of the ported function 0x10954c0 (.pdata)"),
    # VideoOptions' type set-up (FH2 0xeca710..0xeca8c8, F&F 0xee4ca0..0xee4e58, the same 0x1b8 bytes; the
    # only caller of the int template 0xef8ac0, whose lea names "Subtitles"): its first property's
    # registration and the two property templates the page copies.
    0xeca710: (0xee4ca0, "VideoOptions type set-up: calls the Subtitles template 0xef8ac0, by hand"),
    0xeca808: (0xee4d98, "its lea rcx, [registry] before the first call 0x1586070, by hand"),
    0xeca80f: (0xee4d9f, "its first call 0x1586070 (register property), by hand"),
    0x35470d0: (0x35adf70, "the registry its lea at 0xee4d98 names, by hand"),
    0xed825b: (0xef8b2b, "the int template's lea of \"Subtitles\", by hand"),
    0xed77d0: (0xef7aa0, "the third template the type set-up calls (0xee4df0), by hand"),
    0xed782e: (0xef7afe, "the list template's lea of \"OnOffOptions\", by hand"),
    # MyStats' Next page handler: slot +0x10 of the lambda vtable its init (0xde13a0) stores at 0xde15xx.
    0xd96200: (0xdf8130, "slot +0x10 of the std::function vtable the ported MyStats init stores (lea at FH2 0xd81b03)"),
}


class Image:
    def __init__(self, path):
        pe = pefile.PE(str(path), fast_load=True)
        self.base = pe.OPTIONAL_HEADER.ImageBase
        self.size = pe.OPTIONAL_HEADER.SizeOfImage
        self.stamp = pe.FILE_HEADER.TimeDateStamp
        self.data = bytes(pe.get_memory_mapped_image())
        self.sections = {s.Name.rstrip(b"\0").decode(): (s.VirtualAddress, s.VirtualAddress + s.Misc_VirtualSize)
                         for s in pe.sections}
        self.text = self.sections[".text"]

    def section(self, rva):
        for name, (start, end) in self.sections.items():
            if start <= rva < end:
                return name
        return None


COMMENT = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"', re.S)


def strip_comments(text):
    """Comments and string literals blanked (newlines kept, so line numbers stay)."""
    return COMMENT.sub(lambda m: re.sub(r"[^\n]", " ", m.group(0)), text)


def inventory(src):
    """{rva: [where, ...]} for every game RVA literal in the sources' code (not comments or strings)."""
    found = {}
    for name in SOURCES:
        for number, line in enumerate(strip_comments((src / name).read_text(encoding="utf-8")).splitlines(), 1):
            define = DEFINE.match(line)
            for match in LITERAL.finditer(line):
                rva = int(match.group(1), 16)
                if rva in NOT_SITES:
                    continue
                label = define.group(1) if define and define.group(2).lower() == match.group(1).lower() else \
                    f"{name}:{number}"
                found.setdefault(rva, []).append(label)
    return found


class Porter:
    def __init__(self, fh2, ff):
        self.fh2, self.ff = fh2, ff
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        self.md.detail = True
        t0, t1 = ff.text
        self.ff_text = ff.data[t0:t1]
        self.code_cache = {}
        self.xref_index = None

    # ---------- code ----------
    def around(self, image, rva, before, after):
        for k in range(before, before + 16):
            start = rva - k
            found = list(self.md.disasm(image.data[start:rva + after], start))
            if rva in [i.address for i in found]:
                return found
        return None

    @staticmethod
    def relocated(ins):
        """Byte positions (within the instruction) that move between builds."""
        keep = []
        if ins.disp_size and any(op.type == x86.X86_OP_MEM and op.mem.base == x86.X86_REG_RIP for op in ins.operands):
            keep += range(ins.disp_offset, ins.disp_offset + ins.disp_size)
        if (ins.group(capstone.CS_GRP_JUMP) or ins.group(capstone.CS_GRP_CALL)) and ins.imm_size >= 4:
            keep += range(ins.imm_offset, ins.imm_offset + ins.imm_size)
        return keep

    def pattern(self, instructions):
        parts = []
        for ins in instructions:
            moved = set(self.relocated(ins))
            parts += [b"." if k in moved else re.escape(bytes([b])) for k, b in enumerate(ins.bytes)]
        return b"".join(parts)

    def port_code(self, rva, depth=0):
        if rva in self.code_cache:
            return self.code_cache[rva]
        self.code_cache[rva] = (None, "being ported")  # no recursion loops
        found, why = self.port_code_pattern(rva, self.ff.text[0], self.ff_text)
        if found is not None and not self.consistent(rva, found):
            why = f"{why}, but in another function than its function's match (rejected)"
            found = None
        if found is not None and depth < 2:
            # A function entry's code can repeat (a short function and its neighbour): where its
            # callers or its vtable slots say otherwise, they decide.
            span = self.function_of(rva)
            leaf = span is None and self.fh2.data[rva - 1] in (0xCC, 0xC3)  # no .pdata: a leaf function
            if (span and span[0] == rva) or leaf:
                for check in (self.port_by_callers, self.port_by_slots):
                    other, how = check(rva, depth + 1)
                    if other is not None and other != found:
                        why = f"{how} (its code matched 0x{found:x} instead)"
                        found = other
                        break
                    if other is not None:
                        why += f"; {how}"
                        break
        if found is None and depth < 2:
            for fallback in (self.port_by_callers, self.port_by_slots, self.port_by_code_xrefs, self.port_in_function):
                f2, w2 = fallback(rva, depth + 1)
                if f2 is not None and not self.consistent(rva, f2):
                    w2 += " (rejected: another function than its function's match)"
                    f2 = None
                if f2 is not None:
                    found, why = f2, w2
                    break
                why += f"; {w2}"
        self.code_cache[rva] = (found, why)
        return found, why

    def ff_function_of(self, rva):
        if not hasattr(self, "_ff_pdata"):
            p0, p1 = self.ff.sections[".pdata"]
            raw = np.frombuffer(self.ff.data[p0:p1 - (p1 - p0) % 12], dtype=np.uint32).reshape(-1, 3)
            self._ff_pdata = raw[raw[:, 0] != 0]
        k = np.searchsorted(self._ff_pdata[:, 0], rva, side="right") - 1
        if k < 0 or not (self._ff_pdata[k, 0] <= rva < self._ff_pdata[k, 1]):
            return None
        return int(self._ff_pdata[k, 0]), int(self._ff_pdata[k, 1])

    def verified(self, rva, found):
        """How a code site's match was cross-checked: 'entry' (a function entry matching a
        function entry whose code continues the same), 'function' (inside the match of its
        function's entry), or 'unchecked'."""
        a, b = self.function_of(rva), self.ff_function_of(found)
        if not a or not b:
            return "unchecked"
        if rva == a[0]:
            return "entry" if found == b[0] else "unchecked"
        entry, _ = self.port_code_pattern(a[0], self.ff.text[0], self.ff_text)
        return "function" if entry == b[0] else "unchecked"

    def consistent(self, rva, found):
        """A function entry matches a function entry; a site inside a function lies in
        the match of that function's entry (when that entry is found by its own code)."""
        a, b = self.function_of(rva), self.ff_function_of(found)
        if not a or not b:
            return True
        if rva == a[0]:
            return found == b[0]
        entry, _ = self.port_code_pattern(a[0], self.ff.text[0], self.ff_text, forward_only=True)
        if entry is None:
            entry, _ = self.port_code_pattern(a[0], self.ff.text[0], self.ff_text)
        return entry is None or entry == b[0]

    def port_code_pattern(self, rva, base, haystack, forward_only=False):
        notes = []
        for family in FAMILIES[:1] if forward_only else FAMILIES:
            for before, after in family:
                instructions = self.around(self.fh2, rva, before, after)
                if not instructions:
                    notes.append(f"{before}/{after}: no instruction start")
                    break
                hits = [m.start() for m in re.finditer(self.pattern(instructions), haystack, re.DOTALL)]
                if len(hits) == 1:
                    return base + hits[0] + rva - instructions[0].address, f"code, unique with {before}/{after} bytes"
                notes.append(f"{len(hits)} with {before}/{after}")
                if not hits:
                    break
        return None, "code, " + "; ".join(notes)

    def port_by_callers(self, rva, depth):
        """A function: its direct callers (call/jmp rel32) found again, their FF targets agree."""
        if self.xref_index is None:
            self.build_xrefs()
        t0 = self.fh2.text[0]
        votes = Counter()
        for p in np.nonzero(self.xref_sum == rva - 4)[0][:40]:
            at = int(p) + t0 - 1
            if self.fh2.data[at] not in (0xE8, 0xE9):
                continue
            ff_at, _ = self.port_code_pattern(at, self.ff.text[0], self.ff_text)
            if ff_at is None or self.ff.data[ff_at] != self.fh2.data[at]:
                continue
            votes[ff_at + 5 + int.from_bytes(self.ff.data[ff_at + 1:ff_at + 5], "little", signed=True)] += 1
        if votes:
            target, count = votes.most_common(1)[0]
            others = sum(votes.values()) - count
            if len(votes) == 1 or (count >= 3 and count >= 4 * others):
                return target, f"code, {count} caller(s) agree" + (f", {others} other(s)" if others else "")
        return None, f"callers: {len(votes)} targets"

    def port_by_code_xrefs(self, rva, depth):
        """A function whose address the code takes (lea reg, [rip+fn])."""
        return self.port_by_xrefs(rva)

    def port_by_slots(self, rva, depth):
        """A virtual function: its vtable slot (vtable found by RTTI) read in FF."""
        needle = (rva + self.fh2.base).to_bytes(8, "little")
        r0, r1 = self.fh2.sections[".rdata"][0], self.fh2.sections[".data"][1]
        votes = Counter()
        for m in re.finditer(re.escape(needle), self.fh2.data[r0:r1]):
            slot = r0 + m.start()
            if slot % 8:
                continue
            for back in range(0, 0x1000, 8):
                start = slot - back
                found, _ = self.port_rtti(start)
                if found is not None:
                    value = int.from_bytes(self.ff.data[found + back:found + back + 8], "little") - self.ff.base
                    if self.ff.text[0] <= value < self.ff.text[1]:
                        votes[value] += 1
                    break
                pointer = int.from_bytes(self.fh2.data[start - 8:start], "little") - self.fh2.base
                if 0 < pointer < self.fh2.size and self.locator(self.fh2, pointer):
                    # A vtable whose type is not found by name (a lambda's): the code that
                    # stores it is found again instead.
                    found, _ = self.port_by_xrefs(start)
                    if found is not None:
                        value = int.from_bytes(self.ff.data[found + back:found + back + 8], "little") - self.ff.base
                        if self.ff.text[0] <= value < self.ff.text[1]:
                            votes[value] += 1
                    break
            if len(votes) > 1 or sum(votes.values()) >= 4:
                break
        if len(votes) == 1:
            target, count = votes.most_common(1)[0]
            return target, f"code, {count} vtable slot(s) agree"
        return None, f"slots: {len(votes)} targets"

    def function_of(self, rva):
        """(begin, end) of the FH2 function holding rva, from .pdata."""
        if not hasattr(self, "_pdata"):
            p0, p1 = self.fh2.sections[".pdata"]
            raw = np.frombuffer(self.fh2.data[p0:p1 - (p1 - p0) % 12], dtype=np.uint32).reshape(-1, 3)
            self._pdata = raw[raw[:, 0] != 0]
        k = np.searchsorted(self._pdata[:, 0], rva, side="right") - 1
        if k < 0 or not (self._pdata[k, 0] <= rva < self._pdata[k, 1]):
            return None
        return int(self._pdata[k, 0]), int(self._pdata[k, 1])

    def port_in_function(self, rva, depth):
        """A site inside a function: the function found, then the site within it."""
        span = self.function_of(rva)
        if not span or span[0] == rva:
            return None, "no containing function"
        begin, end = span
        ff_begin, why = self.port_code(begin, depth)
        if ff_begin is None:
            return None, f"function 0x{begin:x}: {why}"
        length = max(end - begin, 0x40)
        lo, hi = ff_begin, min(ff_begin + length * 2 + 0x100, self.ff.text[1])
        found, why = self.port_code_pattern(rva, lo, self.ff.data[lo:hi])
        if found is not None:
            return found, f"{why} inside function 0x{begin:x} -> 0x{ff_begin:x}"
        if rva - begin < 0x20 and self.fh2.data[begin:rva] == self.ff.data[ff_begin:ff_begin + rva - begin]:
            return ff_begin + rva - begin, f"function 0x{begin:x} -> 0x{ff_begin:x} + same bytes"
        return None, f"not inside function 0x{begin:x} -> 0x{ff_begin:x} ({why})"

    def mask(self, rva):
        """Relocated byte offsets in the MASK_SPAN bytes from a code site."""
        out = set()
        for ins in self.md.disasm(self.fh2.data[rva:rva + MASK_SPAN + 16], rva):
            if ins.address >= rva + MASK_SPAN:
                break
            for k in self.relocated(ins):
                if ins.address + k - rva < MASK_SPAN:
                    out.add(ins.address + k - rva)
        return sorted(out)

    # ---------- data ----------
    def build_xrefs(self):
        """disp32 + position for every byte of FH2's .text (a rip-relative target is that + 4 + tail)."""
        t0, t1 = self.fh2.text
        text = np.frombuffer(self.fh2.data[t0:t1 + 4], dtype=np.uint8)
        n = t1 - t0
        d = (text[0:n].astype(np.int64) | (text[1:n + 1].astype(np.int64) << 8) |
             (text[2:n + 2].astype(np.int64) << 16) | (text[3:n + 3].astype(np.int64) << 24))
        d -= (d >= (1 << 31)) * (1 << 32)
        d += np.arange(n, dtype=np.int64) + t0
        self.xref_sum = d
        self.xref_index = {}

    def xrefs(self, rva):
        if self.xref_index is None:
            self.build_xrefs()
        if rva in self.xref_index:
            return self.xref_index[rva]
        t0 = self.fh2.text[0]
        out = []
        for tail in (0, 1, 2, 4):
            for p in np.nonzero(self.xref_sum == rva - 4 - tail)[0][:400]:
                at = int(p) + t0
                # The instruction holding the displacement at `at`, `tail` bytes before its end.
                for start in range(at - 7, at):
                    ins = next(self.md.disasm(self.fh2.data[start:start + 16], start), None)
                    if ins and ins.disp_size == 4 and start + ins.disp_offset == at and \
                            ins.address + ins.size == at + 4 + tail and \
                            any(op.type == x86.X86_OP_MEM and op.mem.base == x86.X86_REG_RIP for op in ins.operands):
                        out.append((start, ins))
                        break
        self.xref_index[rva] = out
        return out

    def port_by_xrefs(self, rva, limit=12):
        votes = Counter()
        tried = 0
        for start, ins in self.xrefs(rva)[:limit * 4]:
            if tried >= limit:
                break
            ff_at, _ = self.port_code(start)
            tried += 1
            if ff_at is None:
                continue
            ff_ins = next(self.md.disasm(self.ff.data[ff_at:ff_at + 16], ff_at), None)
            if not ff_ins or ff_ins.size != ins.size or ff_ins.mnemonic != ins.mnemonic:
                continue
            d = int.from_bytes(self.ff.data[ff_at + ins.disp_offset:ff_at + ins.disp_offset + 4], "little", signed=True)
            votes[ff_at + ins.size + d] += 1
        if not votes:
            return None, f"data, {len(self.xrefs(rva))} references, none ported"
        best, count = votes.most_common(1)[0]
        others = sum(votes.values()) - count
        if len(votes) > 1 and count >= 3 and count >= 4 * others:
            return best, f"data, {count} references agree, {others} other(s)"
        if len(votes) > 1:
            return None, f"data, references disagree {dict((hex(k), v) for k, v in votes.items())}"
        return best, f"data, {count} reference(s) agree"

    def port_pointer_slot(self, rva):
        """An .rdata entry holding a code pointer (vtable slot): found by its ported content."""
        value = int.from_bytes(self.fh2.data[rva:rva + 8], "little")
        target = value - self.fh2.base
        if not (self.fh2.text[0] <= target < self.fh2.text[1]):
            return None, "not a code pointer"
        ff_target, why = self.port_code(target)
        if ff_target is None:
            return None, f"slot content 0x{target:x}: {why}"
        needle = (ff_target + self.ff.base).to_bytes(8, "little")
        r0, r1 = self.ff.sections[".rdata"][0], self.ff.sections[".data"][1]
        hits = [m.start() for m in re.finditer(re.escape(needle), self.ff.data[r0:r1])]
        hits = [r0 + h for h in hits if h % 8 == 0]
        if len(hits) == 1:
            return hits[0], f"slot, content 0x{target:x} -> 0x{ff_target:x} once"
        # Several vtables share the function: the neighbours decide.
        good = []
        for h in hits:
            ok = True
            for delta in (-8, 8, -16, 16):
                v = int.from_bytes(self.fh2.data[rva + delta:rva + delta + 8], "little") - self.fh2.base
                if not (self.fh2.text[0] <= v < self.fh2.text[1]):
                    continue
                fv, _ = self.port_code(v)
                w = int.from_bytes(self.ff.data[h + delta:h + delta + 8], "little") - self.ff.base
                if fv is None or fv != w:
                    ok = False
                    break
            if ok:
                good.append(h)
        if len(good) == 1:
            return good[0], f"slot, content 0x{target:x} -> 0x{ff_target:x}, neighbours agree"
        return None, f"slot, content found {len(hits)} times"

    def port_type_name(self, rva):
        name = self.fh2.data[rva + 0x10:rva + 0x10 + 256].split(b"\0")[0]
        if not name.startswith(b".?A"):
            return None, "not a type descriptor"
        hits = [m.start() for m in re.finditer(re.escape(b"\0" + name + b"\0"), self.ff.data)]
        if len(hits) == 1:
            return hits[0] + 1 - 0x10, f"type descriptor {name.decode()}"
        return None, f"type descriptor {name.decode()} found {len(hits)} times"

    def locator(self, image, rva):
        """(type name, offset, cd offset) of the RTTI complete object locator at rva, or None."""
        sig, offset, cd, td, _, self_rva = struct.unpack_from("<IIIIII", image.data, rva)
        if sig != 1 or self_rva != rva or not (0 < td < image.size):
            return None
        name = image.data[td + 0x10:td + 0x10 + 256].split(b"\0")[0]
        return (name, offset, cd) if name.startswith(b".?A") else None

    def ff_locators(self):
        if not hasattr(self, "_ff_cols"):
            self._ff_cols = {}
            r0, r1 = self.ff.sections[".rdata"]
            data = self.ff.data
            for at in range(r0, r1 - 24, 4):
                if data[at] == 1 and data[at + 1:at + 4] == b"\0\0\0" and \
                        int.from_bytes(data[at + 20:at + 24], "little") == at:
                    key = self.locator(self.ff, at)
                    if key:
                        self._ff_cols.setdefault(key, []).append(at)
        return self._ff_cols

    def port_rtti(self, rva):
        """A vtable (its RTTI locator at -8) or a locator, found by type name and offsets."""
        key = self.locator(self.fh2, rva)
        if key:
            hits = self.ff_locators().get(key, [])
            return (hits[0], f"RTTI locator {key[0].decode()}+{key[1]:#x}") if len(hits) == 1 else                 (None, f"RTTI locator {key[0].decode()} found {len(hits)} times")
        pointer = int.from_bytes(self.fh2.data[rva - 8:rva], "little") - self.fh2.base
        if not (0 < pointer < self.fh2.size):
            return None, "no RTTI"
        key = self.locator(self.fh2, pointer)
        if not key:
            return None, "no RTTI"
        hits = self.ff_locators().get(key, [])
        if len(hits) != 1:
            return None, f"vtable of {key[0].decode()}: locator found {len(hits)} times"
        needle = (hits[0] + self.ff.base).to_bytes(8, "little")
        r0, r1 = self.ff.sections[".rdata"][0], self.ff.sections[".data"][1]
        spots = [r0 + m.start() + 8 for m in re.finditer(re.escape(needle), self.ff.data[r0:r1]) if m.start() % 8 == 0]
        if len(spots) == 1:
            return spots[0], f"vtable of {key[0].decode()}+{key[1]:#x} (RTTI)"
        return None, f"vtable of {key[0].decode()}: {len(spots)} vtables"

    def port(self, rva):
        if rva in MANUAL:
            return MANUAL[rva][0], MANUAL[rva][1], "code" if self.fh2.section(rva) == ".text" else "data"
        return self.port_auto(rva)

    def port_auto(self, rva):
        section = self.fh2.section(rva)
        if section in (".rdata", ".data"):
            found, why = self.port_rtti(rva)
            if found is not None:
                return found, why, "data"
        if section == ".text":
            return self.port_code(rva) + ("code",)
        if section in (".rdata", ".data"):
            if section == ".data":
                found, why = self.port_type_name(rva)
                if found is not None:
                    return found, why, "data"
            if True:
                found, why = self.port_pointer_slot(rva)
                if found is not None:
                    return found, why, "data"
            found, why2 = self.port_by_xrefs(rva)
            if found is not None:
                return found, why2, "data"
            # A vtable slot with no reference of its own: its vtable's start + offset.
            for back in range(8, 0x800, 8):
                start = rva - back
                if self.xrefs(start):
                    base, why3 = self.port_by_xrefs(start)
                    if base is not None:
                        return base + back, f"{why3} (object start 0x{start:x} + 0x{back:x})", "data"
                    break
            return None, f"{why2}", "data"
        return None, f"section {section}", "other"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--fh2", type=Path, required=True)
    parser.add_argument("--ff", type=Path, required=True)
    parser.add_argument("--src", type=Path, default=ROOT / "src")
    parser.add_argument("--out", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--only", nargs="*", default=None, help="hex RVAs to try (debugging)")
    options = parser.parse_args()
    fh2, ff = Image(options.fh2), Image(options.ff)
    porter = Porter(fh2, ff)
    sites = inventory(options.src)
    if options.only:
        sites = {int(x, 16): sites.get(int(x, 16), ["?"]) for x in options.only}
    rows, lines = [], []
    for rva in sorted(sites):
        found, why, kind = porter.port(rva)
        names = ", ".join(sorted(set(sites[rva]))[:3])
        check = porter.verified(rva, found) if found is not None and kind == "code" else ""
        lines.append(f"{rva:#09x} -> {('%#09x' % found) if found is not None else 'NOT FOUND'}  [{kind}{' ' + check if check else ''}] "
                     f"{why}  ({names})")
        print(lines[-1], flush=True)
        if found is not None:
            rows.append((rva, found, porter.mask(rva) if kind == "code" else []))
    missing = sum(1 for l in lines if "NOT FOUND" in l)
    summary = f"{len(lines)} sites, {len(lines) - missing} found, {missing} not found"
    print(summary)
    if options.report:
        options.report.write_text("\n".join(lines + [summary]) + "\n", encoding="utf-8")
    if options.out:
        out = ["/* Generated by tools/port_sites.py from Forza Horizon 2 1.0.0.64 (TimeDateStamp 0x%08x) and Fast & "
               "Furious (TimeDateStamp 0x%08x, SizeOfImage 0x%x). Do not edit. */" % (fh2.stamp, ff.stamp, ff.size),
               "/* {FH2 RVA, Fast & Furious RVA, first mask byte, mask bytes} */"]
        masks, table = [], []
        for rva, found, mask in rows:
            table.append(f"    {{0x{rva:07x}u, 0x{found:07x}u, {len(masks)}u, {len(mask)}u}},")
            masks += mask
        out.append("static const KbmSitePair ff_site_pairs[] = {")
        out += table
        out.append("};")
        out.append("/* Byte offsets from a code site that hold a relocated field (rip displacement, rel32). */")
        out.append("static const unsigned char ff_site_masks[] = {")
        for k in range(0, len(masks), 24):
            out.append("    " + ", ".join(str(m) for m in masks[k:k + 24]) + ",")
        if not masks:
            out.append("    0,")
        out.append("};")
        options.out.write_text("\n".join(out) + "\n", encoding="utf-8", newline="\n")


if __name__ == "__main__":
    sys.exit(main())
