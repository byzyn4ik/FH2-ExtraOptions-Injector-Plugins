"""Test helpers for the captions' language (common/include/fh2_game.h, fh2_text_language):
fake mods folders with Language Support packs, a fake text table manager, and the
selection cases every plugin's tests run on its own build.

A pack, as tools/make_language_pack.py writes it (plugins/lang-support):
  mods\\<Folder>\\mod.json        {"id": "uk", ...}
  mods\\<Folder>\\language.ini    UTF-16 with BOM: [Language] Name=..., Slot=ru-RU
Language Support's own choice: mods\\Language Support\\plugins\\LangSupport.ini [LangSupport] Pack=<Folder>.
"""

import ctypes
import json
from pathlib import Path
import shutil
import struct

# fh2_game.h: FH2_TEXT_TABLES, the game's text table manager pointer (its code at +0x30).
TEXT_TABLES = 0x3662990

# Ukrainian letters the Russian alphabet does not have.
UKRAINIAN_ONLY = set("іїєґІЇЄҐ")


def make_pack(mods, folder, slot="ru-RU", pack_id="uk", name="Українська"):
    """mods/<folder> as a language pack (mod.json, language.ini); pack_id None: no mod.json."""
    root = Path(mods) / folder
    (root / "media").mkdir(parents=True, exist_ok=True)
    if pack_id is not None:
        (root / "mod.json").write_text(json.dumps({"format_version": 1, "id": pack_id, "name": f"{name} (language pack)",
                                                   "version": "1.0.0", "media": "media", "database": None,
                                                   "dependencies": []}, ensure_ascii=False, indent=2),
                                       encoding="utf-8")
    (root / "language.ini").write_text(f"; Language pack\n[Language]\nName={name}\nSlot={slot}\n", encoding="utf-16")
    return root


def lang_support_ini(mods, pack=""):
    """mods/Language Support/plugins/LangSupport.ini with Pack=<pack>."""
    folder = Path(mods) / "Language Support" / "plugins"
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "LangSupport.ini").write_text(f"[LangSupport]\nPack={pack}\nVoiceSubtitles=1\n", encoding="utf-8")


def fresh_mods(work, name):
    """An empty mods folder below `work` (removed first)."""
    mods = Path(work) / name / "mods"
    shutil.rmtree(mods.parent, ignore_errors=True)
    mods.mkdir(parents=True)
    return mods


class FakeGame:
    """A fake game image base whose text table manager holds `code` (fh2_game_language)."""

    def __init__(self, code="EN"):
        self.cell = ctypes.c_void_p()
        self.manager = ctypes.create_string_buffer(0x60)
        self.cell.value = ctypes.addressof(self.manager)
        self.base = ctypes.addressof(self.cell) - TEXT_TABLES
        self.set(code)

    def set(self, code):
        raw = code.encode()
        ctypes.memset(ctypes.addressof(self.manager), 0, 0x60)
        ctypes.memmove(ctypes.addressof(self.manager) + 0x30, raw + b"\0", len(raw) + 1)
        struct.pack_into("<QQ", self.manager, 0x40, len(raw), 15)


def selection_cases(work):
    """(label, mods folder, game code, expected) for fh2_pack_language, built below `work`."""
    cases = []

    def case(label, game, expected, build):
        mods = fresh_mods(work, f"lang-{len(cases)}")
        build(mods)
        cases.append((label, mods, game, expected))

    case("RU slot with the Ukrainian pack: UK", "RU", "UK", lambda m: make_pack(m, "Ukrainian"))
    case("RU slot, Language Support installed, Pack= empty: UK", "RU", "UK",
         lambda m: (make_pack(m, "Ukrainian"), lang_support_ini(m, "")))
    case("RU without a pack: RU", "RU", "RU", lambda m: None)
    case("RU with a pack of another slot (pl-PL): RU", "RU", "RU", lambda m: make_pack(m, "Ukrainian", slot="pl-PL"))
    case("PL slot with that pack: its id", "PL", "UK", lambda m: make_pack(m, "Ukrainian", slot="pl-PL"))
    case("EN with the Ukrainian pack in ru-RU: EN", "EN", "EN", lambda m: make_pack(m, "Ukrainian"))
    for other in ("PL", "DE", "CHT", "BR", "JP"):
        case(f"{other} with the Ukrainian pack in ru-RU: unchanged", other, other, lambda m: make_pack(m, "Ukrainian"))
    case("Pack= names the pack: UK", "RU", "UK",
         lambda m: (make_pack(m, "Ukrainian"), make_pack(m, "Belarusian", pack_id="be"), lang_support_ini(m, "Ukrainian")))
    case("Pack= picks the other of two ru-RU packs", "RU", "BE",
         lambda m: (make_pack(m, "Ukrainian"), make_pack(m, "Belarusian", pack_id="be"), lang_support_ini(m, "Belarusian")))
    case("Pack= names a folder that is no pack: RU", "RU", "RU",
         lambda m: (make_pack(m, "Ukrainian"), lang_support_ini(m, "Missing")))
    case("Pack= with a path is refused as Language Support does: RU", "RU", "RU",
         lambda m: (make_pack(m, "Ukrainian"), lang_support_ini(m, "..\\Ukrainian")))
    case("two ru-RU packs and no Pack=: RU (Language Support refuses too)", "RU", "RU",
         lambda m: (make_pack(m, "Ukrainian"), make_pack(m, "Belarusian", pack_id="be")))
    case("two packs, one in ru-RU: that one", "RU", "UK",
         lambda m: (make_pack(m, "Ukrainian"), make_pack(m, "Kazakh", slot="pl-PL", pack_id="kk")))
    case("Slot in another case (RU-ru): UK", "RU", "UK", lambda m: make_pack(m, "Ukrainian", slot="RU-ru"))
    case("pack commented out in loadorder.txt: RU", "RU", "RU",
         lambda m: (make_pack(m, "Ukrainian"),
                    (m / "loadorder.txt").write_bytes(b"\xef\xbb\xbfKeyboard & Mouse\r\n# Ukrainian\r\nLanguage Support\r\n")))
    case("pack listed in loadorder.txt: UK", "RU", "UK",
         lambda m: (make_pack(m, "Ukrainian"), (m / "loadorder.txt").write_bytes(b"Keyboard & Mouse\r\nUkrainian\r\n")))
    case("pack without mod.json: RU", "RU", "RU", lambda m: make_pack(m, "Ukrainian", pack_id=None))
    case("pack id with a region (uk-UA): UK", "RU", "UK", lambda m: make_pack(m, "Ukrainian", pack_id="uk-UA"))
    case("pack id that is no language code (ukrainian): RU", "RU", "RU",
         lambda m: make_pack(m, "Ukrainian", pack_id="ukrainian"))
    case("a dot folder is no pack: RU", "RU", "RU", lambda m: make_pack(m, ".Ukrainian"))
    return cases


def ukrainian_mods(work, name="uk-mods"):
    """A mods folder with the Ukrainian pack in ru-RU (as the user's install)."""
    mods = fresh_mods(work, name)
    make_pack(mods, "Ukrainian")
    lang_support_ini(mods, "")
    return mods


def plugin_in_mods(mods, mod_name, dll_path, dll_name=None):
    """A copy of a plugin's test DLL at mods/<mod_name>/plugins/<dll_name>; its path."""
    folder = Path(mods) / mod_name / "plugins"
    folder.mkdir(parents=True, exist_ok=True)
    target = folder / (dll_name or Path(dll_path).name)
    shutil.copy2(dll_path, target)
    return target


def has_ukrainian_letters(texts):
    """At least one of the Ukrainian-only letters across `texts` (a sign it is not Russian)."""
    return any(UKRAINIAN_ONLY & set(t) for t in texts)


def check_selection(check, work, dll_path, mod_name, load, caption, expected):
    """Every selection case through the plugin as installed: a copy of its test DLL in the case's
    mods/<mod_name>/plugins/, a fake game in the case's language; caption(dll, game_base) is what
    the plugin shows, expected(code) what it should show for the chosen language code."""
    for label, mods, game_code, want in selection_cases(work):
        dll = load(plugin_in_mods(mods, mod_name, dll_path))
        game = FakeGame(game_code)
        got = caption(dll, game.base)
        check(got == expected(want), f"{label}: {got}")
