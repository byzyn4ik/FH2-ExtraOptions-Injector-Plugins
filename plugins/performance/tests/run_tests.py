"""Offline tests of the Performance plugin (no game run, nothing installed).

Usage: py tests/run_tests.py [--output out] [--zig zig.exe] [--game <FH2 folder>]
                             [--azimuth <azimuth-windows-1.0.4.zip>] [--upstream <Extra Options 0.4.4 checkout>]
                             [--release <Extra Options 0.4.4 release's Azimuth folder>]
                             [--graphics <Azimuth Graphics Options source>] [--warp]

Real files are only read (the game's executable and UI.zip, Azimuth's ZIP, the
Extra Options release and source, the Azimuth Graphics Options source); the
game executable is mapped into this process' own memory and patched there.
Defaults: --game ~/FH2, --azimuth <game>/azimuth-windows-1.0.4.zip,
--upstream <game>/src/eo-pr/wt-upstream,
--release <game>/src/eo-pr/upstream-0.4.4/FH2_Extra_Options_0.4.4-azimuth/Azimuth,
--graphics plugins/azimuth-graphics-options of this repository.
A missing input skips the tests that need it.

1. package   mod.json as Extra Options 0.4.4 validates it, the mod layout, the
             DLL (PE32+ x64, the one export, only Windows imports, version
             resource, loads), the INI.
2. azimuth   azimuth.exe 1.0.4: how it makes and presents its swap chain
             (CreateDXGIFactory2, CreateSwapChainForHwnd, flip discard, the
             waitable object, IDXGISwapChain::Present after its own copy is
             submitted on the queue); its game window class.
3. release   Extra Options 0.4.4 (release and source) does not hook DXGI or
             D3D12; it patches the XAML resource call the row chains.
4. options   Performance.ini parsing (defaults, valid and invalid values, the
             CSV path) and ShowFps written by the row.
5. guards    FH2PluginInitialize refuses a bad context, does nothing off
             Azimuth, hooks nothing with ShowFps=0 and PerfLog=0.
6. game      the game executable passes the row's guards, alone and as Extra
             Options patches it; changed bytes are refused; the plugin chains
             the resource call behind Extra Options' thunk.
7. xaml      the real Video page (UI.zip): the row alone, after Extra
             Options 0.4.4's own edit (its source, built here) and with the
             Azimuth Graphics Options plugin's edit (its source, built here) in
             either order: well-formed XML, every row once, both plugins' checks
             pass.
8. captions  the row's captions in the game's 16 language codes, from the
             game's language setting.
9. d3d12     tests/d3d12_test.cpp in two processes: late (the plugin attaches
             after the device and the swap chain exist) and early (before any
             device); then the Nitrous gauge (a fake NitrousStatus, with and
             without Fast & Furious' SpireHUD.zip). WARP when there is no GPU
             (--warp forces it).
10. nitrous  the Nitrous gauge's settings ([Nitrous]), its layout (values to
             quads: Fast & Furious' bar and outline on the speedometer, the
             boost), inflate against zlib, the swatchbin detile against
             the upscale tool's, finding SpireHUD.zip, mods/Nitrous.
"""

import argparse
import configparser
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(ROOT.parents[1] / "common"))
import language_testing as lt  # noqa: E402
MOD_NAME = "Performance"
DLL_NAME = "Performance.dll"
INI_NAME = "Performance.ini"
TEST_DLL = "PerformanceTest.dll"
SYSTEM_DLLS = re.compile(r"^(kernel32|user32|d3d12|dxgi|msvcrt|api-ms-win-[a-z0-9-]+)\.dll$", re.I)
GAME_EXE = Path("Anthem_1.0.0.64_x64__8wekyb3d8bbwe/Mount/forza_durango_release_final.exe")
UI_ZIP = Path("Anthem_1.0.0.64_x64__8wekyb3d8bbwe/Mount/media/UI.zip")
VIDEO_PAGE = "Scenes/Anthem/A_VideoOptions.xaml"
CAPTIONS = json.loads((ROOT / "src" / "translations.json").read_text(encoding="utf-8"))

IMAGE_SIZE = 0x3A18000
RESOURCE_CALL, GET_RESOURCE, EO_THUNK = 0x16EBB37, 0xAABEC0, 0x13FE354
TEXT_TABLES = 0x3662990
AZIMUTH_104 = 0x6AE393EB

failures = []


def check(ok, what):
    print(("  ok   " if ok else "  FAIL ") + what)
    if not ok:
        failures.append(what)


def skip(what):
    print("  skip " + what)


kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
kernel32.VirtualAlloc.restype = ctypes.c_void_p
kernel32.VirtualAlloc.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint32, ctypes.c_uint32]
kernel32.VirtualFree.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint32]


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
            self.sections.append((name.rstrip(b"\0").decode(), va, vsize, rsize, raw))
        directories = optional + 112
        self.exports_rva = struct.unpack_from("<I", d, directories)[0]
        self.imports_rva = struct.unpack_from("<I", d, directories + 8)[0]
        self.resources_rva = struct.unpack_from("<I", d, directories + 16)[0]
        self.relocations_rva, self.relocations_size = struct.unpack_from("<II", d, directories + 40)
        self._relocations = None

    def offset(self, rva):
        for _, va, vsize, rsize, raw in self.sections:
            if va <= rva < va + max(vsize, rsize):
                return rva - va + raw
        raise ValueError(hex(rva))

    def read(self, rva, size):
        o = self.offset(rva)
        return self.data[o:o + size]

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
        """{dll: [function name or #ordinal]}"""
        result, o = {}, self.offset(self.imports_rva) if self.imports_rva else None
        while o is not None:
            lookup, _, _, name, first = struct.unpack_from("<IIIII", self.data, o)
            if not name:
                break
            functions = []
            thunk = lookup or first
            for i in range(100000):
                value = struct.unpack_from("<Q", self.data, self.offset(thunk + 8 * i))[0]
                if not value:
                    break
                functions.append("#%d" % (value & 0xFFFF) if value >> 63 else self.text(value + 2))
            result[self.text(name)] = functions
            o += 20
        return result

    def relocations(self):
        if self._relocations is None:
            table = self.read(self.relocations_rva, self.relocations_size)
            found, at = [], 0
            while at + 8 <= len(table):
                page, size = struct.unpack_from("<II", table, at)
                if size < 8:
                    break
                for (entry,) in struct.iter_unpack("<H", table[at + 8:at + size]):
                    if entry >> 12 == 10:  # IMAGE_REL_BASED_DIR64
                        found.append(page + (entry & 0xFFF))
                at += size
            self._relocations = found
        return self._relocations

    def map(self):
        """The image laid out and relocated as the loader maps it, in private RWX memory; returns its address."""
        base = kernel32.VirtualAlloc(None, self.image_size, 0x3000, 0x40)
        if not base:
            raise MemoryError("VirtualAlloc")
        image = bytearray(self.image_size)
        image[:self.headers_size] = self.data[:self.headers_size]
        for _, va, vsize, rsize, raw in self.sections:
            size = min(rsize, vsize) if vsize else rsize
            image[va:va + size] = self.data[raw:raw + size]
        delta = base - self.image_base
        for rva in self.relocations():
            value = int.from_bytes(image[rva:rva + 8], "little")
            image[rva:rva + 8] = ((value + delta) & 0xFFFFFFFFFFFFFFFF).to_bytes(8, "little")
        ctypes.memmove(base, bytes(image), self.image_size)
        return base


def peek(address, size):
    return ctypes.string_at(address, size)


def poke(address, data):
    ctypes.memmove(address, data, len(data))


# --------------------------------------------------------------- 1. package

def valid_mod_id(value):
    # features/addons.c valid_id() in Extra Options 0.4.4.
    if not isinstance(value, str) or not 0 < len(value) <= 128:
        return False
    for i, ch in enumerate(value):
        if not (("a" <= ch <= "z") or ch.isdigit() and ch.isascii() or ch == "-"):
            return False
        if ch == "-" and (i == 0 or i == len(value) - 1 or value[i - 1] == "-"):
            return False
    return True


def test_package(output):
    print("package")
    mod = output / "mods" / MOD_NAME
    manifest = json.loads((mod / "mod.json").read_text(encoding="utf-8"))
    check(manifest.get("format_version") == 1 and manifest.get("id") == "performance" and valid_mod_id(manifest["id"]),
          f"mod.json: format_version 1 and the id performance ({manifest.get('id')})")
    check(manifest.get("name") == MOD_NAME, "mod.json: the name Performance")
    check(isinstance(manifest.get("dependencies"), list) and "media" not in manifest and "database" not in manifest,
          "mod.json: dependencies list, no media or database (nothing for the media merge)")
    files = sorted(str(p.relative_to(mod)).replace("\\", "/") for p in mod.rglob("*") if p.is_file())
    check(files == ["mod.json", "plugins/" + DLL_NAME, "plugins/" + INI_NAME],
          f"out/mods/{MOD_NAME}: mod.json, plugins/{DLL_NAME}, plugins/{INI_NAME} ({files})")
    pe = PE(mod / "plugins" / DLL_NAME)
    check(pe.machine == 0x8664 and pe.magic == 0x20B and pe.characteristics & 0x2000, "the DLL is a PE32+ x64 DLL")
    check(pe.exports() == ["FH2PluginInitialize"], f"the one export is FH2PluginInitialize ({pe.exports()})")
    imports = pe.imports()
    check(all(SYSTEM_DLLS.match(name) for name in imports), f"only Windows DLLs are imported ({', '.join(imports)})")
    check(not any(n.lower().startswith("d3dcompiler") for n in imports) and b"D3DCompile" not in pe.data,
          "no shader compiler at run time (the shaders are built in)")
    check(b"DXBC" in pe.data, "the overlay's compiled shaders (DXBC) are in the DLL")
    version = manifest.get("version", "")
    check(bool(pe.resources_rva) and ("FileVersion\0" + version).encode("utf-16le") in pe.data.replace(b"\0\0\0", b"\0"),
          f"version resource with FileVersion {version}")
    library = ctypes.WinDLL(str(mod / "plugins" / DLL_NAME))
    check(bool(library.FH2PluginInitialize), "the DLL loads and FH2PluginInitialize resolves")
    test_pe = PE(output / "tests" / TEST_DLL)
    check("FH2PluginInitialize" in test_pe.exports() and "PerfTestStats" in test_pe.exports(),
          "the test DLL has the plugin entry and the test entry points")
    raw = (mod / "plugins" / INI_NAME).read_bytes()
    check(raw.isascii(), "Performance.ini is ASCII (WritePrivateProfileString keeps it as it is)")
    ini = configparser.ConfigParser()
    ini.read(mod / "plugins" / INI_NAME, encoding="utf-8")
    values = {k: ini.get("Performance", k, fallback=None) for k in ("ShowFps", "PerfLog", "PerfLogFile", "MenuRow")}
    check(values == {"ShowFps": "1", "PerfLog": "0", "PerfLogFile": "", "MenuRow": "1"},
          f"Performance.ini: [Performance] ShowFps=1, PerfLog=0, PerfLogFile= (beside it), MenuRow=1 ({values})")
    nos = {k: ini.get("Nitrous", k, fallback=None) for k in ("Gauge", "ScreenEffect", "OnlyWhileDriving", "GaugeScale",
                                                             "GaugeOffsetX", "GaugeOffsetY", "FastFuriousMedia")}
    check(nos == {"Gauge": "1", "ScreenEffect": None, "OnlyWhileDriving": "1", "GaugeScale": "1", "GaugeOffsetX": "0",
                  "GaugeOffsetY": "0", "FastFuriousMedia": ""},
          f"Performance.ini: [Nitrous] Gauge=1, no ScreenEffect (dropped), OnlyWhileDriving=1, scale 1, no offsets ({nos})")


# ---------------------------------------------------------------- 2. azimuth

def test_azimuth(azimuth_zip):
    print("azimuth")
    if not azimuth_zip.is_file():
        skip(f"{azimuth_zip} not found")
        return
    with zipfile.ZipFile(azimuth_zip) as archive:
        exe = next((n for n in archive.namelist() if n.lower().endswith("azimuth.exe")), None)
        data = archive.read(exe) if exe else b""
    if not data:
        check(False, "the ZIP holds azimuth.exe")
        return
    pe = PE(data=data)
    check(pe.timestamp == AZIMUTH_104, f"azimuth.exe is 1.0.4 (TimeDateStamp {pe.timestamp:#x})")
    imports = {k.lower(): v for k, v in pe.imports().items()}
    check(imports.get("dxgi.dll") == ["CreateDXGIFactory2"], f"DXGI: CreateDXGIFactory2 only ({imports.get('dxgi.dll')})")
    check("#101" in imports.get("d3d12.dll", []), "D3D12: D3D12CreateDevice by ordinal 101")
    check(not any(n.lower().startswith("d3d11") for n in imports), "no D3D11")
    code = lambda rva, size: pe.read(rva, size)
    # The swap chain description written before CreateSwapChainForHwnd.
    # mov [rbp+34h],40h; mov [rbp+2Ch],4; mov [rbp+24h],3; mov [rbp+20h],20h; mov [rbp+30h],3; mov [rbp+28h],r12d
    check(code(0x4BDCE, 0x27) == bytes.fromhex("c7453440000000" "c7452c04000000" "c7452403000000" "c7452020000000"
                                                "c7453003000000" "44896528"),
          "its swap chain: Flags FRAME_LATENCY_WAITABLE_OBJECT, SwapEffect FLIP_DISCARD, 3 buffers, "
          "render-target output, alpha IGNORE, scaling STRETCH")
    check(code(0x4BE71, 7) == bytes.fromhex("814d3400080000") and code(0x4BE59, 6) == bytes.fromhex("ff90e0000000"),
          "ALLOW_TEARING when IDXGIFactory5::CheckFeatureSupport says so")
    check(code(0x4BF33, 3) == bytes.fromhex("ff5078"), "made with IDXGIFactory2::CreateSwapChainForHwnd (slot 15)")
    check(code(0x4BF8F, 14) == bytes.fromhex("41b803000000488b542450ff5040"),
          "then MakeWindowAssociation(NO_WINDOW_CHANGES | NO_ALT_ENTER)")
    check(code(0x4BFAC, 6) == bytes.fromhex("ff90f8000000") and code(0x4BFC4, 6) == bytes.fromhex("ff9008010000"),
          "SetMaximumFrameLatency(1) and GetFrameLatencyWaitableObject on IDXGISwapChain2+")
    check(code(0x4D6CC, 3) == bytes.fromhex("ff5050") and code(0x4D6E2, 3) == bytes.fromhex("ff5070") and
          code(0x4D6F4, 6) == bytes.fromhex("ff9020010000") and code(0x4D717, 3) == bytes.fromhex("ff5040"),
          "its present: ExecuteCommandLists then Signal on the queue, GetCurrentBackBufferIndex, "
          "then IDXGISwapChain::Present (slot 8), on one thread")
    check(code(0x4DBD6, 3) == bytes.fromhex("ff5040"), "the other Present site is slot 8 too (no Present1)")
    check("Azimuth.Game.Win32".encode("utf-16le") in data, "its game window class is Azimuth.Game.Win32")


# ---------------------------------------------------------------- 3. release

def test_release(release, upstream):
    print("release")
    winmm = release / "winmm.dll" if release else None
    if not winmm or not winmm.is_file():
        skip(f"Extra Options 0.4.4 release not found ({release})")
    else:
        data = winmm.read_bytes()
        pe = PE(data=data)
        check(b"0.4.4\0" in data, "winmm.dll is Extra Options 0.4.4")
        imports = [n.lower() for n in pe.imports()]
        check(not any(n.startswith(("dxgi", "d3d")) for n in imports) and
              not any(s in data for s in (b"dxgi", b"DXGI", b"d3d12", b"D3D12", b"Present1", b"SwapChain")),
              f"it imports and names no DXGI or D3D12: it does not hook Present ({', '.join(imports)})")
        call = struct.pack("<i", GET_RESOURCE - (RESOURCE_CALL + 5))
        thunk_call = struct.pack("<i", EO_THUNK - (RESOURCE_CALL + 5))
        check(b"\xe8" + call in data and b"\xe8" + thunk_call in data,
              "its patch data turns the XAML resource call into call 0x13fe354 (its thunk)")
        check(b"<!-- EO:StandaloneOptionsPage -->" in data, "it skips plugins' standalone pages")
        check(b"FH2PluginInitialize" in data, "it loads mod plugins (FH2PluginInitialize)")
    if not upstream or not (upstream / "features").is_dir():
        skip(f"Extra Options 0.4.4 source not found ({upstream})")
        return
    found = []
    for path in list(upstream.rglob("*.c")) + list(upstream.rglob("*.h")):
        text = path.read_text(encoding="utf-8", errors="replace")
        if re.search(r"IDXGI|dxgi\.h|d3d12\.h|ID3D12|->Present\b|Present1|ExecuteCommandLists", text):
            found.append(str(path.relative_to(upstream)))
    check(not found, f"its source uses no DXGI/D3D12 interfaces ({found})")


# --------------------------------------------------------------- test DLL

class TestOptions(ctypes.Structure):
    _fields_ = [("show", ctypes.c_uint), ("log", ctypes.c_uint), ("row", ctypes.c_uint),
                ("csv", ctypes.c_wchar * 520), ("warnings", ctypes.c_char * 512)]


def load_test_dll(output, path=None):
    dll = ctypes.CDLL(str(path or output / "tests" / TEST_DLL))
    dll.PerfTestReadOptions.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.POINTER(TestOptions)]
    dll.PerfTestWriteShow.argtypes = [ctypes.c_wchar_p, ctypes.c_uint]
    dll.PerfTestWaitAttach.argtypes = [ctypes.c_uint]
    dll.PerfTestCheckGame.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    dll.PerfTestCheckGame.restype = ctypes.c_char_p
    dll.PerfTestExtendXaml.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t,
                                       ctypes.c_char_p]
    dll.PerfTestExtendXaml.restype = ctypes.c_size_t
    dll.PerfTestWhy.argtypes = [ctypes.c_char_p, ctypes.c_size_t]
    dll.PerfTestWhy.restype = ctypes.c_char_p
    dll.PerfTestCaption.argtypes = [ctypes.c_char_p, ctypes.c_uint]
    dll.PerfTestCaption.restype = ctypes.c_wchar_p
    dll.PerfTestCaptionCode.argtypes = [ctypes.c_uint]
    dll.PerfTestCaptionCode.restype = ctypes.c_char_p
    dll.PerfTestGameLanguage.argtypes = [ctypes.c_void_p]
    dll.PerfTestGameLanguage.restype = ctypes.c_char_p
    return dll


# ---------------------------------------------------------------- 4. options

def test_options(dll, work):
    print("options")
    folder = work / "options"
    folder.mkdir(parents=True, exist_ok=True)
    ini = folder / INI_NAME

    def read(text):
        if text is None:
            ini.unlink(missing_ok=True)
        else:
            ini.write_text(text, encoding="ascii")
        out = TestOptions()
        dll.PerfTestReadOptions(str(ini), str(folder), ctypes.byref(out))
        return out

    o = read(None)
    check((o.show, o.log, o.row, o.csv) == (1, 0, 1, str(folder / "fh2-perf.csv")) and not o.warnings,
          "no INI: ShowFps 1, PerfLog 0, MenuRow 1, the log beside the plugin")
    shipped = (ROOT / "mod" / INI_NAME).read_text(encoding="ascii")
    o = read(shipped)
    check((o.show, o.log, o.row) == (1, 0, 1) and not o.warnings, "the shipped INI: the same, no warnings")
    for value in (0, 1, 2):
        o = read(f"[Performance]\nShowFps={value}\n")
        check(o.show == value and not o.warnings, f"ShowFps={value}")
    o = read("[Performance]\nShowFps=3\nPerfLog=2\nMenuRow=yes\n")
    check((o.show, o.log, o.row) == (1, 0, 1) and b"ShowFps=3" in o.warnings and b"PerfLog=2" in o.warnings
          and b"MenuRow=yes" in o.warnings, f"invalid values keep the defaults and are named ({o.warnings.decode()})")
    o = read("[Performance]\nShowFps = 2 \nPerfLog=1\nMenuRow=0\n")
    check((o.show, o.log, o.row) == (2, 1, 0), "spaces around values; PerfLog=1; MenuRow=0")
    o = read("[Performance]\nPerfLogFile=logs\\perf.csv\n")
    check(o.csv == str(folder / "logs" / "perf.csv"), f"a relative PerfLogFile is beside the plugin ({o.csv})")
    o = read("[Performance]\nPerfLogFile=D:\\perf\\fps.csv\n")
    check(o.csv == "D:\\perf\\fps.csv", f"an absolute PerfLogFile is kept ({o.csv})")
    read(shipped)
    check(dll.PerfTestWriteShow(str(ini), 2) == 1, "ShowFps=2 written (the Video page row)")
    after = ini.read_text(encoding="ascii")
    check(after.replace("ShowFps=2", "ShowFps=1") == shipped and read(after).show == 2,
          "only the ShowFps line changes; comments and other keys are kept")


# ----------------------------------------------------------------- 5. guards

LOG = ctypes.CFUNCTYPE(None, ctypes.c_char_p)


class Context(ctypes.Structure):
    _fields_ = [("size", ctypes.c_uint32), ("api_version", ctypes.c_uint32), ("adapter", ctypes.c_uint32),
                ("game_image_size", ctypes.c_uint32), ("game_image", ctypes.c_void_p),
                ("plugin_module", ctypes.c_void_p), ("plugins_directory", ctypes.c_wchar_p),
                ("plugin_path", ctypes.c_wchar_p), ("extra_options_version", ctypes.c_char_p),
                ("log_utf8", LOG)]


class Loader:
    def __init__(self, dll, folder):
        self.dll, self.folder, self.lines = dll, folder, []
        self.callback = LOG(lambda line: self.lines.append(line.decode("utf-8", "replace")))
        dll.FH2PluginInitialize.argtypes = [ctypes.c_void_p]
        dll.FH2PluginInitialize.restype = wintypes.BOOL

    def initialize(self, ini_text, adapter=1, game=None, size=0, bad_size=False, path=True):
        self.folder.mkdir(parents=True, exist_ok=True)
        (self.folder / INI_NAME).write_text(ini_text, encoding="ascii")
        self.context = Context(ctypes.sizeof(Context) - (8 if bad_size else 0), 1, adapter, size, game, None,
                               str(self.folder), str(self.folder / DLL_NAME) if path else None, b"0.4.4",
                               self.callback)
        self.lines.clear()
        return self.dll.FH2PluginInitialize(ctypes.byref(self.context))

    def said(self, text):
        return any(text in line for line in self.lines)


def test_guards(loader):
    print("guards")
    check(loader.initialize("[Performance]\nShowFps=1\n", bad_size=True) == 0 and not loader.lines,
          "a context of another size: FALSE, nothing logged or installed")
    check(loader.initialize("[Performance]\nShowFps=1\n", path=False) == 0 and loader.said("no plugin path"),
          "no plugin path: FALSE, logged")
    ok = loader.initialize("[Performance]\nShowFps=2\nPerfLog=1\n", adapter=2)
    check(ok == 1 and loader.said("does not run on Azimuth") and loader.dll.PerfTestWaitAttach(300) == -1,
          "not Azimuth (Mezcal): TRUE, logged, no attach")
    version = json.loads((ROOT / "mod" / "mod.json").read_text(encoding="utf-8"))["version"]
    check(loader.said(f"plugin {version} for Extra Options 0.4.4; FPS counter detailed, performance log on"),
          "the log line names the version and the settings")
    # Another game (Forza Motorsport 5 1.0.0.20's PE header and SizeOfImage): the Video page row is refused
    # before anything past the header is read; the plugin still returns TRUE (the counter works without it).
    header = bytearray(0x1000)
    header[0:2], header[0x3C:0x40], header[0x80:0x84] = b"MZ", struct.pack("<I", 0x80), b"PE\0\0"
    header[0x88:0x8C], header[0x80 + 24 + 56:0x80 + 24 + 60] = struct.pack("<I", 0x53D8D1F2), struct.pack("<I", 0x279D000)
    image = ctypes.create_string_buffer(bytes(header), len(header))
    ok = loader.initialize("[Performance]\nShowFps=0\nPerfLog=0\nMenuRow=1\n", game=ctypes.addressof(image),
                           size=0x279D000)
    check(ok == 1 and loader.said("not the supported size") and loader.said("no Video page row (the INI still works)")
          and image.raw[:len(header)] == bytes(header),
          "an unknown game image: TRUE, no Video page row, the image untouched")


# ------------------------------------------------------------------- 6. game

def eo_patch(base, reader_address):
    """Extra Options 0.4.4's resource patch: its thunk at 0x13fe354 and the call pointed at it."""
    poke(base + EO_THUNK, b"\x48\xb8" + struct.pack("<Q", reader_address) + b"\xff\xe0")
    poke(base + RESOURCE_CALL, b"\xe8" + struct.pack("<i", EO_THUNK - (RESOURCE_CALL + 5)))


def call_target(base, rva):
    return base + rva + 5 + struct.unpack("<i", peek(base + rva + 1, 4))[0]


def test_game(dll, loader, game_exe):
    print("game")
    if not game_exe.is_file():
        skip(f"{game_exe} not found")
        return False
    pe = PE(game_exe)
    check(pe.image_size == IMAGE_SIZE, "the game executable is the inspected build (image size 0x3a18000)")
    base = pe.map()
    result = dll.PerfTestCheckGame(base, IMAGE_SIZE)
    check(result == b"target: the game's resource reader 0xaabec0 (Extra Options' hook is not there)",
          f"stock game: the guards pass ({result.decode()})")
    hook = ctypes.cast(dll.PerfTestStats, ctypes.c_void_p).value  # any executable code outside the image
    eo_patch(base, hook)
    result = dll.PerfTestCheckGame(base, IMAGE_SIZE)
    check(result.startswith(b"target: Extra Options' resource hook (thunk 0x13fe354 -> "),
          f"as Extra Options patches it: {result.decode()}")

    def refused(rva, data, expect, what):
        old = peek(base + rva, len(data))
        poke(base + rva, data)
        problem = dll.PerfTestCheckGame(base, IMAGE_SIZE)
        poke(base + rva, old)
        check(problem is not None and expect in problem and not problem.startswith(b"target:"),
              f"{what} is refused ({problem.decode() if problem else None})")

    refused(0x3229C0, b"\xcc", b"Allocate at 0x3229c0", "a changed native allocator")
    refused(0xED81F0, b"\xcc", b"IntProperty", "a changed int property descriptor")
    refused(0xED825B, b"\x90", b"Subtitles property template", "a changed Subtitles template")
    refused(0xECA808 + 3, b"\0\0\0\0", b"Video page type", "a Video page registration elsewhere")
    refused(RESOURCE_CALL + 5, b"\x90", b"resource call at 0x16ebb37", "changed code after the resource call")
    refused(RESOURCE_CALL, b"\x90", b"resource call at 0x16ebb37", "a resource site that is no longer a call")
    refused(EO_THUNK, b"\x90\x90", b"not Extra Options' thunk", "a resource call to a changed thunk")
    problem = dll.PerfTestCheckGame(base, IMAGE_SIZE - 0x1000)
    check(problem is not None and b"supported size" in problem, "another image size is refused")

    # The plugin on the mapped game (ShowFps=0, PerfLog=0: nothing is hooked in this process).
    previous = call_target(base, RESOURCE_CALL)
    ok = loader.initialize("[Performance]\nShowFps=0\nPerfLog=0\nMenuRow=1\n", game=base, size=IMAGE_SIZE)
    check(ok == 1 and loader.said("Video page row ready; the resource call at 0x16ebb37 is chained to Extra Options'"),
          "FH2PluginInitialize chains the resource call behind Extra Options' thunk")
    jump = call_target(base, RESOURCE_CALL)
    handler = struct.unpack("<Q", peek(jump + 6, 8))[0] if peek(jump, 6) == b"\xff\x25\0\0\0\0" else 0
    module = ctypes.c_void_p()
    kernel32.GetModuleHandleExW.argtypes = [ctypes.c_uint32, ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
    in_plugin = handler and kernel32.GetModuleHandleExW(6, handler, ctypes.byref(module)) and \
        module.value == loader.dll._handle
    check(jump != previous and abs(jump - (base + RESOURCE_CALL)) < 0x7FF00000 and bool(in_plugin),
          "the call goes to a near jump into the plugin's handler")
    check(peek(base + EO_THUNK, 2) == b"\x48\xb8", "Extra Options' thunk is left as it was (called by the handler)")
    check(loader.said("ShowFps=0 and PerfLog=0: nothing is hooked until the counter is turned on on the Video page")
          and loader.dll.PerfTestWaitAttach(300) == -1, "ShowFps=0 and PerfLog=0: no attach")
    return True


# ------------------------------------------------------------------- 7. xaml

def parse(data, label):
    try:
        return ET.fromstring(data.decode("utf-8-sig"))
    except (ET.ParseError, UnicodeDecodeError) as error:
        check(False, f"{label}: well-formed XML ({error})")
        return None


def extend(dll, xaml, code=b"EN"):
    out = ctypes.create_string_buffer(len(xaml) + 0x4000)
    n = dll.PerfTestExtendXaml(xaml, len(xaml), out, len(out), code)
    return out.raw[:n] if n else b""


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


def eo_extend(library, xaml):
    out = ctypes.create_string_buffer(len(xaml) + 0x20000)
    n = library.EoExtend(xaml, len(xaml), out, len(out))
    return out.raw[:n]


def build_eo(zig, source, output):
    if not source or not (source / "features" / "video_options_xaml.c").is_file():
        skip(f"the Extra Options 0.4.4 source not found ({source})")
        return None
    files = {}
    for folder in (source, source / "features", source / "adapters"):
        for header in folder.glob("*.h"):
            files[header.name] = header.read_bytes()
    for name in ("support.c", "features/video_options_xaml.c", "features/video_options_text.c"):
        files[Path(name).name] = (source / name).read_bytes()
    library = build_eo_xaml(zig, files, output / "obj" / "eo-0.4.4")
    check(library is not None, "Extra Options 0.4.4's settings pages code builds for the test")
    return library


def build_graphics(zig, source, output):
    """The Azimuth Graphics Options plugin's test build, from its source (read only)."""
    src = source / "src" if source else None
    names = ["common.cpp", "rows.cpp", "language.cpp", "live.cpp", "call_chain.cpp", "settings_pages.cpp", "plugin.cpp"]
    if not src or not all((src / n).is_file() for n in names):
        skip(f"the Azimuth Graphics Options source not found ({source})")
        return None
    folder = output / "obj" / "graphics"
    folder.mkdir(parents=True, exist_ok=True)
    dll = folder / "AzimuthGraphicsOptionsTest.dll"
    command = [str(zig), "c++", "-target", "x86_64-windows-gnu", "-std=c++17", "-O2", "-w", "-DUNICODE", "-D_UNICODE",
               "-fno-sanitize=all", "-I", str(ROOT.parents[1] / "common" / "include"), "-shared", "-s",
               "-DAZG_TEST_EXPORTS", *[str(src / n) for n in names], "-luser32",
               "-Wl,--out-implib," + str(folder / "graphics.lib"), "-o", str(dll)]
    run = subprocess.run(command, capture_output=True, text=True)
    if run.returncode:
        print(run.stderr)
        check(False, "the Azimuth Graphics Options plugin builds for the test")
        return None
    library = ctypes.CDLL(str(dll))
    library.AzgTestExtendXaml.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_void_p,
                                          ctypes.c_size_t, ctypes.c_int, ctypes.c_int]
    library.AzgTestExtendXaml.restype = ctypes.c_size_t
    library.AzgTestSetFrameRates.argtypes = [ctypes.POINTER(ctypes.c_int), ctypes.c_int]
    rates = (ctypes.c_int * 3)(30, 60, 120)
    library.AzgTestSetFrameRates(rates, 3)
    check(True, "the Azimuth Graphics Options plugin's test build is built from its source")
    return library


def graphics_extend(library, xaml):
    out = ctypes.create_string_buffer(len(xaml) + 0x10000)
    n = library.AzgTestExtendXaml(0, xaml, len(xaml), out, len(out), 0, 31)
    return out.raw[:n] if n else b""


AZG_ROWS = ["GfxFrameRate", "GfxVSync", "GfxResolution", "GfxDisplay", "GfxPreset", "GfxShadows",
            "GfxTextureFiltering", "GfxAntiAliasing", "GfxHdr"]


def check_page(text, label, eo_rows=False, graphics=False):
    root = parse(text, label)
    if root is None:
        return
    page = text.decode("utf-8-sig")
    list_start = page.index('<local:CommonListBox x:Name="OptionsList"')
    opening = page[list_start:page.index(">", list_start)]
    body = page[list_start:page.index("</local:CommonListBox>")]
    bindings = re.findall(r'(?:SelectedIndex|Value)="\{Binding (\w+)\}"', body)
    if graphics == "second" and not eo_rows:
        # Azimuth Graphics Options puts its rows at the list's end when Extra Options has none there.
        at = bindings.index("PerfFpsCounter") if "PerfFpsCounter" in bindings else -1
        check(bindings.count("PerfFpsCounter") == 1 and bindings[at + 1:at + 2] == ["GfxFrameRate"],
              f"{label}: the FPS counter once, before the Azimuth rows Graphics Options appended "
              f"({bindings[max(0, at - 1):at + 2]})")
    else:
        check(bindings.count("PerfFpsCounter") == 1 and bindings[-1] == "PerfFpsCounter",
              f"{label}: the FPS counter is the last row, once ({bindings[-3:]})")
    check(page.count('x:Key="PERF_FpsCounter"') == 1 and
          'ItemsSource="{Binding PerfFpsCounterChoices}"' in body, f"{label}: its TileMenuDataItem and choices")
    check(page.index('x:Key="PERF_FpsCounter"') < page.index("</ResourceDictionary>"),
          f"{label}: the item is in the page resources")
    check("A_Style_ListBox_Scrollable" in opening and "A_Style_ListBox_List_Default" not in opening,
          f"{label}: the list is scrollable")
    height = re.search(r'Height="(\d+)"', opening).group(1)
    check(height == ("440" if graphics == "first" else "275"), f"{label}: the list's height {height}")
    check(bindings[:2] == ["Gamma", "Subtitles"], f"{label}: the page's own rows come first")
    if eo_rows:
        check(all(n in bindings for n in ("RealtimeShadows", "PhotoSamplesPreset", "LiveryResolution", "LiveryMsaa")),
              f"{label}: Extra Options' Video rows are all kept")
    if graphics:
        check([b for b in bindings if b.startswith("Gfx")] == AZG_ROWS, f"{label}: the 9 Azimuth Graphics Options rows")
        if eo_rows:
            check(bindings.index("GfxHdr") < bindings.index("RealtimeShadows"),
                  f"{label}: Azimuth Graphics Options' rows still before Extra Options'")


def test_xaml(dll, zig, game, upstream, graphics_source, output):
    print("xaml")
    ui = game / UI_ZIP
    if not ui.is_file():
        skip(f"{ui} not found")
        return
    with zipfile.ZipFile(ui) as archive:
        video = archive.read(VIDEO_PAGE)
        audio = archive.read("Scenes/Anthem/A_AudioOptions.xaml")
    runs = output / "test-runs"
    ours = extend(dll, video)
    (runs / "video_performance.xaml").write_bytes(ours)
    check(bool(ours), "the Video page gets the row")
    if ours:
        check_page(ours, "alone")
    check(not extend(dll, ours) and dll.PerfTestWhy(ours, len(ours)) == b"it already has the FPS counter row",
          "a page that has the row is left alone")
    check(not extend(dll, audio), "the Audio page is left alone")
    standalone = video.replace(b"<Grid", b"<!-- EO:StandaloneOptionsPage --><Grid", 1)
    check(not extend(dll, standalone), "a plugin's own page on the Video class is left alone")
    built = video.replace(b"A_Style_ContentControl_BrightnessSquare", b"X")
    check(not extend(dll, built), "a page built from the Video page (no brightness preview) is left alone")
    polish = extend(dll, video, b"PL")
    check(CAPTIONS["PL"]["name"].encode("utf-8") in polish, "the Polish caption is UTF-8 in the page")
    ukrainian = extend(dll, video, b"UK")
    check(bool(ukrainian) and parse(ukrainian, "UK") is not None and ukrainian.startswith(b"\xef\xbb\xbf") and
          CAPTIONS["UK"]["name"].encode("utf-8") in ukrainian and
          CAPTIONS["UK"]["description"].replace("'", "'").encode("utf-8") in ukrainian,
          "the Ukrainian row (name and description, with і/ї/є) is UTF-8 in the page, after the BOM")
    if ukrainian:
        check_page(ukrainian, "in Ukrainian")

    eo = build_eo(zig, upstream, output)
    eo_video = eo_extend(eo, video) if eo else b""
    if eo:
        check(b"RealtimeShadows" in eo_video, "Extra Options 0.4.4's own Video page edit (built from its source)")
        after_eo = extend(dll, eo_video)
        (runs / "video_eo_performance.xaml").write_bytes(after_eo)
        check(bool(after_eo), "after Extra Options' edit the page gets the row")
        if after_eo:
            check_page(after_eo, "after Extra Options", eo_rows=True)

    azg = build_graphics(zig, graphics_source, output)
    if not azg:
        return
    pages = [("alone", video, False)] + ([("after Extra Options", eo_video, True)] if eo_video else [])
    for name, base, eo_rows in pages:
        first_azg = graphics_extend(azg, base)
        then_ours = extend(dll, first_azg) if first_azg else b""
        check(bool(first_azg) and bool(then_ours), f"{name}: Azimuth Graphics Options first, then Performance")
        if then_ours:
            check_page(then_ours, f"{name}, Graphics then Performance", eo_rows=eo_rows, graphics="first")
        first_ours = extend(dll, base)
        then_azg = graphics_extend(azg, first_ours) if first_ours else b""
        check(bool(then_azg), f"{name}: Performance first, then Azimuth Graphics Options (its checks pass)")
        if then_azg:
            check_page(then_azg, f"{name}, Performance then Graphics", eo_rows=eo_rows, graphics="second")
        tag = "eo_" if eo_rows else ""
        (runs / f"video_{tag}graphics_performance.xaml").write_bytes(then_ours)
        (runs / f"video_{tag}performance_graphics.xaml").write_bytes(then_azg)


# --------------------------------------------------------------- 8. captions

def test_captions(dll):
    print("captions")
    codes = [dll.PerfTestCaptionCode(i).decode() for i in range(dll.PerfTestCaptionCount())]
    check(codes == list(CAPTIONS), f"the {len(codes)} language codes of src/translations.json, EN first")
    expected = ["EN", "GB", "BR", "DE", "FR", "ES", "MX", "IT", "JP", "KO", "CHT", "CZ", "HU", "PL", "RU", "NL"]
    check(sorted(codes) == sorted(expected + ["UK"]), "the game's 16 language codes and UK (Language Support's pack)")
    uk = CAPTIONS["UK"]
    check(set(uk) == set(CAPTIONS["EN"]) and len(uk["choices"]) == 3 and all(uk["choices"]) and uk["name"] and
          uk["description"] and uk["name"] == "Лічильник FPS" and uk["choices"][:2] == ["Вимк.", "Увімк."] and
          lt.has_ukrainian_letters([uk["name"], uk["description"]]),
          "UK: the row's name, its three choices and its description (Лічильник FPS: Вимк. / Увімк. / Детально)")
    good = all(dll.PerfTestCaption(code.encode(), 1) == CAPTIONS[code]["name"] and
               [dll.PerfTestCaption(code.encode(), i) for i in (2, 3, 4)] == CAPTIONS[code]["choices"] and
               dll.PerfTestCaption(code.encode(), 5) == CAPTIONS[code]["description"] for code in codes)
    check(good, "every caption is in the DLL as written")
    check(dll.PerfTestCaption(b"XX", 0) == "EN" and dll.PerfTestCaption(None, 0) == "EN", "an unknown code: English")
    check(dll.PerfTestCaption(b"pl", 0) == "PL", "codes ignore case")
    # The game's text manager (pointer at image+0x3662990), its language code a std::string at +0x30.
    image = kernel32.VirtualAlloc(None, TEXT_TABLES + 0x1000, 0x2000, 0x04)
    page = kernel32.VirtualAlloc(image + (TEXT_TABLES & ~0xFFF), 0x1000, 0x1000, 0x04)
    manager = ctypes.create_string_buffer(0x60)
    poke(page + (TEXT_TABLES & 0xFFF), struct.pack("<Q", ctypes.addressof(manager)))
    results = []
    for code in ("PL", "CHT", "EN"):
        text = code.encode().ljust(16, b"\0") + struct.pack("<QQ", len(code), 15)
        ctypes.memmove(ctypes.addressof(manager) + 0x30, text, len(text))
        results.append(dll.PerfTestGameLanguage(image))
    check(results == [b"PL", b"CHT", b"EN"], f"the game's language code is read from its text manager ({results})")
    ctypes.memmove(ctypes.addressof(manager) + 0x30, b"PL".ljust(16, b"\0") + struct.pack("<QQ", 2, 31), 32)
    check(dll.PerfTestGameLanguage(image) == b"", "a text manager not as expected gives no code (English)")
    check(dll.PerfTestGameLanguage(None) == b"", "no image: no code")
    kernel32.VirtualFree(image, 0, 0x8000)


def test_languages(output):
    """The row's language through fh2_text_language: the plugin installed beside language packs."""
    print("languages")
    lt.check_selection(check, output / "test-runs" / "languages", output / "tests" / TEST_DLL, MOD_NAME,
                       lambda path: load_test_dll(output, path),
                       lambda dll, game: dll.PerfTestGameLanguage(game).decode(), lambda code: code)


# ------------------------------------------------------------------ 9. d3d12

def test_d3d12(output, warp, spire_hud):
    print("d3d12")
    exe = output / "tests" / "d3d12_test.exe"
    runs = [("late", "late", None), ("early", "early", None), ("nitrous", "nitrous", None)]
    if spire_hud and spire_hud.is_file():
        runs.append(("nitrous-ff", "nitrous", spire_hud))
    else:
        skip(f"nitrous: Fast & Furious' SpireHUD.zip is not there; the gauge is tested without its outline")
    for name, mode, hud in runs:
        work = output / "test-runs" / name
        shutil.rmtree(work, ignore_errors=True)
        if mode == "nitrous":  # the plugin in mods\Performance\plugins, Nitrous beside it
            (work / "mods/Nitrous/plugins").mkdir(parents=True)
            (work / "mods/Nitrous/plugins/Nitrous.dll").write_bytes(b"MZ")
            work = work / "mods/Performance/plugins"
        work.mkdir(parents=True, exist_ok=True)
        command = [str(exe), mode, str(output / "tests" / TEST_DLL), str(work)] + (["--warp"] if warp else [])
        environment = dict(os.environ)
        environment.pop("PERF_TEST_SPIRE_HUD", None)
        if hud:
            environment["PERF_TEST_SPIRE_HUD"] = str(hud)
        try:
            run = subprocess.run(command, capture_output=True, text=True, timeout=180, env=environment)
        except subprocess.TimeoutExpired:
            check(False, f"{name}: the D3D12 test ends in time")
            continue
        lines = run.stdout.splitlines()
        if any(line.startswith("SKIP-D3D12") for line in lines):
            skip(f"{name}: no D3D12 device and flip-model swap chain can be made here (a CI runner?)")
            continue
        (output / "test-runs" / f"d3d12_{name}.txt").write_text(run.stdout + run.stderr, encoding="utf-8")
        for line in lines:
            line = line.rstrip()
            if line.startswith("  ok   ") or line.startswith("  FAIL ") or line.startswith("  skip "):
                kind, what = line[2:6], line[7:]
                if name != mode and what.startswith(mode + ":"):
                    what = name + what[len(mode):]
                if kind == "skip":
                    skip(what if what.startswith(name + ":") else f"{name}: {what}")
                else:
                    check(kind == "ok  ", what if what.startswith(name + ":") else f"{name}: {what}")
            elif line.startswith("  device on") or line.startswith("  attach:"):
                print("       " + line.strip())
        check(run.returncode == 0 and any("0 failure(s)" in line for line in lines),
              f"{name}: the D3D12 test process ends cleanly (exit {run.returncode})")


# ---------------------------------------------------------------- 10. nitrous

class NitrousStatus(ctypes.Structure):
    _fields_ = [("size", ctypes.c_uint32), ("flags", ctypes.c_uint32), ("remaining", ctypes.c_float),
                ("level", ctypes.c_float), ("run_seconds", ctypes.c_float), ("fov_degrees", ctypes.c_float)]


class NosQuad(ctypes.Structure):
    _fields_ = [("rect", ctypes.c_float * 4), ("mode", ctypes.c_uint32)]


class NosDraw(ctypes.Structure):
    _fields_ = [("count", ctypes.c_uint), ("quads", NosQuad * 2), ("ratio", ctypes.c_float),
                ("active", ctypes.c_float), ("level", ctypes.c_float), ("alpha", ctypes.c_float)]


class NosOptions(ctypes.Structure):
    _fields_ = [("gauge", ctypes.c_uint), ("driving_only", ctypes.c_uint),
                ("scale", ctypes.c_float), ("x", ctypes.c_float), ("y", ctypes.c_float),
                ("media", ctypes.c_wchar * 260), ("warnings", ctypes.c_char * 512)]


BOOSTING, HELD, DRIVING, REFILLING, RUN = 1, 2, 4, 8, 16
BAR, OUTLINE = 1, 2
FF_SPIRE_HUD = Path(r"D:\Games\Forza Horizon 2 Presents Fast & Furious\game\base\Mount\media\ui\textures\data_bound"
                    r"\SpireHUD.zip")


def detile_reference(data):
    """The swatchbin detile of the upscale tool (FH2HDTextures swatch.py), in Python: (width, height, blocks)."""
    width, height, fmt = struct.unpack_from("<H", data, 0x40)[0], struct.unpack_from("<H", data, 0x42)[0], data[0x4F]
    ew, eh = (width + 3) // 4, (height + 3) // 4
    out = bytearray(ew * eh * 16)
    for ey in range(eh):
        for ex in range(ew):
            b = lambda v, n: (v >> n) & 1
            m = b(ex, 0) | b(ey, 0) << 1 | b(ex, 1) << 2 | b(ey, 1) << 3 | b(ex, 2) << 4 | b(ey, 2) << 5
            if fmt == 0x2D:
                pw = (ew + 7) & ~7
                src = ((ey >> 3) * (pw >> 3) + (ex >> 3)) * 64 + m
            else:
                pw = (ew + 63) & ~63
                pipe = (b(ex, 3) ^ b(ex, 4) ^ b(ey, 3)) | (b(ex, 4) ^ b(ey, 4)) << 1 | (b(ex, 5) ^ b(ey, 5)) << 2
                bank = (b(ex, 6) ^ b(ey, 4) ^ b(ey, 5)) | (b(ex, 7) ^ b(ey, 3)) << 1
                src = ((ey >> 5) * (pw >> 6) + (ex >> 6)) * 2048 + ((m & 15) | (pipe | bank << 3 | (m >> 4) << 5) * 16)
            out[(ey * ew + ex) * 16:(ey * ew + ex) * 16 + 16] = data[0x53 + src * 16:0x53 + src * 16 + 16]
    return width, height, bytes(out)


def test_nitrous(dll, work, spire_hud):
    print("nitrous")
    dll.PerfTestNosOptions.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(NosOptions)]
    dll.PerfTestNosLayout.argtypes = [ctypes.c_uint, ctypes.c_uint, ctypes.POINTER(NitrousStatus), ctypes.c_uint,
                                      ctypes.c_uint, ctypes.c_float, ctypes.c_float, ctypes.c_float,
                                      ctypes.c_uint, ctypes.POINTER(NosDraw)]
    dll.PerfTestInflate.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t]
    dll.PerfTestLoadOutline.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(ctypes.c_uint),
                                        ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
    dll.PerfTestLoadOutline.restype = ctypes.c_char_p
    dll.PerfTestFindSpireHud.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p]
    dll.PerfTestFindSpireHud.restype = ctypes.c_wchar_p
    dll.PerfTestNitrousInstalled.argtypes = [ctypes.c_wchar_p]
    folder = work / "nitrous"
    folder.mkdir(parents=True, exist_ok=True)
    ini = folder / INI_NAME

    def options(text):
        ini.write_text(text, encoding="ascii")
        out = NosOptions()
        dll.PerfTestNosOptions(str(ini), ctypes.byref(out))
        return out

    o = options("[Performance]\nShowFps=1\n")
    check((o.gauge, o.driving_only, o.scale, o.x, o.y, o.media, o.warnings) == (1, 1, 1.0, 0.0, 0.0, "", b""),
          "[Nitrous] defaults: Gauge=1, OnlyWhileDriving=1, scale 1, no offsets, media searched")
    o = options("[Nitrous]\nGauge=0\nScreenEffect=1\nOnlyWhileDriving=0\nGaugeScale=1.5\nGaugeOffsetX=-40\n"
                "GaugeOffsetY=12.5\nFastFuriousMedia= D:\\FF\\media \n")
    check((o.gauge, o.driving_only, o.scale, o.x, o.y, o.media, o.warnings) ==
          (0, 0, 1.5, -40.0, 12.5, "D:\\FF\\media", b""),
          "[Nitrous] values are read (the media path trimmed; an old ScreenEffect=1 is ignored)")
    o = options("[Nitrous]\nGauge=yes\nGaugeScale=9\nGaugeOffsetX=left\n")
    check(o.gauge == 1 and o.scale == 3.0 and o.x == 0.0 and b"Gauge=yes" in o.warnings and b"GaugeOffsetX=left" in o.warnings,
          f"bad values keep their default and are named; the scale is clamped to 3 ({o.warnings.decode()})")

    def layout(width, height, flags, remaining=0.5, level=0.0, gauge=1, driving_only=1, scale=1.0, x=0.0,
               y=0.0, outline=1):
        status = NitrousStatus(ctypes.sizeof(NitrousStatus), flags, remaining, level, 0.0, 5.0)
        out = NosDraw()
        dll.PerfTestNosLayout(width, height, ctypes.byref(status), gauge, driving_only, scale, x, y, outline,
                              ctypes.byref(out))
        return out

    def quads(d):
        return [(q.mode, tuple(round(v, 3) for v in q.rect)) for q in d.quads[:d.count]]

    d = layout(1920, 1080, DRIVING, 0.5, 0.0)
    bar = 300 * 1.2645
    want = [(BAR, tuple(round(v, 3) for v in (1920 - 92 - 150 - bar / 2, 1080 - 23 - 150 - bar / 2, bar, bar))),
            (OUTLINE, (1920 - 392 - 23, 1080 - 323 + 8, 128.0, 256.0))]
    check(quads(d) == want and d.ratio == 0.5 and d.active == 0 and d.alpha == 1.0,
          f"1080p, driving, half a tank: Fast & Furious' bar on the speedometer box (92, 23 from the corner) and its "
          f"outline at (-23, 8) in it ({quads(d)})")
    d = layout(3840, 2160, DRIVING | BOOSTING | RUN, 0.25, 0.8)
    check(d.count == 2 and quads(d)[0][1][2] == round(bar * 2, 3) and d.active == pytest_float(0.8) and
          d.level == pytest_float(0.8),
          f"2160p while boosting: the bar twice as large, Active = the level; nothing else drawn ({quads(d)})")
    d = layout(2560, 1080, DRIVING)
    check(quads(d)[0][1][0] == round(2560 - 92 - 150 - bar / 2, 3), "an ultrawide screen: anchored at the right edge")
    d = layout(1920, 1080, DRIVING, scale=2.0, x=-100.0, y=-50.0)
    check(quads(d)[1][1] == (1920 - 784 - 100 - 46.0, 1080 - 646 - 50 + 16.0, 256.0, 512.0),
          f"GaugeScale=2 and the offsets move and scale it about the bottom-right corner ({quads(d)[1]})")
    check(layout(1920, 1080, BOOSTING | RUN, level=1.0).count == 0, "not driving: nothing (OnlyWhileDriving=1)")
    check(layout(1920, 1080, BOOSTING | RUN, level=1.0, driving_only=0).count == 2, "OnlyWhileDriving=0: always")
    check(quads(layout(1920, 1080, DRIVING, outline=0)) == want[:1], "no outline texture: the bar alone")
    check(layout(1920, 1080, DRIVING, level=1.0, gauge=0).count == 0, "Gauge=0: nothing")
    check(layout(1920, 1080, DRIVING | REFILLING).alpha == pytest_float(0.8) and
          layout(1920, 1080, DRIVING | REFILLING | RUN).alpha == 1.0,
          "refilling (no boost run open): the bar is dimmed to 0.8")
    check(layout(1920, 1080, DRIVING, remaining=1.7).ratio == 1.0 and layout(1920, 1080, DRIVING, remaining=-1).ratio == 0,
          "the tank is clamped to 0..1")

    # Inflate against zlib's raw deflate: stored, fixed and dynamic blocks.
    import zlib
    import random
    rng = random.Random(7)
    samples = [b"", b"a", bytes(rng.getrandbits(8) for _ in range(70000)), b"nitrous " * 5000,
               bytes(rng.choice(b"NOS ") for _ in range(30000))]
    good = True
    for level in (0, 1, 9):
        for data in samples:
            packer = zlib.compressobj(level, zlib.DEFLATED, -15)
            packed = packer.compress(data) + packer.flush()
            out = ctypes.create_string_buffer(len(data) + 1)
            ok = dll.PerfTestInflate(packed, len(packed), out, len(data))
            good = good and ok and out.raw[:len(data)] == data
    check(good, "inflate matches zlib (stored, fixed and dynamic Huffman blocks)")
    packer = zlib.compressobj(9, zlib.DEFLATED, -15)
    packed = packer.compress(b"nitrous" * 100) + packer.flush()
    out = ctypes.create_string_buffer(800)
    check(not dll.PerfTestInflate(packed[:-3], len(packed) - 3, out, 700) and not dll.PerfTestInflate(packed, len(packed), out, 699),
          "a cut stream or a wrong size is refused")

    # The outline from a zip (a synthetic swatchbin, then the real one when Fast & Furious is installed).
    swatch = bytearray(0x53 + 0x10000)
    swatch[0:4], swatch[0x10:0x14], swatch[0x28:0x2C] = b"burG", b"BCXT", b"HCXT"
    struct.pack_into("<II", swatch, 8, 0x53, 0x53 + 0x10000)
    struct.pack_into("<II", swatch, 0x20, 0x10000, 0x10000)
    struct.pack_into("<HH", swatch, 0x40, 128, 256)
    swatch[0x4C], swatch[0x4F] = 1, 0x2E
    swatch[0x53:] = bytes((i * 7 + i // 16) & 255 for i in range(0x10000))
    synthetic = folder / "SpireHUD.zip"
    with zipfile.ZipFile(synthetic, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("HUD_Something_Else.swatchbin", b"x" * 100)
        z.writestr("HUD_Nos_Outline_Radial_Bar.swatchbin", bytes(swatch))
    width, height, size = ctypes.c_uint(), ctypes.c_uint(), ctypes.c_size_t()
    blocks = ctypes.create_string_buffer(0x10000)
    why = dll.PerfTestLoadOutline(str(synthetic), ctypes.byref(width), ctypes.byref(height), blocks, 0x10000,
                                  ctypes.byref(size))
    ref = detile_reference(bytes(swatch))
    check(why == b"" and (width.value, height.value, size.value) == (128, 256, 0x8000) and blocks.raw[:0x8000] == ref[2],
          f"a 2D-tiled BC7 swatchbin in a zip is made linear as the upscale tool does ({why})")
    with zipfile.ZipFile(folder / "Empty.zip", "w") as z:
        z.writestr("other.txt", b"x")
    why = dll.PerfTestLoadOutline(str(folder / "Empty.zip"), ctypes.byref(width), ctypes.byref(height), blocks, 0x10000,
                                  ctypes.byref(size))
    check(b"is not in" in why, f"a zip without it says so ({why.decode()})")
    bad = bytearray(swatch)
    bad[0x4F] = 0xAE
    with zipfile.ZipFile(folder / "Rgba.zip", "w", zipfile.ZIP_STORED) as z:
        z.writestr("HUD_Nos_Outline_Radial_Bar.swatchbin", bytes(bad))
    why = dll.PerfTestLoadOutline(str(folder / "Rgba.zip"), ctypes.byref(width), ctypes.byref(height), blocks, 0x10000,
                                  ctypes.byref(size))
    check(b"not BC7" in why, f"another format is refused ({why.decode()})")
    if spire_hud and spire_hud.is_file():
        why = dll.PerfTestLoadOutline(str(spire_hud), ctypes.byref(width), ctypes.byref(height), blocks, 0x10000,
                                      ctypes.byref(size))
        with zipfile.ZipFile(spire_hud) as z:
            ref = detile_reference(z.read("HUD_Nos_Outline_Radial_Bar.swatchbin"))
        check(why == b"" and (width.value, height.value) == (128, 256) and blocks.raw[:size.value] == ref[2],
              f"Fast & Furious' own HUD_Nos_Outline_Radial_Bar ({spire_hud}): 128x256 BC7, as the upscale tool reads it")
    else:
        skip(f"Fast & Furious' SpireHUD.zip is not there ({spire_hud})")

    # Where SpireHUD.zip is found, and whether Nitrous is installed.
    games = folder / "Games"
    hud = games / "Forza Horizon 2 Presents Fast & Furious/game/base/Mount/media/ui/textures/data_bound"
    hud.mkdir(parents=True, exist_ok=True)
    shutil.copy2(synthetic, hud / "SpireHUD.zip")
    host = games / "Forza Horizon 2" / "Azimuth" / "Azimuth.exe"
    found = dll.PerfTestFindSpireHud("", str(host))
    check(Path(found) == hud / "SpireHUD.zip", f"found beside the game's folders ({found})")
    media = games / "Forza Horizon 2 Presents Fast & Furious/game/base/Mount/media"
    check(Path(dll.PerfTestFindSpireHud(str(media), str(host))) == hud / "SpireHUD.zip" and
          Path(dll.PerfTestFindSpireHud(str(hud / "SpireHUD.zip"), "")) == hud / "SpireHUD.zip" and
          dll.PerfTestFindSpireHud(str(folder / "nowhere"), str(host)) == "",
          "FastFuriousMedia: the media folder or the file itself; a wrong one is not searched past")
    check(dll.PerfTestFindSpireHud("", str(folder / "a" / "b.exe")) == "", "nothing beside an unrelated folder")
    mods = folder / "mods"
    (mods / "Performance/plugins").mkdir(parents=True, exist_ok=True)
    dll_path = mods / "Performance/plugins/Performance.dll"
    check(not dll.PerfTestNitrousInstalled(str(dll_path)), "without mods\\Nitrous the gauge stays off")
    (mods / "Nitrous/plugins").mkdir(parents=True, exist_ok=True)
    (mods / "Nitrous/plugins/Nitrous.dll").write_bytes(b"MZ")
    check(dll.PerfTestNitrousInstalled(str(dll_path)), "with mods\\Nitrous\\plugins\\Nitrous.dll it is on")


def pytest_float(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", type=Path, default=ROOT / "out")
    parser.add_argument("--zig", type=Path, default=Path(shutil.which("zig") or "zig"))
    parser.add_argument("--game", type=Path, default=Path.home() / "FH2")
    parser.add_argument("--azimuth", type=Path)
    parser.add_argument("--upstream", type=Path)
    parser.add_argument("--release", type=Path)
    parser.add_argument("--graphics", type=Path)
    parser.add_argument("--warp", action="store_true")
    parser.add_argument("--spire-hud", type=Path, default=FF_SPIRE_HUD,
                        help="Fast & Furious' SpireHUD.zip (read only; missing: the outline tests skip)")
    options = parser.parse_args()
    output = options.output.resolve()
    game = options.game
    runs = output / "test-runs"
    runs.mkdir(parents=True, exist_ok=True)
    test_package(output)
    test_azimuth(options.azimuth or game / "azimuth-windows-1.0.4.zip")
    upstream = options.upstream or game / "src/eo-pr/wt-upstream"
    test_release(options.release or game / "src/eo-pr/upstream-0.4.4/FH2_Extra_Options_0.4.4-azimuth/Azimuth",
                 upstream)
    dll = load_test_dll(output)
    with tempfile.TemporaryDirectory(prefix="performance-test-") as temporary:
        work = Path(temporary)
        test_options(dll, work)
        loader = Loader(dll, work / "plugin")
        test_guards(loader)
        test_game(dll, loader, game / GAME_EXE)
        test_nitrous(dll, work, options.spire_hud)
    test_xaml(dll, options.zig, game, upstream,
              options.graphics or ROOT.parent / "azimuth-graphics-options", output)
    test_captions(dll)
    test_languages(output)
    test_d3d12(output, options.warp, options.spire_hud)
    print(f"{len(failures)} failure(s)" if failures else "all tests passed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
