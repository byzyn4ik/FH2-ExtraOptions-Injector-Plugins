"""Fast & Furious tests of the Language Support plugin (run by run_tests.py; offline, nothing installed).

11. ff game   Fast & Furious' executable (read only, mapped and patched in this
              process): the mount's sites (file resolver slot, CFileSys,
              CFileVolume, CFileStream, the functions it calls) and the voice
              subtitle sites pass the guards on the mapped image; each damaged
              site is refused; Forza Horizon 2's image is not taken for it.
12. ff media  the mods the mount serves (load order, "# name", "enabled",
              "games", media\\); ForzaPluginEarlyInitialize swaps the
              resolver slot; the first lookup mounts one volume per mod at
              game:\\media\\ (later mods higher); the volumes' file methods
              answer from the mod's media\\: the pack's fonts and string tables
              are the files the game opens (synchronous reads through the
              game's own open-mode function, asynchronous ones through the
              CreateFile2 alias), each logged; other paths are not served.
13. ff plugin ForzaPluginEarlyInitialize + ForzaPluginInitialize on the mapped
              Fast & Furious image: the pack (games fh2ff) found, an FH2 pack
              ignored, the mount reported; voice subtitles installed at Fast &
              Furious' sites with the hook code's jumps back and SetText call
              moved to them; Forza Horizon 2's plugin path is unchanged.
14. games     fh2_game.h: mod.json "games" and "enabled", the running game
              from azimuth.ini, and the captions' language: an FH2 pack is
              ignored in Fast & Furious and the other way round.
"""

import ctypes
import json
from pathlib import Path
import shutil
import struct

FF_EXE = Path("Forza Horizon 2 Presents Fast & Furious/Spire_1.0.0.6_x64__8wekyb3d8bbwe/Mount/"
              "forza_durango_release_final.exe")
FF_IMAGE_SIZE = 0x3A89000
FF_TIMESTAMP = 0x55B62FDE
FF_SITES = [(0xF771E0, 5), (0xF77400, 5), (0xF775A0, 5), (0xC6EA40, 5), (0x891630, 6)]
FF_SET_TEXT = 0xC6A950
FH2_SITES = [0xF50EE0, 0xF51100, 0xF512A0, 0xC3DF10, 0x85AFA0]
FH2_SET_TEXT = 0xC3A4B0
FF_RESOLVE_SLOT, FF_RESOLVE = 0x2EB7208, 0x121A7A0
FF_MEDIA_GUARDS = [(0x121A7A0, "CFileSys::Resolve"), (0x3286D0, "allocator"), (0x1206420, "CFileVolume's constructor"),
                   (0x1207110, "open mode"), (0x1206320, "CFileStream's constructor")]
FF_VTABLES = [(0x2EB6FF8, "CFileSys"), (0x2EF3C38, "CFileVolume"), (0x2EF3660, "CFileStream")]
FF_CREATEFILE2_IAT = 0x1DF8150
READ_EXISTING = 0x80000003  # the game's open flags: GENERIC_READ, OPEN_EXISTING

LOG = ctypes.CFUNCTYPE(None, ctypes.c_char_p)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
kernel32.GetFinalPathNameByHandleW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32]
kernel32.CloseHandle.argtypes = [ctypes.c_void_p]


def declare(dll):
    dll.LangTestFfGame.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    dll.LangTestFfCheck.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    dll.LangTestFfCheck.restype = ctypes.c_char_p
    dll.LangTestFfScan.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_uint]
    dll.LangTestFfInstall.argtypes = [ctypes.c_void_p, ctypes.c_uint32, LOG]
    dll.LangTestFfInstall.restype = ctypes.c_char_p
    dll.LangTestFfFunctions.argtypes = [ctypes.c_void_p] * 5
    dll.LangTestFfVolume.argtypes = [ctypes.c_uint]
    dll.LangTestFfVolume.restype = ctypes.c_void_p
    dll.LangTestFfCreateFile2.restype = ctypes.c_void_p
    dll.LangTestModForGame.argtypes = [ctypes.c_wchar_p, ctypes.c_char_p, ctypes.c_char_p]
    dll.LangTestRunningGame.argtypes = [ctypes.c_wchar_p]
    dll.LangTestRunningGame.restype = ctypes.c_char_p
    dll.LangTestPackLanguage.argtypes = [ctypes.c_wchar_p, ctypes.c_char_p]
    dll.LangTestPackLanguage.restype = ctypes.c_char_p


def peek(address, size):
    return ctypes.string_at(address, size)


def poke(address, data):
    ctypes.memmove(address, data, len(data))


def q(address):
    return struct.unpack("<Q", peek(address, 8))[0]


class GameString(ctypes.Structure):
    """The game's std::string, as the mount borrows it: pointer, 0, length, capacity 31."""
    _fields_ = [("data", ctypes.c_void_p), ("reserved", ctypes.c_uint64), ("length", ctypes.c_size_t),
                ("capacity", ctypes.c_size_t)]


def game_string(text):
    raw = ctypes.create_string_buffer(text.encode())
    value = GameString(ctypes.addressof(raw), 0, len(text), 31)
    value._keep = raw
    return value


def read_game_string(address):
    s = GameString.from_address(address)
    text = peek(address, s.length) if s.capacity < 16 else peek(s.data, s.length)
    return text.decode("utf-8", "replace")


def handle_path(handle):
    buffer = ctypes.create_unicode_buffer(1024)
    n = kernel32.GetFinalPathNameByHandleW(handle, buffer, 1024, 0)
    return buffer.value[4:] if n and buffer.value.startswith("\\\\?\\") else buffer.value


def write_mod(mods, name, games=None, enabled=None, files=None, manifest=True):
    folder = Path(mods) / name
    folder.mkdir(parents=True, exist_ok=True)
    if manifest:
        data = {"format_version": 1, "id": name.lower().replace(" ", "-").replace("(", "").replace(")", "")
                .replace("&", "and"), "name": name, "version": "1.0.0", "dependencies": []}
        if games is not None:
            data["games"] = games
        if enabled is not None:
            data["enabled"] = enabled
        (folder / "mod.json").write_text(json.dumps(data, indent=2), encoding="utf-8")
    for path, content in (files or {}).items():
        target = folder / "media" / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(content)
    return folder


def write_language(folder, slot="ru-RU", pack_id=None):
    (Path(folder) / "language.ini").write_text(f"[Language]\nName=Українська\nSlot={slot}\n", encoding="utf-16")
    if pack_id:
        manifest = Path(folder) / "mod.json"
        data = json.loads(manifest.read_text(encoding="utf-8")) if manifest.exists() else {}
        data["id"] = pack_id
        manifest.write_text(json.dumps(data, indent=2), encoding="utf-8")


def write_launcher(launcher, title="5a9771a4", language="ru-RU"):
    (Path(launcher) / "data").mkdir(parents=True, exist_ok=True)
    (Path(launcher) / "data" / "azimuth.ini").write_text(
        f"[launcher]\ngame={title}\n[title-{title}]\nlanguage={language}\nspeech=pl-PL\nfolder=game\\base\n",
        encoding="utf-16")


# ------------------------------------------------------------------ 11. ff game

def test_ff_game(t, dll, game):
    print("ff game")
    path = game / FF_EXE
    if not path.is_file():
        t.skip(f"Fast & Furious' executable not found ({path})")
        return None
    pe = t.PE(path)
    t.check(pe.image_size == FF_IMAGE_SIZE and pe.timestamp == FF_TIMESTAMP,
            f"Fast & Furious' exe: image size 0x{pe.image_size:x}, TimeDateStamp 0x{pe.timestamp:x}")
    fh2_path = game / t.GAME_EXE
    if fh2_path.is_file():
        fh2 = t.PE(fh2_path)
        for (ff_rva, _), fh2_rva in zip(FF_SITES + [(FF_SET_TEXT, 0)], FH2_SITES + [FH2_SET_TEXT]):
            t.check(pe.read(ff_rva, 16) == fh2.read(fh2_rva, 16),
                    f"voice site 0x{ff_rva:x} begins as Forza Horizon 2's 0x{fh2_rva:x}")
        base = fh2.map()
        t.check(dll.LangTestFfGame(base, t.IMAGE_SIZE) == 0 and dll.LangTestFfCheck(base, t.IMAGE_SIZE) is not None,
                "Forza Horizon 2's image is not taken for Fast & Furious")
        t.unmap(base)
    base = pe.map()
    why = dll.LangTestFfCheck(base, FF_IMAGE_SIZE)
    t.check(dll.LangTestFfGame(base, FF_IMAGE_SIZE) == 1 and why is None,
            f"the mapped image passes the media mount's guards ({why})")
    why = dll.LangTestCheck(base, FF_IMAGE_SIZE)
    t.check(why is None, f"and the voice subtitles' guards (five hook sites and SetText) ({why})")
    for rva, name in FF_MEDIA_GUARDS:
        original = peek(base + rva + 3, 1)
        poke(base + rva + 3, b"\xcc")
        why = dll.LangTestFfCheck(base, FF_IMAGE_SIZE)
        poke(base + rva + 3, original)
        t.check(why is not None and f"0x{rva:x}" in why.decode(), f"{name} 0x{rva:x} changed: refused ({why})")
    for rva, name in FF_VTABLES:
        original = peek(base + rva - 8, 8)
        poke(base + rva - 8, bytes(8))
        why = dll.LangTestFfCheck(base, FF_IMAGE_SIZE)
        poke(base + rva - 8, original)
        t.check(why is not None and name in why.decode(), f"{name}'s vtable without its type: refused ({why})")
    original = peek(base + FF_RESOLVE_SLOT, 8)
    poke(base + FF_RESOLVE_SLOT, struct.pack("<Q", base + 0x1000))
    why = dll.LangTestFfCheck(base, FF_IMAGE_SIZE)
    poke(base + FF_RESOLVE_SLOT, original)
    t.check(why is not None and b"another module hooks it" in why, f"the resolver slot taken by another module ({why})")
    for (rva, _), name in zip(FF_SITES + [(FF_SET_TEXT, 0)], ["ctor", "prep", "prep3d", "update", "parser", "SetText"]):
        original = peek(base + rva, 1)
        poke(base + rva, b"\xe9")
        why = dll.LangTestCheck(base, FF_IMAGE_SIZE)
        poke(base + rva, original)
        t.check(why is not None and f"0x{rva:x}" in why.decode() and b"another module" in why,
                f"voice site {name} 0x{rva:x} hooked by another module: refused ({why})")
    stamp_at = struct.unpack_from("<I", peek(base + 0x3C, 4))[0] + 8
    original = peek(base + stamp_at, 4)
    poke(base + stamp_at, b"\0\0\0\0")
    t.check(dll.LangTestFfGame(base, FF_IMAGE_SIZE) == 0 and b"TimeDateStamp" in dll.LangTestFfCheck(base, FF_IMAGE_SIZE)
            and b"TimeDateStamp" in dll.LangTestCheck(base, FF_IMAGE_SIZE),
            "another TimeDateStamp with the same image size: refused by both")
    poke(base + stamp_at, original)
    t.unmap(base)
    return pe


# ----------------------------------------------------------------- 12. ff media

class FakeFileSystem:
    """CFileSys as the mount sees it: +0x68 its root, vtable +0x1e8 mount, +0x1f8 sort, and the whole-file-system
    methods a full game:\\media\\ path goes to."""

    MOUNT = ctypes.CFUNCTYPE(ctypes.c_ubyte, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint,
                             ctypes.c_ubyte, ctypes.c_ubyte, ctypes.c_void_p)
    SORT = ctypes.CFUNCTYPE(None, ctypes.c_void_p)
    EXISTS = ctypes.CFUNCTYPE(ctypes.c_ubyte, ctypes.c_void_p, ctypes.c_void_p)

    def __init__(self):
        self.object = ctypes.create_string_buffer(0x100)
        self.vtable = (ctypes.c_void_p * (0x220 // 8))()
        self.mounted, self.sorted, self.asked = [], 0, []
        self.mount = self.MOUNT(self._mount)
        self.sort = self.SORT(self._sort)
        self.exists = self.EXISTS(self._exists)
        self.vtable[0x1E8 // 8] = ctypes.cast(self.mount, ctypes.c_void_p)
        self.vtable[0x1F8 // 8] = ctypes.cast(self.sort, ctypes.c_void_p)
        self.vtable[0xE8 // 8] = ctypes.cast(self.exists, ctypes.c_void_p)
        struct.pack_into("<Q", self.object, 0, ctypes.addressof(self.vtable))
        struct.pack_into("<Q", self.object, 0x68, 1)
        self.address = ctypes.addressof(self.object)

    def _mount(self, fs, destination, volume_pointer, priority, a, b, c):
        volume = q(volume_pointer)
        refs = ctypes.c_int.from_address(volume + 0x20)
        refs.value += 1  # the file system keeps a reference, as the game's does
        self.mounted.append((read_game_string(destination), volume, priority))
        return 1

    def _sort(self, fs):
        self.sorted += 1

    def _exists(self, fs, path):
        self.asked.append(read_game_string(path))
        return 1


class GameFunctions:
    """The game functions the mount calls that cannot run here: allocator, CFileVolume's and CFileStream's
    constructors, the original resolver and the volume's asynchronous open."""

    ALLOCATE = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_size_t)
    VOLUME = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_ubyte)
    RESOLVE = ctypes.CFUNCTYPE(ctypes.c_ubyte, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p,
                               ctypes.c_ubyte, ctypes.c_ubyte, ctypes.c_ubyte, ctypes.c_void_p, ctypes.c_ubyte)
    FILE = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint)
    ASYNC = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint,
                             ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint, ctypes.c_ubyte)
    REFERENCE = ctypes.CFUNCTYPE(None, ctypes.c_void_p)
    SET_NAME = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_void_p)

    def __init__(self):
        self.blocks, self.sources, self.resolved, self.handles, self.names, self.async_paths = [], [], 0, [], [], []
        self.file_vtable = (ctypes.c_void_p * (0x290 // 8))()
        self.reference = self.REFERENCE(lambda o: None)
        self.set_name = self.SET_NAME(lambda o, p: self.names.append(read_game_string(p)))
        self.file_vtable[0x10 // 8] = ctypes.cast(self.reference, ctypes.c_void_p)
        self.file_vtable[0x18 // 8] = ctypes.cast(self.reference, ctypes.c_void_p)
        self.file_vtable[0x280 // 8] = ctypes.cast(self.set_name, ctypes.c_void_p)
        self.allocate = self.ALLOCATE(self._allocate)
        self.volume = self.VOLUME(self._volume)
        self.resolve = self.RESOLVE(self._resolve)
        self.file = self.FILE(self._file)
        self.async_open = self.ASYNC(self._async)

    def pointers(self):
        return [ctypes.cast(f, ctypes.c_void_p) for f in (self.allocate, self.volume, self.resolve, self.file,
                                                          self.async_open)]

    def _allocate(self, size):
        block = ctypes.create_string_buffer(size + 0x100)
        self.blocks.append(block)
        return ctypes.addressof(block)

    def _volume(self, storage, source, flag):
        self.sources.append(read_game_string(source))
        struct.pack_into("<i", (ctypes.c_char * 0x50).from_address(storage), 0x20, 0)
        return storage

    def _resolve(self, *arguments):
        self.resolved += 1
        return 0

    def _file(self, storage, handle, owner, flags):
        self.handles.append(handle)
        struct.pack_into("<Q", (ctypes.c_char * 8).from_address(storage), 0, ctypes.addressof(self.file_vtable))
        return storage

    def _async(self, volume, output, path, flags, device, a, b, c):
        self.async_paths.append(read_game_string(path))
        block = ctypes.create_string_buffer(0x20)
        self.blocks.append(block)
        struct.pack_into("<Q", block, 0, ctypes.addressof(self.file_vtable))
        struct.pack_into("<Q", (ctypes.c_char * 8).from_address(output), 0, ctypes.addressof(block))
        return output


FONT = b"Ukrainian pack's verdana.vfont"
TABLE = b"LSB2 Ukrainian Main.str"


def media_mods(work):
    mods = Path(work) / "mods"
    shutil.rmtree(mods.parent, ignore_errors=True)
    pack = write_mod(mods, "Ukrainian (Fast & Furious)", games=["fh2ff"], files={
        "ui/Fonts/verdana.vfont": FONT, "ui/Fonts/Anthem_A.vfont": FONT + b" A",
        "stripped/stringtables/RU/Main.str": TABLE, "zipmanifest.xml": b"<no/>"})
    write_language(pack, pack_id="uk-fh2ff")
    write_mod(mods, "Ukrainian", games=["fh2"], files={"ui/Fonts/verdana.vfont": b"FH2 pack"})
    write_mod(mods, "Keyboard & Mouse", games=["fh2", "fh2ff"])
    write_mod(mods, "Off", enabled=False, files={"x.txt": b"x"})
    write_mod(mods, "Disabled", files={"x.txt": b"x"})
    write_mod(mods, "By Title", games=["5A9771A4"], files={"y.txt": b"y"})
    write_mod(mods, "Legacy", manifest=False, files={"z.txt": b"z"})
    (mods / "loadorder.txt").write_bytes("\ufeffKeyboard & Mouse\r\n# Disabled\r\nUkrainian (Fast & Furious)\r\n"
                                         "By Title\r\n".encode("utf-8"))
    return mods


def test_ff_media(t, dll, pe, work):
    print("ff media")
    if not pe:
        t.skip("the media mount on the mapped game: needs Fast & Furious' executable")
        return
    mods = media_mods(work)
    names = ctypes.create_unicode_buffer(2000)
    dll.LangTestFfReset()
    count = dll.LangTestFfScan(str(mods), names, 2000)
    t.check(names.value == "Ukrainian (Fast & Furious)|By Title|Legacy",
            f"served: the mods for Fast & Furious with media\\ in load order, not '# Disabled', \"enabled\": false or "
            f"an FH2-only pack ({names.value})")
    base = pe.map()
    lines = []
    log = LOG(lambda text: lines.append(text.decode("utf-8", "replace")))
    why = dll.LangTestFfInstall(base, FF_IMAGE_SIZE, log)
    hook = q(base + FF_RESOLVE_SLOT)
    t.check(why is None and hook != base + FF_RESOLVE and hook != 0,
            f"installed: the resolver slot 0x{FF_RESOLVE_SLOT:x} now holds the mount's resolver ({why})")
    why = dll.LangTestFfInstall(base, FF_IMAGE_SIZE, log)
    t.check(why is not None and b"already" in why, "a second install is refused")
    game = GameFunctions()
    dll.LangTestFfFunctions(*game.pointers())
    fs = FakeFileSystem()
    resolve = GameFunctions.RESOLVE(hook)
    path = game_string("game:\\media\\stripped\\stringtables\\RU.zip")
    resolve(fs.address, None, None, ctypes.addressof(path), 0, 0, 0, None, 0)
    t.check([m[0] for m in fs.mounted] == ["game:\\media\\"] * count and
            [m[2] for m in fs.mounted] == [0x70000001 + 2 * i for i in range(count)] and fs.sorted == 1 and
            game.resolved == 1,
            f"the first lookup mounts {count} volumes at game:\\media\\ (priorities 0x70000001 + 2 per mod: the "
            f"later mod first), sorts them once and resolves on")
    t.check(game.sources == [f"game:\\__lang_support_mods__\\{i:03d}\\" for i in range(count)],
            "each volume is the game's CFileVolume with its own source")
    resolve(fs.address, None, None, ctypes.addressof(path), 0, 0, 0, None, 0)
    t.check(len(fs.mounted) == count and game.resolved == 2, "later lookups mount nothing more")
    volume = dll.LangTestFfVolume(0)
    vtable = q(volume)
    t.check(volume == fs.mounted[0][1] and q(vtable - 8) == q(base + 0x2EF3C30) and
            all(q(vtable + o) == q(base + 0x2EF3C38 + o) for o in (0x10, 0x18, 0x100, 0x1E8)),
            "the volume's vtable is CFileVolume's (its type, its other methods) ...")
    t.check(all(q(vtable + o) != q(base + 0x2EF3C38 + o) for o in (0xE8, 0xF8, 0x120, 0x138, 0x148)),
            "... with its exists, attributes, hash, open and asynchronous open replaced")
    exists = ctypes.CFUNCTYPE(ctypes.c_ubyte, ctypes.c_void_p, ctypes.c_void_p)(q(vtable + 0xE8))
    attributes = ctypes.CFUNCTYPE(ctypes.c_uint32, ctypes.c_void_p, ctypes.c_void_p)(q(vtable + 0xF8))
    open_file = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint,
                                 ctypes.c_ubyte, ctypes.c_ubyte, ctypes.c_void_p)(q(vtable + 0x138))
    open_async = GameFunctions.ASYNC(q(vtable + 0x148))

    def has(text):
        s = game_string(text)
        return exists(volume, ctypes.addressof(s))

    pack_media = mods / "Ukrainian (Fast & Furious)" / "media"
    t.check(has("ui\\Fonts\\verdana.vfont") == 1 and has("UI/fonts/Anthem_A.vfont") == 1 and
            has("stripped\\stringtables\\RU\\Main.str") == 1,
            "the pack's fonts and string table exist in its volume (any case, / or \\)")
    t.check(not any(has(p) for p in ("ui\\Fonts\\missing.vfont", "zipmanifest.xml", "..\\Ukrainian\\media\\x",
                                     "ui\\Fonts\\verdana.vfont.", "<*>0", "C:\\x")),
            "not there: a missing file, the game's zipmanifest.xml, .., a trailing dot, an archive selector, a drive")
    s, font = game_string("ui\\Fonts"), game_string("ui\\Fonts\\verdana.vfont")
    folder_attributes, font_attributes = attributes(volume, ctypes.addressof(s)), attributes(volume, ctypes.addressof(font))
    t.check(folder_attributes == 0xFFFFFFFF and font_attributes != 0xFFFFFFFF and not font_attributes & 0x10,
            f"attributes: a folder is no file; the font is a file ({folder_attributes:#x}, {font_attributes:#x})")
    for relative, content in (("ui\\Fonts\\verdana.vfont", FONT), ("stripped\\stringtables\\RU\\Main.str", TABLE)):
        s = game_string(relative)
        output = ctypes.c_void_p()
        game.handles.clear()
        open_file(volume, ctypes.addressof(output), ctypes.addressof(s), READ_EXISTING, 0, 0, None)
        handle = game.handles[0] if game.handles else None
        opened = handle_path(handle) if handle else ""
        data = b""
        if handle:
            buffer = ctypes.create_string_buffer(256)
            got = ctypes.c_uint32()
            ctypes.windll.kernel32.ReadFile(ctypes.c_void_p(handle), buffer, 256, ctypes.byref(got), None)
            data = buffer.raw[:got.value]
            kernel32.CloseHandle(handle)
        t.check(output.value and Path(opened) == pack_media / relative and data == content and
                game.names[-1] == relative,
                f"open {relative} (the game's open mode for flags 0x{READ_EXISTING:x}): CFileStream on the pack's file "
                f"({opened}), named as the game asked")
        t.check(any(f"the game opens mods\\Ukrainian (Fast & Furious)\\media\\{relative} (read)" in line for line in lines),
                f"forza-injector.log names it ({relative})")
    output = ctypes.c_void_p(1)
    s = game_string("ui\\Fonts\\verdana.vfont")
    game.handles.clear()
    open_file(volume, ctypes.addressof(output), ctypes.addressof(s), 0xC0000002, 0, 0, None)
    t.check(not output.value and not game.handles, "a write open (CREATE_ALWAYS, GENERIC_WRITE) gets nothing")

    output = ctypes.c_void_p()
    s = game_string("ui\\Fonts\\Anthem_A.vfont")
    open_async(volume, ctypes.addressof(output), ctypes.addressof(s), READ_EXISTING, None, 0, 0, 0)
    alias = game.async_paths[-1] if game.async_paths else ""
    slot = q(base + FF_CREATEFILE2_IAT)
    create_file2 = dll.LangTestFfCreateFile2()
    t.check(output.value and alias.startswith("game:\\__lang_support_file__\\") and
            game.names[-1] == "ui\\Fonts\\Anthem_A.vfont" and slot == create_file2,
            f"asynchronous open: the volume opens an alias ({alias}); the game's CreateFile2 import (0x{FF_CREATEFILE2_IAT:x}) "
            f"is the mount's")
    open2 = ctypes.WINFUNCTYPE(ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32,
                               ctypes.c_void_p)(create_file2)
    handle = open2(alias.upper(), 0x80000000, 1, 3, None)
    opened = handle_path(handle) if handle and handle != ctypes.c_void_p(-1).value else ""
    if opened:
        kernel32.CloseHandle(handle)
    t.check(Path(opened) == pack_media / "ui" / "Fonts" / "Anthem_A.vfont",
            f"CreateFile2 with the alias opens the pack's Anthem_A.vfont ({opened})")
    bad = open2(alias, 0x40000000, 0, 2, None)
    t.check(bad in (None, ctypes.c_void_p(-1).value), "the alias is read only")
    t.check(any("Anthem_A.vfont (asynchronous read)" in line for line in lines), "the asynchronous read is logged too")
    s = game_string("game:\\media\\ui\\Fonts\\other.vfont")
    t.check(exists(volume, ctypes.addressof(s)) == 1 and fs.asked == ["game:\\media\\ui\\Fonts\\other.vfont"],
            "a full game:\\media\\ path asked of a mod's volume goes to the whole file system")
    t.unmap(base)

    # A refused install leaves the slot as it is.
    dll.LangTestFfReset()
    dll.LangTestFfScan(str(mods), names, 2000)
    base = pe.map()
    poke(base + 0x1206420 + 2, b"\xcc")
    before = q(base + FF_RESOLVE_SLOT)
    why = dll.LangTestFfInstall(base, FF_IMAGE_SIZE, log)
    t.check(why is not None and q(base + FF_RESOLVE_SLOT) == before == base + FF_RESOLVE,
            f"a changed site: nothing installed, the slot unchanged ({why})")
    t.unmap(base)
    dll.LangTestFfReset()
    empty = Path(work) / "empty" / "mods"
    empty.mkdir(parents=True, exist_ok=True)
    dll.LangTestFfScan(str(empty), names, 2000)
    base = pe.map()
    why = dll.LangTestFfInstall(base, FF_IMAGE_SIZE, log)
    t.check(why is not None and b"no mod" in why and q(base + FF_RESOLVE_SLOT) == base + FF_RESOLVE,
            f"no mod with media\\: nothing installed ({why})")
    t.unmap(base)
    dll.LangTestFfReset()


# ---------------------------------------------------------------- 13. ff plugin

class ForzaContext(ctypes.Structure):
    _fields_ = [("size", ctypes.c_uint32), ("api_version", ctypes.c_uint32), ("v1", ctypes.c_void_p),
                ("game", ctypes.c_uint32), ("game_id", ctypes.c_char_p), ("title_id", ctypes.c_char_p),
                ("game_timestamp", ctypes.c_uint32), ("host_image", ctypes.c_void_p),
                ("host_timestamp", ctypes.c_uint32), ("host_image_size", ctypes.c_uint32),
                ("injector_version", ctypes.c_char_p), ("subscribe", ctypes.c_void_p)]


def test_ff_plugin(t, pe, host, work, fh2_pe):
    print("ff plugin")
    if not pe or not host:
        t.skip("the plugin on mapped Fast & Furious: needs its executable and Azimuth 1.0.4")
        return
    root = Path(work)
    shutil.rmtree(root, ignore_errors=True)
    launcher = root / "ru"
    mods = launcher / "mods"
    pack = t.write_pack(mods, "Ukrainian (Fast & Furious)")
    write_mod(mods, "Ukrainian (Fast & Furious)", games=["fh2ff"], files={"ui/Fonts/verdana.vfont": FONT})
    fh2_pack = t.write_pack(mods, "Ukrainian")
    write_mod(mods, "Ukrainian", games=["fh2"])
    write_launcher(launcher, "5a9771a4", "ru-RU")
    plugin = t.Plugin(t.OUTPUT, launcher, host=host)
    dll = plugin.dll
    dll.ForzaPluginEarlyInitialize.argtypes = [ctypes.POINTER(ForzaContext)]
    dll.ForzaPluginInitialize.argtypes = [ctypes.POINTER(ForzaContext)]
    dll.LangTestFfReset()
    base = pe.map()
    v1 = t.Context(ctypes.sizeof(t.Context), 1, 1, FF_IMAGE_SIZE, base, None, plugin.folder, plugin.path, b"",
                   plugin.log)
    context = ForzaContext(ctypes.sizeof(ForzaContext), 2, ctypes.addressof(v1), 2, b"fh2ff", b"5A9771A4",
                           FF_TIMESTAMP, host, 0, 0, b"0.2.2", None)
    early = dll.ForzaPluginEarlyInitialize(ctypes.byref(context))
    t.check(early == 1 and plugin.said("media: mods\\Ukrainian (Fast & Furious)\\media is served on Fast & Furious") and
            not plugin.said("mods\\Ukrainian\\media is served") and q(base + FF_RESOLVE_SLOT) != base + FF_RESOLVE,
            f"ForzaPluginEarlyInitialize: the F&F pack's media\\ mount installed, the FH2 pack's left out "
            f"({plugin.last()})")
    ready = dll.ForzaPluginInitialize(ctypes.byref(context))
    print("    " + "    ".join(plugin.lines).rstrip())
    cave = dll.LangTestCave()
    t.check(ready == 1 and plugin.said("plugin 0.") and plugin.said("on Fast & Furious (forza_injector)") and
            plugin.said("language pack mods\\Ukrainian (Fast & Furious): Українська in the ru-RU slot") and
            plugin.said("the pack's media\\ is mounted over the game's files"),
            "ForzaPluginInitialize: the pack for Fast & Furious, its media mounted")
    t.check(cave and plugin.said("voice subtitles installed (Fast & Furious): 4 lines") and
            plugin.said("speech hooks 0xf771e0, 0xf77400, 0xf775a0, 0xc6ea40 and 0x891630"),
            f"voice subtitles installed at Fast & Furious' sites ({plugin.last()})")
    if cave:
        _, _, _, _, rel32, hooks = t.code_header()
        ok = True
        for i, (rva, n) in enumerate(FF_SITES):
            at = peek(base + rva, n)
            ok &= at[0] == 0xE9 and base + rva + 5 + struct.unpack_from("<i", at, 1)[0] == cave + hooks[i][1]
        t.check(ok, "each Fast & Furious site jumps to its hook in the code")
        found = sorted(cave + o + 4 + struct.unpack("<i", peek(cave + o, 4))[0] for o in rel32)
        expected = sorted([base + rva + n for rva, n in FF_SITES] + [base + FF_SET_TEXT])
        t.check(found == expected, "the code's jumps back and its SetText call go to Fast & Furious' functions")
    t.unmap(base)

    # The other language: no voice subtitles, the mount still in place.
    launcher = root / "en"
    shutil.copytree(mods, launcher / "mods", ignore=shutil.ignore_patterns("Language Support"))
    write_launcher(launcher, "5a9771a4", "en-US")
    plugin = t.Plugin(t.OUTPUT, launcher, host=host)
    dll = plugin.dll
    dll.ForzaPluginEarlyInitialize.argtypes = [ctypes.POINTER(ForzaContext)]
    dll.ForzaPluginInitialize.argtypes = [ctypes.POINTER(ForzaContext)]
    base = pe.map()
    v1 = t.Context(ctypes.sizeof(t.Context), 1, 1, FF_IMAGE_SIZE, base, None, plugin.folder, plugin.path, b"",
                   plugin.log)
    context = ForzaContext(ctypes.sizeof(ForzaContext), 2, ctypes.addressof(v1), 2, b"fh2ff", b"5A9771A4",
                           FF_TIMESTAMP, host, 0, 0, b"0.2.2", None)
    dll.ForzaPluginEarlyInitialize(ctypes.byref(context))
    before = [peek(base + rva, 8) for rva, _ in FF_SITES]
    dll.ForzaPluginInitialize(ctypes.byref(context))
    t.check(plugin.said("choose ru-RU in the launcher") and [peek(base + rva, 8) for rva, _ in FF_SITES] == before and
            q(base + FF_RESOLVE_SLOT) != base + FF_RESOLVE,
            "launcher in en-US: voice subtitles off, the sites unchanged; the media mount stays")
    t.unmap(base)

    # ModMedia=0: nothing mounted.
    launcher = root / "off"
    shutil.copytree(mods, launcher / "mods", ignore=shutil.ignore_patterns("Language Support"))
    write_launcher(launcher, "5a9771a4", "ru-RU")
    plugin = t.Plugin(t.OUTPUT, launcher, "[LangSupport]\nModMedia=0\n", host=host)
    dll = plugin.dll
    dll.ForzaPluginEarlyInitialize.argtypes = [ctypes.POINTER(ForzaContext)]
    dll.ForzaPluginInitialize.argtypes = [ctypes.POINTER(ForzaContext)]
    base = pe.map()
    v1 = t.Context(ctypes.sizeof(t.Context), 1, 1, FF_IMAGE_SIZE, base, None, plugin.folder, plugin.path, b"",
                   plugin.log)
    context = ForzaContext(ctypes.sizeof(ForzaContext), 2, ctypes.addressof(v1), 2, b"fh2ff", b"5A9771A4",
                           FF_TIMESTAMP, host, 0, 0, b"0.2.2", None)
    dll.ForzaPluginEarlyInitialize(ctypes.byref(context))
    dll.ForzaPluginInitialize(ctypes.byref(context))
    t.check(q(base + FF_RESOLVE_SLOT) == base + FF_RESOLVE and plugin.said("ModMedia=0") and
            plugin.said("warning: the pack's media\\ is not mounted"),
            "[LangSupport] ModMedia=0: the resolver is left alone and the log says the pack is not mounted")
    t.unmap(base)

    # Forza Horizon 2 through ForzaPluginInitialize: nothing early, the same Extra Options path.
    if fh2_pe:
        launcher = root / "fh2"
        shutil.copytree(mods, launcher / "mods", ignore=shutil.ignore_patterns("Language Support"))
        write_launcher(launcher, "1a96545b", "ru-RU")
        plugin = t.Plugin(t.OUTPUT, launcher, host=host)
        dll = plugin.dll
        dll.ForzaPluginEarlyInitialize.argtypes = [ctypes.POINTER(ForzaContext)]
        dll.ForzaPluginInitialize.argtypes = [ctypes.POINTER(ForzaContext)]
        base = fh2_pe.map()
        v1 = t.Context(ctypes.sizeof(t.Context), 1, 1, t.IMAGE_SIZE, base, None, plugin.folder, plugin.path, b"0.4.4",
                       plugin.log)
        context = ForzaContext(ctypes.sizeof(ForzaContext), 2, ctypes.addressof(v1), 1, b"fh2", b"1A96545B", 0, host,
                               0, 0, b"0.2.2", None)
        early = dll.ForzaPluginEarlyInitialize(ctypes.byref(context))
        t.check(early == 1 and not plugin.lines, "Forza Horizon 2: ForzaPluginEarlyInitialize does nothing")
        dll.ForzaPluginInitialize(ctypes.byref(context))
        t.check(plugin.said("language pack mods\\Ukrainian: ") and plugin.said("voice subtitles installed (Forza Horizon 2)")
                and not plugin.said("Fast & Furious"),
                f"Forza Horizon 2: the FH2 pack (games fh2), not the F&F one; voice subtitles at its sites ({plugin.last()})")
        t.unmap(base)
    del pack, fh2_pack


# -------------------------------------------------------------------- 14. games

def test_games(t, dll, work):
    print("games")
    work = Path(work)
    shutil.rmtree(work, ignore_errors=True)
    mods = work / "mods"
    cases = [("no mod.json", dict(manifest=False), {"fh2": 1, "fh2ff": 1, "": 1}),
             ("no games", dict(), {"fh2": 1, "fh2ff": 1, "": 1}),
             ("games []", dict(games=[]), {"fh2": 1, "fh2ff": 1, "": 1}),
             ("games fh2", dict(games=["fh2"]), {"fh2": 1, "fh2ff": 0, "": 1}),
             ("games FH2FF", dict(games=["FH2FF"]), {"fh2": 0, "fh2ff": 1, "": 1}),
             ("games both", dict(games=["fh2", "fh2ff"]), {"fh2": 1, "fh2ff": 1, "": 1}),
             ("games by title", dict(games=["5a9771a4"]), {"fh2": 0, "fh2ff": 1, "": 1}),
             ("enabled false", dict(enabled=False), {"fh2": 0, "fh2ff": 0, "": 0}),
             ("enabled true, games fh2", dict(enabled=True, games=["fh2"]), {"fh2": 1, "fh2ff": 0, "": 1})]
    titles = {"fh2": b"1A96545B", "fh2ff": b"5A9771A4", "": b""}
    for label, options, expected in cases:
        folder = write_mod(mods, label, **options)
        got = {g: dll.LangTestModForGame(str(folder), g.encode(), titles[g]) for g in expected}
        t.check(got == expected, f"mod.json {label}: {got}")
    write_launcher(work, "5a9771a4")
    t.check(dll.LangTestRunningGame(str(mods)) == b"fh2ff|5A9771A4", "azimuth.ini [launcher] game=5a9771a4: fh2ff")
    write_launcher(work, "1A96545B")
    t.check(dll.LangTestRunningGame(str(mods) + "\\") == b"fh2|1A96545B", "game=1A96545B: fh2")
    write_launcher(work, "64f9397e")
    t.check(dll.LangTestRunningGame(str(mods)) == b"|64F9397E", "another title: not a game of these packs")
    (work / "data" / "azimuth.ini").unlink()
    t.check(dll.LangTestRunningGame(str(mods)) == b"|", "no azimuth.ini: not known")

    # The captions' language (fh2_text_language's search) and Language Support's own pick.
    def language(game_title, build):
        root = work / f"lang-{game_title}-{len(list(work.glob('lang-*')))}"
        m = root / "mods"
        m.mkdir(parents=True)
        build(m)
        if game_title:
            write_launcher(root, game_title)
        return dll.LangTestPackLanguage(str(m), b"RU").decode()

    def fh2_pack(m):
        write_language(write_mod(m, "Ukrainian", games=["fh2"]), pack_id="uk")

    def ff_pack(m):
        write_language(write_mod(m, "Ukrainian (Fast & Furious)", games=["fh2ff"]), pack_id="uk-fh2ff")

    def any_pack(m):
        write_language(write_mod(m, "Ukrainian"), pack_id="uk")

    t.check(language("5a9771a4", fh2_pack) == "RU", "Fast & Furious with only the FH2 pack: RU (the pack is ignored)")
    t.check(language("1a96545b", ff_pack) == "RU", "Forza Horizon 2 with only the F&F pack: RU")
    t.check(language("5a9771a4", ff_pack) == "UK", "Fast & Furious with its pack: UK")
    t.check(language("1a96545b", fh2_pack) == "UK", "Forza Horizon 2 with its pack: UK")
    t.check(language("5a9771a4", lambda m: (fh2_pack(m), ff_pack(m))) == "UK",
            "both packs in Fast & Furious: its own one (the FH2 one is not counted against it)")
    t.check(language("1a96545b", lambda m: (fh2_pack(m), ff_pack(m))) == "UK", "both packs in Forza Horizon 2: its own")
    t.check(language("5a9771a4", any_pack) == "UK", "a pack without \"games\": every game")
    t.check(language("", lambda m: (fh2_pack(m), ff_pack(m))) == "RU",
            "both packs and the game not known (no azimuth.ini): two packs, none taken (as before)")

    def lang_support_pack(name):
        def build(m):
            fh2_pack(m)
            ff_pack(m)
            ini = m / "Language Support" / "plugins"
            ini.mkdir(parents=True)
            (ini / "LangSupport.ini").write_text(f"[LangSupport]\nPack={name}\n", encoding="utf-8")
        return build

    t.check(language("5a9771a4", lang_support_pack("Ukrainian")) == "RU",
            "Pack= names the FH2 pack in Fast & Furious: not taken")
    t.check(language("5a9771a4", lang_support_pack("Ukrainian (Fast & Furious)")) == "UK",
            "Pack= names the F&F pack in Fast & Furious: UK")

    both = work / "find" / "mods"
    fh2_pack(both)
    ff_pack(both)
    out = ctypes.create_unicode_buffer(600)
    for game, expected in ((b"fh2ff", "Ukrainian (Fast & Furious)"), (b"fh2", "Ukrainian")):
        why = dll.LangTestFindPack(str(both), "", out, 600, game)
        t.check(why is None and out.value.split("|")[0] == expected,
                f"Language Support in {game.decode()}: Pack= empty picks {expected} ({why})")
    why = dll.LangTestFindPack(str(both), "Ukrainian", out, 600, b"fh2ff")
    t.check(why is not None and b"games" in why, f"Pack= an FH2 pack in Fast & Furious: refused ({why})")
    why = dll.LangTestFindPack(str(both), "", out, 600, None)
    t.check(why is not None and b"several" in why, "game not known: both are packs (as before)")
