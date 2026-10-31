"""Offline tests of the Nitrous plugin (no game run, nothing installed).

Usage: py tests/run_tests.py [--output out] [--game <FH2 folder>] [--ff-game <F&F exe>]

The game executables are only read (default: ~/FH2/Anthem_1.0.0.64_x64__8wekyb3d8bbwe/
Mount/forza_durango_release_final.exe, and Fast & Furious' below ~/FH2/Forza Horizon 2
Presents Fast & Furious/Spire_*/Mount). A missing one skips the tests that need it (CI).

1. package   mod.json, the mod layout, the DLL (PE32+ x64, its exports, only
             Windows imports, version resource), the INI's defaults.
2. settings  key and pad button names.
3. car       the per-frame logic on a fake ForzaCar and CCarDynamics: the five
             parameters written (and again after the game clears them), the
             request follows the input, the tank refills only while idle, the
             tuning's limits; another car or dynamics object is never written.
4. game      Forza Horizon 2 1.0.0.64 mapped as the loader maps it: every
             guarded site matches, the vtables are ForzaCar's and
             TRefCountedObjectThreadSafe<CCarDynamics>'s (run-time type names),
             their nitrous getter slots; a changed byte, Fast & Furious and
             another size are refused.
5. plugin    FH2PluginInitialize and FH2PluginVehicleUpdate on the mapped
             game: pad button, key, NitrousHold, input off, NitrousTank,
             NitrousStatus, the boost log (why a boost ended), the camera
             FOV chain; Enabled=0, Fast & Furious and a wrong build do nothing.
6. runs      boost runs on a fake car: the build-up put back after a short
             cut (a gear change), why a run ends (released, empty, throttle,
             gear change, engine speed, another car), the effects' envelope.
7. effects   [Effects] parsing; Fast & Furious' camera FOV kick and impulse;
             the FOV factor; the sample player (start, looped sustain with a
             crossfaded seam, stop); WAV loading; our synthesized sound.
"""

import argparse
import configparser
import ctypes
from ctypes import wintypes
import json
import math
import os
from pathlib import Path
import re
import struct
import sys
import tempfile
import wave

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent

MOD_NAME = "Nitrous"
DLL_NAME = "Nitrous.dll"
INI_NAME = "Nitrous.ini"
TEST_DLL = "NitrousTest.dll"
SYSTEM_DLLS = re.compile(r"^(kernel32|user32|winmm|msvcrt|api-ms-win-[a-z0-9-]+)\.dll$", re.I)
FH2_EXE = Path("Anthem_1.0.0.64_x64__8wekyb3d8bbwe/Mount/forza_durango_release_final.exe")

# nitrous.h
FH2_SIZE, FF_SIZE = 0x3A18000, 0x3A89000
CAR_VTABLE, DYN_VTABLE, IFACE_VTABLE = 0x2C5CB80, 0x2EB5D68, 0x2EB5E18
CAR_DYNAMICS, IFACE = 0x4C08, 0x3A0
REQUEST, ACTIVE, USED, BUILDUP, PARAMS = 0x4F8, 0x4FC, 0x500, 0x504, 0xDB0
BOOSTING, IDLE, ARMED, NOT_LOCAL_CAR, NO_DYNAMICS, UNREADABLE = range(6)
SITES = [0x12EB284, 0x12B4332, 0x12E0E42, 0x12D67E0, 0xF605D0]

failures = []


def check(ok, what):
    print(("  ok   " if ok else "  FAIL ") + what, flush=True)
    if not ok:
        failures.append(what)


def skip(what):
    print("  skip " + what, flush=True)


# ---------------------------------------------------------------- PE helpers

def sections(data):
    e = struct.unpack_from("<I", data, 0x3C)[0]
    count = struct.unpack_from("<H", data, e + 6)[0]
    table = e + 24 + struct.unpack_from("<H", data, e + 20)[0]
    return [struct.unpack_from("<IIII", data, table + 40 * i + 8) for i in range(count)]  # vsize, va, rawsize, raw


def offset(data, rva):
    for vsize, va, rawsize, raw in sections(data):
        if va <= rva < va + max(vsize, rawsize):
            return raw + rva - va
    raise ValueError(hex(rva))


def cstr(data, rva):
    at = offset(data, rva)
    return data[at:data.index(b"\0", at)].decode("ascii", "replace")


def pe_info(path):
    d = Path(path).read_bytes()
    e = struct.unpack_from("<I", d, 0x3C)[0]
    machine = struct.unpack_from("<H", d, e + 4)[0]
    characteristics = struct.unpack_from("<H", d, e + 22)[0]
    optional = e + 24
    magic = struct.unpack_from("<H", d, optional)[0]
    exports, imports = [], []
    exports_rva = struct.unpack_from("<I", d, optional + 112)[0]
    if exports_rva:
        o = offset(d, exports_rva)
        count, names = struct.unpack_from("<I", d, o + 24)[0], struct.unpack_from("<I", d, o + 32)[0]
        exports = [cstr(d, struct.unpack_from("<I", d, offset(d, names) + 4 * i)[0]) for i in range(count)]
    o = offset(d, struct.unpack_from("<I", d, optional + 120)[0])
    while struct.unpack_from("<I", d, o + 12)[0]:
        imports.append(cstr(d, struct.unpack_from("<I", d, o + 12)[0]))
        o += 20
    resources = struct.unpack_from("<I", d, optional + 112 + 16)[0]
    return machine, magic, characteristics, exports, imports, resources, d


def map_image(path):
    """The executable as the loader maps it (sections at their RVAs; not relocated)."""
    data = Path(path).read_bytes()
    e = struct.unpack_from("<I", data, 0x3C)[0]
    size = struct.unpack_from("<I", data, e + 24 + 56)[0]
    headers = struct.unpack_from("<I", data, e + 24 + 60)[0]
    image = bytearray(size)
    image[:headers] = data[:headers]
    for vsize, va, rawsize, raw in sections(data):
        n = min(vsize or rawsize, rawsize)
        image[va:va + n] = data[raw:raw + n]
    return image, struct.unpack_from("<Q", data, e + 24 + 24)[0]


# ---------------------------------------------------------------- 1. package

def test_package(output):
    print("package")
    mod = output / "mods" / MOD_NAME
    manifest = json.loads((mod / "mod.json").read_text(encoding="utf-8"))
    check(manifest.get("format_version") == 1 and manifest.get("id") == "nitrous" and manifest.get("name") == MOD_NAME,
          "mod.json: format_version 1, id nitrous, name Nitrous")
    check(manifest.get("games") == ["fh2"] and isinstance(manifest.get("dependencies"), list) and
          "media" not in manifest and "database" not in manifest, "mod.json: games [fh2], no media or database")
    files = sorted(str(p.relative_to(mod)).replace("\\", "/") for p in mod.rglob("*") if p.is_file())
    check(files == ["mod.json", "plugins/" + DLL_NAME, "plugins/" + INI_NAME], f"mods/{MOD_NAME}: {files}")
    machine, magic, characteristics, exports, imports, resources, blob = pe_info(mod / "plugins" / DLL_NAME)
    check(machine == 0x8664 and magic == 0x20B and characteristics & 0x2000, "the DLL is a PE32+ x64 DLL")
    check(sorted(exports) == ["FH2PluginInitialize", "FH2PluginVehicleUpdate", "NitrousHold", "NitrousStatus",
                              "NitrousTank", "NitrousUsage"],
          f"exports: the plugin entries and NitrousHold/NitrousTank/NitrousStatus/NitrousUsage ({exports})")
    check(all(SYSTEM_DLLS.match(name) for name in imports), f"only Windows DLLs are imported ({', '.join(imports)})")
    check(bool(resources) and ("FileVersion\0" + manifest["version"]).encode("utf-16le") in blob.replace(b"\0\0\0", b"\0"),
          f"version resource with FileVersion {manifest['version']}")
    ini = configparser.ConfigParser()
    ini.read(mod / "plugins" / INI_NAME, encoding="utf-8")
    want = {"Enabled": "1", "Key": "LShift", "PadButton": "LeftStick", "TorqueScale": "1.6", "Duration": "20",
            "BuildupTime": "0.5", "RechargeTime": "60"}
    got = {k: ini.get("Nitrous", k, fallback=None) for k in want}
    check(got == want, f"Nitrous.ini defaults ({got})")
    rules = [k for k in ("AutoRecharge", "InRaces", "FreeRoam", "InOtherEvents", "SkillRecharge", "CrashPenalty")
             if ini.has_option("Nitrous", k)]
    check(ini.get("Nitrous", "Mode", fallback=None) == "Arcade" and not rules and ini.has_section("SkillRecharge") and
          not ini.options("SkillRecharge"),
          f"Nitrous.ini: Mode=Arcade, the rule keys only as comments, so the preset rules ({rules})")
    check(ini.get("Effects", "ExhaustTail", fallback=None) == "0.35" and ini.get("Effects", "EaseOut", fallback=None) == "1.5",
          "Nitrous.ini [Effects]: ExhaustTail=0.35, EaseOut=1.5")
    effects = {k: ini.get("Effects", k, fallback=None) for k in
               ("ExhaustFlames", "CameraLayers", "Rumble", "Fov", "FovDegrees", "FovImpulseDegrees", "FovCockpitDegrees", "Sound",
                "SoundVolume")}
    check(effects == {"ExhaustFlames": "1", "CameraLayers": "1", "Rumble": "1", "Fov": "1", "FovDegrees": "5", "FovImpulseDegrees": "5",
                      "FovCockpitDegrees": "0", "Sound": "1", "SoundVolume": "0.6"},
          f"Nitrous.ini [Effects] defaults: flames, camera layers, Fast & Furious' camera values ({effects})")


# ---------------------------------------------------------------- the test DLL

LOG = ctypes.CFUNCTYPE(None, ctypes.c_char_p)


class Context(ctypes.Structure):
    _fields_ = [("size", ctypes.c_uint32), ("api_version", ctypes.c_uint32), ("adapter", ctypes.c_uint32),
                ("game_image_size", ctypes.c_uint32), ("game_image", ctypes.c_void_p),
                ("plugin_module", ctypes.c_void_p), ("plugins_directory", ctypes.c_wchar_p),
                ("plugin_path", ctypes.c_wchar_p), ("extra_options_version", ctypes.c_char_p),
                ("log_utf8", LOG)]


class Frame(ctypes.Structure):
    _fields_ = [("size", ctypes.c_uint32), ("input_enabled", ctypes.c_uint32), ("car", ctypes.c_void_p),
                ("buttons", ctypes.c_uint32), ("delta_seconds", ctypes.c_float), ("flash", ctypes.c_void_p)]


class Plugin:
    def __init__(self, output):
        d = self.dll = ctypes.CDLL(str(output / "tests" / TEST_DLL))
        d.NitrousTestCheck.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        d.NitrousTestCheck.restype = ctypes.c_char_p
        d.NitrousTestParseKey.argtypes = [wintypes.LPCWSTR]
        d.NitrousTestParseButton.argtypes = [wintypes.LPCWSTR]
        d.NitrousTestFrame.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_float]
        d.NitrousTestConfigure.argtypes = [ctypes.c_float] * 4
        d.NitrousTestClock.argtypes = [ctypes.c_ulonglong]
        d.NitrousTestState.argtypes = [ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_uint),
                                       ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_int)]
        d.FH2PluginInitialize.argtypes = [ctypes.c_void_p]
        d.FH2PluginVehicleUpdate.argtypes = [ctypes.c_void_p]
        d.NitrousHold.argtypes = [ctypes.c_int]
        d.NitrousTank.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_int)]
        self.lines = []
        self.log = LOG(lambda text: self.lines.append(text.decode("utf-8", "replace")))

    def state(self):
        dyn, arms, remaining, active = ctypes.c_void_p(), ctypes.c_uint(), ctypes.c_float(), ctypes.c_int()
        self.dll.NitrousTestState(ctypes.byref(dyn), ctypes.byref(arms), ctypes.byref(remaining), ctypes.byref(active))
        return dyn.value, arms.value, remaining.value, active.value

    def initialize(self, folder, ini_text, image, size):
        dll_path = folder / DLL_NAME
        (folder / INI_NAME).write_text(ini_text, encoding="utf-8")
        self.lines.clear()
        context = Context(ctypes.sizeof(Context), 1, 1, size, image, None, str(folder), str(dll_path), b"0.4.4",
                          self.log)
        return self.dll.FH2PluginInitialize(ctypes.byref(context))

    def text(self):
        return "".join(self.lines)


class FakeCar:
    """A ForzaCar and its CCarDynamics in Python-owned memory, for an image based at `base`."""

    def __init__(self, base, car_vtable=CAR_VTABLE, dyn_vtable=DYN_VTABLE, iface_vtable=IFACE_VTABLE, linked=True):
        self.car = ctypes.create_string_buffer(0x5000)
        self.dyn = ctypes.create_string_buffer(0x1000 + 0x1000)
        struct.pack_into("<Q", self.car, 0, base + car_vtable)
        struct.pack_into("<Q", self.dyn, 0, base + dyn_vtable)
        struct.pack_into("<Q", self.dyn, IFACE, base + iface_vtable)
        if linked:
            struct.pack_into("<Q", self.car, CAR_DYNAMICS, ctypes.addressof(self.dyn) + IFACE)

    @property
    def address(self):
        return ctypes.addressof(self.car)

    @property
    def dyn_address(self):
        return ctypes.addressof(self.dyn)

    def params(self):
        return list(struct.unpack_from("<5f", self.dyn, PARAMS))

    def int(self, at):
        return struct.unpack_from("<i", self.dyn, at)[0]

    def float(self, at):
        return struct.unpack_from("<f", self.dyn, at)[0]

    def set(self, at, value, kind="<f"):
        struct.pack_into(kind, self.dyn, at, value)


# ---------------------------------------------------------------- 2. settings

def test_settings(plugin):
    print("settings")
    d = plugin.dll
    keys = {"N": ord("N"), "n": ord("N"), "7": ord("7"), "F1": 0x70, "f12": 0x7B, "F24": 0x87, "LShift": 0xA0,
            "shift": 0x10, "Space": 0x20, "Mouse4": 0x05, "0x41": 0x41, " RCtrl ": 0xA3, "": 0, "None": 0}
    got = {k: d.NitrousTestParseKey(k) for k in keys}
    check(got == keys, f"keys parse ({got})")
    bad = {k: d.NitrousTestParseKey(k) for k in ("F25", "F0", "Nn", "0x", "0x100", "Hyper")}
    check(all(v == -1 for v in bad.values()), f"bad keys are refused ({bad})")
    buttons = {"LeftStick": 0x40, "L3": 0x40, "ls": 0x40, "RightStick": 0x80, "A": 0x1000, "Y": 0x8000,
               "LB": 0x100, "DPadDown": 0x2, "View": 0x20, "None": 0, "": 0}
    got = {k: d.NitrousTestParseButton(k) for k in buttons}
    check(got == buttons, f"pad buttons parse ({got})")
    check(d.NitrousTestParseButton("RT") == -1 and d.NitrousTestParseButton("Start ") == -1,
          "a trigger or an unknown name is refused (the frame carries XInput's buttons only)")


# ---------------------------------------------------------------- 3. car

def test_car(plugin):
    print("car")
    d = plugin.dll
    base = 0x7FF6_0000_0000
    d.NitrousTestReset()
    d.NitrousTestConfigure(1.6, 20.0, 0.5, 60.0)
    car = FakeCar(base)
    status = d.NitrousTestFrame(base, car.address, 0, 0.016)
    check(status == ARMED and car.params() == [0.0, 1.0e6, pytest_float(1.6), 0.5, 20.0],
          f"first frame: the parameters [0, 1e6 rad/s, 1.6, 0.5 s, 20 s] are written ({status}, {car.params()})")
    dyn, arms, remaining, active = plugin.state()
    check(dyn == car.dyn_address and arms == 1 and remaining == 1.0 and not active,
          "the state names the dynamics object (not its interface), one arm, a full tank")
    check(car.int(REQUEST) == 0, "not held: no request")
    check(d.NitrousTestFrame(base, car.address, 1, 0.016) == IDLE and car.int(REQUEST) == 1, "held: request 1")
    # The physics boosts: active, the tank in use.
    car.set(ACTIVE, 1, "<i")
    car.set(USED, 5.0)
    check(d.NitrousTestFrame(base, car.address, 1, 0.5) == BOOSTING and car.float(USED) == 5.0,
          "boosting: the plugin leaves the timers to the physics")
    check(abs(plugin.state()[2] - 0.75) < 1e-6 and plugin.state()[3] == 1, "the tank reads 75 %, boosting")
    car.set(ACTIVE, 0, "<i")
    check(d.NitrousTestFrame(base, car.address, 0, 0.5) == IDLE and car.int(REQUEST) == 0, "released: request 0")
    check(abs(car.float(USED) - (5.0 - 0.5 * 20 / 60)) < 1e-5, f"idle: it refills at 20 s per 60 s ({car.float(USED)})")
    car.set(USED, 0.05)
    d.NitrousTestFrame(base, car.address, 0, 0.5)
    check(car.float(USED) == 0.0, "the refill stops at a full tank")
    car.set(USED, 4.0)
    d.NitrousTestFrame(base, car.address, 1, 0.5)
    check(car.float(USED) == 4.0, "no refill while held (with the throttle up, the physics does not boost)")
    car.set(USED, 4.0)
    d.NitrousTestFrame(base, car.address, 0, 5.0)
    check(car.float(USED) == 4.0, "no refill over a frame of 1 s or more (a pause or a hitch)")
    car.set(USED, float("nan"))
    d.NitrousTestFrame(base, car.address, 0, 0.1)
    check(car.float(USED) == 0.0, "a NaN tank (not a time the physics writes) is set to full")
    car.set(USED, -3.0)
    car.set(ACTIVE, 1, "<i")
    d.NitrousTestFrame(base, car.address, 1, 0.1)
    check(car.float(USED) == -3.0, "while the physics boosts, even a bad tank is left to it")
    car.set(ACTIVE, 0, "<i")
    # The game rebuilds the engine: its builder writes the setup's (empty) block.
    struct.pack_into("<5f", car.dyn, PARAMS, 0.0, 0.0, 1.0, 0.001, 0.0)
    check(d.NitrousTestFrame(base, car.address, 0, 0.016) == ARMED and car.params()[1] == 1.0e6 and
          plugin.state()[1] == 2, "parameters cleared by the game are written again (second arm)")
    other = FakeCar(base)
    check(d.NitrousTestFrame(base, other.address, 0, 0.016) == ARMED and plugin.state()[0] == other.dyn_address,
          "another car is armed on its first frame")

    def untouched(fake, expected, what):
        before = bytes(fake.dyn)
        status = d.NitrousTestFrame(base, fake.address, 1, 0.016)
        check(status == expected and bytes(fake.dyn) == before, f"{what}: status {status}, nothing written")

    untouched(FakeCar(base, car_vtable=CAR_VTABLE + 8), NOT_LOCAL_CAR, "a car of another class")
    untouched(FakeCar(base, iface_vtable=IFACE_VTABLE + 8), NO_DYNAMICS, "dynamics with another interface vtable")
    untouched(FakeCar(base, dyn_vtable=DYN_VTABLE - 8), NO_DYNAMICS, "dynamics with another object vtable")
    untouched(FakeCar(base, linked=False), NO_DYNAMICS, "a car without dynamics")
    check(d.NitrousTestFrame(base, None, 1, 0.016) == NOT_LOCAL_CAR, "no car")
    # Tuning limits.
    d.NitrousTestConfigure(10.0, 0.0, 0.0, -1.0)
    car = FakeCar(base)
    d.NitrousTestFrame(base, car.address, 0, 0.016)
    check(car.params()[2:] == [5.0, pytest_float(0.01), 0.5], f"limits: scale 5, build-up 0.01 s, tank 0.5 s ({car.params()})")
    car.set(USED, 0.25)
    d.NitrousTestFrame(base, car.address, 0, 0.5)
    check(car.float(USED) == 0.25, "RechargeTime <= 0: the tank never refills")
    d.NitrousTestConfigure(float("nan"), float("nan"), float("nan"), float("nan"))
    car = FakeCar(base)
    d.NitrousTestFrame(base, car.address, 0, 0.016)
    check(car.params()[2:] == [pytest_float(1.6), 0.5, 20.0], "NaN settings fall back to Fast & Furious' values")
    d.NitrousTestConfigure(1.6, 20.0, 0.5, 60.0)


def pytest_float(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


# ---------------------------------------------------------------- 6. runs

THROTTLE, ENGINE_SPEED, SHIFT_TIME = 0x1530, 0x4E8, 0x540
END_NONE, END_RELEASED, END_EMPTY, END_GEAR_CHANGE, END_THROTTLE, END_ENGINE_SPEED, END_OTHER = range(7)


class State(ctypes.Structure):
    _fields_ = [("dynamics", ctypes.c_void_p), ("arms", ctypes.c_uint), ("remaining", ctypes.c_float),
                ("active", ctypes.c_int), ("held", ctypes.c_int), ("throttle", ctypes.c_float),
                ("buildup_time", ctypes.c_float), ("level", ctypes.c_float), ("run", ctypes.c_int),
                ("run_seconds", ctypes.c_float), ("gap", ctypes.c_float), ("carry", ctypes.c_float),
                ("bridged", ctypes.c_uint), ("cut_reason", ctypes.c_int), ("cut_throttle", ctypes.c_float),
                ("ended", ctypes.c_int), ("ended_seconds", ctypes.c_float), ("ended_bridged", ctypes.c_uint),
                ("ended_throttle", ctypes.c_float), ("started", ctypes.c_int)]


def state_of(plugin):
    s = State()
    plugin.dll.NitrousTestStateCopy(ctypes.byref(s))
    return s


def test_runs(plugin):
    """Boost runs: the build-up kept over short cuts, why a run ends, the effects' envelope."""
    print("runs")
    d = plugin.dll
    d.NitrousTestEndText.restype = ctypes.c_char_p
    base = 0x7FF6_0000_0000
    d.NitrousTestReset()
    d.NitrousTestConfigure(1.6, 20.0, 0.5, 60.0)
    car = FakeCar(base)
    car.set(THROTTLE, 1.0)
    car.set(ENGINE_SPEED, 500.0)
    d.NitrousTestFrame(base, car.address, 0, 0.016)  # armed

    def frame(held, dt=0.1, active=None, buildup=None):
        if active is not None:
            car.set(ACTIVE, active, "<i")
        if buildup is not None:
            car.set(BUILDUP, buildup)
        d.NitrousTestFrame(base, car.address, held, dt)
        return state_of(plugin)

    s = frame(1, active=1, buildup=0.1)
    check(s.started and s.run and abs(s.level - 0.2) < 1e-6 and s.ended == END_NONE,
          f"a boost starts a run; the level follows the build-up (0.1 / 0.5 s: {s.level})")
    s = frame(1, buildup=0.6)
    check(not s.started and s.level == 1.0 and abs(s.carry - 0.6) < 1e-6, "full build-up: level 1")
    # A gear change: the physics stops (its throttle cut), and resets this boost's time.
    car.set(THROTTLE, 0.2)
    car.set(SHIFT_TIME, 0.15)
    s = frame(1, active=0, buildup=0.0)
    check(s.run and s.ended == END_NONE and s.cut_reason == END_GEAR_CHANGE and abs(s.cut_throttle - 0.2) < 1e-6 and
          abs(s.level - (1 - 0.1 / 1.5)) < 1e-5,
          f"a cut while held keeps the run open (a gear change: throttle 0.2 while shifting), the level eases slowly ({s.level})")
    car.set(THROTTLE, 1.0)
    car.set(SHIFT_TIME, 0.0)
    s = frame(1, active=1, buildup=0.05)
    check(abs(car.float(BUILDUP) - 0.65) < 1e-5 and s.bridged == 1 and s.level == 1.0 and s.cut_reason == END_NONE,
          f"the boost resumes with its build-up put back (0.6 + 0.05 s: {car.float(BUILDUP)}): full torque at once")
    s = frame(1, active=1, buildup=0.66)
    check(s.bridged == 1 and car.float(BUILDUP) == pytest_float(0.66), "while it goes on, the physics' time is left alone")
    # The physics resets the time between two frames without a frame seeing the cut.
    s = frame(1, active=1, buildup=0.02)
    check(s.bridged == 2 and abs(car.float(BUILDUP) - 0.68) < 1e-5, "a cut no frame saw is bridged too")
    s = frame(0, active=1)
    check(s.run and not s.ended, "released: the physics still boosts this frame (the request goes off now)")
    s = frame(0, active=0)
    check(s.ended == END_RELEASED and not s.run and s.ended_bridged == 2 and abs(s.ended_seconds - 0.6) < 1e-5,
          f"next frame the run ends: released, 0.6 s boosted, 2 cuts bridged ({s.ended}, {s.ended_seconds}, {s.ended_bridged})")
    check(d.NitrousTestEndText(END_RELEASED) == b"the input was released" and b"0.99" in d.NitrousTestEndText(END_THROTTLE),
          "each reason has its text")
    d.NitrousTestClearEnded()
    s = frame(0)
    check(s.ended == END_NONE and abs(s.level - (1 - 0.1 / 1.5 * 0 - 0.1 / 0.4 * 2)) < 0.1 and s.level < 1,
          f"after the run the level eases off quickly ({s.level})")
    for _ in range(4):
        s = frame(0)
    check(s.level == 0.0, "and is 0 after 0.4 s")
    # A long cut: the throttle stays below 0.99 (pedal lifted, no gear change).
    s = frame(1, active=1, buildup=0.3)
    check(s.started and s.bridged == 0, "a new run")
    car.set(THROTTLE, 0.5)
    ended = None
    for i in range(5):
        s = frame(1, dt=0.3, active=0, buildup=0.0)
        if s.ended:
            ended = (i, s.ended, s.ended_throttle)
            break
    check(ended and ended[0] == 3 and ended[1] == END_THROTTLE and abs(ended[2] - 0.5) < 1e-6,
          f"a cut of a second or more ends the run: the throttle (0.5) ({ended})")
    d.NitrousTestClearEnded()
    car.set(THROTTLE, 1.0)
    s = frame(1, active=1, buildup=0.1)
    check(s.started and abs(car.float(BUILDUP) - 0.1) < 1e-6, "after that, a new run builds up from 0 again")
    car.set(USED, 20.0)
    s = frame(1, active=0, buildup=0.0)
    check(s.ended == END_EMPTY, "the tank used up ends the run at once")
    d.NitrousTestClearEnded()
    car.set(USED, 0.0)
    car.set(ENGINE_SPEED, -10.0)
    frame(1, active=1, buildup=0.1)
    s = frame(1, active=0, buildup=0.0)
    check(s.cut_reason == END_ENGINE_SPEED, "an engine speed out of the range is named")
    car.set(ENGINE_SPEED, 500.0)
    other = FakeCar(base)
    d.NitrousTestFrame(base, other.address, 1, 0.1)
    s = state_of(plugin)
    check(s.ended == END_OTHER and not s.run and s.level == 0.0, "another car ends the open run")
    d.NitrousTestClearEnded()
    d.NitrousTestReset()


# ---------------------------------------------------------------- 7. effects

class Effects(ctypes.Structure):
    _fields_ = [("fov", ctypes.c_int), ("fov_degrees", ctypes.c_float), ("impulse_degrees", ctypes.c_float),
                ("cockpit_degrees", ctypes.c_float), ("sound", ctypes.c_int), ("sound_volume", ctypes.c_float),
                ("flames", ctypes.c_int), ("camera_layers", ctypes.c_int), ("rumble", ctypes.c_int)]


class Camera(ctypes.Structure):
    _fields_ = [("nitrous", ctypes.c_float), ("impulse", ctypes.c_float), ("pulse", ctypes.c_float)]


class Clip(ctypes.Structure):
    _fields_ = [("data", ctypes.POINTER(ctypes.c_short)), ("frames", ctypes.c_uint)]


class Player(ctypes.Structure):
    _fields_ = [("start", Clip), ("sustain", Clip), ("stop", Clip), ("start_pos", ctypes.c_int),
                ("stop_pos", ctypes.c_int), ("sustain_pos", ctypes.c_uint), ("sustain_gain", ctypes.c_float),
                ("gain", ctypes.c_float), ("run", ctypes.c_int)]


class Synth(ctypes.Structure):
    _fields_ = [("seed", ctypes.c_uint)] + [(n, ctypes.c_float) for n in
                                            ("burst", "burst_time", "low", "band", "high_last", "hiss_low", "gain",
                                             "level")]


def clip(values):
    data = (ctypes.c_short * (2 * len(values)))(*[v for v in values for _ in (0, 1)])
    return Clip(ctypes.cast(data, ctypes.POINTER(ctypes.c_short)), len(values)), data


def rms(samples):
    return math.sqrt(sum(s * s for s in samples) / max(1, len(samples)))


def test_effects(plugin):
    print("effects")
    d = plugin.dll
    d.NitrousTestEffectsRead.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(Effects)]
    d.NitrousTestEffectsRead.restype = ctypes.c_char_p
    d.NitrousTestCameraStep.argtypes = [ctypes.POINTER(Camera), ctypes.c_int, ctypes.c_int, ctypes.c_float]
    d.NitrousTestCameraDegrees.argtypes = [ctypes.POINTER(Camera), ctypes.POINTER(Effects), ctypes.c_int]
    d.NitrousTestCameraDegrees.restype = ctypes.c_float
    d.NitrousTestFovFactor.argtypes = [ctypes.c_float, ctypes.c_float]
    d.NitrousTestFovFactor.restype = ctypes.c_float
    d.NitrousTestPlayerRender.argtypes = [ctypes.POINTER(Player), ctypes.c_void_p, ctypes.c_uint, ctypes.c_int,
                                          ctypes.c_int, ctypes.c_float]
    d.NitrousTestPlayerIdle.argtypes = [ctypes.POINTER(Player)]
    d.NitrousTestLoadWav.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(Clip)]
    d.NitrousTestSynth.argtypes = [ctypes.POINTER(Synth), ctypes.c_int, ctypes.c_void_p, ctypes.c_uint, ctypes.c_float,
                                   ctypes.c_int, ctypes.c_float]
    with tempfile.TemporaryDirectory() as temp:
        folder = Path(temp)
        ini = folder / "Nitrous.ini"

        def read(text):
            ini.write_text(text, encoding="ascii")
            e = Effects()
            warning = d.NitrousTestEffectsRead(str(ini), ctypes.byref(e))
            return (e.fov, round(e.fov_degrees, 3), round(e.impulse_degrees, 3), round(e.cockpit_degrees, 3), e.sound,
                    round(e.sound_volume, 3), e.flames, e.camera_layers, e.rumble), warning

        check(read("[Nitrous]\nEnabled=1\n") == ((1, 5.0, 5.0, 0.0, 1, 0.6, 1, 1, 1), b""),
              "[Effects] defaults: Fast & Furious' FOV +5 / impulse 5 on the follow cameras, none in the cockpit; sound 0.6; "
              "exhaust flames, camera layers and rumble on")
        check(read("[Effects]\nFov=0\nFovDegrees=8\nFovImpulseDegrees=0\nFovCockpitDegrees=2.5\nSound=0\nSoundVolume=0.3\n"
                   "ExhaustFlames=0\nCameraLayers=0\nRumble=0\n")
              == ((0, 8.0, 0.0, 2.5, 0, 0.3, 0, 0, 0), b""), "[Effects] values are read")
        check(read("[Effects]\nFovDegrees=99\nSoundVolume=7\nFovImpulseDegrees=-3\n")[0] == (1, 30.0, 0.0, 0.0, 1, 1.0, 1, 1, 1),
              "out of range: clamped (30 degrees, volume 1, no negative impulse)")
        check(read("[Effects]\nFovDegrees=wide\n") == ((1, 5.0, 5.0, 0.0, 1, 0.6, 1, 1, 1), b"FovDegrees"),
              "not a number: the default, and the key is named")
        check(read("[Effects]\nFovDegrees=0\nFovImpulseDegrees=0\n")[0][0] == 0 and read("[Effects]\nSoundVolume=0\n")[0][4] == 0,
              "all FOV degrees 0 turn the camera effect off; SoundVolume=0 the sound")
    effects = Effects(1, 5.0, 5.0, 2.0, 1, 0.6, 1, 1, 1)
    camera = Camera()
    degrees = lambda mounted=0: d.NitrousTestCameraDegrees(ctypes.byref(camera), ctypes.byref(effects), mounted)
    d.NitrousTestCameraStep(ctypes.byref(camera), 1, 1, 0.016)
    first = degrees()
    for _ in range(4):
        d.NitrousTestCameraStep(ctypes.byref(camera), 1, 0, 0.016)
    early = degrees()
    for _ in range(150):
        d.NitrousTestCameraStep(ctypes.byref(camera), 1, 0, 0.016)
    held = degrees()
    check(abs(first) < 0.01 and -0.5 < early < 1.0 and abs(held - 5.0) < 1e-4 and abs(degrees(1) - 2.0) < 1e-4,
          f"the follow cameras: no jump at the start (the impulse cancels the kick: {first:.2f}, {early:.2f}), "
          f"+5 once the impulse faded ({held:.2f}); the cockpit +2 ({degrees(1):.2f})")
    for _ in range(62):
        d.NitrousTestCameraStep(ctypes.byref(camera), 0, 0, 0.016)
    middle = degrees()
    for _ in range(70):
        d.NitrousTestCameraStep(ctypes.byref(camera), 0, 0, 0.016)
    check(1.0 < middle < 4.0 and degrees() == 0.0, f"after the boost it narrows back over 2 s ({middle:.2f}, then 0)")
    d.NitrousTestCameraStep(ctypes.byref(camera), 1, 0, 5.0)
    check(camera.nitrous == 0.0, "a step of 0.5 s or more (a pause) changes nothing")
    effects.fov = 0
    camera.nitrous = 1.0
    check(degrees() == 0.0, "Fov=0: no kick")
    factor = d.NitrousTestFovFactor
    check(abs(factor(60.0, 5.0) - 65 / 60) < 1e-6 and factor(60.0, 0.0) == 1.0 and abs(factor(60.0, -100.0) - 10 / 60) < 1e-6
          and factor(5.0, 5.0) == 1.0 and factor(float("nan"), 5.0) == 1.0 and factor(60.0, float("nan")) == 1.0,
          "the FOV factor: (base + kick) / base, at least 10 degrees; an odd base or kick: 1")

    # Fast & Furious' samples: start, sustain looped while boosting (a 50 ms crossfade at the seam), stop.
    start, a = clip([1000] * 100)
    sustain, b = clip([100 + i * 200 // 7200 for i in range(7200)])
    stop, c = clip([-500] * 50)
    player = Player(start, sustain, stop, -1, -1, 0, 0.0, 1.0, 0)
    out = (ctypes.c_short * (2 * 24000))()
    d.NitrousTestPlayerRender(ctypes.byref(player), out, 100, 1, 1, 1.0)
    check(out[0] == 1000 and out[1] == 1000 and player.start_pos == 100 and 0 < player.sustain_gain < 0.01,
          f"started: the start sample plays, the sustain fades in under it ({out[0]}, {player.sustain_gain})")
    d.NitrousTestPlayerRender(ctypes.byref(player), out, 24000, 1, 0, 1.0)
    check(player.start_pos == -1 and player.sustain_gain == 1.0 and 2400 <= player.sustain_pos < 7200,
          f"the start ends; the sustain reaches full level in 0.5 s and loops ({player.sustain_pos})")
    seam = [out[2 * i] for i in range(24000)]
    check(max(abs(seam[i + 1] - seam[i]) for i in range(len(seam) - 1)) <= 2,
          "the loop seam is crossfaded (no step between samples)")
    d.NitrousTestPlayerRender(ctypes.byref(player), out, 50, 0, 0, 1.0)
    check(player.stop_pos == 50 and out[0] < 0, f"the run ends: the stop sample plays ({out[0]})")
    d.NitrousTestPlayerRender(ctypes.byref(player), out, 9550, 0, 0, 1.0)
    halfway = player.sustain_gain
    d.NitrousTestPlayerRender(ctypes.byref(player), out, 20000, 0, 0, 1.0)
    check(0.4 < halfway < 0.6 and d.NitrousTestPlayerIdle(ctypes.byref(player)) and out[2 * 19999] == 0,
          f"then the sustain fades out over 0.4 s (half way at 0.2 s: {halfway:.2f}), no cut, and the player idles")
    player = Player(start, sustain, stop, -1, -1, 0, 0.0, 1.0, 0)
    d.NitrousTestPlayerRender(ctypes.byref(player), out, 10, 0, 1, 1.0)
    check(player.start_pos == 10 and player.stop_pos == 10, "a boost shorter than one buffer: start and stop both play")
    player = Player(start, sustain, stop, -1, -1, 0, 0.0, 0.0, 0)
    d.NitrousTestPlayerRender(ctypes.byref(player), out, 100, 1, 1, 0.0)
    check(all(v == 0 for v in out[:200]), "volume 0 (no focus, menus): silent")
    with tempfile.TemporaryDirectory() as temp:
        def wav(name, rate, channels, width, frames):
            path = Path(temp) / name
            with wave.open(str(path), "wb") as w:
                w.setnchannels(channels)
                w.setsampwidth(width)
                w.setframerate(rate)
                w.writeframes(b"".join(struct.pack("<h", (i % 200) - 100) * channels for i in range(frames))
                              if width == 2 else bytes(frames * channels))
            return path

        loaded = Clip()
        ok = d.NitrousTestLoadWav(str(wav("stereo.wav", 48000, 2, 2, 480)), ctypes.byref(loaded))
        check(ok and loaded.frames == 480 and loaded.data[2] == -99 and loaded.data[3] == -99,
              "a 48 kHz stereo 16-bit WAV loads")
        ok = d.NitrousTestLoadWav(str(wav("mono.wav", 48000, 1, 2, 480)), ctypes.byref(loaded))
        check(ok and loaded.frames == 480 and loaded.data[10] == loaded.data[11] == -95, "a mono one is made stereo")
        check(not d.NitrousTestLoadWav(str(wav("cd.wav", 44100, 2, 2, 480)), ctypes.byref(loaded)) and
              not d.NitrousTestLoadWav(str(wav("8bit.wav", 48000, 1, 1, 480)), ctypes.byref(loaded)) and
              not d.NitrousTestLoadWav(str(Path(temp) / "none.wav"), ctypes.byref(loaded)),
              "another rate, 8-bit or a missing file: refused (our synthesized sound instead)")
    synth = Synth()
    mono = (ctypes.c_short * 48000)()
    d.NitrousTestSynth(ctypes.byref(synth), 1, mono, 48000, 0.0, 0, 0.6)
    check(all(v == 0 for v in mono), "our synthesized sound: silent with no boost")
    d.NitrousTestSynth(ctypes.byref(synth), 1, mono, 48000, 0.0, 1, 0.6)
    burst_first, burst_last = rms(mono[2400:14400]), rms(mono[40000:48000])
    d.NitrousTestSynth(ctypes.byref(synth), 1, mono, 48000, 1.0, 0, 0.6)
    hiss = rms(mono[24000:48000])
    check(burst_first > 500 and burst_last < burst_first / 4 and 100 < hiss < burst_first,
          f"a burst that decays ({burst_first:.0f} -> {burst_last:.0f}) and a quieter hiss while boosting ({hiss:.0f})")
    d.NitrousTestSynth(ctypes.byref(synth), 1, mono, 48000, 1.0, 1, 0.0)
    check(all(v == 0 for v in mono), "volume 0: silent")


# ---------------------------------------------------------------- 8. hooks

EFFECT = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_float, ctypes.c_void_p)
CAMERA = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_float)
GETTER = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p)
RELEASE = ctypes.CFUNCTYPE(ctypes.c_ulong, ctypes.c_void_p)
# hooks.h
BACKFIRE_RVA, BURBLE_RVA, CAMERA_RVA, RUMBLE_RVA = 0xF7D8A0, 0xF7DD70, 0x1037DF0, 0x1076010
RUMBLE = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_float, ctypes.c_void_p, ctypes.c_int)
I_SPEED, I_ACTIVE, I_BUILDUP, I_BUILDUP_TIME = 0xE28, 0x15C, 0x164, 0xA1C
MPH = 0.44704


def test_hooks(plugin):
    print("hooks")
    d = plugin.dll
    d.NitrousTestHooksSetup.argtypes = [ctypes.c_void_p] * 4
    d.NitrousTestBackfire.argtypes = d.NitrousTestBurble.argtypes = [ctypes.c_void_p, ctypes.c_float, ctypes.c_void_p]
    d.NitrousTestCameraInput.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_float]
    d.NitrousTestCameraNitrous.argtypes = [ctypes.c_float] * 3
    d.NitrousTestCameraNitrous.restype = ctypes.c_float
    d.NitrousTestCameraImpulse.argtypes = [ctypes.c_float, ctypes.c_int, ctypes.POINTER(ctypes.c_float)]
    d.NitrousTestCameraImpulse.restype = ctypes.c_float
    calls = {"backfire": 0, "burble": 0, "camera": 0, "release": 0}

    def original(name, field, value):
        def run(self, dt, dynamics):
            calls[name] += 1
            struct.pack_into("<i", (ctypes.c_char * 0x80).from_address(self), field, value)
        return EFFECT(run)

    backfire = original("backfire", 0x0C, 3)  # "a backfire of the game's own"
    burble = original("burble", 0x70, 0)

    def camera_run(layer, target, context, external):
        calls["camera"] += 1
        struct.pack_into("<f", (ctypes.c_char * 0x110).from_address(layer), 0, 0.0)  # the carriers' case writes 0
    camera = CAMERA(camera_run)
    release = RELEASE(lambda self: calls.__setitem__("release", calls["release"] + 1) or 0)
    vtable = (ctypes.c_void_p * 0x80)()
    vtable[3] = ctypes.cast(release, ctypes.c_void_p).value
    iface = ctypes.create_string_buffer(0x1000)
    struct.pack_into("<Q", iface, 0, ctypes.addressof(vtable))
    target_iface = ctypes.c_void_p(ctypes.addressof(iface))
    getter = GETTER(lambda self: target_iface.value)
    target_vtable = (ctypes.c_void_p * 0x20)()
    target_vtable[0x70 // 8] = ctypes.cast(getter, ctypes.c_void_p).value
    target_object = (ctypes.c_void_p * 1)(ctypes.addressof(target_vtable))
    target = ctypes.c_void_p(ctypes.addressof(target_object))
    d.NitrousTestHooksSetup(ctypes.addressof(vtable), ctypes.cast(backfire, ctypes.c_void_p),
                            ctypes.cast(burble, ctypes.c_void_p), ctypes.cast(camera, ctypes.c_void_p))
    effect = ctypes.create_string_buffer(0x80)
    reference = ctypes.c_void_p(ctypes.addressof(iface))

    def state(field):
        return struct.unpack_from("<i", effect, field)[0]

    d.NitrousTestBackfire(effect, 0.016, ctypes.byref(reference))
    d.NitrousTestBurble(effect, 0.016, ctypes.byref(reference))
    check(calls["backfire"] == 1 and calls["burble"] == 1 and calls["release"] == 0 and state(0x0C) == 3,
          "not boosting: the game's own backfire and burble run (they release the dynamics)")
    struct.pack_into("<i", iface, I_ACTIVE, 1)
    d.NitrousTestBackfire(effect, 0.016, ctypes.byref(reference))
    d.NitrousTestBurble(effect, 0.016, ctypes.byref(reference))
    check(calls["backfire"] == 1 and calls["burble"] == 1 and calls["release"] == 2 and state(0x0C) == 2 and
          state(0x70) == 1, "boosting: Fast & Furious' backfire (2) and pop (1) instead, the dynamics released once each")
    other = (ctypes.c_void_p * 0x80)()
    struct.pack_into("<Q", iface, 0, ctypes.addressof(other))
    d.NitrousTestBackfire(effect, 0.016, ctypes.byref(reference))
    struct.pack_into("<Q", iface, 0, ctypes.addressof(vtable))
    check(calls["backfire"] == 2, "another object than CCarDynamics' interface: the game's own backfire")
    struct.pack_into("<i", iface, I_ACTIVE, 0)

    ramp = lambda mph: d.NitrousTestCameraNitrous(mph * MPH, 1.0, 0.5)
    check(ramp(0) == 0 and ramp(1) == 0 and abs(ramp(15.5) - 0.5) < 1e-4 and ramp(30) == 1 and ramp(150) == 1,
          "camera Nitrous: 0 at 1 mph, 1 from 30 mph (Fast & Furious' CameraNitrous tunables)")
    check(abs(d.NitrousTestCameraNitrous(30 * MPH, 0.25, 0.5) - 0.5) < 1e-6 and
          d.NitrousTestCameraNitrous(30 * MPH, 0.0, 0.5) == 0 and d.NitrousTestCameraNitrous(30 * MPH, 1.0, 0.0) == 0,
          "camera Nitrous: times the build-up (boost time / build-up time, at most 1)")
    previous = ctypes.c_float(0)
    pulses = [d.NitrousTestCameraImpulse(40 * MPH, a, ctypes.byref(previous)) for a in (0, 1, 1, 1, 0, 1)]
    check(pulses == [0, 1, 0, 0, 0, 1], f"camera NitrousImpulse: 1 on the first update of each boost ({pulses})")

    layer = ctypes.create_string_buffer(0x110)
    def feed(kind, speed_mph, active=0, boost=0.0, buildup=0.5):
        struct.pack_into("<i", layer, 0x48, kind)
        struct.pack_into("<f", iface, I_SPEED, speed_mph * MPH)
        struct.pack_into("<i", iface, I_ACTIVE, active)
        struct.pack_into("<f", iface, I_BUILDUP, boost)
        struct.pack_into("<f", iface, I_BUILDUP_TIME, buildup)
        d.NitrousTestCameraInput(layer, target, None, 0.0)
        return round(struct.unpack_from("<f", layer, 0)[0], 4)

    check(feed(0x17, 60, 1, 0.25) == 0.5 and feed(0x17, 60, 1, 2.0) == 1.0 and feed(0x17, 60, 0, 0.0) == 0.0,
          "the camera input on a ShoulderYaw layer: Fast & Furious' Nitrous (the build-up at speed)")
    struct.pack_into("<f", layer, 4, 0.0)
    check([feed(0x18, 60, a) for a in (1, 1, 0, 1)] == [1.0, 0.0, 0.0, 1.0],
          "the camera input on a ShoulderRoll layer: Fast & Furious' NitrousImpulse (its +4 keeps the last activity)")
    check(feed(0x0C, 60, 1, 0.25) == 0.0 and calls["camera"] == 8, "other layers: the game's own input only")

    d.NitrousTestCurve.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_float]
    d.NitrousTestCurve.restype = ctypes.c_float
    curve = lambda wheel, motor, x: round(d.NitrousTestCurve(wheel, motor, x), 3)
    check([curve(0, 0, x) for x in (0.0, 0.001, 0.35, 0.7, 0.85, 1.0)] == [0.1, 0.1, 0.15, 0.2, 0.15, 0.1] and
          [curve(0, 1, x) for x in (0.5, 0.7, 0.85, 1.0)] == [0.1, 0.1, 0.55, 1.0] and curve(1, 0, 0.0) == 0.2,
          "Fast & Furious' <Nitrous> rumble curves: left 0.1..0.2..0.1, right 0.1 then up to 1 as the tank empties")
    motors = {"value": (1000, 0)}

    def rumble_run(self, dt, input_, wheel):
        struct.pack_into("<II", (ctypes.c_char * 16).from_address(self), 0, *motors["value"])
    rumble_original = RUMBLE(rumble_run)
    d.NitrousTestRumbleSetup.argtypes = [ctypes.c_void_p]
    d.NitrousTestRumble.argtypes = [ctypes.c_void_p, ctypes.c_float, ctypes.c_void_p, ctypes.c_int, ctypes.c_float]
    d.NitrousTestRumbleSetup(ctypes.cast(rumble_original, ctypes.c_void_p))
    pad = ctypes.create_string_buffer(16)

    def rumble(x, strength=1.0, wheel=0, paused=0, value=(1000, 0)):
        motors["value"] = value
        struct.pack_into("<fB", pad, 8, strength, paused)
        d.NitrousTestRumble(pad, 0.016, None, wheel, x)
        return struct.unpack_from("<II", pad, 0)

    check(rumble(0.0) == (1000, 0), "not boosting (input 0): Forza Horizon 2's rumble as it is")
    check(rumble(0.85) == (1000 + int(0.15 * 65535), int(0.55 * 65535)),
          f"boosting, 85 % of the tank used: Fast & Furious' curves added per motor ({rumble(0.85)})")
    check(rumble(1.0, strength=0.5, value=(30000, 100)) == (32767, 32767) and
          rumble(0.85, paused=1) == (1000, 0) and rumble(0.85, value=(0xFFFF, 0))[0] == 0xFFFF,
          "the sum is clamped at the rumble strength; paused or a forced full rumble: untouched")

# ---------------------------------------------------------------- 9. rules and the end of a boost

class Rules(ctypes.Structure):
    _fields_ = [(n, ctypes.c_int) for n in ("mode", "auto_recharge", "in_races", "free_roam", "in_other_events",
                                            "skill_recharge")] + \
               [("skill", ctypes.c_float * 9), ("chain_per_1000", ctypes.c_float), ("crash_penalty", ctypes.c_float),
                ("crash_impact", ctypes.c_float), ("crash_cancels", ctypes.c_int)]


FREE_ROAM, IN_RACE, IN_OTHER = 0, 1, 2
SKILLS = ["Drift", "NearMiss", "Draft", "Air", "Pass", "Speed", "Wrecker", "Contact", "Other"]


def test_rules(plugin):
    print("rules")
    d = plugin.dll
    d.NitrousTestRules.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(Rules)]
    d.NitrousTestRules.restype = ctypes.c_char_p
    d.NitrousTestAllowed.argtypes = [ctypes.POINTER(Rules), ctypes.c_int]
    d.NitrousTestSkillGroup.argtypes = [wintypes.LPCWSTR]
    d.NitrousTestPlaceAfter.argtypes = [ctypes.c_int, ctypes.c_char_p]
    d.NitrousTestCrash.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.c_int, ctypes.c_float, ctypes.c_float,
                                   ctypes.POINTER(ctypes.c_int)]
    with tempfile.TemporaryDirectory() as temp:
        ini = Path(temp) / "Nitrous.ini"

        def read(text):
            ini.write_text(text, encoding="ascii")
            r = Rules()
            warning = d.NitrousTestRules(str(ini), ctypes.byref(r))
            return r, warning

        r, w = read("[Nitrous]\n")
        check((r.mode, r.auto_recharge, r.in_races, r.free_roam, r.in_other_events, r.skill_recharge, r.crash_penalty,
               r.crash_cancels, w) == (0, 1, 1, 1, 1, 0, 0.0, 0, b""),
              "Mode=Arcade (the default): time recharge, everywhere, no skill refill, no crash penalty")
        r, w = read("[Nitrous]\nMode=Underground\n")
        check((r.mode, r.auto_recharge, r.skill_recharge, r.crash_penalty, r.crash_cancels, r.in_races, r.free_roam) ==
              (1, 0, 1, 4.0, 1, 1, 1) and round(r.skill[0], 2) == 2.0 and round(r.chain_per_1000, 2) == 0.5,
              "Mode=Underground: skills refill, crashes drain 4 s and end the boost, no time recharge, races and free roam")
        r, w = read("[Nitrous]\nMode=underground\nAutoRecharge=1\nFreeRoam=0\nCrashPenalty=2.5\nCrashMinImpact=40\n"
                    "[SkillRecharge]\nDrift=3\nNearMiss=0\nChainPer1000=1\n")
        check((r.auto_recharge, r.free_roam, r.crash_penalty, r.crash_impact, round(r.skill[0], 2), r.skill[1],
               r.chain_per_1000) == (1, 0, 2.5, 40.0, 3.0, 0.0, 1.0),
              "explicit keys win over the preset (any case for Mode)")
        r, w = read("[Nitrous]\nMode=Turbo\nInRaces=maybe\n")
        check(r.mode == 0 and w == b"Mode", f"a bad Mode: Arcade, and named ({w})")
        r, w = read("[Nitrous]\nInRaces=0\nInOtherEvents=0\n")
        check([d.NitrousTestAllowed(ctypes.byref(r), p) for p in (FREE_ROAM, IN_RACE, IN_OTHER)] == [1, 0, 0],
              "InRaces=0 / InOtherEvents=0: allowed in free roam only")
    groups = {n: SKILLS[d.NitrousTestSkillGroup(n)] for n in
              ("Drift", "EDrift", "NearMiss", "ThreadingTheNeedle", "Draft", "SlingShot", "Air", "BigAir",
               "AirbornePass", "Kangaroo", "Pass", "TriplePass", "CleanSpeed", "FastAndClean", "Destruction",
               "Landscaping", "SideSwipe", "LoveTap", "Burnout", "TwoWheels", "")}
    check(groups == {"Drift": "Drift", "EDrift": "Drift", "NearMiss": "NearMiss", "ThreadingTheNeedle": "NearMiss",
                     "Draft": "Draft", "SlingShot": "Draft", "Air": "Air", "BigAir": "Air", "AirbornePass": "Air",
                     "Kangaroo": "Air", "Pass": "Pass", "TriplePass": "Pass", "CleanSpeed": "Speed",
                     "FastAndClean": "Speed", "Destruction": "Wrecker", "Landscaping": "Wrecker",
                     "SideSwipe": "Contact", "LoveTap": "Contact", "Burnout": "Other", "TwoWheels": "Other",
                     "": "Other"}, f"Forza Horizon 2's skill names in the INI's groups ({groups})")
    place = FREE_ROAM
    seen = []
    for event in ("SkillAchieved", "AnthemRaceStart", "AnthemLapEnd", "AnthemRaceEnd", "ShowcaseRaceStart",
                  "ShowcaseRaceEnd", "RaceEncounterStart", "RaceEncounterEnd", "IslandBucketListChallengeStart",
                  "SkillAchieved", "CarMeetEntered", "AnthemRaceStart", "RewardBoardSmashed"):
        place = d.NitrousTestPlaceAfter(place, event.encode())
        seen.append(place)
    check(seen == [0, 1, 1, 0, 2, 0, 1, 0, 2, 2, 0, 1, 0],
          f"the place from the telemetry: races, head-to-heads, other events, free roam again ({seen})")

    def crashes(speeds, dt=1 / 60, impact=25.0):
        first = ctypes.c_int()
        array = (ctypes.c_float * len(speeds))(*speeds)
        return d.NitrousTestCrash(array, len(speeds), dt, impact, ctypes.byref(first)), first.value

    braking = [40 - 9.8 * 1.3 * i / 60 for i in range(120)]  # 1.3 g of braking from 144 km/h
    wall = [40.0] * 30 + [10.0] * 30 + [9.0] * 60             # 108 km/h lost at once
    check(crashes(braking) == (0, -1) and crashes(wall) == (1, 30) and crashes(wall, impact=150) == (0, -1),
          "a crash is 25 km/h lost within 0.15 s: hard braking is not one, a wall is (once)")
    check(crashes([30.0, 10.0] * 40, dt=1 / 30)[0] == 3, "repeated hits: at most one crash a second")


def test_ending(plugin):
    print("end of a boost")
    d = plugin.dll
    d.NitrousTestEndingFrame.argtypes = [ctypes.c_int, ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.c_float]
    d.NitrousTestEndingFrame.restype = ctypes.c_float
    d.NitrousTestEnding.argtypes = [ctypes.c_void_p, ctypes.c_float, ctypes.c_int, ctypes.c_int]
    calls = {"backfire": 0, "burble": 0}

    def original(name):
        def run(self, dt, dynamics):
            calls[name] += 1
            memory = (ctypes.c_char * 0x80).from_address(self)
            if name == "burble":
                struct.pack_into("<i", memory, 0x70, 0)  # Forza Horizon 2: the pop state cleared first
            elif struct.unpack_from("<I", memory, 8)[0]:
                struct.pack_into("<i", memory, 0x0C, 0)  # no backfire this update
            # a car with no backfire level (+8 == 0): Forza Horizon 2 returns before clearing +0xc
        return EFFECT(run)
    backfire, burble = original("backfire"), original("burble")
    release = RELEASE(lambda self: 0)
    vtable = (ctypes.c_void_p * 0x80)()
    vtable[3] = ctypes.cast(release, ctypes.c_void_p).value
    iface = ctypes.create_string_buffer(0x1000)
    struct.pack_into("<Q", iface, 0, ctypes.addressof(vtable))
    struct.pack_into("<f", iface, I_SPEED, 50.0)
    getter = GETTER(lambda self: ctypes.addressof(iface))
    target_vtable = (ctypes.c_void_p * 0x20)()
    target_vtable[0x70 // 8] = ctypes.cast(getter, ctypes.c_void_p).value
    target_object = (ctypes.c_void_p * 1)(ctypes.addressof(target_vtable))
    camera = CAMERA(lambda layer, target, context, external: None)
    d.NitrousTestHooksSetup(ctypes.addressof(vtable), ctypes.cast(backfire, ctypes.c_void_p),
                            ctypes.cast(burble, ctypes.c_void_p), ctypes.cast(camera, ctypes.c_void_p))
    d.NitrousTestHooksClock.argtypes = [ctypes.c_ulonglong]
    d.NitrousTestEnding(ctypes.addressof(iface), 0.0, 0, 0)
    layer = ctypes.create_string_buffer(0x110)
    struct.pack_into("<i", layer, 0x48, 0x17)
    reference = ctypes.c_void_p(ctypes.addressof(iface))
    dt = 1 / 60

    def simulate(plan, level=2, tail=0.35):
        """plan: per frame (active, buildup fraction, plugin update): the exhaust, camera and pop per frame."""
        d.NitrousTestEndingFrame(0, 0.0, 0.0, 0.0, 0.0)  # reset
        effect = ctypes.create_string_buffer(0x80)
        struct.pack_into("<I", effect, 8, level)
        out = []
        for i, (active, buildup, update) in enumerate(plan):
            d.NitrousTestHooksClock(1000 + round(i * 1000 * dt))
            struct.pack_into("<i", iface, I_ACTIVE, active)
            struct.pack_into("<f", iface, I_BUILDUP, buildup * 0.5)
            struct.pack_into("<f", iface, I_BUILDUP_TIME, 0.5)
            envelope = d.NitrousTestEndingFrame(active, buildup, dt, 1.5, tail) if update else None
            d.NitrousTestBackfire(effect, dt, ctypes.byref(reference))
            flames = struct.unpack_from("<i", effect, 0x0C)[0]
            d.NitrousTestBurble(effect, dt, ctypes.byref(reference))
            pop = struct.unpack_from("<i", effect, 0x70)[0]
            d.NitrousTestCameraInput(layer, ctypes.addressof(target_object), None, 0.0)
            out.append((envelope, round(struct.unpack_from("<f", layer, 0)[0], 3), flames, pop))
        d.NitrousTestHooksClock(0)
        return out

    boost = [(1, 1.0, 1)] * 30
    frames = simulate(boost + [(0, 0.0, 1)] * 120)
    after = frames[30:]
    camera_values = [f[1] for f in after]
    check(frames[29][1:3] == (1.0, 2), f"boosting: camera input 1, flames ({frames[29]})")
    check(all(camera_values[i] >= camera_values[i + 1] for i in range(len(camera_values) - 1)) and
          0.6 < camera_values[30] < 0.7 and camera_values[-1] == 0.0,
          f"after the boost the camera's Nitrous input eases from 1 to 0 over 1.5 s (0.5 s on: "
          f"{camera_values[30]}), though the build-up is 0 at once")
    tail_frames = 21  # 0.35 s at 60 fps

    def ending_ok(frames, start, tail_frames=tail_frames, pop=True):
        after = frames[start:]
        flames = [i for i, f in enumerate(after) if f[2] == 2]
        pops = [i for i, f in enumerate(after) if f[3] == 1]
        last = flames[-1] + 1 if flames else 0
        pops_ok = len(pops) == 1 and pops[0] <= tail_frames + 1 if pop else len(pops) <= 1
        return (last <= tail_frames + 1 and flames == list(range(last)) and pops_ok and
                after[-1][2:] == (0, 0)), (last, pops, after[-1][2:])

    # Every way a boost ends looks the same to the exhaust: the physics' flag falls.
    cases = {
        "the key released": boost + [(0, 0.0, 1)] * 60,
        "the tank empty (the physics stops, the key still held)": boost + [(0, 0.0, 1)] * 60,
        "a crash ending it (CrashCancelsBoost)": boost + [(0, 0.0, 1)] * 60,
        "a race starting where it is not allowed": boost + [(0, 0.0, 1)] * 60,
        "a rewind (the flag off, back on, off)": boost + [(0, 0.0, 1)] * 5 + [(1, 0.2, 1)] * 5 + [(0, 0.0, 1)] * 60,
        "a pause (no more plugin updates, the car's own update goes on)": boost + [(0, 0.0, 0)] * 60,
        "a pause mid-boost, then the end": boost + [(1, 1.0, 0)] * 20 + [(0, 0.0, 1)] * 60,
    }
    for what, plan in cases.items():
        for level in (2, 0):
            start = len(plan) - 60
            paused = "no more plugin updates" in what  # the plugin never sees the end: no tail, no pop
            ok, detail = ending_ok(simulate(plan, level), start, pop=not paused)
            check(ok, f"{what}, a car with backfire level {level}: the flames stop within ExhaustTail + 1 frame, "
                      f"{'no tail' if paused else 'one pop'}, then the game's own state (0) ({detail})")
    ok, detail = ending_ok(simulate(boost + [(0, 0.0, 1)] * 60, 0, tail=0.0), 30, tail_frames=0)
    check(ok, f"ExhaustTail=0: the flames stop at once, one pop ({detail})")

    motors = {"value": (0, 0)}

    def rumble_run(self, dt_, input_, wheel):
        struct.pack_into("<II", (ctypes.c_char * 16).from_address(self), 0, *motors["value"])
    rumble_original = RUMBLE(rumble_run)
    d.NitrousTestRumbleSetup(ctypes.cast(rumble_original, ctypes.c_void_p))
    pad = ctypes.create_string_buffer(16)
    struct.pack_into("<f", pad, 8, 1.0)
    d.NitrousTestRumble(pad, dt, None, 0, 0.9)  # boosting, 90 % used
    boosting = struct.unpack_from("<II", pad, 0)
    d.NitrousTestEnding(ctypes.addressof(iface), 0.5, 0, 0)
    d.NitrousTestRumble(pad, dt, None, 0, 0.0)
    half = struct.unpack_from("<II", pad, 0)
    d.NitrousTestEnding(ctypes.addressof(iface), 0.0, 0, 0)
    d.NitrousTestRumble(pad, dt, None, 0, 0.0)
    check(boosting[1] > 30000 and abs(half[1] - boosting[1] / 2) <= 1 and struct.unpack_from("<II", pad, 0) == (0, 0),
          f"the rumble fades with the same envelope ({boosting} -> {half} -> 0)")

# ---------------------------------------------------------------- 4. game

def vtable_name(image, base, vtable_rva):
    col = struct.unpack_from("<Q", image, vtable_rva - 8)[0] - base
    td = struct.unpack_from("<I", image, col + 12)[0]
    offset_in_object = struct.unpack_from("<I", image, col + 4)[0]
    name = bytes(image[td + 16:image.index(b"\0", td + 16)]).decode("ascii")
    return name, offset_in_object


def slot(image, base, vtable_rva, offset_in_vtable):
    return struct.unpack_from("<Q", image, vtable_rva + offset_in_vtable)[0] - base


def test_game(plugin, fh2_exe, ff_exe):
    print("game")
    if not fh2_exe or not fh2_exe.is_file():
        skip(f"Forza Horizon 2's executable is not there ({fh2_exe}); the game tests skip")
        return None
    image, base = map_image(fh2_exe)
    check(len(image) == FH2_SIZE, f"SizeOfImage 0x{len(image):x}")
    buffer = (ctypes.c_char * len(image)).from_buffer(image)
    address = ctypes.addressof(buffer)
    why = plugin.dll.NitrousTestCheck(address, len(image))
    check(why == b"", f"every guarded site matches Forza Horizon 2 1.0.0.64 ({why})")
    check(vtable_name(image, base, CAR_VTABLE) == (".?AVForzaCar@@", 0), "0x2c5cb80 is ForzaCar's vtable")
    check(vtable_name(image, base, DYN_VTABLE) == (".?AV?$TRefCountedObjectThreadSafe@VCCarDynamics@@@@", 0) and
          vtable_name(image, base, IFACE_VTABLE) == (".?AV?$TRefCountedObjectThreadSafe@VCCarDynamics@@@@", IFACE),
          "0x2eb5d68 / 0x2eb5e18 are TRefCountedObjectThreadSafe<CCarDynamics>'s vtables (the second at +0x3a0)")
    check(slot(image, base, IFACE_VTABLE, 0x388) == 0x12D67E0 and slot(image, base, IFACE_VTABLE, 0x378) == 0x12D67F0,
          "its IsNitrousActive / HasNitrous slots (0x388, 0x378) are the guarded getters")
    check(slot(image, base, IFACE_VTABLE, 0x238) == 0x12DA500,
          "its ApplySetup slot (0x238, which rebuilds the engine and clears the parameters) is 0x12da500")
    check(struct.unpack_from("<f", image, 0x2363580)[0] == pytest_float(0.99),
          "the physics step boosts only with the throttle at 0.99 or more (0x2363580)")
    cameras = {0x2E783A0: "CCamFollow", 0x2E78610: "CCamFollowLow", 0x2E78880: "CCamFollowHigh",
               0x2E78120: "CCamDriver", 0x2E77EA8: "CCamHood", 0x2E703B0: "CCamBumperHigh"}
    check(all(vtable_name(image, base, vt) == (f".?AV{name}@Camera@@", 0) and
              slot(image, base, vt, 0x70) == (0x1034120 if "Follow" in name else 0x10340F0)
              for vt, name in cameras.items()),
          "the six driving cameras' vtables, their FOV getter (slot 0x70) the follow / mounted ones")
    for site in SITES:
        saved = image[site + 2]
        image[site + 2] ^= 0xFF
        why = plugin.dll.NitrousTestCheck(address, len(image))
        image[site + 2] = saved
        check(why and f"0x{site:x}".encode() in why, f"a changed byte at 0x{site:x} is refused ({why})")
    stamp_at = struct.unpack_from("<I", image, 0x3C)[0] + 8
    saved = bytes(image[stamp_at:stamp_at + 4])
    image[stamp_at] ^= 1
    why = plugin.dll.NitrousTestCheck(address, len(image))
    image[stamp_at:stamp_at + 4] = saved
    check(why and b"time stamp" in why, f"another time stamp is refused ({why})")
    check(b"1.0.0.64" in plugin.dll.NitrousTestCheck(address, len(image) - 0x1000), "another image size is refused")
    d = plugin.dll
    d.NitrousTestHooksCheck.argtypes = [ctypes.c_void_p]
    d.NitrousTestHooksCheck.restype = ctypes.c_char_p
    # The loader relocates the telemetry table's name pointers to the image: do so for the first.
    struct.pack_into("<Q", image, 0x2CB9280 + 0x10, address + 0x1E64338)
    seen = []
    SINK = ctypes.CFUNCTYPE(None, ctypes.c_char_p, ctypes.c_uint, ctypes.c_void_p)
    PREVIOUS = ctypes.CFUNCTYPE(ctypes.c_ulong, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint64, ctypes.c_ulong,
                                ctypes.c_void_p)
    sink = SINK(lambda name, count, data: seen.append(name))
    previous = PREVIOUS(lambda *a: seen.append(b"previous") or 7)
    d.NitrousTestEtx.argtypes = [ctypes.c_void_p] * 4
    d.NitrousTestEtx.restype = ctypes.c_uint
    results = [d.NitrousTestEtx(address, address + 0x2CB9280, ctypes.cast(sink, ctypes.c_void_p),
                                ctypes.cast(previous, ctypes.c_void_p)),
               d.NitrousTestEtx(address, address + 0x2CB9280 + 5, ctypes.cast(sink, ctypes.c_void_p),
                                ctypes.cast(previous, ctypes.c_void_p))]
    check(seen == [b"AnnaRecommendation", b"previous", b"previous"] and results == [7, 7],
          f"the telemetry wrapper names the game's events, passes anything else through, always calls the import ({seen})")
    why = d.NitrousTestHooksCheck(address)
    check(why == b"", f"the exhaust backfire, burble and camera-input functions are 1.0.0.64's ({why})")
    for site in (BACKFIRE_RVA, BURBLE_RVA, CAMERA_RVA, 0x1037E52, RUMBLE_RVA):
        image[site + 1] ^= 0xFF
        why = d.NitrousTestHooksCheck(address)
        image[site + 1] ^= 0xFF
        check(why != b"", f"a changed byte at 0x{site:x} leaves the hooks out ({why})")

    def input_type(table_rva):
        name_va, value = struct.unpack_from("<QQ", image, table_rva)
        return bytes(image[name_va - base:image.index(b"\0", name_va - base)]).decode(), value

    check(input_type(0x2010D20) == ("ShoulderYaw", 0x17) and input_type(0x2010D30) == ("ShoulderRoll", 0x18) and
          struct.unpack_from("<2I", image, 0x1038558 + 4 * 0x17) == (0x103808B, 0x103808B),
          "the carriers: ShoulderYaw (0x17) and ShoulderRoll (0x18) parse, and their case writes 0")
    if ff_exe and ff_exe.is_file():
        ff_image, _ = map_image(ff_exe)
        ff_buffer = (ctypes.c_char * len(ff_image)).from_buffer(ff_image)
        check(len(ff_image) == FF_SIZE, "Fast & Furious' SizeOfImage is 0x3a89000")
        check(plugin.dll.NitrousTestCheck(ctypes.addressof(ff_buffer), FH2_SIZE) != b"",
              "Fast & Furious' code does not pass Forza Horizon 2's guards")
        # What the hooks port: Fast & Furious' backfire and burble test IsNitrousActive (slot 0x388) first.
        check(bytes(ff_image[0xFA3C8A:0xFA3C8A + 23]).hex() == "498b08488b01ff908803000084c0740cc7430c02000000" and
              bytes(ff_image[0xFA415D:0xFA415D + 23]).hex() == "498b08488b01ff908803000084c0740cc7467001000000",
              "Fast & Furious 0xfa3c40 / 0xfa4130: while IsNitrousActive, backfire +0xc = 2 and burble +0x70 = 1")
        names = []
        for rva in (0x203A2B0, 0x203A2C0):
            name_va, value = struct.unpack_from("<QQ", ff_image, rva)
            names.append((bytes(ff_image[name_va - base:ff_image.index(b"\0", name_va - base)]).decode(), value))
        check(names == [("Nitrous", 0x2D), ("NitrousImpulse", 0x2E)] and
              bytes(ff_image[0x105D8C2:0x105D8C9]).hex() == "4863464883f82e",
              "Fast & Furious' camera inputs Nitrous (0x2d) and NitrousImpulse (0x2e) in its switch (0x105d8c2)")
        check(bytes(ff_image[0x109C68E:0x109C697]).hex() == "c4c16a588160060000" and
              bytes(ff_image[0x109CA27:0x109CA35]).hex() == "4438bbe80600007419c5fa104e28",
              "Fast & Furious' rumble adds its Nitrous curve (0x109c68e), evaluated while its input (+0x28) is not 0")
        del ff_buffer
    else:
        skip(f"Fast & Furious' executable is not there ({ff_exe})")
    return image, buffer, address


# ---------------------------------------------------------------- 5. plugin

INI = """[Nitrous]
Enabled={enabled}
Key={key}
PadButton={pad}
TorqueScale=1.8
Duration=10
BuildupTime=0.25
RechargeTime=30

[Effects]
Sound=0
FovCockpitDegrees=2
"""
CAMERA_SLOTS = [(0x2E783A0, 0x1034120), (0x2E78610, 0x1034120), (0x2E78880, 0x1034120),
                (0x2E78120, 0x10340F0), (0x2E77EA8, 0x10340F0), (0x2E703B0, 0x10340F0)]


class Status(ctypes.Structure):
    _fields_ = [("size", ctypes.c_uint32), ("flags", ctypes.c_uint32), ("remaining", ctypes.c_float),
                ("level", ctypes.c_float), ("run_seconds", ctypes.c_float), ("fov_degrees", ctypes.c_float)]


def test_plugin(plugin, game):
    print("plugin")
    d = plugin.dll
    with tempfile.TemporaryDirectory() as temp:
        folder = Path(temp)
        fake = ctypes.create_string_buffer(0x1000)
        check(d.FH2PluginInitialize and plugin.initialize(folder, INI.format(enabled=0, key="N", pad="LeftStick"),
                                                          ctypes.addressof(fake), FH2_SIZE) and
              "Enabled=0" in plugin.text() and not d.NitrousTestReady(), "Enabled=0: nothing (it says so)")
        plugin.initialize(folder, INI.format(enabled=1, key="N", pad="LeftStick"), ctypes.addressof(fake), FF_SIZE)
        check("Fast & Furious" in plugin.text() and not d.NitrousTestReady(),
              f"Fast & Furious: nothing, it has its own nitrous ({plugin.text().strip()})")
        plugin.initialize(folder, INI.format(enabled=1, key="N", pad="LeftStick"), ctypes.addressof(fake), FH2_SIZE)
        check("does nothing" in plugin.text() and not d.NitrousTestReady(),
              f"an image that is not the guarded build: nothing ({plugin.text().strip()})")
        frame = Frame(ctypes.sizeof(Frame), 1, None, 0x40, 0.016, None)
        car = FakeCar(ctypes.addressof(fake))
        frame.car = car.address
        d.FH2PluginVehicleUpdate(ctypes.byref(frame))
        check(car.params() == [0.0] * 5 and car.int(REQUEST) == 0, "not ready: a vehicle update writes nothing")
        if not game:
            skip("the mapped game is needed for the rest")
            return
        image, buffer, address = game
        # The camera FOV getters as the loader relocates them (the game's own), one taken by another game function.
        for vt, native in CAMERA_SLOTS:
            struct.pack_into("<Q", image, vt + 0x70, address + native)
        struct.pack_into("<Q", image, CAMERA_SLOTS[5][0] + 0x70, address + 0x1034160)
        ok = plugin.initialize(folder, INI.format(enabled=1, key="F", pad="X"), address, FH2_SIZE)
        slots = [struct.unpack_from("<Q", image, vt + 0x70)[0] for vt, _ in CAMERA_SLOTS]
        check("on 5 of 6 driving cameras" in plugin.text() and all(not address <= v < address + FH2_SIZE for v in slots[:5])
              and len(set(slots[:5])) == 1 and slots[5] == address + 0x1034160 and "0x2e703b0 slot 0x70" in plugin.text(),
              "camera FOV: the five game getters are chained to the plugin's; a slot holding other game code is left alone")
        check(ok and d.NitrousTestReady() and "physics' own nitrous" in plugin.text(),
              f"Forza Horizon 2 1.0.0.64: ready ({plugin.text().strip()})")
        check(d.NitrousTestKey() == ord("F") and d.NitrousTestPad() == 0x4000, "Key=F, PadButton=X")
        check(image[BACKFIRE_RVA] == 0xE9 and image[BURBLE_RVA] == 0xE9 and
              "exhaust flames: Fast & Furious'" in plugin.text(),
              "exhaust flames: the backfire and burble functions jump to the plugin's tests")
        check(image[RUMBLE_RVA] == 0xE9 and "pad rumble: Fast & Furious'" in plugin.text(),
              "pad rumble: the rumble update jumps to the plugin's")
        check(image[CAMERA_RVA] != 0xE9 and "no media\\Camera\\CameraPhysics.xml" in plugin.text(),
              "no CameraPhysics.xml in the mod's media: the camera input is left alone, the FOV kick is used")
        tank, boosting = ctypes.c_float(), ctypes.c_int()
        d.NitrousTestKeys(0)
        d.NitrousTestClock(1000)
        car = FakeCar(address)
        frame = Frame(ctypes.sizeof(Frame), 1, car.address, 0, 0.016, None)
        check(not d.NitrousTank(ctypes.byref(tank), ctypes.byref(boosting)), "NitrousTank: FALSE before a car")
        d.FH2PluginVehicleUpdate(ctypes.byref(frame))
        check(car.params() == [0.0, 1.0e6, pytest_float(1.8), 0.25, 10.0] and car.int(REQUEST) == 0,
              f"the INI's tuning is written on the first update ({car.params()})")
        check("armed the player's car" in plugin.text(), "the arm is logged")
        frame.buttons = 0x4000
        d.FH2PluginVehicleUpdate(ctypes.byref(frame))
        check(car.int(REQUEST) == 1, "the pad button asks for nitrous")
        frame.buttons = 0x40
        d.FH2PluginVehicleUpdate(ctypes.byref(frame))
        check(car.int(REQUEST) == 0, "another button does not")
        d.NitrousTestKeys(1)
        d.FH2PluginVehicleUpdate(ctypes.byref(frame))
        check(car.int(REQUEST) == 1, "the key asks for nitrous")
        frame.input_enabled = 0
        d.FH2PluginVehicleUpdate(ctypes.byref(frame))
        check(car.int(REQUEST) == 0, "input off (menus, loading, no focus): no request")
        frame.input_enabled = 1
        d.NitrousTestKeys(0)
        d.NitrousHold(1)
        d.FH2PluginVehicleUpdate(ctypes.byref(frame))
        check(car.int(REQUEST) == 1, "NitrousHold(TRUE) asks for nitrous")
        d.NitrousTestClock(1000 + 249)
        d.FH2PluginVehicleUpdate(ctypes.byref(frame))
        held_late = car.int(REQUEST)
        d.NitrousTestClock(1000 + 250)
        d.FH2PluginVehicleUpdate(ctypes.byref(frame))
        check(held_late == 1 and car.int(REQUEST) == 0, "it lapses 250 ms after the last call")
        d.NitrousHold(1)
        d.NitrousHold(0)
        d.FH2PluginVehicleUpdate(ctypes.byref(frame))
        check(car.int(REQUEST) == 0, "NitrousHold(FALSE) releases at once")
        car.set(USED, 2.5)
        car.set(ACTIVE, 1, "<i")
        d.FH2PluginVehicleUpdate(ctypes.byref(frame))
        check(d.NitrousTank(ctypes.byref(tank), ctypes.byref(boosting)) and abs(tank.value - 0.75) < 1e-6 and
              boosting.value == 1, f"NitrousTank: 75 %, boosting ({tank.value}, {boosting.value})")
        status = Status(ctypes.sizeof(Status))
        check(d.NitrousStatus(ctypes.byref(status)) and status.flags & 1 and status.flags & 4 and status.flags & 16 and
              abs(status.remaining - 0.75) < 1e-6 and status.fov_degrees == 5.0,
              f"NitrousStatus: boosting, driving, a run open, 75 %, the FOV kick ({status.flags:#x})")
        small_status = Status(8)
        check(not d.NitrousStatus(ctypes.byref(small_status)), "NitrousStatus refuses a smaller structure")
        d.NitrousTestClock(1000 + 250 + 600)
        d.NitrousStatus(ctypes.byref(status))
        check(not status.flags & 4, "no update for half a second: not driving")
        boosts, seconds = ctypes.c_uint(), ctypes.c_float()
        check(d.NitrousUsage(ctypes.byref(boosts), ctypes.byref(seconds)) and boosts.value >= 1 and
              0.0 < seconds.value < 1.0, f"NitrousUsage: the boosts begun and the seconds boosted ({boosts.value}, "
              f"{seconds.value:.3f} s)")
        frame.buttons = 0
        d.NitrousTestKeys(1)
        car.set(0x1530, 0.4)
        car.set(ACTIVE, 0, "<i")
        for _ in range(70):
            d.FH2PluginVehicleUpdate(ctypes.byref(frame))
        d.NitrousTestKeys(0)
        check("boost ended after" in plugin.text() and "fell below 0.99" in plugin.text() and "(throttle 0.40)" in plugin.text(),
              f"the log says why a boost ended (the throttle; {plugin.lines[-1].strip()})")
        other = FakeCar(address, car_vtable=CAR_VTABLE + 0x10)
        frame.car = other.address
        before = bytes(other.dyn)
        d.FH2PluginVehicleUpdate(ctypes.byref(frame))
        check(bytes(other.dyn) == before and "not a ForzaCar" in plugin.text(), "another car class: left alone, logged")
        small = Frame(8, 1, car.address, 0x4000, 0.016, None)
        car.set(REQUEST, 0, "<i")
        d.FH2PluginVehicleUpdate(ctypes.byref(small))
        check(car.int(REQUEST) == 0, "a frame smaller than the API's is ignored")
        d.NitrousTestClock(0)
        d.NitrousTestKeys(-1)
        mod = folder / "Nitrous"
        (mod / "media" / "Camera").mkdir(parents=True)
        (mod / "plugins").mkdir()
        (mod / "media" / "Camera" / "CameraPhysics.xml").write_text("<CameraPhysics/>", encoding="utf-8")
        plugin.initialize(mod / "plugins", INI.format(enabled=1, key="N", pad="LeftStick"), address, FH2_SIZE)
        check(image[CAMERA_RVA] == 0xE9 and "the plugin's FOV kick is off" in plugin.text(),
              "with the mod's CameraPhysics.xml: the camera input jumps to the plugin, its own FOV kick is off")

        # The rules on the plugin: Underground, no nitrous in races, skills and crashes.
        class EtxData(ctypes.Structure):
            _fields_ = [("pointer", ctypes.c_uint64), ("size", ctypes.c_uint32), ("reserved", ctypes.c_uint32)]
        d.NitrousTestEvent.argtypes = [ctypes.c_char_p, ctypes.c_uint, ctypes.c_void_p]
        rules_ini = INI.format(enabled=1, key="N", pad="LeftStick").replace("RechargeTime=30\n",
                                                                           "RechargeTime=30\nMode=Underground\nInRaces=0\n")
        rules_ini += "[SkillRecharge]\nDrift=2\nChainPer1000=1\n"
        plugin.initialize(mod / "plugins", rules_ini, address, FH2_SIZE)
        check("mode Underground" in plugin.text() and "not over time" in plugin.text() and "from skills" in plugin.text(),
              "Mode=Underground is logged with its rules")
        car = FakeCar(address)
        frame = Frame(ctypes.sizeof(Frame), 1, car.address, 0x40, 1 / 60, None)
        d.NitrousTestClock(5000)
        d.FH2PluginVehicleUpdate(ctypes.byref(frame))
        check(car.int(REQUEST) == 1, "free roam: the button asks for nitrous")
        d.NitrousTestEvent(b"AnthemRaceStart", 0, None)
        d.FH2PluginVehicleUpdate(ctypes.byref(frame))
        status = Status(ctypes.sizeof(Status))
        d.NitrousStatus(ctypes.byref(status))
        check(car.int(REQUEST) == 0 and status.flags & 32 and "now in a race: nitrous not allowed" in plugin.text(),
              "InRaces=0: in a race the button does nothing, the gauge is told (BLOCKED) and the log says so")
        d.NitrousTestEvent(b"AnthemRaceEnd", 0, None)
        d.FH2PluginVehicleUpdate(ctypes.byref(frame))
        check(car.int(REQUEST) == 1, "after the race: allowed again")
        frame.buttons = 0
        car.set(USED, 8.0)
        d.FH2PluginVehicleUpdate(ctypes.byref(frame))
        check(car.float(USED) == 8.0, "Underground: no refill over time")
        name = ctypes.create_unicode_buffer("Drift")
        points = ctypes.c_int(1500)
        fields = (EtxData * 5)()
        fields[3] = EtxData(ctypes.addressof(name), 12, 0)
        d.NitrousTestEvent(b"SkillAchieved", 5, fields)
        fields[3] = EtxData(ctypes.addressof(points), 4, 0)
        d.NitrousTestEvent(b"SkillChainCompleted", 5, fields)
        d.FH2PluginVehicleUpdate(ctypes.byref(frame))
        d.NitrousStatus(ctypes.byref(status))
        check(abs(car.float(USED) - (8.0 - 2.0 - 1.5)) < 1e-4 and status.flags & 64 and "skill Drift (Drift): +2.0 s" in plugin.text(),
              f"a Drift (+2 s) and a 1500-point chain (+1.5 s) refill the tank; the gauge is told (GAINED) ({car.float(USED)})")
        car.set(0x3A0 + 0xE28, 40.0)
        for _ in range(10):
            d.FH2PluginVehicleUpdate(ctypes.byref(frame))
        car.set(0x3A0 + 0xE28, 5.0)
        d.FH2PluginVehicleUpdate(ctypes.byref(frame))  # the crash
        d.FH2PluginVehicleUpdate(ctypes.byref(frame))  # its drain
        check(abs(car.float(USED) - 8.5) < 1e-4 and "crash: -4.0 s of nitrous" in plugin.text(),
              f"a crash (126 km/h lost at once) takes 4 s of the tank ({car.float(USED)})")
        d.NitrousTestClock(0)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", type=Path, default=ROOT / "out")
    parser.add_argument("--game", type=Path, default=Path.home() / "FH2")
    parser.add_argument("--ff-game", type=Path)
    options = parser.parse_args()
    output = options.output.resolve()
    fh2_exe = options.game / FH2_EXE
    ff_exe = options.ff_game
    if not ff_exe:
        found = sorted((Path.home() / "FH2" / "Forza Horizon 2 Presents Fast & Furious").glob(
            "Spire_*/Mount/forza_durango_release_final.exe"))
        ff_exe = found[0] if found else None
    test_package(output)
    plugin = Plugin(output)
    test_settings(plugin)
    test_car(plugin)
    test_runs(plugin)
    test_effects(plugin)
    test_hooks(plugin)
    test_rules(plugin)
    test_ending(plugin)
    game = test_game(plugin, fh2_exe, ff_exe)
    test_plugin(plugin, game)
    print(f"{len(failures)} failed" if failures else "all tests passed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
