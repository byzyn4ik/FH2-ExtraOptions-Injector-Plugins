"""Offline tests of the Custom Radio plugin (no game, nothing installed).

Usage: py tests/run_tests.py [--output out] [--game-exe <forza_durango_release_final.exe>]
                             [--ff-exe <Fast & Furious' forza_durango_release_final.exe>]
                             [--extra-options <folder with winmm.dll and mods\\>]

1. package: mod.json as Extra Options validates it (its media folder), the
   plugin layout, the DLL (PE32+ x64, exactly the loader's exports, only
   Windows imports), the INI, the station logos (196x96 BC7 swatchbins of
   logo/*.png premultiplied, with the game's drop shadow); with --game-exe
   and --ff-exe the game's own radio logos (196x96 BC7, our header, their
   baked drop shadow, which ours match) and RGBA8 UI textures
   (tools/logo_texture.py writes them back byte for byte).
2. sites: src/sites.h is well formed; with --game-exe (or FH2_GAME_EXE) every
   guard byte is checked against the game exe and none of them is relocated.
   src/ff_sites.h (Fast & Furious) names the same sites; with --ff-exe (or
   FF_GAME_EXE) its guards, slots, calls, virtual tables (run-time types) and
   the Fast & Furious key tick's sites are checked against that exe the same
   way.
3. coexistence: the default keys are not Extra Options 0.4.4's; with
   --extra-options (or EO_RELEASE_DIR) its INI bindings and binaries are
   checked too (no default key and no hook site in common).
4. config: CustomRadio.ini parsing (defaults, valid values, bad values and
   unknown keys logged, relative/absolute folders, key and button names).
5. keys: one action per press, only with input enabled, release before rearm.
6. mp3: generated silent MP3s: ID3v2/ID3v1 tags, Xing and CBR lengths, and
   the Windows decode to a 16-bit PCM WAV.
7. logo: the DLL's texture writer against tools/logo_texture.py; Logo= PNGs
   (sizes, RGB, a 196x96 PNG as it is, others fitted into the art area
   against its right edge, the drop shadow) accepted and refused (not a
   PNG, missing, too small or large, fully transparent, broken).
8. plugin: the test DLL loaded with a fake game image (built from sites.h) in
   a child process per scenario: vanilla sites, sites already hooked by
   another module (chained: XML injection, the HUD radio display's logo
   triggers through the XAML resource call, sound/subsound serving, station
   lock, keys through FH2PluginVehicleUpdate), mismatches in the core and in
   an optional group, Enabled=0, UnlockAllStations alone, wrong image size,
   Logo= used and refused, the media folder missing; FH2PluginMediaDirectory.
   On a fake Fast & Furious image (built from ff_sites.h, its TimeDateStamp):
   every site hooked, the local car's update slot hooked for the keys; chained:
   RadioInfo without stingers or DJ list, F9 through the local car's update
   (not another car's); the HUD logo from the mod's media\\ folder with Language
   Support's media mount (its DLL in the file resolver slot; on Fast & Furious'
   own HUDRadioControl.xaml with --ff-exe), off and logged without it; a Fast &
   Furious image with another TimeDateStamp refused.
"""

import argparse
import configparser
import ctypes
from ctypes import wintypes
import json
import math
import wave
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ElementTree
import zipfile
import zlib

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(ROOT / "tools"))
import logo_texture  # noqa: E402
sys.path.insert(0, str(HERE))
from flac_writer import flac  # noqa: E402
sys.path.insert(0, str(ROOT.parents[1] / "common"))
import language_testing as lt  # noqa: E402
MOD_NAME = "Custom Radio"
DLL_NAME = "CustomRadio.dll"
INI_NAME = "CustomRadio.ini"
GAME_IMAGE_SIZE = 0x3A18000
FF_IMAGE_SIZE = 0x3A89000
FF_STAMP = 0x55B62FDE
SYSTEM_DLLS = re.compile(r"^(kernel32|user32|msvcrt|ntdll|api-ms-win-[a-z0-9-]+)\.dll$", re.I)
VK_F1 = 0x70
LOGO_FOLDER = Path("CustomRadio/Media/UI/Textures/Anthem/HUD")
LOGOS = {"my_music.png": "HUD_RadioIcon_CustomRadio", "moja_muzyka.png": "HUD_RadioIcon_CustomRadio_PL",
         "moya_muzyka.png": "HUD_RadioIcon_CustomRadio_UK", "badge.png": "HUD_RadioIcon_CustomRadio_Badge"}
# The default station names: English, Polish game text, Language Support's Ukrainian pack in ru-RU.
DEFAULT_NAMES = {"EN": "MY MUSIC", "PL": "MOJA MUZYKA", "UK": "МОЯ МУЗИКА"}
LOGO_SWATCH_SIZE = 83 + 64 * 32 * 16  # BC7: 196x96 as 64x32 blocks of 16 bytes
# The built-in logos' sources beside the plugin (the plugin makes them into the textures when it starts).
LOGO_PNGS = {"my_music.png": "Logo.png", "moja_muzyka.png": "Logo_PL.png", "moya_muzyka.png": "Logo_UK.png",
             "badge.png": "Logo_Badge.png"}

failures = []


def check(ok, what):
    print(("  ok   " if ok else "  FAIL ") + what, flush=True)
    if not ok:
        failures.append(what)
    return ok


# ------------------------------------------------------------------ PE reader

class PE:
    def __init__(self, path=None, data=None):
        self.data = data if data is not None else Path(path).read_bytes()
        d = self.data
        e = struct.unpack_from("<I", d, 0x3C)[0]
        if d[:2] != b"MZ" or d[e:e + 4] != b"PE\0\0":
            raise ValueError("not a PE file")
        self.machine, count, self.timestamp, _, _, optional_size, self.characteristics = \
            struct.unpack_from("<HHIIIHH", d, e + 4)
        optional = e + 24
        self.magic = struct.unpack_from("<H", d, optional)[0]
        self.image_base = struct.unpack_from("<Q", d, optional + 24)[0]
        self.image_size = struct.unpack_from("<I", d, optional + 56)[0]
        self.sections = []
        table = optional + optional_size
        for i in range(count):
            name, vsize, va, rsize, raw = struct.unpack_from("<8sIIII", d, table + 40 * i)
            self.sections.append((va, vsize, rsize, raw))
        directories = optional + 112
        self.directories = [struct.unpack_from("<II", d, directories + 8 * i) for i in range(16)]

    def offset(self, rva):
        for va, vsize, rsize, raw in self.sections:
            if va <= rva < va + max(vsize, rsize):
                if rva - va >= rsize:
                    return None
                return rva - va + raw
        raise ValueError(hex(rva))

    def read(self, rva, size):
        out = bytearray()
        for i in range(size):
            o = self.offset(rva + i)
            out.append(self.data[o] if o is not None else 0)
        return bytes(out)

    def text(self, rva):
        o = self.offset(rva)
        return self.data[o:self.data.index(b"\0", o)].decode("ascii")

    def exports(self):
        rva = self.directories[0][0]
        if not rva:
            return []
        o = self.offset(rva)
        count, names = struct.unpack_from("<I", self.data, o + 24)[0], struct.unpack_from("<I", self.data, o + 32)[0]
        return [self.text(struct.unpack_from("<I", self.data, self.offset(names) + 4 * i)[0]) for i in range(count)]

    def imports(self):
        result = {}
        rva = self.directories[1][0]
        o = self.offset(rva) if rva else None
        while o is not None:
            fields = struct.unpack_from("<IIIII", self.data, o)
            if not fields[3]:
                break
            names, thunk = [], self.offset(fields[0] or fields[4])
            while True:
                value = struct.unpack_from("<Q", self.data, thunk)[0]
                if not value:
                    break
                names.append(self.text(value + 2) if not value >> 63 else f"#{value & 0xffff}")
                thunk += 8
            result[self.text(fields[3])] = names
            o += 20
        return result

    def relocations(self):
        """RVAs of 64-bit relocated fields."""
        rva, size = self.directories[5]
        out = set()
        at = 0
        while at < size:
            page, block = struct.unpack("<II", self.read(rva + at, 8))
            if block < 8:
                break
            entries = self.read(rva + at + 8, block - 8)
            for i in range(0, len(entries), 2):
                value = struct.unpack_from("<H", entries, i)[0]
                if value >> 12 == 10:
                    out.add(page + (value & 0xFFF))
            at += block
        return out


# --------------------------------------------------------------- sites.h

def c_bytes(text):
    return bytes(int(h, 16) for h in re.findall(r"\\x([0-9a-fA-F]{2})", text))


def load_sites(header="sites.h"):
    source = (ROOT / "src" / header).read_text(encoding="utf-8")
    functions, slots, calls, data = {}, {}, {}, {}
    for name, rva, body in re.findall(r'X\((FN_\w+),\s*(0x[0-9a-fA-F]+),\s*"([^"]*)"\)', source):
        functions[name] = (int(rva, 16), c_bytes(body))
    for name, rva, function in re.findall(r"X\((SLOT_\w+),\s*(0x[0-9a-fA-F]+),\s*(FN_\w+)\)", source):
        slots[name] = (int(rva, 16), function)
    for name, rva, function, before, after in re.findall(
            r'X\((CALL_\w+),\s*(0x[0-9a-fA-F]+),\s*(FN_\w+),\s*"([^"]*)",\s*"([^"]*)"\)', source):
        calls[name] = (int(rva, 16), function, c_bytes(before), c_bytes(after))
    for name, rva in re.findall(r"#define (?:FF_)?(RVA_\w+) (0x[0-9a-fA-F]+)u", source):
        data[name] = int(rva, 16)
    return functions, slots, calls, data


def load_ff_extra():
    """ff_sites.h's Fast & Furious-only sites: {name: rva} and {name: guard bytes}."""
    source = (ROOT / "src" / "ff_sites.h").read_text(encoding="utf-8")
    rvas = {n: int(r, 16) for n, r in re.findall(r"#define (FF_(?:FN|SLOT)_\w+?) (0x[0-9a-fA-F]+)u", source)}
    guards = {n: c_bytes(b) for n, b in re.findall(r'#define (FF_FN_\w+)_GUARD "([^"]*)"', source)}
    return rvas, guards


def test_ff_sites(ff_exe):
    print("Fast & Furious sites")
    functions, slots, calls, data = load_sites("ff_sites.h")
    fh2 = load_sites()
    extra, guards = load_ff_extra()
    check([set(x) for x in (functions, slots, calls)] == [set(x) for x in fh2[:3]] and set(fh2[3]) <= set(data),
          f"ff_sites.h: the same {len(functions)} functions, {len(slots)} slots, {len(calls)} calls and "
          f"{len(fh2[3])} data RVAs as sites.h")
    check(all(len(functions[n][1]) == len(fh2[0][n][1]) for n in functions),
          "every function guard is as long as Forza Horizon 2's")
    check(all(slots[n][1] == fh2[1][n][1] for n in slots) and all(calls[n][1] == fh2[2][n][1] for n in calls),
          "slots and calls name the same functions as in sites.h")
    check(set(extra) == {"FF_FN_CAR_UPDATE", "FF_SLOT_CAR_UPDATE", "FF_FN_LOCAL_PLAYER", "FF_FN_PLAYER_CAR"} and
          set(guards) == {"FF_FN_CAR_UPDATE", "FF_FN_LOCAL_PLAYER", "FF_FN_PLAYER_CAR"},
          "ff_sites.h: the key tick's sites (car update, its slot, local player, its car)")
    if not ff_exe:
        print("  skip Fast & Furious exe check (pass --ff-exe or set FF_GAME_EXE)")
        return
    exe = PE(ff_exe)
    stamp = exe.timestamp
    check(exe.image_size == FF_IMAGE_SIZE and stamp == FF_STAMP,
          f"Fast & Furious exe: image size {exe.image_size:#x}, TimeDateStamp {stamp:#x}")
    relocated = exe.relocations()

    def relocated_in(rva, size):
        return any(r <= x < r + 8 for r in relocated for x in range(rva, rva + size) if r <= x)

    for name, (rva, guard) in functions.items():
        check(exe.read(rva, len(guard)) == guard and not relocated_in(rva, len(guard)),
              f"F&F {name} {rva:#x}: first {len(guard)} bytes match, none relocated")
    for name, (rva, function) in slots.items():
        value = struct.unpack("<Q", exe.read(rva, 8))[0]
        check(value - exe.image_base == functions[function][0] and rva in relocated,
              f"F&F {name} {rva:#x} holds {function} (a relocated pointer)")
    for name, (rva, function, before, after) in calls.items():
        code = exe.read(rva - 8, 21)
        target = rva + 5 + struct.unpack("<i", code[9:13])[0]
        check(code[:8] == before and code[8] == 0xE8 and code[13:] == after and
              target == functions[function][0] and not relocated_in(rva - 8, 21),
              f"F&F {name} {rva:#x}: call {function} between the guard bytes, none relocated")

    def type_name(vtable):
        locator = struct.unpack("<Q", exe.read(vtable - 8, 8))[0] - exe.image_base
        descriptor = struct.unpack("<I", exe.read(locator + 12, 4))[0]
        return exe.read(descriptor + 0x10, 64).split(b"\0")[0].decode("ascii", "replace")

    types = {"RVA_RADIO_SYSTEM_VTABLE": ".?AVRadioSystem@@", "RVA_STATION_VTABLE": ".?AVRadioStation@@",
             "RVA_PLAYLIST_VTABLE": ".?AVRadioPlaylist@@", "RVA_PLAYER_VTABLE": ".?AVRadioStreamFmod@@"}
    for name, expected in types.items():
        check(data[name] in relocated and type_name(data[name]) == expected,
              f"F&F {name} {data[name]:#x}: the virtual table of {expected}")
    source = (ROOT / "src" / "ff_sites.h").read_text(encoding="utf-8")
    car_vtable = int(re.search(r"#define FF_RVA_FORZA_CAR_VTABLE (0x[0-9a-fA-F]+)u", source).group(1), 16)
    manager_vtable = int(re.search(r"#define FF_RVA_CONTROLLER_MANAGER_VTABLE (0x[0-9a-fA-F]+)u", source).group(1), 16)
    check(type_name(car_vtable) == ".?AVForzaCar@@" and
          extra["FF_SLOT_CAR_UPDATE"] == car_vtable + 0x80 and
          struct.unpack("<Q", exe.read(extra["FF_SLOT_CAR_UPDATE"], 8))[0] - exe.image_base == extra["FF_FN_CAR_UPDATE"],
          "F&F ForzaCar's vtable + 0x80 holds its update (the key tick's slot)")
    check(type_name(manager_vtable) == ".?AVCControllerManager@@", "F&F the controller manager's vtable (RTTI)")
    for name, guard in guards.items():
        rva = extra[name]
        check(exe.read(rva, len(guard)) == guard and not relocated_in(rva, len(guard)),
              f"F&F {name} {rva:#x}: first {len(guard)} bytes match, none relocated")
    # SetStation's rip-relative load names the audio manager global (radio.c checks it at run time too).
    rva = functions["FN_SET_STATION"][0]
    check(rva + 13 + struct.unpack("<i", exe.read(rva + 9, 4))[0] == data["RVA_AUDIO_MANAGER"],
          "F&F SetStation loads the audio manager global the plugin reads")


def test_sites(game_exe):
    print("sites")
    functions, slots, calls, data = load_sites()
    check(len(functions) == 18 and len(slots) == 3 and len(calls) == 9 and len(data) == 7,
          f"sites.h: {len(functions)} functions, {len(slots)} slots, {len(calls)} calls, {len(data)} data RVAs")
    check(all(len(b) >= 10 for _, b in functions.values()), "every function guard has at least 10 bytes")
    check(all(len(b) == 8 and len(a) == 8 for _, _, b, a in calls.values()), "every call guard has 8 bytes on each side")
    check(all(f in functions for _, f in slots.values()) and all(c[1] in functions for c in calls.values()),
          "slots and calls name known functions")
    if not game_exe:
        print("  skip game exe check (pass --game-exe or set FH2_GAME_EXE)")
        return
    exe = PE(game_exe)
    check(exe.image_size == GAME_IMAGE_SIZE, f"game exe image size {exe.image_size:#x}")
    relocated = exe.relocations()

    def relocated_in(rva, size):
        return any(r <= x < r + 8 for r in relocated for x in range(rva, rva + size) if r <= x)

    for name, (rva, guard) in functions.items():
        check(exe.read(rva, len(guard)) == guard and not relocated_in(rva, len(guard)),
              f"{name} {rva:#x}: first {len(guard)} bytes match, none relocated")
    for name, (rva, function) in slots.items():
        value = struct.unpack("<Q", exe.read(rva, 8))[0]
        check(value - exe.image_base == functions[function][0] and rva in relocated,
              f"{name} {rva:#x} holds {function} (a relocated pointer)")
    for name, (rva, function, before, after) in calls.items():
        code = exe.read(rva - 8, 21)
        target = rva + 5 + struct.unpack("<i", code[9:13])[0]
        check(code[:8] == before and code[8] == 0xE8 and code[13:] == after and
              target == functions[function][0] and not relocated_in(rva - 8, 21),
              f"{name} {rva:#x}: call {function} between the guard bytes, none relocated")
    for name, rva in data.items():
        if name.endswith("_VTABLE"):
            check(rva in relocated, f"{name} {rva:#x} is a virtual table (relocated pointer)")


# --------------------------------------------------------------- 1. package

def valid_mod_id(value):
    # features/addons.c valid_id() in Extra Options 0.4.4.
    if not isinstance(value, str) or not 0 < len(value) <= 128:
        return False
    for i, ch in enumerate(value):
        if not ((ch.isascii() and ch.islower()) or ch.isdigit() or ch == "-"):
            return False
        if ch == "-" and (i == 0 or i == len(value) - 1 or value[i - 1] == "-"):
            return False
    return True


def test_package(output):
    print("package")
    mod = output / "mods" / MOD_NAME
    manifest = json.loads((mod / "mod.json").read_text(encoding="utf-8"))
    check(manifest.get("format_version") == 1, "mod.json format_version is 1")
    check(valid_mod_id(manifest.get("id")), f"mod.json id {manifest.get('id')!r} is a valid mod ID")
    check(manifest.get("dependencies") == [], "mod.json has no dependencies")
    check(all(key not in manifest for key in ("database", "strings")), "mod.json names no database or strings")
    check(manifest.get("media") == "plugins/CustomRadio/Media" and (mod / manifest["media"]).is_dir(),
          "mod.json's media is the plugin's media folder plugins/CustomRadio/Media, which is there")
    top = sorted(p.name for p in mod.iterdir())
    check(top == ["media", "mod.json", "plugins"], f"the mod folder holds media, mod.json and plugins only ({top})")
    ff_media = sorted(p.relative_to(mod).as_posix() for p in (mod / "media").rglob("*") if p.is_file())
    check(ff_media == sorted(f"media/UI/Textures/Anthem/HUD/{name}.swatchbin" for name in LOGOS.values()) and
          all((mod / f).read_bytes() == (mod / "plugins" / LOGO_FOLDER / Path(f).name).read_bytes() for f in ff_media),
          f"media\\ (Fast & Furious, Language Support's mount) holds the same logos ({ff_media})")
    files = sorted(p.name for p in (mod / "plugins").iterdir())
    check(files == sorted(["CustomRadio", DLL_NAME, INI_NAME, *LOGO_PNGS.values()]),
          f"plugins holds the DLL, its INI, the logos' PNGs and its media folder CustomRadio only ({files})")
    check(all((mod / "plugins" / name).read_bytes() == (ROOT / "logo" / source).read_bytes()
              for source, name in LOGO_PNGS.items()), "Logo*.png are logo/*.png")
    media = sorted(p.relative_to(mod / "plugins").as_posix() for p in (mod / "plugins" / "CustomRadio").rglob("*")
                   if p.is_file())
    expected = sorted((LOGO_FOLDER / f"{name}.swatchbin").as_posix() for name in LOGOS.values())
    check(media == expected, f"the media folder holds the three logos only ({media})")
    for source, name in LOGOS.items():
        data = (mod / "plugins" / LOGO_FOLDER / f"{name}.swatchbin").read_bytes()
        width, height, rgba = logo_texture.read_png(ROOT / "logo" / source)
        try:
            got = logo_texture.decode(data)
        except ValueError:
            got = None
        check(len(data) == LOGO_SWATCH_SIZE and data[:4] == b"burG" and data[0x4F] == 0x2E and data[0x4C] == 1 and
              data[0x52] == 0xB8 and struct.unpack_from("<HHHHHH", data, 0x40) == (196, 96, 1, 196, 96, 1),
              f"{name}.swatchbin: a 196x96 BC7 2D-tiled swatchbin with one mip, as the game's logos ({len(data)} bytes)")
        shadowed = logo_texture.shadow(logo_texture.premultiply(rgba), width, height)
        error = sum(abs(a - b) for a, b in zip(got[2], shadowed)) / len(shadowed) if got else 999
        worst = max(abs(a - b) for a, b in zip(got[2], shadowed)) if got else 999
        check((width, height) == (196, 96) and data == logo_texture.encode(shadowed, 196, 96) and error < 1.5 and
              worst < 80, f"{name}.swatchbin: logo/{source} (196x96), premultiplied, the game's drop shadow, BC7 "
                          f"(mean error {error:.2f} per channel, at most {worst})")
        alpha = rgba[3::4]
        check(any(a == 255 for a in alpha) and alpha[0] == 0 and sum(a == 0 for a in alpha) > len(alpha) // 3,
              f"logo/{source}: opaque art on a transparent background")
        art = [(i % 196, i // 196) for i, a in enumerate(alpha) if a >= 8]  # (not the resampling's faint ringing)
        box = (min(x for x, _ in art), min(y for _, y in art), max(x for x, _ in art), max(y for _, y in art))
        check(box[0] >= 1 and box[1] >= 2 and box[2] <= 188 and box[3] <= 88,
              f"logo/{source}: the art in the game's art area x 1..188, y 2..88 (room for the drop shadow), as the "
              f"game's logos ({box})")
    pe = PE(mod / "plugins" / DLL_NAME)
    check(pe.machine == 0x8664 and pe.magic == 0x20B and pe.characteristics & 0x2000,
          "CustomRadio.dll is a PE32+ x64 DLL")
    check(sorted(pe.exports()) == ["FH2PluginInitialize", "FH2PluginMediaDirectory", "FH2PluginVehicleUpdate"],
          f"exports exactly FH2PluginInitialize, FH2PluginMediaDirectory and FH2PluginVehicleUpdate ({pe.exports()})")
    imports = pe.imports()
    check(all(SYSTEM_DLLS.match(name) for name in imports), f"imports only Windows DLLs ({sorted(imports)})")
    check(not any(name.startswith("Fh2Test") or name.startswith("CustomRadioTest") for name in pe.exports()),
          "no test exports in the release DLL")
    ini = configparser.ConfigParser(interpolation=None)
    ini.optionxform = str
    ini.read(mod / "plugins" / INI_NAME, encoding="utf-8")
    radio, keys = ini["CustomRadio"], ini["Keys"]
    check(radio.get("Enabled") == "1" and radio.get("UnlockAllStations") == "0" and radio.get("PcmDecode") == "1"
          and radio.get("Volume") == "100" and radio.get("Folder") == "Music" and radio.get("Logo") == "",
          "INI: station on, UnlockAllStations off, PcmDecode on, Volume 100, Folder Music, the built-in logo")
    check([keys.get(f"Radio{a}Keyboard") for a in ("Station", "Next", "Restart")] == ["F9", "F7", "F6"]
          and all(keys.get(f"Radio{a}Xbox") == "None" for a in ("Station", "Next", "Restart")),
          "INI: keys F9/F7/F6, no Xbox buttons")
    return ini


# ---------------------------------------------------------- 3. coexistence

# Extra Options 0.4.4's [Bindings] keyboard defaults (FH2_ExtraOptions.ini):
# Autodrive F2, Headlights F3, Roof F4, ToggleUI F1, JumpToManufacturer and
# RimJumpToManufacturer Tab; everything else None.
EO_044_KEYBOARD = {"F1", "F2", "F3", "F4", "Tab"}


def test_coexistence(ini, eo_dir):
    print("coexistence")
    ours = {ini["Keys"].get(f"Radio{a}Keyboard") for a in ("Station", "Next", "Restart")}
    check(not ours & EO_044_KEYBOARD, f"default keys {sorted(ours)} are not Extra Options 0.4.4's {sorted(EO_044_KEYBOARD)}")
    if not eo_dir:
        print("  skip release check (pass --extra-options or set EO_RELEASE_DIR)")
        return
    eo = Path(eo_dir)
    release = configparser.ConfigParser(interpolation=None, strict=False)
    release.optionxform = str
    release.read(eo / "FH2_ExtraOptions.ini", encoding="utf-8-sig")
    bound = {value.strip() for key, value in release["Bindings"].items() if key.endswith("Keyboard")}
    check(not ours & {b for b in bound if b.lower() != "none"},
          f"no key in common with {eo / 'FH2_ExtraOptions.ini'} [Bindings] ({sorted(bound)})")
    functions, slots, calls, _ = load_sites()
    # The XAML resource call is shared by design: Extra Options 0.4.4 and its
    # Weather Control plugin chain it as well (as the others in this repository do).
    sites = [rva for rva, _ in slots.values()] + [c[0] for n, c in calls.items() if n != "CALL_XAML_RESOURCE"]
    binaries = [eo / "winmm.dll"] + sorted((eo / "mods").glob("*/plugins/*.dll"))
    for binary in binaries:
        data = binary.read_bytes()
        hits = [hex(rva) for rva in sites if struct.pack("<I", rva) in data]
        check(not hits, f"{binary.name} names none of the hooked sites ({hits})")
    for binary in binaries:
        if binary.name.lower() == "winmm.dll":
            continue
        for path in binary.parent.glob("*.ini"):
            text = path.read_text(encoding="utf-8", errors="replace")
            numbers = {int(v) for v in re.findall(r"^\s*\w*Keyboard\s*=\s*(\d+)", text, re.M)}
            ours_vk = {VK_F1 + int(k[1:]) - 1 for k in ours}
            check(not numbers & ours_vk, f"{path.name}: its keyboard settings do not use our keys")


def shadow_profile(rgba, width, height):
    """The drop shadow of a logo: (shadow pixels per art pixel, their mean alpha, how far below the art they are).

    Shadow: translucent and dark (alpha 16 or more, colour under a quarter of
    it, premultiplied); art: at least half opaque and bright."""
    shadow, art = [], []
    for i in range(width * height):
        r, g, b, a = rgba[i * 4:i * 4 + 4]
        if a >= 16 and max(r, g, b) * 4 < a:
            shadow.append(i)
        if a >= 128 and max(r, g, b) * 2 >= a:
            art.append(i)
    if not shadow or not art:
        return 0.0, 0.0, 0.0
    below = sum(i // width for i in shadow) / len(shadow) - sum(i // width for i in art) / len(art)
    return len(shadow) / len(art), sum(rgba[i * 4 + 3] for i in shadow) / len(shadow), below


def test_game_textures(game_exe, output):
    """The game's radio logos are 196x96 (one 196x104) with a drop shadow, ours alike; RGBA8 UI textures written back."""
    print("game textures")
    if not game_exe:
        print("  skip the game's textures (pass --game-exe or set FH2_GAME_EXE)")
        return
    textures = Path(game_exe).parent / "media" / "ui" / "textures"
    if not (textures / "Anthem.zip").is_file():
        print(f"  skip the game's textures (no {textures / 'Anthem.zip'})")
        return
    with zipfile.ZipFile(textures / "Anthem.zip") as archive:
        icons = {name: archive.read(name) for name in archive.namelist() if "HUD_RadioIcon_" in name}
    sizes = {name.split("/")[-1]: struct.unpack_from("<HH", data, 0x40) for name, data in icons.items()}
    check(len(sizes) == 9 and all(w == 196 and h in (96, 104) for w, h in sizes.values()) and
          sum(h == 96 for _, h in sizes.values()) >= 7, f"the game's 9 station logos are 196x96 (or 196x104) ({sizes})")
    check(not any(name.startswith("HUD_RadioIcon_CustomRadio") for name in sizes),
          "none of them has the plugin's names (the plugin's are new files)")
    ours = logo_texture.encode(bytes(196 * 96 * 4), 196, 96)
    xs = icons.get("HUD/HUD_RadioIcon_XS.swatchbin", b"")
    check(ours[:83] == xs[:83] and len(ours) == len(xs),
          f"our logo's header and size are the game's 196x96 logo's (HUD_RadioIcon_XS), byte for byte ({len(ours)} bytes)")
    tiled = True
    for name, data in icons.items():
        width, height = struct.unpack_from("<HH", data, 0x40)
        blocks = data[83:]
        used = {logo_texture.block_index(bx, by) for by in range(-(-height // 4)) for bx in range(-(-width // 4))}
        padding = [k for k in range(len(blocks) // 16) if k not in used]
        tiled &= data[0x4F] == 0x2E and data[0x52] == 0xB8 and len(blocks) == 64 * 32 * 16 and             all(not any(blocks[k * 16:k * 16 + 16]) for k in padding)
    check(tiled, "the game's logos are BC7 (0x2e), 64x32 blocks, and every block outside the picture is empty where "
                 "logo_texture.block_index puts the padding (the block tiling)")
    # The game's logos have a soft dark drop shadow baked in (what keeps them
    # readable over a bright road); ours have the same (logo_texture.shadow).
    profiles = {}
    for name, data in icons.items():
        width, height, rgba = logo_texture.decode_any(data)
        profiles[name.split("/")[-1]] = shadow_profile(rgba, width, height)
    check(all(0.25 <= share <= 1.0 and 25 <= alpha <= 65 and below >= 2 for share, alpha, below in profiles.values()),
          "the game's 9 station logos have a drop shadow (dark, translucent, below the art): " +
          ", ".join(f"{n[14:-10]} {s:.2f}/{a:.0f}/{b:.1f}" for n, (s, a, b) in sorted(profiles.items())))
    folder = output / "mods" / MOD_NAME / "plugins" / LOGO_FOLDER
    for texture in LOGOS.values():
        path = folder / f"{texture}.swatchbin"
        rgba = logo_texture.decode(path.read_bytes())[2] if path.is_file() else bytes(196 * 96 * 4)
        share, alpha, below = shadow_profile(rgba, 196, 96)
        check(min(p[0] for p in profiles.values()) <= share <= max(p[0] for p in profiles.values()) and
              min(p[1] for p in profiles.values()) <= alpha <= max(p[1] for p in profiles.values()) and
              min(p[2] for p in profiles.values()) <= below <= max(p[2] for p in profiles.values()),
              f"{texture}: its drop shadow is the game's logos' (shadow {share:.2f} of the art, alpha {alpha:.0f}, "
              f"{below:.1f} px below; all within the game's logos' range)")
    checked = 0
    for zip_name in ("Anthem.zip", "Data_Bound.zip"):
        if not (textures / zip_name).is_file():
            continue
        with zipfile.ZipFile(textures / zip_name) as archive:
            for name in archive.namelist():
                data = archive.read(name)
                if not name.endswith(".swatchbin") or len(data) < 84 or data[0x4F] != 0xAE or data[0x4C] != 1:
                    continue
                width, height = struct.unpack_from("<HH", data, 0x40)
                if width * height > 512 * 512:
                    continue
                again = logo_texture.encode_rgba8(logo_texture.decode_rgba8(data)[2], width, height)
                checked += 1
                if not check(again[:0x52] == data[:0x52] and again[0x53:] == data[0x53:] and len(again) == len(data),
                             f"{zip_name}/{name} ({width}x{height}): written back byte for byte (but its flags byte)"):
                    break
    check(checked >= 20, f"{checked} RGBA8 UI textures of the game checked")


def png_bytes(width, height, rgba, colour=6):
    """A PNG (8-bit RGBA, or RGB with colour=2) of `rgba` rows."""
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
    channels = 4 if colour == 6 else 3
    raw = b"".join(b"\0" + b"".join(rgba[(y * width + x) * 4:(y * width + x) * 4 + channels] for x in range(width))
                   for y in range(height))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, colour, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def pattern(width, height, opaque=True):
    """A test image: a colour gradient, and an alpha ramp unless `opaque`."""
    out = bytearray()
    for y in range(height):
        for x in range(width):
            out += bytes((x * 255 // max(1, width - 1), y * 255 // max(1, height - 1), 128,
                          255 if opaque else (x + y) * 255 // max(1, width + height - 2)))
    return bytes(out)


def test_logo(dll, work):
    print("logo")
    dll.CustomRadioTestLogoEncode.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
    dll.CustomRadioTestLogoFromPng.argtypes = [ctypes.c_wchar_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_uint),
                                               ctypes.POINTER(ctypes.c_uint)]
    dll.CustomRadioTestLogoFromPng.restype = ctypes.c_char_p
    pixels = bytes((i * 7 + i // 3) & 255 for i in range(196 * 96 * 4))
    output = ctypes.create_string_buffer(LOGO_SWATCH_SIZE)
    dll.CustomRadioTestLogoEncode(pixels, output)
    check(output.raw == logo_texture.encode(pixels, 196, 96),
          "the DLL writes the same 196x96 BC7 swatchbin as tools/logo_texture.py (header, blocks and tiling)")

    folder = work / "logos"
    folder.mkdir()

    def from_png(name, data=None):
        path = folder / name
        if data is not None:
            path.write_bytes(data)
        width, height = ctypes.c_uint(), ctypes.c_uint()
        texture = ctypes.create_string_buffer(LOGO_SWATCH_SIZE)
        why = dll.CustomRadioTestLogoFromPng(str(path), texture, ctypes.byref(width), ctypes.byref(height))
        if why:
            return why.decode(), (width.value, height.value), None
        return None, (width.value, height.value), logo_texture.decode(texture.raw)[2]

    def column(rgba, x):
        return [rgba[(y * 196 + x) * 4 + 3] for y in range(96)]

    def row(rgba, y):
        return [rgba[(y * 196 + x) * 4 + 3] for x in range(196)]

    source = pattern(196, 96, opaque=False)
    why, size, got = from_png("exact.png", png_bytes(196, 96, source))
    expected = logo_texture.shadow(logo_texture.premultiply(source), 196, 96)
    check(why is None and size == (196, 96) and got is not None and
          sum(abs(a - b) for a, b in zip(got, expected)) / len(expected) < 4,
          f"Logo= 196x96 RGBA: used as it is, premultiplied, with the drop shadow ({why})")
    # A white square: the DLL's drop shadow is tools/logo_texture.py's, byte for
    # byte, and the game's: 2 px right, 3 px down, soft, dark, about 30%.
    square = bytearray(196 * 96 * 4)
    for y in range(30, 60):
        for x in range(100, 160):
            square[(y * 196 + x) * 4:(y * 196 + x) * 4 + 4] = bytes((255, 255, 255, 255))
    why, size, got = from_png("white.png", png_bytes(196, 96, bytes(square)))
    shadowed = logo_texture.shadow(bytes(square), 196, 96)
    texture = ctypes.create_string_buffer(LOGO_SWATCH_SIZE)
    dll.CustomRadioTestLogoFromPng(str(folder / "white.png"), texture, ctypes.byref(ctypes.c_uint()),
                                   ctypes.byref(ctypes.c_uint()))

    def pixel(rgba, x, y):
        return tuple(rgba[(y * 196 + x) * 4:(y * 196 + x) * 4 + 4])

    check(why is None and texture.raw == logo_texture.encode(shadowed, 196, 96),
          "the DLL's drop shadow and encoding are tools/logo_texture.py's, byte for byte")
    below, right, further = pixel(shadowed, 130, 60), pixel(shadowed, 160, 45), pixel(shadowed, 130, 65)
    check(pixel(shadowed, 130, 45) == (255, 255, 255, 255) and below[:3] == right[:3] == further[:3] == (0, 0, 0) and
          60 <= below[3] <= 77 and 50 <= right[3] <= 77 and right[3] < below[3] and 0 < further[3] < 10 and
          pixel(shadowed, 130, 28) == pixel(shadowed, 96, 45) == pixel(shadowed, 130, 67) == (0, 0, 0, 0),
          "the shadow: black, under the art, 2 px right and 3 px down, at most 30%, fading out within 6 px "
          f"(below {below}, right {right}, further {further})")

    def area(rgba, left, top, right, bottom):
        return [rgba[(y * 196 + x) * 4 + 3] for y in range(top, bottom) for x in range(left, right)]

    why, size, got = from_png("double.png", png_bytes(392, 192, pattern(392, 192)))
    check(why is None and size == (392, 192) and got is not None and min(area(got, 11, 2, 189, 89)) >= 250 and
          max(area(got, 0, 0, 8, 96)) < 8 and max(area(got, 0, 0, 196, 1)) < 8,
          f"Logo= 392x192: scaled down to 178x87 into the art area (x 11..188, y 2..88) ({why})")
    why, size, got = from_png("square.png", png_bytes(100, 100, pattern(100, 100)))
    check(why is None and got is not None and max(area(got, 0, 0, 99, 96)) < 8 and
          min(area(got, 102, 2, 189, 89)) >= 250 and 0 < max(area(got, 191, 10, 192, 80)) < 128,
          f"Logo= 100x100: fitted to 87x87 against the art area's right edge, its shadow beside it ({why})")
    why, size, got = from_png("wide.png", png_bytes(300, 50, pattern(300, 50), colour=2))
    check(why is None and size == (300, 50) and got is not None and max(area(got, 0, 0, 196, 25)) < 8 and
          min(area(got, 1, 31, 189, 60)) >= 250 and max(area(got, 0, 75, 196, 96)) < 8,
          f"Logo= 300x50 RGB (no alpha): opaque, fitted to 188x31 and centred in the art area ({why})")
    refused = {
        "missing.png": (None, "not found"),
        "text.png": (b"not a picture at all", "not a PNG file"),
        "tiny.png": (png_bytes(4, 4, pattern(4, 4)), "outside 8x8 to 4096x4096"),
        "huge.png": (png_bytes(5000, 8, pattern(5000, 8)), "outside 8x8 to 4096x4096"),
        "clear.png": (png_bytes(64, 32, bytes(64 * 32 * 4)), "fully transparent"),
        "broken.png": (b"\x89PNG\r\n\x1a\n" + b"\0" * 40, "could not be decoded"),
        # Cut off in its pixel data: WIC reads what is there (nothing visible) or gives up.
        "cut.png": (png_bytes(64, 32, pattern(64, 32))[:60], "could not be|fully transparent"),
    }
    for name, (data, reason) in refused.items():
        why, _, got = from_png(name, data)
        check(why is not None and any(r in why for r in reason.split("|")) and got is None,
              f"Logo= {name}: refused ({why})")


# ------------------------------------------------------------- DLL helpers

class RadioConfig(ctypes.Structure):
    _fields_ = [("enabled", ctypes.c_int), ("unlock_all", ctypes.c_int), ("pcm_decode", ctypes.c_int),
                ("volume", ctypes.c_int), ("folder", ctypes.c_wchar * 1024), ("name", ctypes.c_char * 256),
                ("keyboard", ctypes.c_uint * 3), ("xbox", ctypes.c_uint * 3), ("logo", ctypes.c_wchar * 1024)]


LOG_FN = ctypes.CFUNCTYPE(None, ctypes.c_char_p)


def load_test_dll(output, log, path=None):
    dll = ctypes.CDLL(str(path or output / "obj" / "CustomRadio-test.dll"))
    sink = LOG_FN(lambda line: log.append(line.decode("utf-8", "replace")))
    dll.CustomRadioTestBindLog.argtypes = [LOG_FN]
    dll.CustomRadioTestBindLog(sink)
    dll._sink = sink
    dll.CustomRadioTestConfig.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.POINTER(RadioConfig), ctypes.c_uint]
    dll.CustomRadioTestParseKey.argtypes = [ctypes.c_wchar_p, ctypes.c_int, ctypes.POINTER(ctypes.c_uint)]
    dll.CustomRadioTestKeyEdges.argtypes = [ctypes.c_uint, ctypes.c_int]
    dll.CustomRadioTestKeyEdges.restype = ctypes.c_uint
    dll.CustomRadioTestDescribe.argtypes = [ctypes.c_wchar_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_uint)]
    dll.CustomRadioTestDecode.argtypes = [ctypes.c_char_p, ctypes.c_uint, ctypes.c_char_p, ctypes.POINTER(ctypes.c_uint)]
    dll.CustomRadioTestDefaultName.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    dll.CustomRadioTestDefaultName.restype = ctypes.c_char_p
    return dll


def test_names(output, work):
    """The default station name by the game's text language (fh2_text_language) and RadioInfo's file."""
    print("names")
    log = []
    test_dll = output / "obj" / "CustomRadio-test.dll"
    with_pack = load_test_dll(output, log, lt.plugin_in_mods(lt.ukrainian_mods(work / "names"), MOD_NAME, test_dll))
    without = load_test_dll(output, log, lt.plugin_in_mods(lt.fresh_mods(work / "names", "plain"), MOD_NAME, test_dll))
    game = lt.FakeGame()
    name = lambda dll, resource: dll.CustomRadioTestDefaultName(game.base, resource).decode("utf-8")
    for code, resource, pack, want in (("EN", b"media\\audio\\RadioInfo_en.xml", 0, "EN"),
                                       ("PL", b"media\\audio\\RadioInfo_pl.xml", 0, "PL"),
                                       ("RU", b"media\\audio\\RadioInfo_ru.xml", 0, "EN"),
                                       ("RU", b"media\\audio\\RadioInfo_ru.xml", 1, "UK"),
                                       ("PL", b"media\\audio\\RadioInfo_pl.xml", 1, "PL"),
                                       ("DE", b"media\\audio\\RadioInfo_de.xml", 1, "EN")):
        game.set(code)
        got = name(with_pack if pack else without, resource)
        check(got == DEFAULT_NAMES[want], f"game text {code}, {resource.decode().split(chr(92))[-1]}, "
                                          f"{'the' if pack else 'no'} Ukrainian pack: {got}")
    game.set("RU")
    check(with_pack.CustomRadioTestDefaultName(game.base, b"RadioInfo_ru.xml") == "МОЯ МУЗИКА".encode("utf-8"),
          "the Ukrainian name is UTF-8 in RadioInfo, as Name= is")
    lt.check_selection(check, work / "selection", test_dll, MOD_NAME, lambda path: load_test_dll(output, log, path),
                       lambda dll, base: dll.CustomRadioTestDefaultName(base, b"RadioInfo.xml").decode("utf-8"),
                       lambda code: DEFAULT_NAMES["UK" if code == "UK" else "EN"])


# ------------------------------------------------------------- 4. config

def test_config(dll, log, work, shipped_ini):
    print("config")
    check(dll.CustomRadioTestConfigSize() == ctypes.sizeof(RadioConfig), "RadioConfig layout matches")
    base = str(work / "Azimuth")

    def load(text):
        path = work / "CustomRadio.ini"
        path.write_text(text, encoding="utf-16")
        log.clear()
        config = RadioConfig()
        dll.CustomRadioTestConfig(str(path), base, ctypes.byref(config), ctypes.sizeof(config))
        return config, list(log)

    config, lines = load("")
    check(config.enabled == 1 and config.unlock_all == 0 and config.pcm_decode == 1 and config.volume == 100,
          "no INI: station on, UnlockAllStations off, PcmDecode on, Volume 100")
    check(config.folder == base + "\\Music", f"no INI: folder is Music beside the launcher ({config.folder})")
    check(list(config.keyboard) == [0x78, 0x76, 0x75] and list(config.xbox) == [0, 0, 0] and config.name == b""
          and config.logo == "", "no INI: keys F9/F7/F6, no buttons, default name, the built-in logo")
    check(not lines, "no INI: nothing logged")

    shipped = Path(shipped_ini).read_text(encoding="utf-8")
    config, lines = load(shipped)
    check(config.enabled == 1 and config.volume == 100 and list(config.keyboard) == [0x78, 0x76, 0x75]
          and config.folder == base + "\\Music" and not lines,
          "the shipped CustomRadio.ini gives the defaults without a warning")

    config, lines = load("[CustomRadio]\nEnabled=0\nFolder=..\\Songs\\\nName=MY TUNES & MORE\nVolume=150\n"
                         "UnlockAllStations=1\nPcmDecode=0\n[Keys]\nRadioStationKeyboard=F10\n"
                         "RadioNextKeyboard=0x41\nRadioRestartKeyboard=None\nRadioNextXbox=LB+Y\n"
                         "RadioStationXbox=dpadup\n")
    check(config.enabled == 0 and config.unlock_all == 1 and config.pcm_decode == 0 and config.volume == 150,
          "valid values: Enabled=0, UnlockAllStations=1, PcmDecode=0, Volume=150")
    check(config.folder == str(work / "Songs"), f"relative folder with .. is resolved ({config.folder})")
    check(config.name == "MY TUNES & MORE".encode(), "Name is kept")
    check(list(config.keyboard) == [0x79, 0x41, 0] and list(config.xbox) == [0x1, 0x100 | 0x8000, 0],
          "keys F10 / 0x41 / None, buttons DPadUp / LB+Y")
    check(not lines, f"valid values: nothing logged ({lines})")

    config, lines = load("[CustomRadio]\nLogo=  logos\\my logo.png  \n")
    check(config.logo == "logos\\my logo.png" and not lines, "Logo= is kept as written (trimmed; checked at start)")
    config, _ = load("[CustomRadio]\nFolder=D:\\My Music\n")
    check(config.folder == "D:\\My Music", "absolute folder is kept")
    config, _ = load("[CustomRadio]\nFolder=\\\\server\\share\\mp3\n")
    check(config.folder == "\\\\server\\share\\mp3", "UNC folder is kept")

    config, lines = load("[CustomRadio]\nEnabled=yes\nVolume=401\nPcmDecode=2\n[Keys]\n"
                         "RadioStationKeyboard=F25\nRadioNextXbox=LB+Q\n")
    check(config.enabled == 1 and config.volume == 100 and config.pcm_decode == 1 and
          config.keyboard[0] == 0x78 and config.xbox[1] == 0, "bad values: defaults used")
    check(sum("is not valid" in line for line in lines) == 5, f"bad values: each logged ({len(lines)} lines)")
    check(any("[CustomRadio] Volume=401 is not valid; default used: 100" in line for line in lines),
          "the log names the setting, value and default")
    config, lines = load("[CustomRadio]\nVolume=-5\n")
    check(config.volume == 100 and len(lines) == 1, "negative volume refused")
    config, lines = load("[CustomRadio]\nVolume=0x10\n")
    check(config.volume == 100 and len(lines) == 1, "hex volume refused")
    config, lines = load("[CustomRadio]\nSingleEntry=1\nEnabled=1\n[Keys]\nRadioPause=F8\n")
    check(sum("unknown key ignored" in line for line in lines) == 2 and config.enabled == 1,
          "unknown keys are logged and ignored")

    def parse(text, xbox=0):
        value = ctypes.c_uint(0xFFFF)
        ok = dll.CustomRadioTestParseKey(text, xbox, ctypes.byref(value))
        return value.value if ok else None

    good = {"F1": 0x70, "F24": 0x87, "f6": 0x75, " F9 ": 0x78, "A": 0x41, "z": 0x5A, "7": 0x37, "Esc": 0x1B,
            "PageDown": 0x22, "0x41": 0x41, "120": 120, "None": 0, "none": 0}
    for text, value in good.items():
        check(parse(text) == value, f"keyboard {text!r} -> {value:#x}")
    for text in ("F0", "F25", "F01", "0x100", "255", "Ctrl+A", "", "Q2"):
        check(parse(text) is None, f"keyboard {text!r} refused")
    pads = {"A": 0x1000, "LB+Y": 0x8100, "View": 0x20, "back": 0x20, "LS + RS": 0xC0, "None": 0, "DPadDown": 2}
    for text, value in pads.items():
        check(parse(text, 1) == value, f"xbox {text!r} -> {value:#x}")
    for text in ("LT", "RT", "LB+", "Q", "A++B"):
        check(parse(text, 1) is None, f"xbox {text!r} refused")


# --------------------------------------------------------------- 5. keys

def test_keys(dll):
    print("keys")
    steps = [  # (down, enabled, expected pressed)
        (0b010, 1, 0b010, "a press while driving fires"),
        (0b010, 1, 0, "holding does not repeat"),
        (0, 1, 0, "release fires nothing"),
        (0b010, 1, 0b010, "the next press fires again"),
        (0, 1, 0, ""),
        (0b001, 0, 0, "a press in a menu does not fire"),
        (0b001, 1, 0, "a key held from the menu does not fire when driving resumes"),
        (0, 1, 0, ""),
        (0b001, 1, 0b001, "after release it fires"),
        (0b111, 1, 0b110, "only the newly pressed actions fire"),
    ]
    for down, enabled, expected, what in steps:
        got = dll.CustomRadioTestKeyEdges(down, enabled)
        if what:
            check(got == expected, f"{what} ({got:#05b})")


# ---------------------------------------------------------------- 6. mp3

FRAME_HEADER = bytes([0xFF, 0xFB, 0x94, 0x00])  # MPEG-1 Layer III, 128 kbps, 48 kHz, stereo
FRAME_SIZE = 384                                 # 144 * 128000 / 48000, no padding


def silent_frame():
    # Zero side information: no Huffman data, so every granule decodes to silence.
    return FRAME_HEADER + bytes(FRAME_SIZE - 4)


def info_frame(frames):
    frame = bytearray(silent_frame())
    frame[36:40] = b"Info"
    frame[40:44] = struct.pack(">I", 1)  # frame count present
    frame[44:48] = struct.pack(">I", frames)
    return bytes(frame)


def id3v2(title=None, artist=None):
    def frame(name, payload):
        return name + struct.pack(">I", len(payload)) + b"\0\0" + payload
    body = b""
    if title is not None:
        body += frame(b"TIT2", b"\x01\xff\xfe" + title.encode("utf-16-le") + b"\0\0")
    if artist is not None:
        body += frame(b"TPE1", b"\x00" + artist.encode("latin-1"))
    size = len(body)
    syncsafe = bytes([(size >> 21) & 0x7F, (size >> 14) & 0x7F, (size >> 7) & 0x7F, size & 0x7F])
    return b"ID3\x03\x00\x00" + syncsafe + body


def id3v1(title, artist):
    return (b"TAG" + title.encode("latin-1").ljust(30, b"\0") + artist.encode("latin-1").ljust(30, b"\0")
            + bytes(30) + b"2026" + bytes(30) + b"\xff")


def make_mp3(seconds, xing=True, tag=b"", tail=b""):
    frames = round(seconds * 48000 / 1152)
    return tag + (info_frame(frames) if xing else b"") + silent_frame() * frames + tail, frames


def write_music(folder):
    """The test music folder; returns the expected playable tracks in folder order."""
    folder.mkdir(parents=True, exist_ok=True)
    data, frames = make_mp3(5, tag=id3v2('T\u00ebst & "S\u00f3ng"', "Tester <1>"))
    (folder / "01 tagged.mp3").write_bytes(data)
    expected = [("T\u00ebst & 'S\u00f3ng'", "Tester (1)", frames * 24)]
    data, frames = make_mp3(4, xing=False)
    (folder / "02 plain.mp3").write_bytes(data)
    expected.append(("02 plain", "", frames * FRAME_SIZE * 8 // 128))
    data, frames = make_mp3(6, tail=id3v1("Old Tag", "V1 Artist"))
    (folder / "03 v1.mp3").write_bytes(data)
    expected.append(("Old Tag", "V1 Artist", frames * 24))
    if flac_decoder():
        # A FLAC between the MP3s (folder order mixes the formats): 5 s of 16-bit stereo, Vorbis comments.
        rate = 44100
        tone = [int(math.sin(i * 2 * math.pi * 440 / rate) * 8000) for i in range(rate * 5)]
        (folder / "02b lossless.flac").write_bytes(flac([tone, tone], rate, 16, ["TITLE=Lossless & \"Free\"",
                                                                                  "ARTIST=Flac <Band>"]))
        expected.insert(2, ("Lossless & 'Free'", "Flac (Band)", 5000))
    (folder / "04 short.mp3").write_bytes(make_mp3(1)[0])
    (folder / "notes.txt").write_text("not music")
    (folder / "cover.png").write_bytes(b"\x89PNG not music")
    return expected


def flac_decoder():
    """Windows' Media Foundation with its FLAC decoder (Windows 10 1709 and later)."""
    system = Path(os.environ.get("WINDIR", r"C:\Windows")) / "System32"
    return (system / "mfreadwrite.dll").is_file() and (system / "MSFlacDecoder.dll").is_file()


def test_media(dll, work):
    """FLAC (generated: 8/16/24-bit, mono/stereo, 44.1/96 kHz) and WAV through Media Foundation; the user's own
    FLAC files (read only) when there are some in FH2_MUSIC_DIR."""
    print("media (FLAC, WAV, M4A, WMA)")
    if not flac_decoder():
        if os.environ.get("CI"):
            print("  skip: this Windows has no Media Foundation FLAC decoder")
            return
        check(False, "Windows' Media Foundation FLAC decoder (MSFlacDecoder.dll) is there")
        return
    dll.CustomRadioTestDescribe.argtypes = [ctypes.c_wchar_p, ctypes.c_char_p, ctypes.c_char_p,
                                            ctypes.POINTER(ctypes.c_uint)]
    dll.CustomRadioTestDecodeFile.argtypes = [ctypes.c_wchar_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_uint),
                                              ctypes.c_char_p, ctypes.c_uint]
    folder = work / "media"
    folder.mkdir(parents=True, exist_ok=True)

    def describe(path):
        title, artist, duration = ctypes.create_string_buffer(256), ctypes.create_string_buffer(256), ctypes.c_uint()
        dll.CustomRadioTestDescribe(str(path), title, artist, ctypes.byref(duration))
        return title.value.decode("utf-8"), artist.value.decode("utf-8"), duration.value

    def decode(path, capacity=8000):
        header, size, pcm = ctypes.create_string_buffer(44), ctypes.c_uint(), ctypes.create_string_buffer(capacity)
        result = dll.CustomRadioTestDecodeFile(str(path), header, ctypes.byref(size), pcm, capacity)
        tag, channels, rate, byte_rate, align, bits = struct.unpack_from("<HHIIHH", header.raw, 20)
        return result, (tag, channels, rate, byte_rate, align, bits), size.value, pcm.raw

    for bits in (8, 16, 24):
        for count in (1, 2):
            for rate in (44100, 96000):
                seconds = 4
                top = (1 << (bits - 1)) - 1
                channels = [[int(math.sin(i * 2 * math.pi * 440 * (c + 1) / rate) * top * 0.5)
                             for i in range(rate * seconds)] for c in range(count)]
                name = f"tone {bits}-bit {count}ch {rate}.flac"
                path = folder / name
                path.write_bytes(flac(channels, rate, bits, [f"TITLE=T\u00ebst {bits}", "artist=Somebody"]))
                check(describe(path) == (f"T\u00ebst {bits}", "Somebody", seconds * 1000),
                      f"{name}: TITLE/ARTIST (any case) and the length from STREAMINFO")
                result, fmt, size, pcm = decode(path)
                out_rate = 44100 if rate == 44100 else 48000
                check(result == 1 and fmt == (1, 2, out_rate, out_rate * 4, 4, 16) and
                      abs((size - 44) // 4 - out_rate * seconds) <= out_rate // 100,
                      f"{name}: decoded to a {out_rate} Hz 16-bit stereo WAV ({fmt}, {(size - 44) // 4} samples)")
                if rate == 44100:
                    got = struct.unpack_from("<1000h", pcm, 0)
                    want = []
                    for i in range(500):
                        for c in range(2):
                            sample = channels[c if count == 2 else 0][i]
                            want.append(sample << 8 if bits == 8 else sample >> 8 if bits == 24 else sample)
                    check(all(abs(g - w) <= 1 for g, w in zip(got, want)),
                          f"{name}: the samples (16-bit{', mono on both sides' if count == 1 else ''})")
    rate = 22050
    wav = folder / "plain.wav"
    with wave.open(str(wav), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(struct.pack(f"<{rate * 3}h", *[(i * 37) % 2000 - 1000 for i in range(rate * 3)]))
    result, fmt, size, pcm = decode(wav)
    check(describe(wav) == ("plain", "", 3000), f"plain.wav: the file name as its title, 3 s ({describe(wav)})")
    check(result == 1 and fmt[:3] == (1, 2, rate) and struct.unpack_from("<4h", pcm, 8) == (-926, -926, -889, -889),
          f"plain.wav: decoded to 16-bit stereo at its own rate ({fmt})")
    broken = folder / "broken.flac"
    broken.write_bytes(b"fLaC" + bytes(64))
    check(describe(broken)[2] == 0 and decode(broken)[0] == 0, "a broken FLAC: no length (skipped), not decoded")
    mine = Path(os.environ.get("FH2_MUSIC_DIR", r"D:\Games\Forza Horizon 2\Music"))
    files = sorted(mine.glob("*.flac")) if mine.is_dir() else []
    if not files:
        print(f"  skip the user's FLAC files (none in {mine}; FH2_MUSIC_DIR names a folder, only read)")
        return
    described = [describe(f) for f in files]
    check(all(t and d > 3000 for t, _, d in described),
          f"{len(files)} FLAC files of {mine} (read only): titles and lengths read")
    result, fmt, size, _ = decode(files[0])
    check(result == 1 and fmt[1:3] in ((2, 44100), (2, 48000)) and
          abs((size - 44) // fmt[3] * 1000 - described[0][2]) < 1500,
          f"{files[0].name}: decoded whole ({size // (1 << 20)} MB of PCM for {described[0][2] // 1000} s)")


def test_mp3(dll, work):
    print("mp3")
    music = work / "Music"
    expected = [e for e in write_music(music) if e[1] != "Flac (Band)"]  # the MP3s
    title, artist, duration = ctypes.create_string_buffer(256), ctypes.create_string_buffer(256), ctypes.c_uint()
    for name, (want_title, want_artist, want_length) in zip(("01 tagged.mp3", "02 plain.mp3", "03 v1.mp3"), expected):
        dll.CustomRadioTestDescribe(str(music / name), title, artist, ctypes.byref(duration))
        got = (title.value.decode("utf-8"), artist.value.decode("utf-8"), duration.value)
        raw = ('T\u00ebst & "S\u00f3ng"', "Tester <1>") if name.startswith("01") else (want_title, want_artist)
        check(got == (raw[0], raw[1], want_length), f"{name}: title/artist/length {got}")
    dll.CustomRadioTestDescribe(str(music / "04 short.mp3"), title, artist, ctypes.byref(duration))
    check(duration.value < 3000, f"04 short.mp3 is under 3 s ({duration.value} ms)")
    for name, frames in (("01 tagged.mp3", 208), ("02 plain.mp3", 167)):
        data = (music / name).read_bytes()
        header, size = ctypes.create_string_buffer(44), ctypes.c_uint()
        result = dll.CustomRadioTestDecode(data, len(data), header, ctypes.byref(size))
        if result == -1:
            if os.environ.get("CI"):
                print("  skip decode: this CI image has no Windows MP3 codec (msacm32)")
            else:
                check(False, "Windows MP3 codec (msacm32) available")
            continue
        (riff, _, wave, _, _, tag, channels, rate, byte_rate, align, bits, data_id,
         data_size) = struct.unpack("<4sI4s4sIHHIIHH4sI", header.raw)
        samples = data_size // 4
        check(result == 1 and riff == b"RIFF" and wave == b"WAVE" and data_id == b"data" and tag == 1 and
              channels == 2 and rate == 48000 and bits == 16 and align == 4 and byte_rate == 192000 and
              size.value == 44 + data_size,
              f"{name}: decoded by Windows to a 48 kHz 16-bit stereo WAV")
        check(abs(samples - frames * 1152) <= 2 * 1152,
              f"{name}: {samples} samples for {frames} frames ({frames * 1152})")
    data = bytes(4096)
    header, size = ctypes.create_string_buffer(44), ctypes.c_uint()
    check(dll.CustomRadioTestDecode(data, len(data), header, ctypes.byref(size)) == 0,
          "a file without MP3 frames is not decoded (served as is)")


# ------------------------------------------------------ 7. plugin scenarios

def run_scenarios(output, work, game_exe=None, ff_exe=None):
    print("plugin")
    scenarios = ["vanilla", "chained", "core-mismatch", "optional-mismatch", "disabled", "unlock-only",
                 "slot-not-code", "wrong-size", "logo-user", "logo-refused", "no-media",
                 "ff-vanilla", "ff-chained", "ff-wrong-stamp"]
    for scenario in scenarios:
        folder = work / f"scenario-{scenario}"
        result = subprocess.run([sys.executable, str(Path(__file__).resolve()), "--scenario", scenario,
                                 "--output", str(output), "--work", str(folder)] +
                                (["--game-exe", str(game_exe)] if game_exe else []) +
                                (["--ff-exe", str(ff_exe)] if ff_exe else []),
                                capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=300,
                                env={**os.environ, "PYTHONIOENCODING": "utf-8"})
        print(f" [{scenario}]")
        for line in result.stdout.splitlines():
            print(line)
            if line.startswith("  FAIL "):
                failures.append(f"{scenario}: {line[7:]}")
        if result.returncode != 0 and not any(line.startswith("  FAIL ") for line in result.stdout.splitlines()):
            check(False, f"scenario {scenario} exited with {result.returncode}: {result.stderr.strip()[-400:]}")


# The child side: one scenario per process (the plugin's hooks are process-wide).

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
kernel32.VirtualAlloc.restype = ctypes.c_void_p
kernel32.VirtualAlloc.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint32, ctypes.c_uint32]
kernel32.HeapAlloc.restype = ctypes.c_void_p
kernel32.HeapAlloc.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_size_t]
kernel32.GetProcessHeap.restype = ctypes.c_void_p
kernel32.VirtualQuery.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
kernel32.VirtualQuery.restype = ctypes.c_size_t


class Context(ctypes.Structure):
    _fields_ = [("size", ctypes.c_uint32), ("api_version", ctypes.c_uint32), ("adapter", ctypes.c_uint32),
                ("game_image_size", ctypes.c_uint32), ("game_image", ctypes.c_void_p),
                ("plugin_module", ctypes.c_void_p), ("plugins_directory", ctypes.c_wchar_p),
                ("plugin_path", ctypes.c_wchar_p), ("extra_options_version", ctypes.c_char_p),
                ("log_utf8", LOG_FN)]


class Frame(ctypes.Structure):
    _fields_ = [("size", ctypes.c_uint32), ("input_enabled", ctypes.c_uint32), ("car", ctypes.c_void_p),
                ("buttons", ctypes.c_uint32), ("delta_seconds", ctypes.c_float), ("flash", ctypes.c_void_p)]


KEY_STATE_FN = ctypes.WINFUNCTYPE(ctypes.c_short, ctypes.c_int)
SELECT_FN = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_void_p)

# Foreign hooks (another module's code) placed in the fake image's first page.
STUBS = {
    "xml": (0x2000, bytes([0xB0, 0x01, 0xC3])),                                  # mov al,1; ret
    "update": (0x2010, bytes([0xC7, 0x41, 0x3C, 0x55, 0x55, 0, 0, 0xC3])),       # mov dword [rcx+3Ch],5555h
    "next": (0x2020, bytes([0x48, 0x89, 0xD0, 0xC3])),                           # mov rax,rdx
    "sound": (0x2030, bytes([0xB8, 0x77, 0, 0, 0, 0xC3])),                       # mov eax,77h
    "subsound": (0x2040, bytes([0xB8, 0x55, 0, 0, 0, 0xC3])),                    # mov eax,55h
    "find": (0x2050, bytes([0x48, 0x89, 0xC8, 0xC3])),                           # mov rax,rcx
    "locked": (0x2060, bytes([0xB0, 0x01, 0xC3])),                               # every station locked
    "change": (0x2070, bytes([0xC7, 0x01, 0x34, 0x12, 0, 0, 0xC3])),             # mov dword [rcx],1234h
    "xaml": (0x2080, bytes([0x48, 0x89, 0xD0, 0xC3])),                           # mov rax,rdx (the output as set)
}
CHAINED_SLOTS = {"SLOT_XML_READ": "xml", "SLOT_PLAYER_UPDATE": "update", "SLOT_PLAYLIST_NEXT": "next"}
CHAINED_CALLS = {"CALL_SOUND_INFO": "sound", "CALL_SUBSOUND": "subsound", "CALL_HISTORY_FIND": "find",
                 "CALL_LOCKED_1": "locked", "CALL_LOCKED_2": "locked", "CALL_LOCKED_3": "locked",
                 "CALL_CHANGE_1": "change", "CALL_CHANGE_2": "change", "CALL_XAML_RESOURCE": "xaml"}

# The HUD radio display's XAML as the game has it, cut down to the station icon style.
RADIO_CONTROL = (
    '<UserControl\r\n    xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"\r\n'
    '    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"\r\n    xmlns:local="clr-namespace:ForzaUI"\r\n'
    '    x:Class="ForzaUI.HUDRadioControl"\r\n    x:Name="UserControl">\r\n\r\n'
    '    <UserControl.Resources>\r\n        <ResourceDictionary>\r\n'
    '            <Style x:Key="A_Style_FZMaterial_StationIcon" TargetType="local:FZMaterial">\r\n'
    '                <Setter Property="Height" Value="96" />\r\n                <Style.Triggers>\r\n'
    '                    <DataTrigger Binding="{Binding StationName}" Value="Horizon XS">\r\n'
    '                        <Setter Property="ImageSource" '
    'Value="/ForzaUI;component/Textures/Anthem/HUD/HUD_RadioIcon_XS.png" />\r\n'
    '                    </DataTrigger>\r\n                </Style.Triggers>\r\n            </Style>\r\n'
    '            <Style x:Key="A_Style_RadioTransitions" TargetType="ContentControl">\r\n'
    '                <Style.Triggers>\r\n                </Style.Triggers>\r\n            </Style>\r\n'
    '        </ResourceDictionary>\r\n    </UserControl.Resources>\r\n</UserControl>\r\n').encode()
TRIGGER = re.compile(r'<DataTrigger Binding="\{Binding StationName\}" Value="([^"]*)">\s*'
                     r'<Setter Property="ImageSource" Value="/ForzaUI;component/Textures/Anthem/HUD/([^"]*)\.png" />')
MARK = "<!-- Custom Radio: the custom station's logo -->"
DEFAULT_IMAGE = re.compile(r'<Setter Property="ImageSource" Value="/ForzaUI;component/Textures/Anthem/HUD/([^"]*)\.png" />'
                           r'\r?\n([ \t]*)(?=<Style\.Triggers>)')
# On Fast & Furious the logos take the names of FH2's station icons it ships
# unused: a texture name its Anthem.zip lacks is drawn as a white box.
FF_STOCK = {"HUD_RadioIcon_CustomRadio": "HUD_RadioIcon_Bass", "HUD_RadioIcon_CustomRadio_PL": "HUD_RadioIcon_Hospital",
            "HUD_RadioIcon_CustomRadio_UK": "HUD_RadioIcon_Innovative_Leisure",
            "HUD_RadioIcon_CustomRadio_Badge": "HUD_RadioIcon_Levante",
            "HUD_RadioIcon_CustomRadio_User": "HUD_RadioIcon_Ninja_Tune"}


def check_radio_control(original, edited, entries, media, what):
    """The radio display's XAML with one trigger per (name, texture) of `entries` right after the station icon
    style's <Style.Triggers>, and nothing else changed."""
    text = edited.decode("utf-8")
    # The style's default image: the logo of one of the names, on its own line before <Style.Triggers>
    # (the game loads an icon's texture when the HUD is made; one a trigger asks for later is a white box).
    default = DEFAULT_IMAGE.search(text)
    check(default is not None and default.group(1) in [texture for _, texture in entries],
          f"{what}: the style's default image is a logo it names ({default and default.group(1)})")
    if default:
        text = text[:default.start()] + text[default.end():]
    start = text.find(MARK)
    style = text.find('x:Key="A_Style_FZMaterial_StationIcon"')
    opening = text.find("<Style.Triggers>", style)
    end = text.find("</DataTrigger>", start + 1)
    for _ in entries[1:]:
        end = text.find("</DataTrigger>", end + 1)
    end = text.find("\n", end) + 1
    added = text[start:end]
    line_start = text.rfind("\n", 0, start) + 1
    check(text.count(MARK) == 1 and opening < start and text[opening:line_start].strip() == "<Style.Triggers>",
          f"{what}: the triggers come first in the station icon style's triggers")
    check(TRIGGER.findall(added) == entries, f"{what}: one trigger per name ({TRIGGER.findall(added)})")
    check(text[:line_start] + text[end:] == original.decode("utf-8"), f"{what}: nothing else changed")
    check(all((media / LOGO_FOLDER.relative_to("CustomRadio/Media") / f"{t}.swatchbin").is_file() for _, t in entries),
          f"{what}: each logo it names is in the media folder")
    try:
        ElementTree.fromstring(edited)
        parsed = True
    except ElementTree.ParseError:
        parsed = False
    check(parsed, f"{what}: well-formed XML")

RADIO_INFO = (b'<?xml version="1.0" encoding="utf-8"?>\r\n<RadioStations>\r\n'
              b'  <RadioStation Name="HORIZON PULSE" Number="1" RecentlyPlayedListSize="5">\r\n'
              b'  </RadioStation>\r\n  <RadioStation Name="HORIZON BASS ARENA" Number="7">\r\n'
              b'  </RadioStation>\r\n</RadioStations>\r\n')


# Fast & Furious' key tick in a fake image: real code behind the guards of the
# local player getter (mov rcx,[rcx+48h]; test rcx,rcx; jz; mov rax,[rcx+98h]; ret)
# and of the player's car getter (prologue; mov rax,[rcx]; mov [rdx],rax; pop rdi; ret).
FF_LOCAL_PLAYER_TAIL = bytes([0x74, 0x08, 0x48, 0x8B, 0x81, 0x98, 0, 0, 0, 0xC3, 0x31, 0xC0, 0xC3])
FF_PLAYER_CAR_TAIL = bytes([0x48, 0x8B, 0x01, 0x48, 0x89, 0x02, 0x5F, 0xC3])
FF_RET = 0x2090          # a plain ret (the car's release, vtable slot 3)
FF_CAR_UPDATE_STUB = 0x20A0  # another module's car update: mov dword [rcx+8], 4321h
FF_STUBS = {FF_RET: bytes([0xC3]), FF_CAR_UPDATE_STUB: bytes([0xC7, 0x41, 0x08, 0x21, 0x43, 0, 0, 0xC3])}


class FakeGame:
    def __init__(self, ff=False, stamp=FF_STAMP):
        self.ff = ff
        self.size = FF_IMAGE_SIZE if ff else GAME_IMAGE_SIZE
        self.base = kernel32.VirtualAlloc(None, self.size, 0x3000, 0x40)  # RWX
        if not self.base:
            raise OSError("VirtualAlloc failed")
        self.functions, self.slots, self.calls, self.data = load_sites("ff_sites.h" if ff else "sites.h")
        if ff:
            # The PE header the plugin reads the TimeDateStamp from.
            self.write(0, b"MZ")
            self.write(0x3C, struct.pack("<I", 0x100))
            self.write(0x100, b"PE\0\0" + struct.pack("<HHI", 0x8664, 1, stamp))
            self.extra, guards = load_ff_extra()
            source = (ROOT / "src" / "ff_sites.h").read_text(encoding="utf-8")
            for name in ("RVA_FORZA_CAR_VTABLE", "RVA_PLAYER_MANAGER"):
                self.data[name] = int(re.search(rf"#define FF_{name} (0x[0-9a-fA-F]+)u", source).group(1), 16)
            for name, guard in guards.items():
                self.write(self.extra[name], guard)
            self.write(self.extra["FF_FN_LOCAL_PLAYER"] + 7, FF_LOCAL_PLAYER_TAIL)
            self.write(self.extra["FF_FN_PLAYER_CAR"] + 16, FF_PLAYER_CAR_TAIL)
            for rva, code in FF_STUBS.items():
                self.write(rva, code)
            self.write(self.data["RVA_FORZA_CAR_VTABLE"] + 0x18, struct.pack("<Q", self.base + FF_RET))
            self.write(self.extra["FF_SLOT_CAR_UPDATE"], struct.pack("<Q", self.base + self.extra["FF_FN_CAR_UPDATE"]))
        for rva, guard in self.functions.values():
            self.write(rva, guard)
        for rva, function in self.slots.values():
            self.write(rva, struct.pack("<Q", self.base + self.functions[function][0]))
        for rva, function, before, after in self.calls.values():
            self.write(rva - 8, before + b"\xE8" + struct.pack("<i", self.functions[function][0] - rva - 5) + after)

    def write(self, rva, data):
        ctypes.memmove(self.base + rva, data, len(data))

    def read(self, rva, size):
        return ctypes.string_at(self.base + rva, size)

    def snapshot(self):
        return ctypes.string_at(self.base, self.size)

    def call_target(self, name):
        rva = self.calls[name][0]
        return self.base + rva + 5 + struct.unpack("<i", self.read(rva + 1, 4))[0]

    def slot_value(self, name):
        return struct.unpack("<Q", self.read(self.slots[name][0], 8))[0]

    def chain(self, slots=CHAINED_SLOTS, calls=CHAINED_CALLS):
        for name, (rva, code) in STUBS.items():
            self.write(rva, code)
        for slot, stub in slots.items():
            self.write(self.slots[slot][0], struct.pack("<Q", self.base + STUBS[stub][0]))
        for call, stub in calls.items():
            rva = self.calls[call][0]
            self.write(rva + 1, struct.pack("<i", STUBS[stub][0] - rva - 5))


def changed_offsets(before, after):
    out = []
    for start in range(0, len(before), 65536):
        if before[start:start + 65536] != after[start:start + 65536]:
            out.extend(i for i in range(start, min(start + 65536, len(before))) if before[i] != after[i])
    return out


def module_range(handle):
    pe_offset = struct.unpack("<I", ctypes.string_at(handle + 0x3C, 4))[0]
    size = struct.unpack("<I", ctypes.string_at(handle + pe_offset + 24 + 56, 4))[0]
    return handle, handle + size


def scenario(name, output, work):
    if work.exists():
        shutil.rmtree(work)
    plugins = work / "mods" / MOD_NAME / "plugins"
    plugins.mkdir(parents=True)
    shutil.copy2(output / "obj" / "CustomRadio-test.dll", plugins / DLL_NAME)
    media = plugins / "CustomRadio" / "Media"
    if name != "no-media":
        shutil.copytree(output / "mods" / MOD_NAME / "plugins" / "CustomRadio", plugins / "CustomRadio")
    user_logo = plugins / LOGO_FOLDER / "HUD_RadioIcon_CustomRadio_User.swatchbin"
    if name in ("vanilla", "logo-refused"):
        for png in LOGO_PNGS.values():
            shutil.copy2(output / "mods" / MOD_NAME / "plugins" / png, plugins / png)
        shipped = {p.name: p.read_bytes() for p in (plugins / LOGO_FOLDER).iterdir()}
        (plugins / LOGO_FOLDER / "HUD_RadioIcon_CustomRadio.swatchbin").write_bytes(b"stale")
    if name == "logo-refused":
        (plugins / "Logo_PL.png").write_bytes(b"\x89PNG broken")
    if name == "logo-user":
        (plugins / "mine.png").write_bytes(png_bytes(120, 60, pattern(120, 60, opaque=False)))
    if name == "logo-refused":
        (plugins / "nope.png").write_text("not a picture", encoding="utf-8")
        user_logo.write_bytes(b"an earlier Logo=")
    expected_tracks = write_music(work / "Music")
    settings = {"vanilla": "Enabled=1\nPcmDecode=1\n", "chained": "Enabled=1\nPcmDecode=1\n",
                "core-mismatch": "Enabled=1\n", "optional-mismatch": "Enabled=1\n", "disabled": "Enabled=0\n",
                "unlock-only": "Enabled=0\nUnlockAllStations=1\n", "slot-not-code": "Enabled=1\n",
                "wrong-size": "Enabled=1\n", "logo-user": "Enabled=1\nName=ROCK FM\nLogo=mine.png\n",
                "logo-refused": "Enabled=1\nName={Rock} & Roll\nLogo=nope.png\n", "no-media": "Enabled=1\n",
                "ff-vanilla": "Enabled=1\n", "ff-chained": "Enabled=1\n", "ff-wrong-stamp": "Enabled=1\n"}[name]
    (plugins / INI_NAME).write_text(f"[CustomRadio]\n{settings}Folder={work / 'Music'}\n", encoding="utf-8")

    ff = name.startswith("ff-")
    game = FakeGame(ff, FF_STAMP if name != "ff-wrong-stamp" else 0x12345678)
    if name == "ff-chained":
        # Language Support's media mount: its DLL's code in the file resolver slot, and the mod's media\\.
        shutil.copytree(output / "mods" / MOD_NAME / "media", work / "mods" / MOD_NAME / "media")
        lang = work / "mods" / "Language Support" / "plugins"
        lang.mkdir(parents=True)
        shutil.copy2(output / "obj" / "CustomRadio-test.dll", lang / "LangSupport.dll")
        lang_dll = ctypes.CDLL(str(lang / "LangSupport.dll"))
        game.write(0x2EB7208, struct.pack("<Q", ctypes.cast(lang_dll.FH2PluginInitialize, ctypes.c_void_p).value))
        game._lang = lang_dll
        game.chain()
        game.write(game.extra["FF_SLOT_CAR_UPDATE"], struct.pack("<Q", game.base + FF_CAR_UPDATE_STUB))
        audio = ctypes.create_string_buffer(64)  # the audio manager SetStation checks for
        game.write(game.data["RVA_AUDIO_MANAGER"], struct.pack("<Q", ctypes.addressof(audio)))
        game._keep = audio
    if name == "chained":
        game.chain()
        audio = ctypes.create_string_buffer(64)  # the audio manager SetStation checks for
        game.write(game.data["RVA_AUDIO_MANAGER"], struct.pack("<Q", ctypes.addressof(audio)))
        game._keep = audio
    if name == "core-mismatch":
        rva = game.functions["FN_SUBSOUND"][0]
        game.write(rva + 3, b"\x90")
    if name == "optional-mismatch":
        rva = game.calls["CALL_CHANGE_2"][0]
        game.write(rva + 6, b"\x90")
    if name == "slot-not-code":
        game.write(game.slots["SLOT_XML_READ"][0], struct.pack("<Q", 0x1234))
    if name == "vanilla":
        # A radio system that already has stations: the plugin started late.
        system = ctypes.create_string_buffer(0x40)
        stations = ctypes.create_string_buffer(32)
        ctypes.memmove(system, struct.pack("<Q", game.base + game.data["RVA_RADIO_SYSTEM_VTABLE"]), 8)
        ctypes.memmove(ctypes.addressof(system) + 0x30,
                       struct.pack("<QQ", ctypes.addressof(stations), ctypes.addressof(stations) + 16), 16)
        game.write(game.data["RVA_RADIO_SYSTEM_GLOBAL"], struct.pack("<Q", ctypes.addressof(system)))
        game._keep = (system, stations)
    before = game.snapshot()

    log = []
    sink = LOG_FN(lambda line: log.append(line.decode("utf-8", "replace")))
    dll = ctypes.CDLL(str(plugins / DLL_NAME))
    dll.FH2PluginInitialize.argtypes = [ctypes.POINTER(Context)]
    dll.FH2PluginVehicleUpdate.argtypes = [ctypes.POINTER(Frame)]
    context = Context(ctypes.sizeof(Context), 1, 1, game.size if name != "wrong-size" else 0x1000,
                      game.base, dll._handle, str(plugins), str(plugins / DLL_NAME), b"0.4.4", sink)
    check(ctypes.sizeof(Context) == 64, "the loader context is 64 bytes")
    result = dll.FH2PluginInitialize(ctypes.byref(context))
    dll.FH2PluginMediaDirectory.restype = ctypes.c_wchar_p
    media_directory = dll.FH2PluginMediaDirectory()
    after = game.snapshot()
    text = "".join(log)
    changed = changed_offsets(before, after)
    dll_low, dll_high = module_range(dll._handle)

    def slot_hooked(slot):
        return dll_low <= game.slot_value(slot) < dll_high

    def call_hooked(call):
        target = game.call_target(call)
        code = ctypes.string_at(target, 14)
        destination = struct.unpack("<Q", code[6:14])[0]
        return (not game.base <= target < game.base + game.size and code[:6] == b"\xFF\x25\0\0\0\0" and
                dll_low <= destination < dll_high)

    def allowed(sites_slots, sites_calls):
        ranges = [range(game.slots[s][0], game.slots[s][0] + 8) for s in sites_slots]
        ranges += [range(game.calls[c][0] + 1, game.calls[c][0] + 5) for c in sites_calls]
        return all(any(o in r for r in ranges) for o in changed)

    core_slots = ["SLOT_XML_READ", "SLOT_PLAYER_UPDATE"]
    core_calls = ["CALL_SOUND_INFO", "CALL_SUBSOUND"]
    lock_calls = ["CALL_LOCKED_1", "CALL_LOCKED_2", "CALL_LOCKED_3"]
    change_calls = ["CALL_CHANGE_1", "CALL_CHANGE_2"]
    all_slots = list(game.slots)
    all_calls = list(game.calls)

    if name in ("vanilla", "chained"):
        check(result == 1, "FH2PluginInitialize returns TRUE")
        check(all(slot_hooked(s) for s in all_slots), "all 3 slots point into the plugin")
        check(all(call_hooked(c) for c in all_calls),
              "all 9 calls go through a jump of the plugin's own (not in the game image) into the plugin")
        check(media_directory == str(media), f"FH2PluginMediaDirectory: the plugin's media folder ({media_directory})")
        for line in ("station logo HUD_RadioIcon_CustomRadio for the name MY MUSIC",
                     "station logo HUD_RadioIcon_CustomRadio_PL for the name MOJA MUZYKA",
                     "station logo HUD_RadioIcon_CustomRadio_UK for the name МОЯ МУЗИКА"):
            check(line in text, f"log: {line}")
        onto = "the game's resource reader 0xaabec0" if name == "vanilla" else "another module's hook at 0x"
        check(f"XAML resource call 0x16ebb37 chained onto {onto}" in text, f"log: the XAML call chained onto {onto}")
        check(allowed(all_slots, all_calls), f"nothing else in the image changed ({len(changed)} bytes changed)")
        for line in ("custom station hooks installed", "station lock hook ready (UnlockAllStations=0)",
                     "D-pad cycle hook installed", "RadioStation key tunes straight",
                     "folder order with wrap-around installed", "decoded to PCM by Windows"):
            check(line in text, f"log: {line}")
        check(("already hooked by another module" in text) == (name == "chained"),
              "chaining is logged only when another hook was there")
    if name == "vanilla":
        check("stations were already loaded" in text, "log: a late start (stations already loaded) is reported")
        check(all(f"Logo{s}.png -> HUD_RadioIcon_CustomRadio{s}" in text for s in ("", "_PL", "_UK", "_Badge")),
              "log: the built-in logos are made from Logo*.png")
        check(all((plugins / LOGO_FOLDER / n).read_bytes() == b for n, b in shipped.items()),
              "the textures made from the PNGs are the package's (tools/logo_texture.py's) byte for byte; "
              "a stale one is rewritten")
    if name == "chained":
        check(text.count("already hooked by another module; chained") == 12, "log: all 12 sites chained")
        chained_checks(dll, game, log, expected_tracks)
        logo_checks(dll, game, log, media, options_game_exe())
    if name == "core-mismatch":
        check(result == 0, "FH2PluginInitialize returns FALSE")
        check(not changed, "the game image is untouched")
        check("SUBSOUND: the called game function's first bytes are not as expected" in text and
              "game code did not match; custom station disabled" in text, "log: the mismatch and its site")
    if name == "optional-mismatch":
        check(result == 1, "FH2PluginInitialize returns TRUE")
        check(allowed(all_slots, [c for c in all_calls if c not in change_calls]),
              "the D-pad calls (both) are untouched; the rest is hooked")
        check(all(call_hooked(c) for c in core_calls + lock_calls) and all(slot_hooked(s) for s in core_slots),
              "the station's core and lock hooks are installed")
        check("D-pad cycle hook did not match; native cycle only" in text and
              "CHANGE_2: the code around the call is not as expected" in text, "log: the D-pad group refused")
    if name == "disabled":
        check(result == 1 and not changed, "Enabled=0: TRUE and the game image is untouched")
        check("custom station is off (Enabled=0)" in text, "log: the station is off")
    if name == "unlock-only":
        check(result == 1 and all(call_hooked(c) for c in lock_calls) and allowed([], lock_calls),
              "UnlockAllStations=1 alone hooks only the three lock calls")
        check("all radio stations unlocked" in text, "log: all stations unlocked")
    if name == "slot-not-code":
        check(result == 0 and not changed, "a slot that holds no code: FALSE, nothing written")
        check("XML_READ: the slot holds neither the game function nor code" in text, "log: the slot refused")
    if name in ("logo-user", "logo-refused", "no-media"):
        check(result == 1 and all(slot_hooked(s) for s in all_slots), "FH2PluginInitialize returns TRUE, the station is in")
        check(call_hooked("CALL_XAML_RESOURCE"),
              "the XAML resource call is hooked" if name != "no-media" else "the XAML resource call is hooked (the icon hidden: no media folder)")
    if name == "logo-user":
        check("Logo=mine.png: the station shows this logo (120x60 pixels, fitted into the art area 188x87, "
              "with the game's drop shadow)" in text, "log: Logo=mine.png is used (120x60)")
        data = user_logo.read_bytes() if user_logo.is_file() else b""
        rgba = logo_texture.decode(data)[2] if len(data) == LOGO_SWATCH_SIZE else b""
        check(len(rgba) == 196 * 96 * 4 and max(rgba[3::4]) > 0 and
              all(rgba[(y * 196 + x) * 4 + 3] < 8 for y in range(96) for x in range(0, 11)) and
              rgba[(88 * 196 + 188) * 4 + 3] >= 200 and rgba[(45 * 196 + 192) * 4:(45 * 196 + 192) * 4 + 3] == bytes(3),
              "its texture HUD_RadioIcon_CustomRadio_User is written (174x87 against the art area's right edge)")
        logo_edit_checks(dll, [("ROCK FM", "HUD_RadioIcon_CustomRadio_User")], media, "Logo=mine.png, Name=ROCK FM")
    if name == "logo-refused":
        check("Logo_PL.png is not used: " in text and
              (plugins / LOGO_FOLDER / "HUD_RadioIcon_CustomRadio_PL.swatchbin").read_bytes() ==
              shipped["HUD_RadioIcon_CustomRadio_PL.swatchbin"],
              "a broken Logo_PL.png is logged and the package's texture stays")
        check("Logo=nope.png is not used: it is not a PNG file; the built-in logo is shown." in text,
              "log: Logo=nope.png is refused (not a PNG) and the built-in logo shown")
        check(not user_logo.exists(), "an earlier Logo= texture is removed")
        logo_edit_checks(dll, [("{}{Rock} &amp; Roll", "HUD_RadioIcon_CustomRadio_Badge")], media,
                         "Name={Rock} & Roll")
    if name == "no-media":
        check(media_directory is None, "FH2PluginMediaDirectory: none (the folder is missing)")
        check("the plugin's media folder is missing" in text and "custom station hooks installed" in text,
              "log: the media folder is missing; the station works without its logo")
    if name == "disabled":
        check(media_directory is None, "Enabled=0: no media folder")
    if name == "wrong-size":
        check(result == 0 and not changed, "another image size: FALSE, nothing written")
    if name == "ff-wrong-stamp":
        check(result == 0 and not changed, "Fast & Furious' image size with another TimeDateStamp: FALSE, nothing written")
    if name in ("ff-vanilla", "ff-chained"):
        car_slot = game.extra["FF_SLOT_CAR_UPDATE"]
        car_value = struct.unpack("<Q", game.read(car_slot, 8))[0]
        check(result == 1, "Fast & Furious: FH2PluginInitialize returns TRUE")
        check("Forza Horizon 2 Presents Fast & Furious 1.0.0.6" in text, "log: the game is named")
        logo = name == "ff-chained"  # with Language Support's media mount
        ff_calls = all_calls
        check(all(slot_hooked(s) for s in all_slots) and all(call_hooked(c) for c in ff_calls),
              f"Fast & Furious: all 3 slots and the {len(ff_calls)} calls (its own RVAs) go into the plugin "
              "(the XAML call: the logo, or the icon hidden)")
        check(dll_low <= car_value < dll_high, "Fast & Furious: ForzaCar's update slot points into the plugin")
        ranges = [range(game.slots[s][0], game.slots[s][0] + 8) for s in all_slots]
        ranges += [range(game.calls[c][0] + 1, game.calls[c][0] + 5) for c in ff_calls]
        ranges.append(range(car_slot, car_slot + 8))
        check(all(any(o in r for r in ranges) for o in changed),
              f"Fast & Furious: nothing else in the image changed ({len(changed)} bytes changed)")
        for line in ("custom station hooks installed", "station lock hook ready", "D-pad cycle hook installed",
                     "RadioStation key tunes straight", "folder order with wrap-around installed",
                     "the keys are read in the local car's update"):
            check(line in text, f"log: {line}")
        if logo:
            mod_media = work / "mods" / MOD_NAME / "media"
            check(media_directory == str(mod_media) and "Language Support mounts the mods' media" in text,
                  f"Fast & Furious: the logos are the mod's media\\ folder, which Language Support mounts ({media_directory})")
            check("XAML resource call 0x1712d47 chained onto another module's hook at 0x" in text,
                  "log: the XAML call chained onto the other hook")
        else:
            check("the station's HUD logo is off (Language Support does not mount" in text and media_directory is None,
                  "Fast & Furious without Language Support: the logo is off and logged")
            dll.CustomRadioTestLogoEdit.argtypes = [ctypes.c_char_p, ctypes.c_uint, ctypes.c_char_p, ctypes.c_uint,
                                                    ctypes.POINTER(ctypes.c_char_p)]
            dll.CustomRadioTestLogoEdit.restype = ctypes.c_uint
            out = ctypes.create_string_buffer(len(RADIO_CONTROL) + 4096)
            why = ctypes.c_char_p()
            length = dll.CustomRadioTestLogoEdit(RADIO_CONTROL, len(RADIO_CONTROL), out, len(out), ctypes.byref(why))
            edited = out.raw[:length].decode("utf-8")
            hidden = re.findall(r'<DataTrigger Binding="\{Binding StationName\}" Value="([^"]*)">\s*'
                                r'<Setter Property="Visibility" Value="Hidden" />', edited)
            check(hidden == ["MY MUSIC", "MOJA MUZYKA", "МОЯ МУЗИКА"] and "CustomRadio.png" not in edited,
                  f"Fast & Furious without Language Support: the station's icon is hidden for its names, "
                  f"no white box ({hidden})")
    if name == "ff-chained":
        check("chained onto another module's hook" in text, "log: the car update chained onto the other hook")
        ff_chained_checks(dll, game, log, expected_tracks)
        logo_checks(dll, game, log, work / "mods" / MOD_NAME / "media", options_ff_exe(), ff=True)
    os._exit(1 if failures else 0)


_GAME_EXE = []


def options_game_exe():
    return _GAME_EXE[0] if _GAME_EXE else None


_FF_EXE = []


def options_ff_exe():
    return _FF_EXE[0] if _FF_EXE else None


def ff_chained_checks(dll, game, log, expected_tracks):
    """Fast & Furious: RadioInfo without stingers, and the keys through the local car's update."""
    selected = []
    select = SELECT_FN(lambda _, name: selected.append(read_game_string(name)))
    dll.CustomRadioTestUseFakes.argtypes = [SELECT_FN]
    dll.CustomRadioTestUseFakes(select)
    resource = ctypes.create_string_buffer(0x100)
    name, name_heap = game_string("media\\audio\\RadioInfo_EN.xml")
    ctypes.memmove(ctypes.addressof(resource) + 0xB8, name, 32)
    info = (b'<Radio>\r\n  <RadioStations>\r\n'
            b'    <RadioStation Name="Spire Radio" Number="1" RecentlyPlayedListSize="-1">\r\n    </RadioStation>\r\n'
            b'    <RadioStation Name="Spire Score" Number="2" RecentlyPlayedListSize="-1">\r\n    </RadioStation>\r\n'
            b'  </RadioStations>\r\n</Radio>')
    xml = kernel32.HeapAlloc(kernel32.GetProcessHeap(), 0, len(info) + 1)
    ctypes.memmove(xml, info + b"\0", len(info) + 1)
    ctypes.memmove(ctypes.addressof(resource) + 0xE0, struct.pack("<QI", xml, len(info)), 12)
    read = ctypes.CFUNCTYPE(ctypes.c_ubyte, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p)(
        game.slot_value("SLOT_XML_READ"))
    check(read(ctypes.addressof(resource), None, None, None) == 1, "F&F XML read: the chained read's result comes back")
    buffer, length = struct.unpack_from("<QI", resource.raw, 0xE0)
    text = ctypes.string_at(buffer, length).decode("utf-8")
    station = re.search(r'<RadioStation Name="MY MUSIC" Number="3" RecentlyPlayedListSize="-1">(.*?)</RadioStation>',
                        text, re.S)
    check(station is not None and station.group(1).count("FH2MUSIC_") == 2 * len(expected_tracks) and
          text.index("MY MUSIC") < text.index("</RadioStations>"),
          "F&F XML read: MY MUSIC added as number 3 with its tracks, inside RadioStations")
    check(station is not None and 'Type="Stingers"' not in station.group(1) and
          'Type="ShortStingers"' not in station.group(1) and 'Type="DJ"' not in station.group(1) and
          "FH2MUSIC_SILENCE" not in station.group(1),
          "F&F XML read: no stinger or DJ playlists (Fast & Furious' stations have none)")

    held = set()
    key_state = KEY_STATE_FN(lambda key: -32768 if key in held else 0)
    dll.CustomRadioTestInput.argtypes = [KEY_STATE_FN, ctypes.c_int]
    dll.CustomRadioTestInput(key_state, 1)
    # The player manager -> +0x48 -> +0x98: the local player, whose first field is its car.
    car = ctypes.create_string_buffer(0x40)
    other_car = ctypes.create_string_buffer(0x40)
    for c in (car, other_car):
        ctypes.memmove(c, struct.pack("<Q", game.base + game.data["RVA_FORZA_CAR_VTABLE"]), 8)
    player = ctypes.create_string_buffer(0x40)
    ctypes.memmove(player, struct.pack("<Q", ctypes.addressof(car)), 8)
    inner = ctypes.create_string_buffer(0x100)
    ctypes.memmove(ctypes.addressof(inner) + 0x98, struct.pack("<Q", ctypes.addressof(player)), 8)
    manager = ctypes.create_string_buffer(0x100)
    ctypes.memmove(ctypes.addressof(manager) + 0x48, struct.pack("<Q", ctypes.addressof(inner)), 8)
    game.write(game.data["RVA_PLAYER_MANAGER"], struct.pack("<Q", ctypes.addressof(manager)))
    update = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_float, ctypes.c_float, ctypes.c_uint)(
        struct.unpack("<Q", game.read(game.extra["FF_SLOT_CAR_UPDATE"], 8))[0])
    held.add(0x78)  # F9
    update(ctypes.addressof(other_car), 0.016, 0.016, 1)
    check(not selected and struct.unpack_from("<I", other_car.raw, 8)[0] == 0x4321,
          "F&F another car's update: the chained update runs, no key read")
    update(ctypes.addressof(car), 0.016, 0.016, 1)
    check(selected == ["MY MUSIC"] and struct.unpack_from("<I", car.raw, 8)[0] == 0x4321,
          f"F&F F9 in the local car's update tunes to MY MUSIC, then the chained update runs ({selected})")
    update(ctypes.addressof(car), 0.016, 0.016, 2)
    check(len(selected) == 1, "F&F F9 held: no repeat")
    held.clear()
    update(ctypes.addressof(car), 0.016, 0.016, 3)
    dll.CustomRadioTestInput(key_state, 0)
    held.add(0x78)
    update(ctypes.addressof(car), 0.016, 0.016, 4)
    check(len(selected) == 1, "F&F F9 with another window focused: nothing")
    held.clear()
    update(ctypes.addressof(car), 0.016, 0.016, 5)
    dll.CustomRadioTestInput(key_state, 1)
    held.add(0x76)  # F7
    update(ctypes.addressof(car), 0.016, 0.016, 6)
    dll.CustomRadioTestRequests.restype = ctypes.c_uint
    check(dll.CustomRadioTestRequests() == 0b010, "F&F F7: a next-track request for the radio thread")
    held.clear()
    frame = Frame(ctypes.sizeof(Frame), 1, ctypes.addressof(car), 0, 0.016, None)
    held.add(0x78)
    dll.FH2PluginVehicleUpdate(ctypes.byref(frame))
    check(len(selected) == 1, "F&F FH2PluginVehicleUpdate is ignored (the plugin's own tick reads the keys)")
    held.clear()


def logo_edit_checks(dll, entries, media, what):
    """The XAML edit with the entries FH2PluginInitialize prepared, on the cut-down radio display."""
    dll.CustomRadioTestLogoEdit.argtypes = [ctypes.c_char_p, ctypes.c_uint, ctypes.c_char_p, ctypes.c_uint,
                                            ctypes.POINTER(ctypes.c_char_p)]
    dll.CustomRadioTestLogoEdit.restype = ctypes.c_uint
    output = ctypes.create_string_buffer(len(RADIO_CONTROL) + 4096)
    why = ctypes.c_char_p()
    length = dll.CustomRadioTestLogoEdit(RADIO_CONTROL, len(RADIO_CONTROL), output, len(output), ctypes.byref(why))
    check(length > len(RADIO_CONTROL), f"{what}: the radio display gets its triggers ({why.value})")
    if length:
        check_radio_control(RADIO_CONTROL, output.raw[:length], entries, media, what)


def logo_checks(dll, game, log, media, game_exe, ff=False):
    """The HUD radio display through the hooked XAML resource call (the foreign hook leaves *output as set)."""
    events, allocations, keep = [], {}, []

    def allocate(size):
        buffer = ctypes.create_string_buffer(size)
        allocations[ctypes.addressof(buffer)] = buffer
        events.append("allocate")
        return ctypes.addressof(buffer)

    ALLOC = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_size_t)
    OBJECT = ctypes.CFUNCTYPE(None, ctypes.c_void_p)
    callbacks = [ALLOC(allocate), OBJECT(lambda p: (allocations.pop(p, None), events.append("free"))),
                 OBJECT(lambda p: events.append("object init")), OBJECT(lambda p: events.append("object cleanup")),
                 OBJECT(lambda p: events.append(("delete", p)))]
    dll.CustomRadioTestLogoNatives.argtypes = [ctypes.c_void_p] * 5
    dll.CustomRadioTestLogoNatives(*[ctypes.cast(c, ctypes.c_void_p).value for c in callbacks])
    keep += callbacks
    reader = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p))(
        game.call_target("CALL_XAML_RESOURCE"))

    def resource(text):
        """A resource object like the game's (refcount 1), with a vtable whose slot 0 is never called."""
        vtable = (ctypes.c_void_p * 8)(0x1111, 0x2222, 0x3333, 0x4444, 0x5555, 0x6666, 0x7777, 0x8888)
        data = ctypes.create_string_buffer(text, len(text) + 1)
        obj = ctypes.create_string_buffer(0x38)
        struct.pack_into("<Q", obj, 0, ctypes.addressof(vtable) + 8)
        struct.pack_into("<i", obj, 0x10, 1)
        struct.pack_into("<Qii", obj, 0x28, ctypes.addressof(data), len(text), len(text) + 1)
        keep.extend([vtable, data, obj])
        return ctypes.addressof(obj)

    def read(text):
        original = resource(text)
        out = ctypes.c_void_p(original)
        events.clear()
        reader(None, ctypes.byref(out))
        raw = ctypes.string_at(out.value, 0x38)
        at, length, capacity = struct.unpack_from("<Qii", raw, 0x28)
        return original, out.value, ctypes.string_at(at, length), struct.unpack_from("<Q", raw, 0)[0], capacity

    defaults = [("MY MUSIC", "HUD_RadioIcon_CustomRadio"), ("MOJA MUZYKA", "HUD_RadioIcon_CustomRadio_PL"),
                ("МОЯ МУЗИКА", "HUD_RadioIcon_CustomRadio_UK")]
    if ff:
        defaults = [(name, FF_STOCK[texture]) for name, texture in defaults]
    sources = [("the HUD radio display (cut down)", RADIO_CONTROL)]
    ui_zip = Path(game_exe).parent / "media" / "UI.zip" if game_exe else None
    if ui_zip and ui_zip.is_file():
        with zipfile.ZipFile(ui_zip) as archive:
            sources.append(("the game's HUDRadioControl.xaml", archive.read("Controls/HUDRadioControl.xaml")))
    else:
        print("  skip the game's HUDRadioControl.xaml (pass --game-exe)")
    for what, text in sources:
        original, result, edited, vtable, capacity = read(text)
        check(result != original and events.count("allocate") == 1 and ("delete", original) in events,
              f"{what}: comes back as a new resource; the original is released (deleted)")
        check(capacity > len(edited) and ctypes.string_at(vtable - 8, 8) == struct.pack("<Q", 0x1111) and
              ctypes.string_at(vtable + 8, 40) == struct.pack("<5Q", 0x3333, 0x4444, 0x5555, 0x6666, 0x7777)
              and ctypes.string_at(vtable, 8) != struct.pack("<Q", 0x2222),
              f"{what}: its vtable is the original's with the plugin's delete in slot 0")
        check_radio_control(text, edited, defaults, media, what)
        if what.startswith("the game's"):
            stock = TRIGGER.findall(text.decode("utf-8"))
            count = 1 if b"Spire Radio" in text else 9  # Fast & Furious' icon style has one image trigger
            check(len(stock) == count and TRIGGER.findall(edited.decode("utf-8")) == defaults + stock,
                  f"{what}: its {count} stock image trigger(s) stay, after the plugin's three")
        again, result, _, _, _ = read(edited)
        check(result == again and events.count("free") == 1 and not any(isinstance(e, tuple) for e in events),
              f"{what}: read again with the logo, it is kept as it is")
    other = b'<UserControl x:Class="ForzaUI.HUDSpeedometer"/>'
    original, result, text, _, _ = read(other)
    check(result == original and text == other and not events, "another control passes through untouched")
    check(any("the station's logo is added to the HUD radio display" in line for line in log),
          "log: the logo is added to the HUD radio display (once)")


def game_string(text):
    """An MSVC std::string (32 bytes) holding `text`."""
    data = text.encode("utf-8")
    buffer = ctypes.create_string_buffer(32)
    if len(data) < 16:
        ctypes.memmove(buffer, data, len(data))
        ctypes.memmove(ctypes.addressof(buffer) + 16, struct.pack("<QQ", len(data), 15), 16)
        return buffer, None
    heap = ctypes.create_string_buffer(data + b"\0")
    ctypes.memmove(buffer, struct.pack("<Q", ctypes.addressof(heap)), 8)
    ctypes.memmove(ctypes.addressof(buffer) + 16, struct.pack("<QQ", len(data), len(data)), 16)
    return buffer, heap


def read_game_string(address):
    raw = ctypes.string_at(address, 32)
    size, capacity = struct.unpack_from("<QQ", raw, 16)
    if capacity >= 16:
        return ctypes.string_at(struct.unpack_from("<Q", raw, 0)[0], size).decode("utf-8")
    return raw[:size].decode("utf-8")


def chained_checks(dll, game, log, expected_tracks):
    selected = []
    select = SELECT_FN(lambda _, name: selected.append(read_game_string(name)))
    dll.CustomRadioTestUseFakes.argtypes = [SELECT_FN]
    dll.CustomRadioTestUseFakes(select)
    dll.CustomRadioTestStation.argtypes = [ctypes.c_char_p, ctypes.c_uint]

    # RadioInfo through the hooked read slot (the foreign read returns 1 first).
    resource = ctypes.create_string_buffer(0x100)
    name, name_heap = game_string("media\\audio\\RadioInfo_EN.xml")
    ctypes.memmove(ctypes.addressof(resource) + 0xB8, name, 32)
    heap = kernel32.GetProcessHeap()
    xml = kernel32.HeapAlloc(heap, 0, len(RADIO_INFO) + 1)
    ctypes.memmove(xml, RADIO_INFO + b"\0", len(RADIO_INFO) + 1)
    ctypes.memmove(ctypes.addressof(resource) + 0xE0, struct.pack("<QI", xml, len(RADIO_INFO)), 12)
    read = ctypes.CFUNCTYPE(ctypes.c_ubyte, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p)(
        game.slot_value("SLOT_XML_READ"))
    check(read(ctypes.addressof(resource), None, None, None) == 1, "XML read: the chained read's result comes back")
    buffer, length = struct.unpack_from("<QI", resource.raw, 0xE0)
    text = ctypes.string_at(buffer, length).decode("utf-8")
    check(buffer != xml and text.startswith(RADIO_INFO[:-20].decode()) and text.endswith("</RadioStations>\r\n"),
          "XML read: the text is replaced, original stations kept")
    station = re.search(r'    <RadioStation Name="([^"]*)" Number="(\d+)" RecentlyPlayedListSize="-1">', text)
    check(station is not None and station.group(1) == "MY MUSIC" and station.group(2) == "8",
          "XML read: MY MUSIC added as number 8 (after the highest, 7)")
    entries = re.findall(r'<Entry EventName="/Master/Radio/Track" SoundName="FH2MUSIC_(\d+)" SampleLength="(\d+)" '
                         r'DisplayName="([^"]*)" Artist="([^"]*)"', text)
    freeroam = entries[:len(entries) // 2]
    count = len(expected_tracks)
    check(len(entries) == 2 * count and [e[0] for e in freeroam] == [str(i) for i in range(count)],
          f"XML read: {count} playable tracks (MP3{' and FLAC' if count == 4 else ''}) in folder order, in both "
          f"track playlists ({len(entries)} entries)")
    check([(e[2], e[3], int(e[1])) for e in freeroam] == expected_tracks,
          f"XML read: names (with & kept, quote and <> replaced) and lengths ({freeroam})")
    check(text.count('SoundName="FH2MUSIC_SILENCE"') == 2 and '<Playlist Type="DJ">' in text,
          "XML read: silent stingers and an empty DJ list")
    check(text.index("<RadioStation Name=\"MY MUSIC\"") < text.index("</RadioStations>"),
          "XML read: the station is inside RadioStations")
    station_name = ctypes.create_string_buffer(64)
    check(dll.CustomRadioTestStation(station_name, 64) == 8 and station_name.value == b"MY MUSIC",
          "the plugin knows its station (8, MY MUSIC)")
    other = ctypes.create_string_buffer(0x100)
    other_name, other_heap = game_string("media\\ui\\Something.xml")
    ctypes.memmove(ctypes.addressof(other) + 0xB8, other_name, 32)
    ctypes.memmove(ctypes.addressof(other) + 0xE0, struct.pack("<QI", xml, 10), 12)
    read(ctypes.addressof(other), None, None, None)
    check(struct.unpack_from("<Q", other.raw, 0xE0)[0] == xml, "XML read: other files are left alone")

    # getSoundInfo through the hooked call.
    sound_info = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_char_p, ctypes.c_void_p)(
        game.call_target("CALL_SOUND_INFO"))
    info = ctypes.create_string_buffer(0xF0)
    check(sound_info(None, b"FH2MUSIC_0", info) == 0, "sound info FH2MUSIC_0: answered by the plugin")
    data, mode = struct.unpack_from("<QI", info.raw, 0)
    exinfo, size = struct.unpack_from("<II", info.raw, 0x10)
    marker = struct.unpack_from("<I", info.raw, 0xE8)[0]
    check(mode == 0x10004000 and exinfo == 0xD8 and marker == 0x46480000,
          "sound info: OPENMEMORY_POINT|ACCURATETIME, exinfo size, subsound marker 0")
    dll.CustomRadioTestDecode.argtypes = [ctypes.c_char_p, ctypes.c_uint, ctypes.c_char_p, ctypes.c_void_p]
    codec = dll.CustomRadioTestDecode(b"", 0, ctypes.create_string_buffer(44), None) != -1
    if codec or not os.environ.get("CI"):
        check(ctypes.string_at(data, 4) == b"RIFF" and
              size == 44 + struct.unpack("<I", ctypes.string_at(data + 40, 4))[0],
              f"sound info: the track as a PCM WAV in memory ({size} bytes)")
    else:
        check(ctypes.string_at(data, 3) == b"ID3", "sound info: without a Windows MP3 codec the MP3 is served as is")
    check(sound_info(None, b"FH2MUSIC_SILENCE", info) == 0 and ctypes.string_at(struct.unpack_from("<Q", info.raw)[0], 4) == b"RIFF",
          "sound info FH2MUSIC_SILENCE: the silent stinger")
    check(sound_info(None, b"music_horizon_pulse_01", info) == 0x77, "sound info: other keys go to the chained code")
    check(sound_info(None, b"FH2MUSIC_9", info) == 0x77, "sound info: an unknown track number goes on too")

    # getSubSound through the hooked call.
    subsound = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p)(
        game.call_target("CALL_SUBSOUND"))
    player = ctypes.create_string_buffer(0x40)
    instance = ctypes.create_string_buffer(16)
    ctypes.memmove(ctypes.addressof(player) + 0x10, struct.pack("<Q", ctypes.addressof(instance)), 8)
    ctypes.memmove(ctypes.addressof(player) + 0x20, struct.pack("<I", 3), 4)
    ctypes.memmove(ctypes.addressof(player) + 0x2C, struct.pack("<i", 1500), 4)
    request = ctypes.create_string_buffer(0x28)
    ctypes.memmove(ctypes.addressof(request) + 0x10, struct.pack("<Q", ctypes.addressof(player)), 8)
    check(subsound(0xABCD000, 0x46480000, ctypes.addressof(request)) == 0 and
          struct.unpack_from("<Q", request.raw)[0] == 0xABCD000, "subsound: our marker returns the stream itself")
    check(subsound(0xABCD000, 3, ctypes.addressof(request)) == 0x55, "subsound: other indices go to the chained code")
    subsound(0xABCD000, 0x46480000, ctypes.addressof(request))  # the player plays track 0 again

    # IsStationLocked through the hooked calls (the chained code says "locked").
    locked = ctypes.CFUNCTYPE(ctypes.c_ubyte, ctypes.c_void_p, ctypes.c_void_p)(game.call_target("CALL_LOCKED_2"))
    mine, _ = game_string("MY MUSIC")
    theirs, _ = game_string("HORIZON PULSE")
    check(locked(None, ctypes.addressof(mine)) == 0, "station lock: the custom station is never locked")
    check(locked(None, ctypes.addressof(theirs)) == 1, "station lock: other stations follow the chained code")

    # ChangeStation and the playlist hooks fall through to the chained code here.
    change = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_int)(game.call_target("CALL_CHANGE_1"))
    module = ctypes.create_string_buffer(0x80)
    change(ctypes.addressof(module), 1)
    check(struct.unpack_from("<I", module.raw)[0] == 0x1234, "D-pad: an unreadable station map leaves it to the chained code")
    next_pick = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int)(
        game.slot_value("SLOT_PLAYLIST_NEXT"))
    check(next_pick(0x1000, 0x2222, 0) == 0x2222, "playlist next: other playlists go to the chained code")
    find = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p)(
        game.call_target("CALL_HISTORY_FIND"))
    found = ctypes.create_string_buffer(8)
    check(find(ctypes.addressof(found), 1, 2, 3) == ctypes.addressof(found), "recently played: chained code runs")

    # Keys through FH2PluginVehicleUpdate.
    held = set()
    key_state = KEY_STATE_FN(lambda key: -32768 if key in held else 0)
    dll.CustomRadioTestInput.argtypes = [KEY_STATE_FN, ctypes.c_int]
    dll.CustomRadioTestRequests.restype = ctypes.c_uint
    car = ctypes.create_string_buffer(16)

    def tick(enabled=1, buttons=0):
        frame = Frame(ctypes.sizeof(Frame), enabled, ctypes.addressof(car), buttons, 0.016, None)
        dll.FH2PluginVehicleUpdate(ctypes.byref(frame))

    check(ctypes.sizeof(Frame) == 32, "the vehicle frame is 32 bytes")
    dll.CustomRadioTestInput(key_state, 1)
    held.add(0x78)  # F9
    tick()
    check(selected == ["MY MUSIC"], f"F9 while driving tunes to MY MUSIC ({selected})")
    tick()
    check(len(selected) == 1, "F9 held: no repeat")
    held.clear()
    tick()
    held.add(0x78)
    tick(enabled=0)
    check(len(selected) == 1, "F9 in a menu (input disabled): nothing")
    held.clear()
    tick(enabled=0)
    dll.CustomRadioTestInput(key_state, 0)
    held.add(0x78)
    tick()
    check(len(selected) == 1, "F9 with another window focused: nothing")
    held.clear()
    tick()
    dll.CustomRadioTestInput(key_state, 1)
    held.add(0x76)  # F7
    tick()
    check(dll.CustomRadioTestRequests() == 0b010, "F7 while driving: a next-track request for the radio thread")
    tick(enabled=0)
    check(dll.CustomRadioTestRequests() == 0, "input disabled: the pending request is dropped")
    held.clear()
    tick()
    held.add(0x75)  # F6
    tick()
    check(dll.CustomRadioTestRequests() == 0b100, "F6 while driving: a restart request")
    held.clear()
    tick()

    # The radio thread: the playing custom player's update takes the request.
    update = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_float)(game.slot_value("SLOT_PLAYER_UPDATE"))
    log.clear()
    other_player = ctypes.create_string_buffer(0x40)
    update(ctypes.addressof(other_player), 0.016)
    check(dll.CustomRadioTestRequests() == 0b100, "another player's update leaves the request")
    check(struct.unpack_from("<I", other_player.raw, 0x3C)[0] == 0x5555, "player update: the chained update runs")
    update(ctypes.addressof(player), 0.016)
    check(dll.CustomRadioTestRequests() == 0, "the custom player's update takes the request")
    check(any("station clock not found; key ignored" in line for line in log),
          "with no radio system it is logged and ignored (no crash)")


# ------------------------------------------------------------------- main

def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", type=Path, default=ROOT / "out")
    parser.add_argument("--game-exe", default=os.environ.get("FH2_GAME_EXE"))
    parser.add_argument("--extra-options", default=os.environ.get("EO_RELEASE_DIR"))
    parser.add_argument("--ff-exe", default=os.environ.get("FF_GAME_EXE"))
    parser.add_argument("--scenario")
    parser.add_argument("--work", type=Path)
    options = parser.parse_args()
    sys.stdout.reconfigure(errors="backslashreplace")
    output = options.output.resolve()
    if options.scenario:
        if options.game_exe:
            _GAME_EXE.append(options.game_exe)
        if options.ff_exe:
            _FF_EXE.append(options.ff_exe)
        scenario(options.scenario, output, options.work)
        return
    ini = test_package(output)
    test_sites(options.game_exe)
    test_ff_sites(options.ff_exe)
    test_game_textures(options.game_exe, output)
    test_game_textures(options.ff_exe, output)
    test_coexistence(ini, options.extra_options)
    with tempfile.TemporaryDirectory(prefix="custom-radio-") as temporary:
        work = Path(temporary)
        log = []
        dll = load_test_dll(output, log)
        test_config(dll, log, work, output / "mods" / MOD_NAME / "plugins" / INI_NAME)
        test_keys(dll)
        test_mp3(dll, work)
        test_media(dll, work)
        test_logo(dll, work)
        test_names(output, output / "test-runs" / "names")
        run_scenarios(output, work, options.game_exe, options.ff_exe)
    print()
    if failures:
        print(f"{len(failures)} FAILED:")
        for failure in failures:
            print("  " + failure)
        sys.exit(1)
    print("all tests passed")


if __name__ == "__main__":
    main()
