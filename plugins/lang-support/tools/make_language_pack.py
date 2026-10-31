"""Build a language pack for the Language Support plugin: an Extra Options mod folder.

usage:
  py -I make_language_pack.py build <original media> <mod media> <output folder>
        --slot ru-RU --name "Українська" [--id uk] [--version 1.0.0] [--whole-zips] [--game fh2]
  py -I make_language_pack.py convert <fork pack folder> <output folder> [--id uk] [--version 1.0.0] [--game fh2]
  py -I make_language_pack.py port <pack> <source media> <target media> <output folder>
        [--translations <file.tsv>] [--report <file.tsv>] [--game fh2ff] [--id uk-fh2ff] [--version 1.0.0]
  py -I make_language_pack.py fix <pack> --fixes <fixes.tsv> [--fixes-game fh2ff] [--media <game media>] [--strict]
  build, convert and port also take --fixes <fixes.tsv> [--fixes-game <game>] (convert: [--media <game media>]).

build: <original media> is the game's untouched Mount\\media, <mod media> a
translation mod's replacement files laid out the same way (zips and loose
files). Every zip entry and loose file whose data differs from the original
goes into <output folder>\\media\\ in Extra Options' mod layout (a zip member
media\\X.zip -> a\\b becomes media\\X\\a\\b), so the pack ships only what the
translation changes. --whole-zips copies each changed zip whole instead (Extra
Options registers zips in a mod's media\\ with the game's own zip reader).
zipmanifest.xml is never copied: the original archives stay as they are.

convert: <fork pack folder> is a pack of the Extra Options fork
(<game>\\Mount\\languages\\<id>\\ with language.ini): its files are copied to
<output folder>\\media\\ and its language.ini beside them.

port: a pack made for one game (<pack>: a mod folder from build or convert, or
a fork pack) carried over to another game of the same engine, e.g. Forza
Horizon 2's Ukrainian pack to Forza Horizon 2 Presents Fast & Furious.
<source media> is the Mount\\media of the game the pack was made for,
<target media> the other game's. Both are only read.
  - String tables: every table of the target's slot archive
    (stripped\\stringtables\\<archive>.zip) is rebuilt with its rows in the
    pack's language. A row takes, in this order: the --translations row of the
    same table and key whose English is the target's English; the pack's row
    of the same table and key when the source game's English of that key is
    the target's English; else it keeps the target's slot text and is listed
    as untranslated (--report). A table the pack does not have and no
    translation touches is left to the game, as in the source game. Only
    tables with a changed row are written.
  - Other files of the pack (fonts, subtitles, textures): copied when the
    target game's original is the source game's original (a .vfont may differ
    in its 256-byte header only: its glyphs are the same). The target's own
    slot files (members named _<archive>.xyz or in a <archive> folder) the
    pack has no path for take the pack's file whose source original has the
    same data. A file whose original differs in the target is left to the game
    and reported.
  - --translations rows of a subtitle XML (its media path in the table column,
    Key[n] in the key column: the n-th <Key>) replace that key's value in the
    target's slot file, when the English file's n-th value is the row's English.
  - VoiceSubtitles.str (spoken lines): the pack's rows (and their ~t times) of
    the lines the target game plays too: the events of its
    audio\\vo\\DialogueScript.xml and the line names in its
    audio\\fmodanthem\\durango\\HZ2_DX_*_EN.bank; the summary counts the
    target's lines without a text.
  The translations file is UTF-8 TSV: table, key, english, translation,
  source (new, fh2, fh2-edit, keep: for review only), optional note; # lines
  and the header line are skipped; \\n, \\t, \\r and \\\\ in a cell are a line
  break, a tab, a carriage return and a backslash.

fixes: a TSV of corrections (e.g. translations/uk-fixes.tsv), UTF-8, columns
game (fh2, fh2ff or both), table, key, english, old_uk, new_uk, reason
(agreement, calque, meaning, terminology, length); # lines and the header are
skipped; cells escaped as in the translations file. Subtitle XML rows: the
media path in the table column, Key[n] in the key column. `fix` applies it to
an existing pack in place; --fixes applies it to the pack build, convert or
port makes. The rows of the pack's game (--fixes-game, else --game, else fh2)
and of both apply where the pack's text is old_uk (new_uk: already applied);
with the game's media (build: original, port: target, convert/fix: --media)
the row's English must be the game's, and a table the pack does not have is
taken from the game's slot. Other rows stay byte for byte; what does not apply
(unknown key, other text or English) is listed.

The output folder is a mod folder: mod.json (media only, no database; --game
adds "games": [<game>], so loaders that read it load the pack only in that
game), language.ini ([Language] Name and Slot, read by the plugin) and media\\.
Install it as <Azimuth>\\mods\\<output folder name>\\ beside
mods\\Language Support\\, then choose the slot's language in the Azimuth
launcher. A spoken-line subtitle table goes in
media\\stripped\\stringtables\\<archive>\\VoiceSubtitles.str (loose or stored
in <archive>.zip), as the fork's packs have it.
"""

import argparse
import hashlib
import html
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import sys
import zipfile

# Azimuth language code -> the game's string table archive (src/pack.c slots[]).
SLOTS = {"en-US": "EN", "en-GB": "GB", "de-DE": "DE", "fr-FR": "FR", "it-IT": "IT", "es-ES": "ES", "es-MX": "MX",
         "pt-BR": "br", "pl-PL": "PL", "ru-RU": "RU", "ja-JP": "JP", "ko-KR": "KO", "zh-TW": "CHT", "zh-HK": "CHT",
         "nl-NL": "nl", "cs-CZ": "cz", "hu-HU": "HU"}
ENGLISH = "EN"
# forza_injector's game ids (sdk/include/forza_plugin.h, profiles/*.json): mod.json "games".
GAMES = {"fh2": "Forza Horizon 2", "fh2ff": "Forza Horizon 2 Presents Fast & Furious"}
MOD_ID = re.compile(r"^[a-z0-9]+(-[a-z0-9]+)*$")  # Extra Options 0.4.4 features/addons.c valid_id()
FONT_HEADER = 0x100  # a .vfont's name and header; the game leaves unset bytes in it
SKIPPED_FILES = ("language.ini", "mod.json", "zipmanifest.xml")
VOICE_TABLE = "VoiceSubtitles.str"
VOICE_ID = re.compile(rb"HZ2_[A-Za-z0-9_]{4,}")


def lsb2_table(rows):
    """An LSB2 string table of (id, text) rows, in the layout the plugin reads (src/voice_subtitles.c)."""
    blob, entries, names, ids = b"", [], b"", []
    for k, (row_id, text) in enumerate(rows):
        hash16 = 0x100 + k
        entries.append((hash16, len(blob) // 2))
        blob += (text + "\0").encode("utf-16-le")
        ids.append((hash16, len(names)))
        names += row_id.encode("latin1") + b"\0"
    entries.append((0, len(blob) // 2))
    text_section = 40
    texts = struct.pack("<II", 0, len(rows)) + b"".join(struct.pack("<HI", h, o) for h, o in entries) + blob
    id_section = text_section + len(texts)
    id_part = struct.pack("<II", 0, len(rows)) + b"".join(struct.pack("<HHI", h, 0, o) for h, o in ids) + names
    header = b"LSB2" + bytes(12) + struct.pack("<IIII", 2, text_section, id_section, 0) + bytes(8)
    return header + texts + id_part


def voice_events(media_root):
    """The spoken lines a game plays: the events of audio\\vo\\DialogueScript.xml (their last path
    component) and the line names in its English dialogue banks (audio\\fmodanthem\\durango\\HZ2_DX_*_EN.bank)."""
    found = set()
    script = media_root / "audio" / "vo" / "DialogueScript.xml"
    if script.is_file():
        text = script.read_text(encoding="utf-8-sig", errors="replace")
        found |= {name.replace("\\", "/").split("/")[-1] for name in re.findall(r'name="([^"]+)"', text)}
    for bank in sorted((media_root / "audio" / "fmodanthem" / "durango").glob("HZ2_DX_*_EN.bank")):
        found |= {m.decode() for m in VOICE_ID.findall(bank.read_bytes())}
    return found


def digest(data):
    return hashlib.sha1(data).digest()


def file_digest(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 22), b""):
            h.update(chunk)
    return h.digest()


def safe_parts(name):
    parts = PurePosixPath(name.replace("\\", "/")).parts
    if not parts or any(p in ("", ".", "..") or ":" in p for p in parts):
        sys.exit(f"refusing unsafe archive path {name!r}")
    return parts


def changed_entries(original, modded):
    """[(entry name, data)] of modded entries that are new or differ from the original."""
    with zipfile.ZipFile(original) as zo:
        known = {i.filename.lower(): (i.file_size, i.CRC, i.filename) for i in zo.infolist() if not i.is_dir()}
        out = []
        with zipfile.ZipFile(modded) as zm:
            for info in zm.infolist():
                if info.is_dir():
                    continue
                data = zm.read(info)
                old = known.get(info.filename.lower())
                if old and old[:2] == (info.file_size, info.CRC) and digest(zo.read(old[2])) == digest(data):
                    continue
                out.append((info.filename, data))
    return out


def slot_of(slot):
    for known in SLOTS:
        if known.lower() == slot.lower():
            return known
    sys.exit(f"--slot {slot!r} is not a game language code ({', '.join(SLOTS)})")


def mod_id(given, output):
    value = given if given is not None else re.sub(r"[^a-z0-9]+", "-", output.name.lower()).strip("-")
    if not MOD_ID.match(value) or len(value) > 128:
        sys.exit(f"mod id {value!r}: lower-case letters, digits and single hyphens (give one with --id)")
    return value


def prepare(output):
    if output.exists() and any(output.iterdir()):
        sys.exit(f"{output} is not empty")
    (output / "media").mkdir(parents=True, exist_ok=True)


def write_manifest(output, identifier, name, slot, version, game=None):
    where = f" of {GAMES[game]}" if game else ""
    manifest = {"format_version": 1, "id": identifier, "name": f"{name} (language pack)", "version": version,
                "description": f"{name} in the game's {slot} slot{where}, for the Language Support plugin.",
                "media": "media", "database": None, "dependencies": []}
    if game:
        manifest["games"] = [game]
    (output / "mod.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    # UTF-16 (with BOM) so GetPrivateProfileString reads any language name.
    (output / "language.ini").write_text(
        "; Language pack for the Language Support plugin (Forza Horizon 2 Extra Options).\n"
        f"; Install this folder as <Azimuth>\\mods\\{output.name}\\; choose {slot} in the Azimuth launcher.\n"
        f"[Language]\nName={name}\nSlot={slot}\n", encoding="utf-16")


def build(options):
    if not options.original.is_dir() or not options.modded.is_dir():
        sys.exit("both media folders must exist")
    slot = slot_of(options.slot)
    identifier = mod_id(options.id, options.output)
    prepare(options.output)
    media = options.output / "media"
    entries = files = size = 0
    for source in sorted(p for p in options.modded.rglob("*") if p.is_file()):
        relative = source.relative_to(options.modded)
        if relative.as_posix().lower() == "zipmanifest.xml":
            continue
        original = options.original / relative
        if source.suffix.lower() == ".zip" and original.is_file() and not options.whole_zips:
            for name, data in changed_entries(original, source):
                target = media / relative.with_suffix("") / Path(*safe_parts(name))
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
                entries += 1
                size += len(data)
            continue
        if original.is_file() and file_digest(original) == file_digest(source):
            continue
        target = media / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        files += 1
        size += source.stat().st_size
    write_manifest(options.output, identifier, options.name, slot, options.version, options.game)
    print(f"{entries} changed zip entries, {files} whole files, {size / 1e6:.2f} MB -> {options.output} "
          f"(mod id {identifier}, slot {slot})")
    report_fixes(options, media, SLOTS[slot], options.original)


def read_fork_ini(path):
    data = path.read_bytes()
    for encoding in ("utf-16",) if data[:2] in (b"\xff\xfe", b"\xfe\xff") else ("utf-8-sig", "cp1252"):
        try:
            text = data.decode(encoding)
            break
        except UnicodeDecodeError:
            continue
    else:
        sys.exit(f"{path}: cannot read it")
    values, section = {}, None
    for line in text.splitlines():
        line = line.split(";", 1)[0].strip()
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1].strip().lower()
        elif "=" in line and section == "language":
            key, value = line.split("=", 1)
            values[key.strip().lower()] = value.strip()
    return values


def convert(options):
    ini = options.pack / "language.ini"
    if not ini.is_file():
        sys.exit(f"{options.pack} has no language.ini (not a pack of the Extra Options fork)")
    values = read_fork_ini(ini)
    slot = slot_of(values.get("slot", ""))
    name = values.get("name") or options.pack.name
    identifier = mod_id(options.id if options.id is not None else options.pack.name.lower(), options.output)
    prepare(options.output)
    count = size = 0
    for source in sorted(p for p in options.pack.rglob("*") if p.is_file()):
        relative = source.relative_to(options.pack)
        if relative.as_posix().lower() in ("language.ini", "zipmanifest.xml"):
            continue
        safe_parts(relative.as_posix())
        target = options.output / "media" / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        count += 1
        size += source.stat().st_size
    write_manifest(options.output, identifier, name, slot, options.version, options.game)
    print(f"{count} files, {size / 1e6:.2f} MB -> {options.output} (mod id {identifier}, slot {slot})")
    report_fixes(options, options.output / "media", SLOTS[slot], options.media)


# ------------------------------------------------------------ string tables

class Lsb2:
    """A game LSB2 string table. Header: "LSB2", 0, 0x200, its size (0x24),
    the section count and their offsets. Section 0 (texts): size, count,
    count + 1 (hash16, UTF-16 offset) entries (the last one ends the text),
    the zero-ended UTF-16 texts and one more zero character. Section 1 (ids): size,
    count, (hash16, u16, name offset) entries and the zero-ended names. A row
    is an id with a text of the same hash16. The other sections only name
    hashes: a text can change size without touching them."""

    def __init__(self, data):
        if len(data) < 28 or data[:4] != b"LSB2":
            raise ValueError("not an LSB2 string table")
        count = struct.unpack_from("<I", data, 16)[0] or 2
        if count < 2 or 20 + 4 * count > len(data):
            raise ValueError("truncated LSB2 header")
        self.data = data
        self.offsets = list(struct.unpack_from(f"<{count}I", data, 20))
        at = self.offsets[0]
        self.size, rows = struct.unpack_from("<II", data, at)
        self.entries = [struct.unpack_from("<HI", data, at + 8 + 6 * i) for i in range(rows + 1)]
        blob = at + 8 + 6 * (rows + 1)
        self.texts = {}
        for (hash16, first), (_, last) in zip(self.entries, self.entries[1:]):
            text = data[blob + 2 * first:blob + 2 * last].decode("utf-16-le")
            self.texts[hash16] = text[:-1] if text.endswith("\0") else text
        at = self.offsets[1]
        ids = struct.unpack_from("<I", data, at + 4)[0]
        names = at + 8 + 8 * ids
        self.ids = {}
        for i in range(ids):
            hash16, _, offset = struct.unpack_from("<HHI", data, at + 8 + 8 * i)
            end = data.index(b"\0", names + offset)
            self.ids[data[names + offset:end].decode("latin1")] = hash16

    def rows(self):
        return {key: self.texts.get(hash16) for key, hash16 in self.ids.items()}

    def text(self, key):
        hash16 = self.ids.get(key)
        return None if hash16 is None else self.texts.get(hash16)

    def replaced(self, texts):
        """The table's bytes with the texts of {hash16: text} replaced."""
        blob, entries = bytearray(), []
        for hash16, _ in self.entries[:-1]:
            entries.append(struct.pack("<HI", hash16, len(blob) // 2))
            blob += (texts.get(hash16, self.texts[hash16]) + "\0").encode("utf-16-le")
        entries.append(struct.pack("<HI", self.entries[-1][0], len(blob) // 2))
        body = b"".join(entries) + blob + b"\0\0"
        size = 8 + len(body)
        section = struct.pack("<II", size, len(self.entries) - 1) + body
        at, delta = self.offsets[0], size - self.size
        out = bytearray(self.data[:at] + section + self.data[at + self.size:])
        offsets = [o + delta if o > at else o for o in self.offsets]
        struct.pack_into(f"<{len(offsets)}I", out, 20, *offsets)
        return bytes(out)


class Media:
    """A game's Mount\\media, read only: a path in the mod layout (media\\X\\a\\b)
    is member a\\b of X.zip, else the loose file."""

    def __init__(self, root):
        self.root = root
        self.archives = {}

    def archive(self, path):
        key = str(path).lower()
        if key not in self.archives:
            archive = None
            if path.is_file():
                z = zipfile.ZipFile(path)
                archive = (z, {i.filename.replace("\\", "/").lower(): i for i in z.infolist() if not i.is_dir()})
            self.archives[key] = archive
        return self.archives[key]

    def read(self, parts):
        for i in range(len(parts) - 1, 0, -1):
            archive = self.archive(Path(str(self.root.joinpath(*parts[:i])) + ".zip"))
            if archive:
                info = archive[1].get("/".join(parts[i:]).lower())
                if info:
                    return archive[0].read(info)
        loose = self.root.joinpath(*parts)
        return loose.read_bytes() if loose.is_file() else None

    def table(self, archive, name):
        data = self.read(("stripped", "stringtables", archive, name))
        return Lsb2(data) if data else None

    def table_names(self, archive):
        found = self.archive(self.root / "stripped" / "stringtables" / f"{archive}.zip")
        return sorted(i.filename for i in found[0].infolist() if i.filename.lower().endswith(".str")) if found else []

    def slot_members(self, archive):
        """(mod-layout parts, data) of the members of the game's zips that belong to one language."""
        named = re.compile(rf"(?:^|[/_]){re.escape(archive)}(?:[/.]|$)", re.I)
        for path in sorted(self.root.rglob("*.zip")):
            relative = path.relative_to(self.root).with_suffix("").parts
            if [p.lower() for p in relative[:2]] == ["stripped", "stringtables"]:
                continue
            archive_ = self.archive(path)
            for name, info in sorted(archive_[1].items()):
                if named.search(info.filename.replace("\\", "/")):
                    yield relative + tuple(info.filename.replace("\\", "/").split("/")), archive_[0].read(info)


def unescape(cell):
    def one(match):
        code = match.group(1)
        if code not in "ntr\\":
            raise ValueError(f"unknown escape \\{code}")
        return {"n": "\n", "t": "\t", "r": "\r", "\\": "\\"}[code]
    return re.sub(r"\\(.)", one, cell)


def escape(text):
    return text.replace("\\", "\\\\").replace("\t", "\\t").replace("\r", "\\r").replace("\n", "\\n")


def read_translations(path):
    """{(table lower-case, key): (table, english, translation, source)}"""
    rows = {}
    for number, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
        if not line.strip() or line.startswith("#"):
            continue
        cells = line.split("\t")
        if [c.lower() for c in cells[:2]] == ["table", "key"]:
            continue
        if len(cells) < 5:
            sys.exit(f"{path}:{number}: a row has table, key, english, translation and source (tab-separated)")
        try:
            table, key, english, text = cells[0].replace("\\", "/"), cells[1], unescape(cells[2]), unescape(cells[3])
        except ValueError as error:
            sys.exit(f"{path}:{number}: {error}")
        if (table.lower(), key) in rows:
            sys.exit(f"{path}:{number}: {table} {key} is there twice")
        rows[(table.lower(), key)] = (table, english, text, cells[4].strip())
    return rows


SUBTITLE_KEY = re.compile(r'<Key\b[^>]*?\bvalue="([^"]*)"', re.S)


def subtitle_values(data):
    return [html.unescape(m.group(1)) for m in SUBTITLE_KEY.finditer(data.decode("utf-8-sig"))]


def subtitle_replaced(data, values):
    """A subtitle XML's bytes with the values of {key index: text} replaced; the rest as it is."""
    bom = data.startswith(b"\xef\xbb\xbf")
    text, out, last = data.decode("utf-8-sig"), [], 0
    for index, match in enumerate(SUBTITLE_KEY.finditer(text)):
        if index in values:
            out += [text[last:match.start(1)], html.escape(values[index], quote=True).replace("&#x27;", "'")]
            last = match.end(1)
    out.append(text[last:])
    return (b"\xef\xbb\xbf" if bom else b"") + "".join(out).encode("utf-8")


def english_subtitles(media, parts, archive):
    match = re.match(rf"(?i)(.*)_{re.escape(archive)}(\.xml)$", parts[-1])
    if match:
        for name in (match.group(1) + match.group(2), match.group(1) + "_EN" + match.group(2)):
            data = media.read(parts[:-1] + (name,))
            if data is not None:
                return data
    return None


def same_original(parts, source, target):
    if source == target:
        return "same"
    if parts[-1].lower().endswith(".vfont") and len(source) == len(target) and \
            source[FONT_HEADER:] == target[FONT_HEADER:]:
        return "same font"
    return None


FIX_GAMES = ("both",) + tuple(GAMES)
FIX_REASONS = ("agreement", "calque", "meaning", "terminology", "length")


def read_fixes(path, game):
    """{(table lower-case, key): (table, english, old, new, line)} of a fixes TSV's rows for one game
    (its game column is the game or both)."""
    rows = {}
    for number, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
        if not line.strip() or line.startswith("#"):
            continue
        cells = line.split("\t")
        if [c.lower() for c in cells[:3]] == ["game", "table", "key"]:
            continue
        if len(cells) < 7:
            sys.exit(f"{path}:{number}: a row has game, table, key, english, old_uk, new_uk and reason (tab-separated)")
        if cells[0] not in FIX_GAMES:
            sys.exit(f"{path}:{number}: game {cells[0]!r} is not one of {', '.join(FIX_GAMES)}")
        if cells[6].strip() not in FIX_REASONS:
            sys.exit(f"{path}:{number}: reason {cells[6]!r} is not one of {', '.join(FIX_REASONS)}")
        if cells[0] not in ("both", game):
            continue
        try:
            table, key = cells[1].replace("\\", "/"), cells[2]
            english, old, new = unescape(cells[3]), unescape(cells[4]), unescape(cells[5])
        except ValueError as error:
            sys.exit(f"{path}:{number}: {error}")
        if (table.lower(), key) in rows:
            sys.exit(f"{path}:{number}: {table} {key} is there twice for {game}")
        rows[(table.lower(), key)] = (table, english, old, new, number)
    return rows


def apply_fixes(pack_media, archive, fixes, game_media=None):
    """Apply fixes (read_fixes) to a pack's files below pack_media, in place: string tables in
    stripped\\stringtables\\<archive>\\<table> and subtitle XMLs at their media path. A fix applies when the
    pack's text is the fix's old text (its new text: already applied). With game_media (the game's
    Mount\\media), the fix's English must be the game's, and a table or subtitle file the pack does not have
    is taken from the game's slot file. Rows of the pack no fix names stay byte for byte as they are.
    Returns (applied, already, problems [(table, key, why)])."""
    game = Media(game_media) if game_media else None
    applied = already = 0
    problems = []
    by_file = {}
    for (_, key), row in fixes.items():
        by_file.setdefault(row[0], []).append((key, row))
    for file, rows in sorted(by_file.items()):
        is_table = file.lower().endswith(".str")
        parts = ("stripped", "stringtables", archive, file) if is_table else safe_parts(file)
        path = pack_media.joinpath(*parts)
        data = path.read_bytes() if path.is_file() else (game.read(parts) if game else None)
        if data is None:
            problems += [(file, key, "no such table or file in the pack" + (" or the game" if game else ""))
                         for key, _ in rows]
            continue
        english = None
        if game:
            english = game.table(ENGLISH, file) if is_table else english_subtitles(game, parts, archive)
            if not is_table and english is not None:
                english = subtitle_values(english)
        table = Lsb2(data) if is_table else None
        values = None if is_table else subtitle_values(data)
        texts = {}
        for key, (_, want_english, old, new, _) in sorted(rows, key=lambda r: r[1][4]):
            if is_table:
                slot = table.ids.get(key)
                current = None if slot is None else table.texts.get(slot)
            else:
                match = re.fullmatch(r"Key\[(\d+)\]", key)
                slot = int(match.group(1)) if match and int(match.group(1)) < len(values) else None
                current = None if slot is None else values[slot]
            if slot is None:
                problems.append((file, key, "unknown key"))
                continue
            if game:
                if is_table:
                    game_english = english.text(key) if english else None
                else:
                    game_english = english[slot] if english and slot < len(english) else None
                if game_english != want_english:
                    problems.append((file, key, "the fix's English is not the game's"))
                    continue
            if texts.get(slot, current) == new:
                already += 1
                continue
            if current != old:
                problems.append((file, key, "the pack's text is not the fix's old text"))
                continue
            if slot in texts:
                problems.append((file, key, "shares its text with a row another fix changed"))
                continue
            texts[slot] = new
            applied += 1
        if texts:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(table.replaced(texts) if is_table else subtitle_replaced(data, texts))
    return applied, already, problems


def report_fixes(options, pack_media, archive, game_media=None):
    """Apply --fixes (if given) to a pack being made or an existing one; print what happened. Returns the
    number of fixes not applied."""
    if not getattr(options, "fixes", None):
        return 0
    game = options.fixes_game or options.game or "fh2"
    fixes = read_fixes(options.fixes, game)
    applied, already, problems = apply_fixes(pack_media, archive, fixes, game_media)
    print(f"fixes ({game}): {applied} applied, {already} already in the pack, {len(problems)} not applied "
          f"(of {len(fixes)} rows in {options.fixes.name})")
    for table, key, why in problems:
        print(f"  not applied: {table} {key}: {why}")
    return len(problems)


def fix(options):
    pack = options.pack
    if not (pack / "language.ini").is_file():
        sys.exit(f"{pack} has no language.ini (not a language pack)")
    archive = SLOTS[slot_of(read_fork_ini(pack / "language.ini").get("slot", ""))]
    pack_media = pack / "media" if (pack / "media").is_dir() else pack
    if options.media and not (options.media / "stripped" / "stringtables" / f"{ENGLISH}.zip").is_file():
        sys.exit(f"{options.media} is not a game's media folder (no stripped\\stringtables\\{ENGLISH}.zip)")
    if report_fixes(options, pack_media, archive, options.media) and options.strict:
        sys.exit(1)


def port(options):
    pack = options.pack
    if not (pack / "language.ini").is_file():
        sys.exit(f"{pack} has no language.ini (not a language pack)")
    values = read_fork_ini(pack / "language.ini")
    slot = slot_of(values.get("slot", ""))
    name = values.get("name") or pack.name
    archive = SLOTS[slot]
    pack_media = pack / "media" if (pack / "media").is_dir() else pack
    for folder in (options.source, options.target):
        if not (folder / "stripped" / "stringtables" / f"{archive}.zip").is_file():
            sys.exit(f"{folder} is not a game's media folder (no stripped\\stringtables\\{archive}.zip)")
    translations = read_translations(options.translations) if options.translations else {}
    identifier = mod_id(options.id, options.output)
    prepare(options.output)
    media_out = options.output / "media"
    source, target, packed = Media(options.source), Media(options.target), Media(pack_media)
    used, report, written = set(), [], set()
    counts = dict.fromkeys(("rows", "pack", "empty", "game", "untranslated", "tables"), 0)
    by_source = {}

    def write(parts, data):
        path = media_out.joinpath(*parts)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        written.add("/".join(parts).lower())

    def translated(table, key, english):
        row = translations.get((table.lower(), key))
        if row is None:
            return None
        used.add((table.lower(), key))
        if row[1] != english:
            report.append((table, key, english or "", row[2], "translation's English is not the game's"))
            return None
        by_source[row[3]] = by_source.get(row[3], 0) + 1
        return row[2]

    # String tables.
    target_tables = target.table_names(archive)
    for table in target_tables:
        slot_table, english_table = target.table(archive, table), target.table(ENGLISH, table)
        pack_table, source_english = packed.table(archive, table), source.table(ENGLISH, table)
        if english_table is None:
            sys.exit(f"{options.target}: no {ENGLISH} table {table}")
        texts = {}
        for key, hash16 in slot_table.ids.items():
            counts["rows"] += 1
            english = english_table.text(key)
            text = translated(table, key, english)
            if text is None:
                if pack_table is None:
                    counts["game"] += 1
                    continue
                if key in pack_table.ids and source_english and source_english.text(key) == english:
                    text = pack_table.text(key)
                    counts["pack"] += 1
                elif not english and not slot_table.texts.get(hash16):
                    counts["empty"] += 1
                    continue
                else:
                    counts["untranslated"] += 1
                    why = "new in this game" if not source_english or key not in source_english.ids else \
                        "English differs from the source game's"
                    report.append((table, key, english or "", slot_table.texts.get(hash16, ""), why))
                    continue
            if text != slot_table.texts.get(hash16):
                if hash16 in texts and texts[hash16] != text:
                    print(f"warning: {table}: rows sharing text hash {hash16:#06x} want different texts; "
                          f"{key} keeps the first")
                    continue
                texts[hash16] = text
        if texts:
            write(("stripped", "stringtables", archive, table), slot_table.replaced(texts))
            counts["tables"] += 1
    known = {t.lower() for t in target_tables}
    left_out = sorted(p.name for p in (pack_media / "stripped" / "stringtables" / archive).glob("*.str")
                      if p.name.lower() not in known and p.name.lower() != VOICE_TABLE.lower())

    # Spoken lines: the pack's VoiceSubtitles.str rows for lines the target game plays too (its own
    # lines have no text in its files: no subtitles, no string table; the count is reported).
    voice_note = ""
    pack_voice = packed.table(archive, VOICE_TABLE)
    events = voice_events(options.target)
    if pack_voice is not None:
        rows = pack_voice.rows()
        lines = {key for key in rows if not key.endswith("~t")}
        kept = sorted(key for key in lines if key in events)
        table_rows = []
        for key in kept:
            table_rows.append((key, rows[key] or ""))
            if key + "~t" in rows:
                table_rows.append((key + "~t", rows[key + "~t"] or ""))
        if kept:
            write(("stripped", "stringtables", archive, VOICE_TABLE), lsb2_table(table_rows))
        voice_note = (f"voice subtitles: {len(kept)} of the pack's {len(lines)} lines are lines this game plays"
                      f"{' (written)' if kept else ''}; {len(events - lines)} of its {len(events)} spoken lines have "
                      f"no text in the pack")
    elif events:
        voice_note = f"voice subtitles: the pack has no {VOICE_TABLE}; this game plays {len(events)} spoken lines"

    # Subtitle XMLs from the translations.
    files = {}
    for (table_key, key), row in translations.items():
        if not table_key.endswith(".str"):
            files.setdefault(row[0], []).append((key, row))
    subtitles = 0
    for file, rows in sorted(files.items()):
        parts = safe_parts(file)
        data, english = target.read(parts), english_subtitles(target, parts, archive)
        if data is None or english is None:
            for key, row in rows:
                report.append((row[0], key, row[1], row[2], "no such file (or its English) in the game"))
            continue
        english_values, values = subtitle_values(english), {}
        for key, row in rows:
            match = re.fullmatch(r"Key\[(\d+)\]", key)
            index = int(match.group(1)) if match else -1
            text = translated(row[0], key, english_values[index] if 0 <= index < len(english_values) else None)
            if text is not None:
                values[index] = text
        if values:
            write(parts, subtitle_replaced(data, values))
            subtitles += 1
    for (table_key, key), row in translations.items():
        if (table_key, key) not in used:
            report.append((row[0], key, row[1], row[2], "translation of a row the game does not have"))

    # The pack's other files.
    by_data, copied, matched, absent, differ = {}, 0, 0, [], []
    for path in sorted(p for p in pack_media.rglob("*") if p.is_file()):
        parts = path.relative_to(pack_media).parts
        low = [p.lower() for p in parts]
        if low[:2] == ["stripped", "stringtables"] or "/".join(low) in SKIPPED_FILES:
            continue
        original, game = source.read(parts), target.read(parts)
        if original is not None:
            by_data.setdefault(digest(original), path)
        if "/".join(low) in written:
            continue
        if game is None:
            absent.append("/".join(parts))
        elif original is not None and same_original(parts, original, game):
            write(parts, path.read_bytes())
            copied += 1
        else:
            differ.append("/".join(parts))
    for parts, data in target.slot_members(archive):
        key = "/".join(parts).lower()
        if key in written or digest(data) not in by_data or (pack_media.joinpath(*parts)).is_file():
            continue
        write(parts, by_data[digest(data)].read_bytes())
        matched += 1
    for file in differ:
        report.append((file, "", "", "", "the game's original differs from the source game's: left to the game"))

    write_manifest(options.output, identifier, name, slot, options.version, options.game)
    report_fixes(options, media_out, archive, options.target)
    if options.report:
        options.report.parent.mkdir(parents=True, exist_ok=True)
        with open(options.report, "w", encoding="utf-8", newline="\n") as f:
            f.write("table\tkey\tenglish\tslot text\treason\n")
            for row in report:
                f.write("\t".join(escape(cell) for cell in row) + "\n")
    sources = ", ".join(f"{k} {v}" for k, v in sorted(by_source.items())) or "none"
    print(f"string tables: {counts['rows']} rows in {len(target_tables)} tables; {counts['pack']} from the pack "
          f"(same key, same English), {sum(by_source.values())} from the translations ({sources}), "
          f"{counts['empty']} empty, {counts['game']} left to the game (tables the pack does not have), "
          f"{counts['untranslated']} "
          f"untranslated (kept as the game has them); {counts['tables']} tables written")
    if left_out:
        print(f"  not in this game, left out: {', '.join(left_out)}")
    if voice_note:
        print("  " + voice_note)
    print(f"files: {copied} copied (same original), {matched} matched by content, {subtitles} subtitle files "
          f"translated, {len(absent)} not in this game, {len(differ)} differ in this game (left to the game"
          f"{': ' + ', '.join(differ) if differ else ''})")
    print(f"-> {options.output} (mod id {identifier}, slot {slot}"
          f"{', game ' + options.game if options.game else ''}); {len(report)} report rows")


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    make = commands.add_parser("build", help="a pack from a translation mod's media")
    make.add_argument("original", type=Path)
    make.add_argument("modded", type=Path)
    make.add_argument("output", type=Path)
    make.add_argument("--slot", required=True, help="game language slot the pack takes over, e.g. ru-RU")
    make.add_argument("--name", required=True, help="the language's name (log, mod.json)")
    make.add_argument("--whole-zips", action="store_true")
    old = commands.add_parser("convert", help="a pack of the Extra Options fork as a mod folder")
    old.add_argument("pack", type=Path)
    old.add_argument("output", type=Path)
    move = commands.add_parser("port", help="a pack made for one game carried over to another game")
    move.add_argument("pack", type=Path, help="the pack (a mod folder or a fork pack)")
    move.add_argument("source", type=Path, help="Mount\\media of the game the pack was made for")
    move.add_argument("target", type=Path, help="Mount\\media of the game to make it for")
    move.add_argument("output", type=Path)
    move.add_argument("--translations", type=Path, help="TSV of the target game's own rows")
    move.add_argument("--report", type=Path, help="TSV of what stays untranslated")
    old.add_argument("--media", type=Path, help="the game's Mount\\media: check the fixes' English against it")
    mend = commands.add_parser("fix", help="apply a fixes TSV to an existing pack, in place")
    mend.add_argument("pack", type=Path, help="the pack (a mod folder or a fork pack)")
    mend.add_argument("--fixes", type=Path, required=True, help="TSV of corrections (game, table, key, ...)")
    mend.add_argument("--fixes-game", choices=sorted(GAMES), help="the pack's game (default fh2)")
    mend.add_argument("--media", type=Path, help="the game's Mount\\media: check the fixes' English against it")
    mend.add_argument("--strict", action="store_true", help="exit 1 when a fix is not applied")
    mend.set_defaults(game=None)
    for command in (make, old, move):
        command.add_argument("--id", help="the mod id (default: from the folder name)")
        command.add_argument("--version", default="1.0.0", help="the pack's version in mod.json")
        command.add_argument("--game", choices=sorted(GAMES), help="load only in this game (mod.json games)")
        command.add_argument("--fixes", type=Path, help="TSV of corrections applied to the pack made")
        command.add_argument("--fixes-game", choices=sorted(GAMES), help="the fixes' game (default: --game, else fh2)")
    options = parser.parse_args()
    {"build": build, "convert": convert, "port": port, "fix": fix}[options.command](options)


if __name__ == "__main__":
    main()
