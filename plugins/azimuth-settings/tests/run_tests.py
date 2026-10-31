"""Offline tests of the Azimuth Settings plugin (no game run, nothing installed).

Usage: py tests/run_tests.py [--output out] [--zig zig.exe] [--game <FH2 folder>] [--ff-game <F&F install>]
                             [--azimuth <azimuth-windows-1.0.4.zip>] [--azimuth102 <azimuth-windows-1.0.2.zip>]
                             [--eo-release <Extra Options 0.4.4 release folder>] [--eo-source <its source tree>]
                             [--upstream <Extra Options git clone>]

Real files are only read (the games' executables and UI.zip, Azimuth's ZIPs,
Extra Options' release and sources; the azimuth.ini files are copied to a
private folder). Defaults below --game (default ~/FH2): the FH2 package
Anthem_1.0.0.64_x64__8wekyb3d8bbwe (else D:/Games/Forza Horizon 2/game/base),
data/azimuth.ini, azimuth-windows-1.0.4.zip, azimuth-windows-1.0.2.zip,
src/eo-pr/upstream-0.4.4/FH2_Extra_Options_0.4.4-azimuth/Azimuth (the release:
winmm.dll), src/eo-pr/wt-upstream (its source, read only),
src/eo-pr/ForzaHorizon2_ExtraOptions (a clone with origin/main = 0.3.2 and the
fork's combined-10). --ff-game (default D:/Games/Forza Horizon 2 Presents Fast
& Furious): game/base/Mount (its executable and media/UI.zip) and
data/azimuth.ini (UTF-16). A missing input skips the tests that need it;
capstone (optional) adds the masked code comparisons.

1. package  mod.json as Extra Options validates it, the plugin layout, the
            DLL (PE32+ x64, exports, only Windows imports), the INI.
2. host     Azimuth 1.0.4's azimuth.exe (and 1.0.2's, and the branded "Forza
            Horizon 2.exe") pass the host guard; every site is checked again
            here from the executable's bytes; every INI key and value the rows
            write is one Azimuth reads, with its defaults and its detail
            tables; both titles' own frame rate is 30; other builds are refused.
3. game     Forza Horizon 2 and Fast & Furious pass the native guards; every
            Fast & Furious site is found again here from the executable (its
            property names, the Gamma getter, the code around each site,
            keyboard-mouse's ff_sites.inc, Azimuth's shadow patch records).
4. eo       Extra Options 0.4.4: the release's winmm.dll holds every XAML text
            of its source's settings pages code and its patch of the resource
            call (to the thunk 0x13fe354); that code is built to extend the
            real pages.
5. xaml     the real Video and Audio pages of both games (UI.zip), alone and
            (FH2) as Extra Options 0.4.4 (and 0.3.2, the fork) extend them,
            then with this plugin's rows: well-formed XML, rows, order, list
            style, captions in English, Polish and Ukrainian.
6. rows     copies of the games' data/azimuth.ini: keys written, Azimuth's
            defaults, presets and details, languages, other lines kept, a
            UTF-16 file kept UTF-16; the caption language.
7. plugin   out/tests/plugin_test.exe drives FH2PluginInitialize and both pages
            (modes nohost, disabled, other, badhost, badgame, unknown, layout,
            native, eo, polish, ukrainian on 1.0.4, native on 1.0.2; F&F:
            native, ukrainian, badgame, layout, native on 1.0.2).
"""

import argparse
import configparser
import ctypes
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
REPO = ROOT.parents[1]
sys.path.insert(0, str(REPO / "common"))
import language_testing as lt  # noqa: E402

try:
    import capstone
    from capstone import x86
except ImportError:  # the masked code comparisons are skipped
    capstone = None

MOD_NAME = "Azimuth Settings"
DLL_NAME = "AzimuthSettings.dll"
INI_NAME = "AzimuthSettings.ini"
SYSTEM_DLLS = re.compile(r"^(kernel32|user32|advapi32|shell32|ole32|ntdll|msvcrt|api-ms-win-[a-z0-9-]+)\.dll$", re.I)
FH2_PACKAGE = Path("Anthem_1.0.0.64_x64__8wekyb3d8bbwe/Mount")
FH2_INSTALL = Path("D:/Games/Forza Horizon 2/game/base/Mount")
FF_INSTALL = Path("D:/Games/Forza Horizon 2 Presents Fast & Furious")
EXE = "forza_durango_release_final.exe"

VIDEO_ROWS = ["AzsFrameRate", "AzsVSync", "AzsResolution", "AzsAspect", "AzsDisplay", "AzsHdr", "AzsPreset",
              "AzsShadowDetail", "AzsShadowDistance", "AzsShadowRedraw", "AzsReflections", "AzsSceneryDetail",
              "AzsCarDetail", "AzsParticles", "AzsMotionBlur", "AzsTextureFiltering", "AzsAntiAliasing"]
AUDIO_ROWS = ["AzsAudioOutput", "AzsVolume", "AzsSpeech", "AzsLanguage"]
ROWS = VIDEO_ROWS + AUDIO_ROWS
R = {name[3:]: i for i, name in enumerate(ROWS)}  # R["FrameRate"] == 0
DETAILS = ["shadow_detail", "shadow_distance", "shadow_redraw", "reflections", "scenery_detail", "car_detail",
           "particles", "motion_blur", "texture_filtering"]
KEYS = ["frame_rate", "vsync", "resolution", "aspect", "display", "hdr", "graphics"] + DETAILS + \
       ["antialiasing", "audio", "volume", "speech", "language"]
LANGUAGES = ["en-US", "en-GB", "fr-FR", "de-DE", "it-IT", "es-ES", "es-MX", "pl-PL", "pt-BR", "ru-RU", "ja-JP", "zh-TW"]
VIDEO, AUDIO_PAGE = 0, 1
EN, PL, UK = 0, 1, 2
NATIVES = ["DeleteObject", "CollectionType", "AllocateTyped", "CollectionInit", "MakeString", "InsertItem",
           "RegisterVideoType", "RegisterAudioType", "IntProperty", "FloatProperty", "CollectionProperty",
           "RegisterProperty", "Allocate", "Free", "ObjectInit", "ObjectCleanup", "BoxInt", "BoxFloat",
           "PropertyChanged", "GetResource"]
N = {name: i for i, name in enumerate(NATIVES)}

# Azimuth builds: the sites the plugin uses (live.h), checked below from the bytes.
HOSTS = {
    "1.0.4": dict(timestamp=0x6ae393eb, image=0x8a2000, rate_list=0x10528, set_rates=0x4ba30, set_call=0x945f,
                  target=0x7d8554, title=0x7d8550, reader=0x4e8e7, toggle=0x6e868, fullscreen=0x7fcbc1,
                  alt_enter=0x6eda7, volume_setter=0xc37f0, volume_call=0x938b, volume=0x7da2e0),
    "1.0.2": dict(timestamp=0x6ae05dd1, image=0x9af000, rate_list=0x105f8, set_rates=0x4cc50, set_call=0x92f3,
                  target=0x8e7540, title=0x8e7544, reader=0x4fae7, toggle=0x6f5f8, fullscreen=0x909d61,
                  alt_enter=0x6fb27, volume_setter=0xbaea0, volume_call=0x921f, volume=0x8e7be8),
}
# The game settings keys Azimuth 1.x reads under [title-...] (its key table, the same in 1.0.2 and 1.0.4).
AZIMUTH_KEYS = ["aspect", "antialiasing", "resolution", "shadow_distance", "shadow_redraw", "graphics",
                "shadow_detail", "car_detail", "particles", "reflections", "scenery_detail", "audio", "volume",
                "motion_blur", "texture_filtering", "hdr", "language", "frame_rate", "vsync", "speech"]
TITLES = {"fh2": "1a96545b", "fh2ff": "5a9771a4"}

failures = []


def check(ok, what):
    print(("  ok   " if ok else "  FAIL ") + what)
    if not ok:
        failures.append(what)


def skip(what):
    print("  skip " + what)


# ------------------------------------------------------------------ PE reader

class PE:
    def __init__(self, path=None, data=None):
        self.data = data if data is not None else Path(path).read_bytes()
        d = self.data
        e = struct.unpack_from("<I", d, 0x3C)[0]
        if d[:2] != b"MZ" or d[e:e + 4] != b"PE\0\0":
            raise ValueError("not a PE file")
        self.machine, sections, self.timestamp, _, _, optional_size, self.characteristics = \
            struct.unpack_from("<HHIIIHH", d, e + 4)
        optional = e + 24
        self.magic = struct.unpack_from("<H", d, optional)[0]
        self.image_base = struct.unpack_from("<Q", d, optional + 24)[0]
        self.image_size, self.headers_size = struct.unpack_from("<II", d, optional + 56)
        self.sections = []
        table = optional + optional_size
        for i in range(sections):
            name, vsize, va, rsize, raw = struct.unpack_from("<8sIIII", d, table + 40 * i)
            self.sections.append((name.rstrip(b"\0"), va, vsize, rsize, raw))
        directories = optional + 112
        self.exports_rva, _ = struct.unpack_from("<II", d, directories)
        self.imports_rva, _ = struct.unpack_from("<II", d, directories + 8)
        self.pdata_rva, self.pdata_size = struct.unpack_from("<II", d, directories + 24)
        self._image = None
        self._functions = None

    def offset(self, rva):
        for _, va, vsize, rsize, raw in self.sections:
            if va <= rva < va + max(vsize, rsize):
                return rva - va + raw
        raise ValueError(hex(rva))

    def image(self):
        """The image as the loader lays it out (bytes)."""
        if self._image is None:
            out = bytearray(self.image_size)
            out[:self.headers_size] = self.data[:self.headers_size]
            for _, va, vsize, rsize, raw in self.sections:
                size = min(rsize, vsize) if vsize else rsize
                out[va:va + size] = self.data[raw:raw + size]
            self._image = bytes(out)
        return self._image

    def section(self, name):
        for n, va, vsize, rsize, _ in self.sections:
            if n == name:
                return va, va + max(vsize, rsize)
        return 0, 0

    def function(self, rva):
        """(start, end) of the .pdata function holding rva, or None."""
        if self._functions is None:
            img = self.image()
            entries = [struct.unpack_from("<III", img, self.pdata_rva + 12 * i) for i in range(self.pdata_size // 12)]
            self._functions = sorted((b, e) for b, e, _ in entries if b)
        lo, hi = 0, len(self._functions)
        while lo < hi:
            mid = (lo + hi) // 2
            if self._functions[mid][0] <= rva:
                lo = mid + 1
            else:
                hi = mid
        if lo and self._functions[lo - 1][0] <= rva < self._functions[lo - 1][1]:
            return self._functions[lo - 1]
        return None

    def text(self, rva):
        o = self.offset(rva)
        return self.data[o:self.data.index(b"\0", o)].decode("ascii")

    def exports(self):
        if not self.exports_rva:
            return []
        o = self.offset(self.exports_rva)
        count, names = struct.unpack_from("<I", self.data, o + 24)[0], struct.unpack_from("<I", self.data, o + 32)[0]
        return [self.text(struct.unpack_from("<I", self.data, self.offset(names) + 4 * i)[0]) for i in range(count)]

    def imports(self):
        result, o = [], self.offset(self.imports_rva) if self.imports_rva else None
        while o is not None:
            fields = struct.unpack_from("<IIIII", self.data, o)
            if not fields[3]:
                break
            result.append(self.text(fields[3]))
            o += 20
        return result

    def map(self):
        """The image laid out as the loader maps it, in private RWX memory; returns its address."""
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.VirtualAlloc.restype = ctypes.c_void_p
        kernel32.VirtualAlloc.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint32, ctypes.c_uint32]
        base = kernel32.VirtualAlloc(None, self.image_size, 0x3000, 0x40)
        image = self.image()
        ctypes.memmove(base, image, len(image))
        return base


def rip_target(image, at, length):
    """The target of the rip-relative disp32 that ends instruction `at`..`at+length` (disp at the end)."""
    return at + length + struct.unpack_from("<i", image, at + length - 4)[0]


def call_target(image, at):
    return at + 5 + struct.unpack_from("<i", image, at + 1)[0] if image[at] == 0xE8 else None


def lea_refs(pe, target, prefixes=(b"\x48\x8d\x05", b"\x48\x8d\x0d", b"\x48\x8d\x15", b"\x4c\x8d\x05")):
    """Every "lea reg,[rip+target]" (7 bytes) in .text: their addresses."""
    img = pe.image()
    start, end = pe.section(b".text")
    out = []
    for prefix in prefixes:
        at = img.find(prefix, start, end)
        while at >= 0:
            if rip_target(img, at, 7) == target:
                out.append(at)
            at = img.find(prefix, at + 1, end)
    return sorted(out)


def calls_to(pe, target):
    img = pe.image()
    start, end = pe.section(b".text")
    out, at = [], img.find(b"\xe8", start, end)
    while at >= 0:
        if call_target(img, at) == target:
            out.append(at)
        at = img.find(b"\xe8", at + 1, end)
    return out


def calls_in(pe, start, end):
    """The rel32 call targets in start..end, in order (by disassembly when capstone is there)."""
    img = pe.image()
    if capstone:
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        return [i.address + i.size + struct.unpack_from("<i", i.bytes, 1)[0]
                for i in md.disasm(img[start:end], start) if i.mnemonic == "call" and i.bytes[0] == 0xE8]
    return [call_target(img, a) for a in range(start, end - 5) if img[a] == 0xE8]


def masked(pe, start, length):
    """The code start..start+length with rip displacements and rel32 branch targets zeroed (capstone)."""
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    img = pe.image()
    out = bytearray()
    for ins in md.disasm(img[start:start + length], start):
        b = bytearray(ins.bytes)
        for op in ins.operands:
            if op.type == x86.X86_OP_MEM and op.mem.base == x86.X86_REG_RIP:
                b[ins.disp_offset:ins.disp_offset + 4] = b"\0\0\0\0"
        if (ins.group(capstone.CS_GRP_JUMP) or ins.group(capstone.CS_GRP_CALL)) and ins.imm_size == 4:
            b[-4:] = b"\0\0\0\0"
        out += b
    return bytes(out)


def same_code(fh2, fh2_rva, ff, ff_rva, length):
    """The two code ranges differ only in relocated fields (rip displacements, rel32 targets)."""
    return masked(fh2, fh2_rva, length) == masked(ff, ff_rva, length)


# --------------------------------------------------------------- 1. package

def valid_mod_id(value):
    # features/addons.c valid_id() in Extra Options.
    if not isinstance(value, str) or not 0 < len(value) <= 128:
        return False
    for i, ch in enumerate(value):
        if not (ch.islower() and ch.isascii() or ch.isdigit() or ch == "-"):
            return False
        if ch == "-" and (i == 0 or i == len(value) - 1 or value[i - 1] == "-"):
            return False
    return True


def test_package(output):
    print("package")
    mod = output / "mods" / MOD_NAME
    manifest = json.loads((mod / "mod.json").read_text(encoding="utf-8"))
    check(manifest.get("format_version") == 1, "mod.json format_version is 1")
    check(valid_mod_id(manifest.get("id")), f"mod.json id {manifest.get('id')!r} is a valid Extra Options mod ID")
    check(manifest.get("games") == ["fh2", "fh2ff"], f"mod.json games: fh2, fh2ff ({manifest.get('games')})")
    check(isinstance(manifest.get("dependencies", []), list), "mod.json dependencies is a list")
    check(not (mod / "database.json").exists() and not (mod / "strings.json").exists(), "a plugin-only mod")
    top = sorted(p.name for p in (mod / "plugins").iterdir())
    check(top == [DLL_NAME, INI_NAME], f"plugins folder holds the DLL and its INI only ({top})")
    pe = PE(mod / "plugins" / DLL_NAME)
    check(pe.machine == 0x8664 and pe.magic == 0x20B and bool(pe.characteristics & 0x2000), "the DLL is PE32+ x64")
    exports = pe.exports()
    check(exports == ["FH2PluginInitialize"], f"exports FH2PluginInitialize only ({exports})")
    imports = pe.imports()
    check(not [n for n in imports if not SYSTEM_DLLS.match(n)], f"only Windows DLLs imported ({', '.join(imports)})")
    test_exports = PE(output / "tests" / "AzimuthSettingsTest.dll").exports()
    check("FH2PluginInitialize" in test_exports and "AzsTestExtendXaml" in test_exports,
          "the test build has the same entry point plus test entry points")
    ini = configparser.ConfigParser(interpolation=None, strict=True)
    ini.optionxform = str
    ini.read(mod / "plugins" / INI_NAME, encoding="utf-8")
    expected = {"Video": ["Enabled"], "Audio": ["Enabled"], "Azimuth": ["LiveApply", "Ini"], "Captions": ["Language"],
                "Logging": ["Enabled"]}
    for section, keys in expected.items():
        check(ini.has_section(section) and all(ini.has_option(section, k) for k in keys),
              f"INI [{section}] has {', '.join(keys)}")
    check(ini.get("Video", "Enabled", fallback="") == "1" and ini.get("Audio", "Enabled", fallback="") == "1",
          "INI [Video] and [Audio] Enabled default to 1")


# ------------------------------------------------------------------ helpers

def load_test_dll(output, path=None):
    dll = ctypes.CDLL(str(path or output / "tests" / "AzimuthSettingsTest.dll"))
    signatures = {
        "AzsTestCheckHost": ([ctypes.c_void_p], ctypes.c_char_p),
        "AzsTestHostName": ([ctypes.c_void_p], ctypes.c_char_p),
        "AzsTestHostSite": ([ctypes.c_void_p, ctypes.c_uint], ctypes.c_uint32),
        "AzsTestHostTitleRate": ([ctypes.c_void_p], ctypes.c_int),
        "AzsTestCheckGame": ([ctypes.c_void_p, ctypes.c_uint32], ctypes.c_char_p),
        "AzsTestGameId": ([], ctypes.c_char_p),
        "AzsTestTitleId": ([], ctypes.c_wchar_p),
        "AzsTestShadowSite": ([ctypes.c_uint], ctypes.c_uint32),
        "AzsTestBuildSite": ([ctypes.c_uint, ctypes.c_uint], ctypes.c_uint32),
        "AzsTestOtherPlugin": ([ctypes.c_int], None),
        "AzsTestExtendXaml": ([ctypes.c_int, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t,
                               ctypes.c_int, ctypes.c_int], ctypes.c_size_t),
        "AzsTestRowsTitle": ([ctypes.c_wchar_p, ctypes.c_int], None),
        "AzsTestRowsFile": ([ctypes.c_wchar_p], None),
        "AzsTestRowsLayout": ([], ctypes.c_char_p),
        "AzsTestRowGet": ([ctypes.c_uint], ctypes.c_int),
        "AzsTestRowSet": ([ctypes.c_uint, ctypes.c_int], ctypes.c_int),
        "AzsTestRowKey": ([ctypes.c_uint], ctypes.c_wchar_p),
        "AzsTestDetailLevel": ([ctypes.c_uint], ctypes.c_int),
        "AzsTestPresetDetail": ([ctypes.c_uint, ctypes.c_uint], ctypes.c_int),
        "AzsTestFrameRates": ([ctypes.c_uint, ctypes.POINTER(ctypes.c_int), ctypes.c_int], ctypes.c_int),
        "AzsTestSetFrameRates": ([ctypes.POINTER(ctypes.c_int), ctypes.c_int], None),
        "AzsTestRowChoice": ([ctypes.c_uint, ctypes.c_uint, ctypes.c_int], ctypes.c_wchar_p),
        "AzsTestRowCount": ([ctypes.c_uint], ctypes.c_uint),
        "AzsTestLanguage": ([ctypes.c_int], None),
        "AzsTestCurrentLanguage": ([ctypes.c_void_p], ctypes.c_int),
    }
    for name, (arguments, result) in signatures.items():
        function = getattr(dll, name)
        function.argtypes = arguments
        function.restype = result
    return dll


ALL_LIVE = 31


def extend(dll, xaml, page=VIDEO, language=EN, live=ALL_LIVE):
    out = ctypes.create_string_buffer(len(xaml) + 0x10000)
    n = dll.AzsTestExtendXaml(page, xaml, len(xaml), out, len(out), language, live)
    return out.raw[:n] if n else b""


def parse(xaml):
    text = xaml.decode("utf-8-sig")
    ET.fromstring(text)
    return text


# ------------------------------------------------------------------ 2. host

def find_all(data, needle):
    at, out = data.find(needle), []
    while at >= 0:
        out.append(at)
        at = data.find(needle, at + 1)
    return out


def check_host_sites(name, pe, label):
    """Every site of the plugin's table for this build, from the executable's own bytes."""
    s, img = HOSTS[name], pe.image()
    steps = [struct.unpack_from("<I", img, s["rate_list"] + 4 + 8 * i)[0] for i in range(6)]
    check(steps == [60, 90, 120, 144, 165, 240] and b"\xc7\x44\x24\x20\x1e\x00\x00\x00" in
          img[s["rate_list"] - 0x40:s["rate_list"]],
          f"{label}: the launcher's frame rate steps (30 first, then {steps})")
    tail = img[s["rate_list"]:s["rate_list"] + 0xa0]
    check(b"\x3b\x72\xfc\x76" in tail and b"\x81\xfe\xe8\x03\x00\x00" in tail and b"\xbe\x3c\x00\x00\x00" in
          img[s["rate_list"] - 0x40:s["rate_list"]],
          f"{label}: then the display's refresh rate when higher (at most 1000), 60 when unknown")
    check(img[s["set_rates"]:s["set_rates"] + 2] == b"\x89\x0d" and rip_target(img, s["set_rates"], 6) == s["target"]
          and img[s["set_rates"] + 6:s["set_rates"] + 8] == b"\x89\x15"
          and rip_target(img, s["set_rates"] + 6, 6) == s["title"] and img[s["set_rates"] + 12] == 0xC3,
          f"{label}: {s['set_rates']:#x} stores the target rate at {s['target']:#x} and the title's own at {s['title']:#x}")
    check(call_target(img, s["set_call"]) == s["set_rates"]
          and img[s["set_call"] - 7:s["set_call"] - 4] == b"\x8b\x8c\x24",
          f"{label}: startup calls it at {s['set_call']:#x} with the title's frame_rate")
    check(img[s["reader"]:s["reader"] + 2] == b"\x39\x1d" and rip_target(img, s["reader"], 6) == s["target"]
          and img[s["reader"] - 8:s["reader"] - 3] == b"\xbb\x3c\x00\x00\x00",
          f"{label}: the pacing reads the target at {s['reader']:#x} for every frame")
    check(struct.unpack_from("<I", img, s["target"])[0] == 30 and struct.unpack_from("<I", img, s["volume"])[0] == 100,
          f"{label}: the target rate starts at 30 and the volume at 100")
    check(img[s["toggle"]:s["toggle"] + 2] == b"\x80\x3d" and img[s["toggle"] + 6] == 0
          and s["toggle"] + 7 + struct.unpack_from("<i", img, s["toggle"] + 2)[0] == s["fullscreen"],
          f"{label}: the Alt+Enter toggle reads the fullscreen byte {s['fullscreen']:#x}")
    check(img[s["alt_enter"]:s["alt_enter"] + 11] == bytes.fromhex("4883ff0d751d480fbae61d")
          and img[s["alt_enter"] + 11:s["alt_enter"] + 13] == b"\x73\x16",
          f"{label}: WM_SYSKEYDOWN VK_RETURN with Alt at {s['alt_enter']:#x}")
    v = s["volume_setter"]
    check(call_target(img, s["volume_call"]) == v and img[v + 0x42:v + 0x4c] == bytes.fromhex("b8640000003bd80f47d8")
          and img[v + 0x4c:v + 0x4e] == b"\x87\x1d" and rip_target(img, v + 0x4c, 6) == s["volume"]
          and img[s["volume_call"] - 7:s["volume_call"] - 4] == b"\x8b\x8c\x24",
          f"{label}: the volume setter {v:#x} clamps to 100 and stores {s['volume']:#x}; startup calls it at {s['volume_call']:#x}")
    # The in-game menu of Azimuth calls the same setter too.
    check(len([a for a in find_all(img, b"\xe8") if 0x1000 <= a < len(img) - 5 and call_target(img, a) == v][:3]) >= 2,
          f"{label}: Azimuth calls the volume setter from more than its startup (its in-game menu)")


def detail_tables(img):
    """Azimuth's detail level tables (azimuth.exe 1.0.4 0x3ef0): {detail: levels}, 'default': the others'."""
    starts = [m.start() for m in re.finditer(rb"\x48\x83\xec\x18\x48\x83\xfa\x01\x75", img)]
    if len(starts) != 1:
        return None
    code = img[starts[0]:starts[0] + 0xb0]
    tables = {}
    for m in re.finditer(rb"\x48\x83\xfa(.)\x75.\x48\x8d\x05(....)\x48\xc7\x44\x24\x08(.)\x00\x00\x00", code, re.S):
        at = starts[0] + m.start() + 6
        tables[m.group(1)[0]] = list(struct.unpack_from("<%dI" % m.group(3)[0], img, rip_target(img, at, 7)))
    d = re.search(rb"\x48\x8d\x05(....)\x48\xc7\x44\x24\x08\x04\x00\x00\x00", code, re.S)
    if d:
        tables["default"] = list(struct.unpack_from("<4I", img, rip_target(img, starts[0] + d.start(), 7)))
    return tables


def check_ini_reader(name, pe, label, dll):
    """The data\\azimuth.ini keys and values the rows write, as Azimuth reads them."""
    img = pe.image()

    def wide(text):
        return text.encode("utf-16-le") + b"\0\0"

    missing = [k for k in AZIMUTH_KEYS + ["display", "launcher", "title-"] if wide(k) not in img]
    check(not missing, f"{label}: Azimuth's key table has every title key and [launcher] display (missing {missing})")
    keys = [dll.AzsTestRowKey(i) for i in range(len(ROWS))]
    check(keys == KEYS and set(keys) - {"display"} <= set(AZIMUTH_KEYS),
          f"{label}: the rows write Azimuth's keys only, one row each ({len(keys)} rows)")
    values = [v for v in ("stereo", "system", "multichannel", "msaax2", "msaax4", "coverage", "off", "on", "16:9",
                          "display", "windowed", "fullscreen") if wide(v) not in img]
    values += [v for v in ("low", "default", "high", "ultra", "off", "16x") if v.encode() + b"\0" not in img]
    check(not values, f"{label}: the values the rows write are Azimuth's (missing {values})")
    codes = [c for c in LANGUAGES if wide(c) not in img]
    check(not codes, f"{label}: Azimuth knows every language code the rows offer (missing {codes})")
    # frame_rate: 1..4 digits, 30..1000 ("add eax,-1Eh; cmp eax,3CAh"); volume: 0..100 ("cmp eax,64h");
    # resolution 360..4320 ("lea eax,[rcx-168h]; cmp eax,0F78h").
    check(img.find(b"\x83\xc0\xe2\x3d\xca\x03\x00\x00") > 0 and img.find(b"\x83\xf8\x64\x77") > 0
          and img.find(b"\x8d\x81\x98\xfe\xff\xff\x3d\x78\x0f\x00\x00") > 0,
          f"{label}: frame_rate is read as 30..1000, volume as 0..100, resolution as 360..4320")
    # The settings object's defaults (1.0.4 0x9d70): resolution 1080, anti-aliasing 4 (msaax4), frame rate
    # 30, v-sync on, every detail 1 (default), audio 1 (stereo), volume 100.
    defaults = (b"\xc7\x01\x38\x04\x00\x00", b"\xc7\x41\x04\x04\x00\x00\x00", b"\xc7\x41\x08\x1e\x00\x00\x00",
                b"\xc6\x41\x0c\x01", b"\xc7\x41\x7c\x01\x00\x00\x00", b"\xc7\x81\x80\x00\x00\x00\x64\x00\x00\x00")
    ctor = img.find(defaults[0])
    check(ctor > 0 and all(img.find(d, ctor, ctor + 0xc0) > 0 for d in defaults[1:])
          and all(img.find(b"\xc7\x41" + bytes([0x58 + 4 * i]) + b"\x01\x00\x00\x00", ctor, ctor + 0xc0) > 0
                  for i in range(9)),
          f"{label}: Azimuth's defaults: 1080 lines, 4x MSAA, 30 FPS, v-sync on, details Default, stereo, volume 100")
    tables = detail_tables(img)
    want = {1: [1, 2, 3], 2: [1, 2, 3], 6: [0, 1], 8: [1, 2], "default": [0, 1, 2, 3]}
    check(tables == want, f"{label}: Azimuth's detail levels: shadow distance and redraw Default..Ultra, particles "
                          f"Low..Default, texture filtering Default and 16x, the others Low..Ultra ({tables})")
    counts = [dll.AzsTestRowCount(R[name]) for name in ("ShadowDetail", "ShadowDistance", "ShadowRedraw", "Reflections",
                                                        "SceneryDetail", "CarDetail", "Particles", "MotionBlur",
                                                        "TextureFiltering")]
    check(counts == [len((tables or want).get(d, want["default"])) for d in range(9)],
          f"{label}: each detail row offers Azimuth's levels ({counts})")
    for game, title in TITLES.items():
        at = img.find(wide(title))
        record = img.find(struct.pack("<Q", pe.image_base + at)) if at > 0 else -1
        rate = struct.unpack_from("<I", img, record + 0x1c)[0] if record > 0 else None
        check(rate == 30, f"{label}: {game}'s own frame rate in Azimuth's title table is {rate}")


def test_host(dll, executables):
    print("host")
    rates = (ctypes.c_int * 16)()
    expected = {60: [30, 60], 75: [30, 60, 75], 144: [30, 60, 90, 120, 144], 165: [30, 60, 90, 120, 144, 165],
                240: [30, 60, 90, 120, 144, 165, 240], 360: [30, 60, 90, 120, 144, 165, 240, 360],
                1: [30, 60], 0: [30, 60], 1200: [30, 60, 90, 120, 144, 165, 240]}
    for refresh, want in expected.items():
        n = dll.AzsTestFrameRates(refresh, rates, 16)
        check(list(rates[:n]) == want, f"Frame rate choices for a {refresh} Hz display: {list(rates[:n])}")
    for name, label, path in executables:
        if not path or not Path(path).is_file():
            skip(f"{label} not found")
            continue
        pe = PE(path)
        base = pe.map()
        problem = dll.AzsTestCheckHost(base)
        got = dll.AzsTestHostName(base)
        check(problem is None and got == name.encode(),
              f"{label} (TimeDateStamp {pe.timestamp:#x}, image {pe.image_size:#x}) is Azimuth {name}"
              f"{'' if problem is None else ': ' + problem.decode()}")
        sites = HOSTS[name]
        plugin_sites = [dll.AzsTestHostSite(base, i) for i in range(6)]
        check(plugin_sites == [sites["target"], sites["volume_setter"], sites["fullscreen"], sites["rate_list"],
                               sites["volume"], sites["title"]], f"{label}: the plugin's sites are the ones checked here")
        check_host_sites(name, pe, label)
        check_ini_reader(name, pe, label, dll)
        # The title's own rate as the setter stores it at startup.
        cell = ctypes.c_uint32.from_address(base + sites["title"])
        cell.value = 60
        check(dll.AzsTestHostTitleRate(base) == 60, f"{label}: the title's own rate is read from Azimuth's {sites['title']:#x}")
        cell.value = 0
        check(dll.AzsTestHostTitleRate(base) == 0, f"{label}: a title rate that is not a rate is not used")
        # A changed launcher table, or another timestamp: refused.
        changed = bytearray(pe.data)
        changed[pe.offset(sites["rate_list"] + 4)] = 0x3D
        problem = dll.AzsTestCheckHost(PE(data=bytes(changed)).map())
        check(problem is not None and b"unknown build" in problem, f"{label} with a changed launcher list is refused")
        changed = bytearray(pe.data)
        e = struct.unpack_from("<I", changed, 0x3C)[0]
        struct.pack_into("<I", changed, e + 8, pe.timestamp + 1)
        problem = dll.AzsTestCheckHost(PE(data=bytes(changed)).map())
        check(problem is not None and b"is not Azimuth 1.0.4 or 1.0.2" in problem,
              f"another TimeDateStamp is refused ({problem.decode() if problem else None})")
    exe = PE(sys.executable)
    check(dll.AzsTestCheckHost(exe.map()) is not None, "another executable is refused")


# ------------------------------------------------------------------ 3. game

def site(dll, kind, index=0):
    return dll.AzsTestBuildSite(kind, index)


def check_game_common(dll, pe, base, label, game):
    check(dll.AzsTestGameId() == game.encode() and dll.AzsTestTitleId() == TITLES[game],
          f"{label}: identified as {game}, title {TITLES[game]}")
    img = pe.image()
    call = site(dll, 5)
    after = bytes.fromhex("4c893f488b0848894f08")
    check(call_target(img, call) == site(dll, 0, N["GetResource"]) and img[call + 5:call + 15] == after
          and len([a for a in calls_to(pe, site(dll, 0, N["GetResource"])) if img[a + 5:a + 15] == after]) == 1,
          f"{label}: the XAML resource call {call:#x} is the one call of the reader followed by its result's store")
    for rva, name in ((site(dll, 0, N["InsertItem"]), b"InsertItem at"),
                      (site(dll, 0, N["RegisterAudioType"]), b"RegisterAudioType at"),
                      (site(dll, 2, 1) + 1, b"Audio page type is not as expected"),
                      (site(dll, 1, 1) + 3, b"Gamma property template is not as expected")):
        byte = ctypes.c_ubyte.from_address(base + rva)
        byte.value ^= 0xff
        problem = dll.AzsTestCheckGame(base, pe.image_size)
        byte.value ^= 0xff
        check(problem is not None and name in problem, f"{label}: a changed game is refused "
                                                       f"({problem.decode() if problem else None})")
    problem = dll.AzsTestCheckGame(base, pe.image_size + 0x1000)
    check(problem is not None and b"no settings pages here" in problem,
          f"{label}: another image size (another game) is refused ({problem.decode() if problem else None})")
    dll.AzsTestCheckGame(base, pe.image_size)  # identified again for the next checks


def ported(fh2, fh2_rva, ff, ff_rva, length=0x40):
    return same_code(fh2, fh2_rva, ff, ff_rva, length) if capstone else True


def check_ff_sites(dll, fh2, fh2_sites, ff, label, azimuth):
    """Every Fast & Furious site, found again here from the executable (and FH2's sites)."""
    img = ff.image()
    templates = ("IntProperty", "FloatProperty", "CollectionProperty")
    template_offsets = [fh2_sites[1][i] - fh2_sites[0][N[n]] for i, n in enumerate(templates)]
    for i, (string, native) in enumerate(zip(("Subtitles", "Gamma", "OnOffOptions"), templates)):
        names = [a for a in find_all(img, string.encode() + b"\0") if img[a - 1] == 0]
        refs = [r for a in names for r in lea_refs(ff, a)]
        lea, rva = site(dll, 1, i), site(dll, 0, N[native])
        span = ff.function(lea)
        check(lea in refs and lea - rva == template_offsets[i] and span and span[0] == rva,
              f"{label}: {native} {rva:#x} is the template naming {string!r} (lea {lea:#x}, as FH2's "
              f"+{template_offsets[i]:#x})")
    # The getters the templates install: their second call is the box, their third the delete.
    for native, box in (("IntProperty", "BoxInt"), ("FloatProperty", "BoxFloat")):
        factory = site(dll, 0, N[native])
        getter = rip_target(img, factory + 0x44, 7) if img[factory + 0x44:factory + 0x47] == b"\x48\x8d\x05" else 0
        span = ff.function(getter) if getter else None
        called = calls_in(ff, *span) if span else []
        check(len(called) >= 3 and called[1] == site(dll, 0, N[box]) and called[2] == site(dll, 0, N["DeleteObject"]),
              f"{label}: {box} {site(dll, 0, N[box]):#x} is what {native}'s getter {getter:#x} boxes with "
              f"(DeleteObject {site(dll, 0, N['DeleteObject']):#x} after it)")
    # The Audio page type: the set-up whose first property names "AudioPresetOptions".
    names = [a for a in find_all(img, b"AudioPresetOptions\0") if img[a - 1] == 0]
    factories = [ff.function(r)[0] for a in names for r in lea_refs(ff, a) if ff.function(r)]
    setups = [c for f in factories for c in calls_to(ff, f)]
    lea = site(dll, 2, 1)
    span = ff.function(lea)
    check(len(setups) == 1 and lea == setups[0] + 0x1a and site(dll, 0, N["RegisterAudioType"]) == lea - 0xf8
          and span and span[0] == lea - 0xf8 and rip_target(img, lea, 7) == site(dll, 3, 1),
          f"{label}: the Audio page set-up {lea - 0xf8:#x} registers \"AudioPresetOptions\" first, into "
          f"{site(dll, 3, 1):#x} (lea {lea:#x})")
    lea = site(dll, 2, 0)
    span = ff.function(lea)
    check(span and span[0] == site(dll, 0, N["RegisterVideoType"]) == lea - 0xf8
          and rip_target(img, lea, 7) == site(dll, 3, 0),
          f"{label}: the Video page set-up {lea - 0xf8:#x} registers into {site(dll, 3, 0):#x} (lea {lea:#x})")
    if capstone:
        def length(pe, rva):  # the function's first 0x30 bytes (CollectionType is 8: lea rax; ret)
            span = pe.function(rva)
            return min(0x30, span[1] - rva) if span else 8

        different = [n for i, n in enumerate(NATIVES) if not same_code(
            fh2, fh2_sites[0][i], ff, site(dll, 0, i), length(fh2, fh2_sites[0][i]))]
        check(not different, f"{label}: every native function's code is FH2's but for relocated fields "
                             f"(differs: {different or 'none'})")
        check(same_code(fh2, fh2_sites[0][N["RegisterVideoType"]], ff, site(dll, 0, N["RegisterVideoType"]), 0xf8)
              and same_code(fh2, fh2_sites[0][N["RegisterAudioType"]], ff, site(dll, 0, N["RegisterAudioType"]), 0xf8),
              f"{label}: both page set-ups are FH2's up to their registration")
    else:
        skip("capstone not installed: the masked code comparisons")
    # keyboard-mouse's ported sites (tools/port_sites.py) for the vtables and the text tables.
    table = REPO / "plugins" / "keyboard-mouse" / "src" / "ff_sites.inc"
    if table.is_file():
        pairs = {int(a, 16): int(b, 16) for a, b in re.findall(r"\{0x([0-9a-f]+)u, 0x([0-9a-f]+)u", table.read_text())}
        wanted = [(fh2_sites[4][i], site(dll, 4, i)) for i in range(5)] + [(fh2_sites[6], site(dll, 6))]
        check(all(pairs.get(a) == b for a, b in wanted),
              f"{label}: the collection vtables and text tables are keyboard-mouse's ported sites (ff_sites.inc)")
    else:
        skip("plugins/keyboard-mouse/src/ff_sites.inc not found")
    # The shadow sites.
    cascades, store, pointer, vtable, read = [dll.AzsTestShadowSite(i) for i in range(5)]
    fh2_img = fh2.image()
    fh2_cascades = struct.unpack_from("<8f", fh2_img, 0x2207288)
    ff_cascades = struct.unpack_from("<8f", img, cascades)
    check([ff_cascades[i] for i in (0, 1, 2, 4, 5, 6)] == [fh2_cascades[i] for i in (0, 1, 2, 4, 5, 6)]
          and img.count(struct.pack("<3f", 5, 15, 40)) == 1,
          f"{label}: the cascade distances {cascades:#x} hold FH2's console values (the only copy)")
    if azimuth:
        records = [a for a in find_all(azimuth, struct.pack("<I", cascades)) if a % 8 == 0]
        fh2_records = [a for a in find_all(azimuth, struct.pack("<I", 0x2207288)) if a % 8 == 0]
        check(len(records) == len(fh2_records) == 2 and all(azimuth[a - 0x18:a] == azimuth[b - 0x18:b]
                                                             for a, b in zip(records, fh2_records)),
              f"{label}: Azimuth 1.0.4 patches the same High and Ultra values there as at FH2's 0x2207288")
    else:
        skip(f"{label}: Azimuth 1.0.4's shadow patch records (no azimuth.exe)")
    stores = lea_refs(ff, pointer, (b"\x48\x89\x0d",))
    fh2_stores = lea_refs(fh2, 0x36609a0, (b"\x48\x89\x0d",))
    check(img[store:store + 3] == b"\x48\x89\x0d" and rip_target(img, store, 7) == pointer and img[store + 7] == 0xC3
          and stores == [store] and fh2_stores == [0x112d760],
          f"{label}: {store:#x} is the one store of the shadow renderer at {pointer:#x} (mov [rip],rcx; ret), "
          "as FH2's 0x112d760")
    constructors = lea_refs(ff, vtable)
    fh2_constructors = lea_refs(fh2, 0x2e82b88)
    check(len(constructors) == len(fh2_constructors) == 2 and all(
              ported(fh2, fh2.function(a)[0], ff, ff.function(b)[0], 0x40)
              for a, b in zip(fh2_constructors, constructors)),
          f"{label}: the shadow renderer's vtable {vtable:#x} is set by the same two constructors as FH2's")
    span = ff.function(read)
    fh2_span = fh2.function(0x1131c02)
    check(img[read:read + 7] == bytes.fromhex("418b86bc210000") and span and fh2_span
          and read - span[0] == 0x1131c02 - fh2_span[0]
          and ported(fh2, fh2_span[0], ff, span[0], min(0x1131c02 - fh2_span[0] + 0x20, 0x400)),
          f"{label}: {read:#x} reads the maps per frame where FH2's 0x1131c02 does, in the same code")


def test_game(dll, exe, label, game, fh2_sites=None, fh2=None, azimuth=None):
    print(f"game ({label})")
    if not exe or not exe.is_file():
        skip(f"{exe} not found")
        return None, None
    pe = PE(exe)
    base = pe.map()
    target = dll.AzsTestCheckGame(base, pe.image_size)
    reader = site(dll, 0, N["GetResource"])
    check(target is not None and target.startswith(b"the game's resource reader " + f"{reader:#x}".encode()),
          f"{label} passes the guards; the call goes to {target.decode() if target else None}")
    sites = [[site(dll, 0, i) for i in range(len(NATIVES))], [site(dll, 1, i) for i in range(3)],
             [site(dll, 2, i) for i in range(2)], [site(dll, 3, i) for i in range(2)], [site(dll, 4, i) for i in range(5)],
             site(dll, 5), site(dll, 6), site(dll, 7)]
    check_game_common(dll, pe, base, label, game)
    if game == "fh2":
        # As Extra Options patches it: call 0x13fe354, a mov rax/jmp rax thunk there.
        thunk = (ctypes.c_ubyte * 12).from_address(base + 0x13fe354)
        hook = ctypes.cast(dll.AzsTestCheckHost, ctypes.c_void_p).value
        saved = bytes(thunk)
        thunk[:] = list(b"\x48\xb8" + struct.pack("<Q", hook) + b"\xff\xe0")
        call = (ctypes.c_ubyte * 5).from_address(base + 0x16ebb37)
        original = bytes(call)
        call[:] = list(b"\xe8" + struct.pack("<i", 0x13fe354 - (0x16ebb37 + 5)))
        target = dll.AzsTestCheckGame(base, pe.image_size)
        check(target is not None and target.startswith(b"Extra Options' resource hook (thunk 0x13fe354"),
              f"with Extra Options' patch: {target.decode() if target else None}")
        thunk[:] = list(saved)
        call[:] = list(original)
        dll.AzsTestCheckGame(base, pe.image_size)
        check([dll.AzsTestShadowSite(i) for i in range(5)] == [0x2207288, 0x112d760, 0x36609a0, 0x2e82b88, 0x1131c02],
              f"{label}: the shadow sites are the ones Azimuth Graphics Options inspected")
    else:
        check(sites[7] == 0, f"{label}: no Extra Options thunk (Extra Options has no build for it)")
        if fh2 is not None:
            check_ff_sites(dll, fh2, fh2_sites, pe, label, azimuth)
        else:
            skip(f"{label}: the FH2 executable (to find the sites again)")
    return pe, sites


# ------------------------------------------------------------------ 4. Extra Options

def build_eo_xaml(zig, files, folder):
    """Extra Options' settings pages code (support.c, video_options_xaml.c, video_options_text.c and headers) as a DLL."""
    shutil.rmtree(folder, ignore_errors=True)
    folder.mkdir(parents=True)
    for name, data in files.items():
        (folder / name).write_bytes(data)
    (folder / "eo_wrapper.c").write_text(
        '#include "video_options_xaml.h"\n'
        "__declspec(dllexport) size_t EoExtend(const char *t, size_t n, char *o, size_t c) {\n"
        "    return video_options_extend_xaml(NULL, t, n, o, c);\n}\n", encoding="utf-8")
    sources = [folder / n for n in ("eo_wrapper.c", "support.c", "video_options_xaml.c", "video_options_text.c")]
    dll = folder / "eo_xaml.dll"
    command = [str(zig), "cc", "-target", "x86_64-windows-gnu", "-O1", "-w", "-shared", "-fms-extensions",
               "-funwind-tables", "-fno-builtin", "-DFH2_AZIMUTH", "-I", str(folder), *map(str, sources), "-o", str(dll)]
    run = subprocess.run(command, capture_output=True, text=True)
    if run.returncode:
        # Functions of other features the file refers to: stubs (never called on these pages).
        missing = sorted(set(re.findall(r"undefined symbol: (\w+)", run.stderr)))
        if not missing:
            print(run.stderr)
            return None
        (folder / "stubs.c").write_text("".join(f"void *{n}(void) {{ return 0; }}\n" for n in missing))
        run = subprocess.run(command[:-2] + [str(folder / "stubs.c")] + command[-2:], capture_output=True, text=True)
        if run.returncode:
            print(run.stderr)
            return None
    library = ctypes.CDLL(str(dll))
    library.EoExtend.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t]
    library.EoExtend.restype = ctypes.c_size_t
    return library


def c_literals(text):
    """C string literals (adjacent ones joined, escapes decoded) outside comments."""
    text = re.sub(r"//[^\n]*|/\*.*?\*/", "", text, flags=re.S)
    out = []
    for m in re.finditer(r'(?:"(?:[^"\\\n]|\\.)*"\s*)+', text):
        raw = "".join(re.findall(r'"((?:[^"\\\n]|\\.)*)"', m.group()))
        out.append(raw.encode("latin-1", "backslashreplace").decode("unicode_escape").encode("latin-1"))
    return out


def eo_extend(library, xaml):
    out = ctypes.create_string_buffer(len(xaml) + 0x20000)
    n = library.EoExtend(xaml, len(xaml), out, len(out))
    return out.raw[:n]


def test_eo(zig, release, source, output):
    """Extra Options 0.4.4: its release and the source it was built from; returns the XAML code (or None)."""
    print("eo")
    winmm = release / "winmm.dll" if release else None
    if not winmm or not winmm.is_file():
        skip("the Extra Options 0.4.4 release (winmm.dll) not found (--eo-release)")
        return None
    data = winmm.read_bytes()
    check(b"0.4.4" in data and b"supports the inspected Azimuth 1.0.1, 1.0.2, 1.0.3 and 1.0.4" in data,
          "the release is Extra Options 0.4.4 for Azimuth 1.0.1 ... 1.0.4")
    # Its patch of the resource call: original "call 0xaabec0", patched "call 0x13fe354" (the thunk).
    original = b"\xe8" + struct.pack("<i", 0xaabec0 - (0x16ebb37 + 5))
    patched = b"\xe8" + struct.pack("<i", 0x13fe354 - (0x16ebb37 + 5))
    check(original + patched in data, "its patch table points the resource call at 0x16ebb37 to the thunk 0x13fe354")
    check(b"<!-- EO:StandaloneOptionsPage -->" in data, "it skips plugins' standalone pages marked EO:StandaloneOptionsPage")
    if not source or not (source / "features" / "video_options_xaml.c").is_file():
        skip("the Extra Options 0.4.4 source not found (--eo-source)")
        return None
    xaml_c = (source / "features" / "video_options_xaml.c").read_text(encoding="utf-8")
    literals = [lit for lit in c_literals(xaml_c) if len(lit) >= 12 and not lit.endswith(b".h")]
    missing = [lit for lit in literals if lit not in data]
    check(literals and not missing, f"the release holds all {len(literals)} XAML texts of the source's settings pages "
                                    f"code{'' if not missing else ' (missing ' + repr(missing[:3]) + ')'}")
    files = {}
    for folder in (source, source / "features", source / "adapters"):
        for header in folder.glob("*.h"):
            files[header.name] = header.read_bytes()
    for name in ("support.c", "features/video_options_xaml.c", "features/video_options_text.c"):
        files[Path(name).name] = (source / name).read_bytes()
    library = build_eo_xaml(zig, files, output / "obj" / "eo-0.4.4")
    check(library is not None, "Extra Options 0.4.4's settings pages code builds for the test")
    return library


# ------------------------------------------------------------------ 5. xaml

NATIVE_AUDIO = ["AudioPreset", "RadioVolume", "SFXVolume", "VOVolume", "CarVolume", "TireVolume", "GPSVoice",
                "XboxMusic"]


def check_video(text, label, eo_rows):
    rows = re.findall(r'SelectedIndex="\{Binding (Azs\w+)\}"', text)
    check(rows == VIDEO_ROWS, f"{label}: the {len(VIDEO_ROWS)} Video rows in order ({len(rows)})")
    check(all(text.count(f'x:Key="AZS_{n}"') == 1 for n in VIDEO_ROWS), f"{label}: one TileMenuDataItem per row")
    list_start = text.index('<local:CommonListBox x:Name="OptionsList"')
    opening = text[list_start:text.index(">", list_start)]
    check('A_Style_ListBox_Scrollable' in opening and 'Height="440"' in opening,
          f"{label}: the options list is scrollable, 440 px")
    check(text.index("{Binding Subtitles}") < text.index("{Binding AzsFrameRate}"),
          f"{label}: the rows come after the page's own Brightness and Subtitles")
    if eo_rows:
        eo = text.index("{Binding RealtimeShadows}")
        check(text.index("{Binding AzsAntiAliasing}") < eo, f"{label}: and before Extra Options' rows (Realtime shadows ...)")
        check(all(f"{{Binding {n}}}" in text for n in ("PhotoSamplesPreset", "LiveryResolution", "LiveryMsaa")),
              f"{label}: Extra Options' Video rows are all kept")
    check("AzsAudioOutput" not in text, f"{label}: no audio rows on the Video page")


def check_audio(text, label, eo_rows, native=NATIVE_AUDIO):
    list_start = text.index('<local:CommonListBox x:Name="OptionsList"')
    list_end = text.index("</local:CommonListBox>")
    body = text[list_start:list_end]
    bindings = re.findall(r'(?:SelectedIndex|Value)="\{Binding (\w+)\}"', body)
    check(bindings[-4:] == AUDIO_ROWS, f"{label}: audio output, volume, speech and text language are the last rows "
                                       f"({bindings[-4:]})")
    check(all(text.count(f'x:Key="AZS_{n}"') == 1 for n in AUDIO_ROWS) and 'x:Key="AZS_VolumeSlider"' in text,
          f"{label}: one TileMenuDataItem per row and the volume slider style")
    check('<Slider Value="{Binding AzsVolume}" Tag="{StaticResource AZS_AzsVolume}" '
          'Style="{StaticResource AZS_VolumeSlider}"/>' in body, f"{label}: volume is a slider")
    opening = text[list_start:text.index(">", list_start)]
    check('A_Style_ListBox_Scrollable' in opening and 'Height="550"' in opening,
          f"{label}: the options list is scrollable, 550 px")
    check(all(n in bindings for n in native), f"{label}: the page's own rows are kept")
    if eo_rows:
        eo = ["MasterVolume", "BenPhotoDialogue", "AshleyGarageDialogue", "CheckpointSound"]
        check(all(n in bindings for n in eo) and bindings.index("CheckpointSound") < bindings.index("AzsAudioOutput"),
              f"{label}: Extra Options' Audio rows are kept, ours after them")
    check("AzsFrameRate" not in text, f"{label}: no video rows on the Audio page")


def game_pages(ui):
    with zipfile.ZipFile(ui) as archive:
        return archive.read("Scenes/Anthem/A_VideoOptions.xaml"), archive.read("Scenes/Anthem/A_AudioOptions.xaml")


def test_xaml(dll, zig, fh2_ui, ff_ui, eo044, upstream, output):
    print("xaml")
    if not fh2_ui or not fh2_ui.is_file():
        skip(f"FH2's UI.zip not found ({fh2_ui})")
        return None, None, None, None
    video, audio = game_pages(fh2_ui)
    ours = extend(dll, video)
    check(bool(ours) and ours.startswith(b"\xef\xbb\xbf"), "the game's Video page gets the rows (UTF-8 with BOM)")
    text = parse(ours)
    check_video(text, "game Video page", False)
    check(not extend(dll, ours), "a Video page that already has the rows is left alone")
    check('Description="Target frame rate. Applies at once."' in text, "live descriptions say what applies at once")
    check("How far shadows are drawn. Applies at once." in text and "Shadow sharpness (shadow map size). Restart" in text,
          "shadow distance applies at once, shadow detail after a restart")
    restart = parse(extend(dll, video, live=0))
    check("Target frame rate. Restart the game to apply." in restart, "without live apply they say restart")
    polish = parse(extend(dll, video, language=PL))
    check('Label="Liczba klatek"' in polish and 'Label="Zasięg cieni"' in polish and 'Label="Rozmycie ruchu"' in polish,
          "Polish Video captions")
    ukrainian = parse(extend(dll, video, language=UK))
    check('Label="Частота кадрів"' in ukrainian and 'Label="Дальність тіней"' in ukrainian
          and 'Label="Співвідношення сторін"' in ukrainian and "Діє одразу." in ukrainian,
          "Ukrainian Video captions")
    built = video.replace(b'Content="{local:Loc ScreenTitles.IDS_VideoOptions}"', b'Content="KEYBOARD"')
    check(not extend(dll, built), "a page built from the Video page (other title) is refused")
    hidden = video.replace(b'<StackPanel Margin="0, 0, 0, 6" Orientation="Horizontal"',
                           b'<StackPanel Visibility="Collapsed" Margin="0, 0, 0, 6" Orientation="Horizontal"')
    check(hidden != video and not extend(dll, hidden),
          "a page with the brightness preview collapsed (the fork's World/KBM pages) is refused")
    check(not extend(dll, b"<!-- EO:StandaloneOptionsPage -->" + video), "a plugin's standalone Video-class page is refused")
    check(not extend(dll, b'<Page x:Class="ForzaUI.A_HudOptions"/>'), "another page is refused")
    check(not extend(dll, audio), "the Audio page is not the Video page")

    ours = extend(dll, audio, page=AUDIO_PAGE)
    check(bool(ours) and ours.startswith(b"\xef\xbb\xbf"), "the game's Audio page gets the rows (UTF-8 with BOM)")
    text = parse(ours)
    check_audio(text, "game Audio page", False, NATIVE_AUDIO + ["RadioDJ"])
    check(not extend(dll, ours, page=AUDIO_PAGE), "an Audio page that already has the rows is left alone")
    check("Restart the game to apply." in text and "Applies at once." in text,
          "audio output says restart, volume says at once")
    check("Volume (Azimuth)" in text and "Speech language (Azimuth)" in text and "Text language (Azimuth)" in text,
          "English Audio captions")
    check("Język mowy (Azimuth)" in parse(extend(dll, audio, page=AUDIO_PAGE, language=PL)), "Polish Audio captions")
    check("Мова озвучення (Azimuth)" in parse(extend(dll, audio, page=AUDIO_PAGE, language=UK)), "Ukrainian Audio captions")
    check(not extend(dll, b"<!-- EO:StandaloneOptionsPage -->" + audio, page=AUDIO_PAGE),
          "a plugin's standalone Audio-class page is refused")
    check(not extend(dll, video, page=AUDIO_PAGE), "the Video page is not the Audio page")

    ff_pages = None
    if ff_ui and ff_ui.is_file():
        ff_video, ff_audio = game_pages(ff_ui)
        check(ff_video == video, "Fast & Furious' Video page is FH2's")
        check(b"RadioDJ" in audio and b"RadioDJ" not in ff_audio
              and ff_audio.count(b"\n") == audio.count(b"\n") - 2,
              "Fast & Furious' Audio page is FH2's without the Radio DJ row")
        both = extend(dll, ff_audio, page=AUDIO_PAGE)
        check(bool(both), "Fast & Furious' Audio page gets the rows")
        if both:
            check_audio(parse(both), "Fast & Furious Audio page", False)
        both = extend(dll, ff_video)
        check(bool(both), "Fast & Furious' Video page gets the rows")
        if both:
            check_video(parse(both), "Fast & Furious Video page", False)
        ff_pages = (ff_video, ff_audio)
    else:
        skip(f"Fast & Furious' UI.zip not found ({ff_ui})")

    pages = {}
    builds = []
    if eo044:
        builds.append(("Extra Options 0.4.4", eo044))
    else:
        skip("Extra Options 0.4.4's pages (no release/source)")
    if upstream and (upstream / ".git").exists():
        for ref in ("origin/main", "combined-10"):
            have = subprocess.run(["git", "-C", str(upstream), "rev-parse", "--verify", "-q", ref], capture_output=True)
            if have.returncode:
                skip(f"{ref} not in {upstream}")
                continue
            names = subprocess.run(["git", "-C", str(upstream), "ls-tree", "-r", "--name-only", ref],
                                   capture_output=True, text=True, check=True).stdout.split()
            files = {}
            for name in names:
                if name.endswith(".h") or name in ("support.c", "features/video_options_xaml.c",
                                                   "features/video_options_text.c"):
                    files[Path(name).name] = subprocess.run(["git", "-C", str(upstream), "show", f"{ref}:{name}"],
                                                            capture_output=True, check=True).stdout
            library = build_eo_xaml(zig, files, output / "obj" / ("eo-" + ref.replace("/", "-")))
            check(library is not None, f"Extra Options' settings pages code at {ref} builds for the test")
            if library:
                builds.append((f"Extra Options {ref}", library))
    else:
        skip("Extra Options git clone not found (--upstream)")
    for label, library in builds:
        for page, native, checker, name in ((VIDEO, video, check_video, "Video"), (AUDIO_PAGE, audio, check_audio, "Audio")):
            extended = eo_extend(library, native)
            if not extended:
                if page == AUDIO_PAGE and label != "Extra Options 0.4.4":
                    continue  # older Extra Options leaves the Audio page alone
                check(False, f"{label} extends the game's {name} page")
                continue
            check(True, f"{label} extends the game's {name} page")
            parse(extended)
            both = extend(dll, extended, page=page)
            check(bool(both), f"{label}: the plugin adds its rows to its {name} page")
            if both:
                checker(parse(both), f"{label} {name} page", page == VIDEO or label == "Extra Options 0.4.4")
                check(not eo_extend(library, both), f"{label} leaves an already extended {name} page alone "
                                                    "(the plugin runs after it)")
            if label == "Extra Options 0.4.4":
                pages[page] = extended
    return video, audio, pages, ff_pages


# ------------------------------------------------------------------ 6. rows

SAMPLE_INI = ("[launcher]\ngame=1a96545b\ndisplay=fullscreen\nhdr_peak=1000\n"
              "[title-1a96545b]\nfolder=Anthem\nresolution=2160\nantialiasing=msaax4\ngraphics=ultra\n"
              "shadow_detail=ultra\nshadow_distance=ultra\nshadow_redraw=ultra\ntexture_filtering=16x\n"
              "audio=stereo\nframe_rate=120\nvsync=on\nhdr=on\nlanguage=en-US\n")


class Ini:
    """A copy of an azimuth.ini (UTF-8, or UTF-16 with BOM as Azimuth writes it), edited as text."""

    def __init__(self, path, data, title):
        self.path, self.title = path, title
        self.utf16 = data[:2] == b"\xff\xfe"
        self.original = data.decode("utf-16" if self.utf16 else "utf-8-sig")
        self.write(self.original)

    def write(self, text):
        self.path.write_bytes(text.encode("utf-16") if self.utf16 else text.encode("utf-8"))

    def read(self):
        data = self.path.read_bytes()
        return data.decode("utf-16" if data[:2] == b"\xff\xfe" else "utf-8-sig")

    def section(self, name=None):
        """{key: value} of a section (default: the title's)."""
        name, out, current = name or f"title-{self.title}", {}, None
        for line in self.read().splitlines():
            line = line.strip()
            if line.startswith("["):
                current = line[1:-1]
            elif current == name and "=" in line:
                key, value = line.split("=", 1)
                out[key] = value
        return out

    def with_keys(self, **keys):
        """Writes the original with these title keys set (None: removed); returns the text."""
        header, out, current = f"[title-{self.title}]", [], None
        for line in self.original.splitlines():
            if line.startswith("["):
                current = line.strip()
            key = line.split("=", 1)[0] if "=" in line else None
            if current == header and key in keys:
                continue
            out.append(line)
            if line.strip() == header:
                out += [f"{k}={v}" for k, v in keys.items() if v is not None]
        text = "\r\n".join(out) + "\r\n"
        self.write(text)
        return text


def rows_on(dll, ini, label, game):
    """The rows over one game's azimuth.ini copy."""
    title = TITLES[game]
    dll.AzsTestRowsTitle(title, 30)
    dll.AzsTestRowsFile(str(ini.path))
    check(dll.AzsTestRowsLayout() == b"", f"{label}: Azimuth 1.x layout ([launcher] and [title-{title}])")
    rates = (ctypes.c_int * 4)(30, 60, 90, 120)
    dll.AzsTestSetFrameRates(rates, 4)
    check(dll.AzsTestRowCount(R["FrameRate"]) == 4 and dll.AzsTestRowChoice(R["FrameRate"], 3, EN) == "120 FPS",
          f"{label}: Frame rate labels")
    saved = ini.section()
    frame = int(saved.get("frame_rate", "30"))
    check(dll.AzsTestRowGet(R["FrameRate"]) == max(i for i, r in enumerate(rates) if r <= frame),
          f"{label}: Frame rate shows the saved {frame} (or the step below it)")
    ini.with_keys(frame_rate="100")
    check(dll.AzsTestRowGet(R["FrameRate"]) == 2, f"{label}: a saved 100 shows as 90, as Azimuth's launcher does")
    check(dll.AzsTestRowSet(R["FrameRate"], 2) == 1 and ini.section()["frame_rate"] == "90",
          f"{label}: choosing 90 then writes 90 (the saved 100 was not a choice)")
    check(dll.AzsTestRowSet(R["FrameRate"], 2) == 0, f"{label}: choosing 90 again writes nothing")
    ini.with_keys(frame_rate=None)
    check(dll.AzsTestRowGet(R["FrameRate"]) == 0, f"{label}: without frame_rate: 30 FPS, the title's own rate")
    dll.AzsTestRowsTitle(title, 60)
    check(dll.AzsTestRowGet(R["FrameRate"]) == 1, f"{label}: a title whose own rate is 60 shows 60 without frame_rate")
    dll.AzsTestRowsTitle(title, 30)
    ini.with_keys(frame_rate="12345")
    check(dll.AzsTestRowGet(R["FrameRate"]) == 0, f"{label}: a frame_rate Azimuth refuses (over 4 digits): 30 FPS")

    # Fixed choices: Azimuth's defaults when absent, its values when set.
    for row, key, values, default in (("VSync", "vsync", ["off", "on"], 1), ("Hdr", "hdr", ["off", "on"], 0),
                                      ("Aspect", "aspect", ["16:9", "display"], 0),
                                      ("AntiAliasing", "antialiasing", ["off", "msaax2", "msaax4", "coverage"], 2),
                                      ("AudioOutput", "audio", ["stereo", "system", "multichannel"], 0)):
        ini.with_keys(**{key: None})
        ok = dll.AzsTestRowGet(R[row]) == default
        for index, value in enumerate(values):
            ini.with_keys(**{key: value})
            ok = ok and dll.AzsTestRowGet(R[row]) == index
        ini.with_keys(**{key: "nonsense"})
        ok = ok and dll.AzsTestRowGet(R[row]) == default
        for index in reversed(range(len(values))):
            if index == default:
                continue  # "nonsense" shows as the default: choosing it is no change
            ok = ok and dll.AzsTestRowSet(R[row], index) == 1 and ini.section()[key] == values[index]
        ok = ok and dll.AzsTestRowSet(R[row], len(values)) == 0
        check(ok, f"{label}: {row}: {', '.join(values)} (absent or unknown: {values[default]})")
    check(dll.AzsTestRowCount(R["AntiAliasing"]) == 4
          and dll.AzsTestRowChoice(R["AntiAliasing"], 3, EN) == "Console (coverage)",
          f"{label}: Anti-aliasing offers the console's coverage sampling too")
    # Resolution: 360..4320 lines, shown as the choice at or below.
    shown = []
    for saved_lines, index in (("2160", 9), ("1080", 3), ("1000", 2), ("360", 0), ("5000", 3), (None, 3)):
        ini.with_keys(resolution=saved_lines)
        shown.append(dll.AzsTestRowGet(R["Resolution"]) == index)
    check(all(shown) and dll.AzsTestRowSet(R["Resolution"], 5) == 1 and ini.section()["resolution"] == "1440",
          f"{label}: Resolution: saved heights show as the choice at or below (Azimuth's 1080 when refused), "
          "1440 writes 1440")
    # Display mode in [launcher].
    ini.write(ini.original)
    display = ini.section("launcher").get("display", "fullscreen")
    want_display = "fullscreen" if display == "windowed" else "windowed"
    check(dll.AzsTestRowSet(R["Display"], 1 if want_display == "fullscreen" else 0) == 1
          and ini.section("launcher")["display"] == want_display and "display" not in ini.section(),
          f"{label}: Display mode is [launcher] display")

    # Graphics and the nine details.
    want = {0: ["low", "default", "default", "low", "low", "low", "low", "off", "default"],
            1: ["default"] * 9,
            2: ["high", "high", "high", "high", "high", "high", "default", "high", "16x"],
            3: ["ultra", "ultra", "ultra", "ultra", "ultra", "ultra", "default", "ultra", "16x"]}
    names = {0: "low", 1: "default", 2: "high", 3: "ultra"}
    expanded = {level: [names[dll.AzsTestPresetDetail(level, d)] for d in range(9)] for level in range(4)}
    expanded = {level: [("off" if d == 7 and v == "low" else "16x" if d == 8 and v == "high" else v)
                        for d, v in enumerate(values)] for level, values in expanded.items()}
    check(expanded == want, f"{label}: each preset's details, as Azimuth's 0x4140 moves a missing level towards Default")
    presets_ok = []
    for level in range(4):
        ini.with_keys(graphics=names[level], **{d: None for d in DETAILS})
        presets_ok.append(dll.AzsTestRowGet(R["Preset"]) == level and [dll.AzsTestDetailLevel(d) for d in range(9)] ==
                          [dll.AzsTestPresetDetail(level, d) for d in range(9)])
    check(all(presets_ok), f"{label}: graphics=<preset> without detail keys gives the preset's details")
    ini.with_keys(graphics="ultra", **dict(zip(DETAILS, want[3])))
    check(dll.AzsTestRowGet(R["Preset"]) == 3, f"{label}: graphics=ultra with Ultra's details shows Ultra")
    ini.with_keys(graphics="ultra", shadow_detail="low")
    check(dll.AzsTestRowGet(R["Preset"]) == 4 and dll.AzsTestRowGet(R["ShadowDetail"]) == 0,
          f"{label}: a detail key overrides the preset (as Azimuth reads it): Custom")
    ini.with_keys(graphics="custom", **dict(zip(DETAILS, want[2])))
    check(dll.AzsTestRowGet(R["Preset"]) == 4, f"{label}: graphics=custom shows Custom")
    ini.with_keys(graphics=None, **{d: None for d in DETAILS})
    check(dll.AzsTestRowGet(R["Preset"]) == 1 and all(dll.AzsTestDetailLevel(d) == 1 for d in range(9)),
          f"{label}: no graphics and no details: Default (Azimuth's defaults)")
    for level in (3, 0, 2):
        ok = dll.AzsTestRowSet(R["Preset"], level) == 1
        saved = ini.section()
        check(ok and saved["graphics"] == names[level] and [saved.get(d) for d in DETAILS] == want[level],
              f"{label}: Graphics {names[level].title()} writes graphics and the nine details")
    check(dll.AzsTestRowSet(R["Preset"], 2) == 0, f"{label}: choosing the same preset writes nothing")
    check(dll.AzsTestRowSet(R["ShadowDistance"], 2) == 1 and ini.section()["shadow_distance"] == "ultra"
          and ini.section()["graphics"] == "custom" and dll.AzsTestRowGet(R["Preset"]) == 4,
          f"{label}: Shadow distance Ultra writes shadow_distance=ultra and graphics=custom")
    check(dll.AzsTestRowSet(R["ShadowDistance"], 1) == 1 and ini.section()["graphics"] == "high"
          and dll.AzsTestRowGet(R["Preset"]) == 2, f"{label}: back to High's level: graphics=high again")
    detail_values = {"ShadowDetail": ["low", "default", "high", "ultra"], "ShadowDistance": ["default", "high", "ultra"],
                     "ShadowRedraw": ["default", "high", "ultra"], "Reflections": ["low", "default", "high", "ultra"],
                     "SceneryDetail": ["low", "default", "high", "ultra"], "CarDetail": ["low", "default", "high", "ultra"],
                     "Particles": ["low", "default"], "MotionBlur": ["off", "default", "high", "ultra"],
                     "TextureFiltering": ["default", "16x"]}
    for (row, values), key in zip(detail_values.items(), DETAILS):
        ok = dll.AzsTestRowCount(R[row]) == len(values)
        for index in range(len(values)):
            dll.AzsTestRowSet(R[row], index)
            ok = ok and ini.section()[key] == values[index] and dll.AzsTestRowGet(R[row]) == index
        check(ok, f"{label}: {row}: {', '.join(values)}")
    check(dll.AzsTestRowChoice(R["MotionBlur"], 0, EN) == "Off" and dll.AzsTestRowChoice(R["TextureFiltering"], 1, EN) == "16x"
          and dll.AzsTestRowChoice(R["ShadowRedraw"], 0, PL) == "Domyślna",
          f"{label}: detail captions (Off, 16x, Domyślna)")

    # Volume.
    ini.with_keys(volume=None)
    check(dll.AzsTestRowGet(R["Volume"]) == 100, f"{label}: without volume: 100 (Azimuth's default)")
    check(dll.AzsTestRowSet(R["Volume"], 35) == 1 and ini.section()["volume"] == "35"
          and dll.AzsTestRowGet(R["Volume"]) == 35, f"{label}: volume 35 is written and read back")
    check(dll.AzsTestRowSet(R["Volume"], 35) == 0 and dll.AzsTestRowSet(R["Volume"], 101) == 0
          and dll.AzsTestRowSet(R["Volume"], -1) == 0, f"{label}: the same volume, 101 and -1 write nothing")
    ini.with_keys(volume="250")
    check(dll.AzsTestRowGet(R["Volume"]) == 100 and dll.AzsTestRowSet(R["Volume"], 100) == 1
          and ini.section()["volume"] == "100", f"{label}: a volume Azimuth refuses shows 100; choosing 100 writes it")
    check(dll.AzsTestRowChoice(R["Volume"], 0, EN) is None, f"{label}: volume is a slider (no choices)")

    # Speech and text language.
    ini.write(ini.original)
    dll.AzsTestRowsFile(str(ini.path))
    saved = ini.section()
    for row, key in (("Speech", "speech"), ("Language", "language")):
        code = saved.get(key)
        index = LANGUAGES.index(code) + 1 if code in LANGUAGES else 0 if not code else len(LANGUAGES) + 1
        check(dll.AzsTestRowGet(R[row]) == index, f"{label}: {key}={code} shows choice {index}")
    check(dll.AzsTestRowCount(R["Speech"]) >= 13 and dll.AzsTestRowChoice(R["Speech"], 0, EN) == "Same as text"
          and dll.AzsTestRowChoice(R["Language"], 0, EN) == "Windows language"
          and dll.AzsTestRowChoice(R["Language"], 8, UK) == "Польська", f"{label}: language choices and captions")
    dll.AzsTestRowSet(R["Speech"], 8)
    speech = ini.section().get("speech")
    check(speech == "pl-PL" and dll.AzsTestRowSet(R["Speech"], 0) == 1 and "speech" not in ini.section(),
          f"{label}: Speech Polish writes speech=pl-PL; Same as text removes the key")
    ini.with_keys(language="xx-YY")
    dll.AzsTestRowsFile(str(ini.path))
    count = dll.AzsTestRowCount(R["Language"])
    check(count == len(LANGUAGES) + 2 and dll.AzsTestRowChoice(R["Language"], count - 1, EN) == "xx-YY"
          and dll.AzsTestRowGet(R["Language"]) == count - 1,
          f"{label}: a saved code that is not offered is kept as a choice of its own")
    check(dll.AzsTestRowSet(R["Language"], 10) == 1 and ini.section()["language"] == "ru-RU"
          and dll.AzsTestRowSet(R["Language"], count - 1) == 1 and ini.section()["language"] == "xx-YY",
          f"{label}: and can be chosen again")

    # Every other line kept; the file's encoding kept.
    ini.write(ini.original)
    dll.AzsTestRowsFile(str(ini.path))
    dll.AzsTestRowSet(R["Preset"], 0)
    dll.AzsTestRowSet(R["Hdr"], 1 - dll.AzsTestRowGet(R["Hdr"]))
    changed = {"graphics", "hdr", *DETAILS}
    now = [line.strip() for line in ini.read().splitlines()]
    kept = [line.strip() for line in ini.original.splitlines() if line.split("=", 1)[0] not in changed]
    check(all(line in now for line in kept), f"{label}: every other line of azimuth.ini is kept")
    data = ini.path.read_bytes()
    check((data[:2] == b"\xff\xfe") == ini.utf16, f"{label}: the file stays {'UTF-16' if ini.utf16 else 'UTF-8'}")
    (ini.path.parent / "old.ini").write_bytes(b"[launcher]\nframe_rate=60\n")
    dll.AzsTestRowsFile(str(ini.path.parent / "old.ini"))
    check(f"title-{title}".encode() in dll.AzsTestRowsLayout(), f"{label}: an azimuth.ini without the title section is refused")


def test_rows(dll, inis, output):
    print("rows")
    for game, label, source in inis:
        folder = output / "test-runs" / f"rows-{game}"
        shutil.rmtree(folder, ignore_errors=True)
        folder.mkdir(parents=True)
        data = source.read_bytes() if source and source.is_file() else None
        if data is None:
            if game != "fh2":
                skip(f"{label}: {source} not found")
                continue
            data = SAMPLE_INI.encode()
        print(f"  ({label}: a copy of {source if data != SAMPLE_INI.encode() else 'a sample azimuth.ini'})")
        rows_on(dll, Ini(folder / "azimuth.ini", data, TITLES[game]), label, game)
    dll.AzsTestRowsTitle("1a96545b", 30)


def test_language(output):
    print("language")
    folder = output / "test-runs" / "language"
    shutil.rmtree(folder, ignore_errors=True)
    names = ["English", "Polish", "Ukrainian"]
    # The plugin as installed, in mods\Azimuth Settings\plugins\, with and without the Ukrainian pack.
    test_dll = output / "tests" / "AzimuthSettingsTest.dll"
    with_pack = load_test_dll(output, lt.plugin_in_mods(lt.ukrainian_mods(folder / "languages"), MOD_NAME, test_dll))
    without = load_test_dll(output, lt.plugin_in_mods(lt.fresh_mods(folder / "languages", "plain"), MOD_NAME, test_dll))
    game = lt.FakeGame()
    cases = [("EN", 0, 0, EN), ("PL", 0, 0, PL), ("RU", 0, 0, EN), ("RU", 0, 1, UK), ("DE", 0, 1, EN),
             ("PL", 0, 1, PL), ("EN", 3, 0, UK), ("PL", 1, 0, EN), ("RU", 2, 1, PL), ("RU", 1, 1, EN)]
    for code, choice, pack, want in cases:
        game.set(code)
        plugin = with_pack if pack else without
        plugin.AzsTestLanguage(choice)
        got = plugin.AzsTestCurrentLanguage(game.base)
        check(got == want, f"game text {code}, [Captions] {['auto', 'en', 'pl', 'uk'][choice]}, "
                           f"{'the' if pack else 'no'} Ukrainian pack: {names[got]}")
        plugin.AzsTestLanguage(0)
    lt.check_selection(check, folder / "selection", test_dll, MOD_NAME, lambda path: load_test_dll(output, path),
                       lambda plugin, base: names[plugin.AzsTestCurrentLanguage(base)],
                       lambda code: "Polish" if code == "PL" else "Ukrainian" if code == "UK" else "English")


# ----------------------------------------------------------------- 7. plugin

def plugin_ini(ini):
    """plugin_test expects 120 FPS, stereo, volume 80, Default details and no speech key."""
    return ini.with_keys(frame_rate="120", audio="stereo", volume="80", speech=None, graphics="default",
                         **{d: "default" for d in DETAILS})


def test_plugin(output, games, hosts, pages):
    """games: {game: (exe, azimuth.ini source)}; pages: {(game, kind): xaml}."""
    print("plugin")
    exe = output / "tests" / "plugin_test.exe"
    runs = output / "test-runs"
    plan = [(m, "1.0.4", "fh2") for m in ("nohost", "disabled", "other", "badhost", "badgame", "unknown", "layout",
                                          "native", "eo", "polish", "ukrainian")]
    plan += [("native", "1.0.2", "fh2"), ("badhost", "1.0.2", "fh2")]
    plan += [(m, "1.0.4", "fh2ff") for m in ("native", "ukrainian", "badgame", "layout")]
    plan += [("native", "1.0.2", "fh2ff")]
    for mode, version, game in plan:
        shipping = mode in ("nohost", "disabled", "other")
        host = hosts.get(version)
        game_exe, ini_source = games.get(game, (None, None))
        name = f"{mode}{'' if version == '1.0.4' else '-' + version}{'' if game == 'fh2' else '-' + game}"
        if not shipping and not (host and game_exe and game_exe.is_file()):
            skip(f"plugin_test {name}: needs the game and Azimuth {version} executables")
            continue
        flow = mode in ("native", "eo", "polish", "ukrainian")
        video = pages.get((game, "eo_video" if mode == "eo" else "video"))
        audio = pages.get((game, "eo_audio" if mode == "eo" else "audio"))
        if flow and not (video and audio):
            skip(f"plugin_test {name}: needs the {'Extra Options 0.4.4 ' if mode == 'eo' else ''}pages")
            continue
        work = runs / name
        shutil.rmtree(work, ignore_errors=True)
        plugins = work / "mods" / MOD_NAME / "plugins"
        plugins.mkdir(parents=True)
        dll = output / ("mods/" + MOD_NAME + "/plugins/" + DLL_NAME if shipping else "tests/AzimuthSettingsTest.dll")
        shutil.copy2(dll, plugins / DLL_NAME)
        if mode == "other":  # a stand-in DLL with Azimuth Graphics Options' name
            shutil.copy2(output / "mods" / MOD_NAME / "plugins" / DLL_NAME, plugins / "AzimuthGraphicsOptions.dll")
        source = ini_source.read_bytes() if ini_source and ini_source.is_file() else SAMPLE_INI.encode()
        ini = Ini(work / "azimuth.ini", source, TITLES[game])
        if mode == "layout":
            ini.write("[launcher]\ndisplay=fullscreen\n[title-00000000]\nframe_rate=60\n")
        else:
            plugin_ini(ini)
        enabled = 0 if mode == "disabled" else 1
        (plugins / INI_NAME).write_text(
            f"[Video]\nEnabled={enabled}\n[Audio]\nEnabled={enabled}\n[Azimuth]\nLiveApply=1\nIni={ini.path}\n"
            f"[Captions]\nLanguage={'uk' if mode == 'ukrainian' else 'auto'}\n[Logging]\nEnabled=1\n", encoding="utf-8")
        args = [str(exe), mode, str(plugins / DLL_NAME), str(work)]
        if not shipping:
            args += [str(game_exe), str(host)]
            if flow:
                (work / "video.xaml").write_bytes(video)
                (work / "audio.xaml").write_bytes(audio)
                args += [str(work / "video.xaml"), str(work / "audio.xaml")]
        try:
            run = subprocess.run(args, capture_output=True, text=True, timeout=300, encoding="utf-8", errors="replace")
            print(run.stdout.rstrip())
            if run.stderr:
                print(run.stderr.rstrip())
            check(run.returncode == 0, f"plugin_test {name}")
        except subprocess.TimeoutExpired:
            check(False, f"plugin_test {name} finished in time")
        own_log = plugins / "AzimuthSettings.log"
        check(own_log.is_file(), f"{name}: AzimuthSettings.log is written")
        if flow and (work / "video_out.xaml").is_file() and (work / "audio_out.xaml").is_file():
            check_video(parse((work / "video_out.xaml").read_bytes()), f"{name} (through the hook)", mode == "eo")
            check_audio(parse((work / "audio_out.xaml").read_bytes()), f"{name} (through the hook)", mode == "eo")


def first_file(*paths):
    return next((p for p in paths if p and p.is_file()), paths[0])


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", type=Path, default=ROOT / "out")
    parser.add_argument("--zig", type=Path, default=Path(shutil.which("zig") or "zig"))
    parser.add_argument("--game", type=Path, default=Path.home() / "FH2")
    parser.add_argument("--ff-game", type=Path, default=FF_INSTALL)
    parser.add_argument("--azimuth", type=Path, help="Azimuth 1.0.4's release ZIP")
    parser.add_argument("--azimuth102", type=Path, help="Azimuth 1.0.2's release ZIP")
    parser.add_argument("--eo-release", type=Path, help="Extra Options 0.4.4's release folder (with winmm.dll)")
    parser.add_argument("--eo-source", type=Path, help="Extra Options 0.4.4's source tree")
    parser.add_argument("--upstream", type=Path, help="an Extra Options git clone (origin/main = 0.3.2)")
    options = parser.parse_args()
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    output = options.output.resolve()
    game = options.game
    eo_pr = game / "src" / "eo-pr"
    zips = {"1.0.4": options.azimuth or game / "azimuth-windows-1.0.4.zip",
            "1.0.2": options.azimuth102 or game / "azimuth-windows-1.0.2.zip"}
    release = options.eo_release or eo_pr / "upstream-0.4.4" / "FH2_Extra_Options_0.4.4-azimuth" / "Azimuth"
    source = options.eo_source or eo_pr / "wt-upstream"
    upstream = options.upstream or eo_pr / "ForzaHorizon2_ExtraOptions"
    fh2_exe = first_file(game / FH2_PACKAGE / EXE, FH2_INSTALL / EXE)
    fh2_ui = first_file(game / FH2_PACKAGE / "media" / "UI.zip", FH2_INSTALL / "media" / "UI.zip")
    ff_mount = options.ff_game / "game" / "base" / "Mount"
    ff_exe, ff_ui, ff_ini = ff_mount / EXE, ff_mount / "media" / "UI.zip", options.ff_game / "data" / "azimuth.ini"
    hosts = {}
    for version, path in zips.items():
        if path.is_file():
            exe = output / "test-runs" / f"azimuth-{version}" / "azimuth.exe"
            exe.parent.mkdir(parents=True, exist_ok=True)
            with zipfile.ZipFile(path) as archive:
                exe.write_bytes(archive.read("azimuth.exe"))
            hosts[version] = exe
    test_package(output)
    dll = load_test_dll(output)
    test_host(dll, [("1.0.4", "azimuth.exe (1.0.4 release)", hosts.get("1.0.4")),
                    ("1.0.2", "azimuth.exe (1.0.2 release)", hosts.get("1.0.2")),
                    ("1.0.2", "branded Forza Horizon 2.exe (1.0.2)", game / "Forza Horizon 2.exe")])
    fh2_pe, fh2_sites = test_game(dll, fh2_exe, "Forza Horizon 2", "fh2")
    azimuth = PE(hosts["1.0.4"]).image() if "1.0.4" in hosts else None
    test_game(dll, ff_exe, "Fast & Furious", "fh2ff", fh2_sites, fh2_pe, azimuth)
    eo044 = test_eo(options.zig, release, source, output)
    video, audio, eo_pages, ff_pages = test_xaml(dll, options.zig, fh2_ui, ff_ui, eo044, upstream, output)
    test_rows(dll, [("fh2", "Forza Horizon 2", game / "data" / "azimuth.ini"),
                    ("fh2ff", "Fast & Furious", ff_ini)], output)
    test_language(output)
    pages = {}
    if video:
        pages.update({("fh2", "video"): video, ("fh2", "audio"): audio})
    for page, kind in ((VIDEO, "eo_video"), (AUDIO_PAGE, "eo_audio")):
        if eo_pages and page in eo_pages:
            pages[("fh2", kind)] = eo_pages[page]
    if ff_pages:
        pages.update({("fh2ff", "video"): ff_pages[0], ("fh2ff", "audio"): ff_pages[1]})
    test_plugin(output, {"fh2": (fh2_exe, game / "data" / "azimuth.ini"), "fh2ff": (ff_exe, ff_ini)}, hosts, pages)
    print(f"{len(failures)} failure(s)" if failures else "all tests passed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
