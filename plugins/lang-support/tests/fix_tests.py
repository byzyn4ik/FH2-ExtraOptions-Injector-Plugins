"""Tests of make_language_pack.py's fixes TSV (fix command and --fixes of build, convert and port).

Synthetic packs and games only (nothing installed, no game run); with --fh2 <Mount folder> also the real
Ukrainian fork pack's tables (read only, copied to the work folder): a round trip through the fixes code
keeps every row it does not fix byte for byte.

Usage: py -I tests/fix_tests.py [--work <folder>] [--fh2 <FH2 Mount folder>]
Called from run_tests.py as test_fixes(check, skip, work, fh2_mount).
"""

import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

HERE = Path(__file__).resolve().parent
TOOL = HERE.parent / "tools" / "make_language_pack.py"
sys.path.insert(0, str(TOOL.parent))
sys.path.insert(0, str(HERE))
import make_language_pack as tool  # noqa: E402
from run_tests import lsb2_game, subtitles_xml, zip_of  # noqa: E402

HEADER = "game\ttable\tkey\tenglish\told_uk\tnew_uk\treason\n"


def run(*args):
    return subprocess.run([sys.executable, "-I", str(TOOL), *map(str, args)],
                          capture_output=True, text=True, encoding="utf-8", errors="replace")


def rows_of(path):
    return tool.Lsb2(path.read_bytes()).rows()


def test_fixes(check, skip, work, fh2_mount=None):
    print("fixes")
    shutil.rmtree(work, ignore_errors=True)
    tables = Path("stripped/stringtables")
    game = work / "game" / "media"
    zip_of(game / tables / "EN.zip", {
        "InGame.str": lsb2_game([("LAP", "LAP"), ("BEST", "BEST"), ("POS", "{0} of {1}"), ("NL", "Line\nbreak"),
                                 ("SP", " Lap "), ("ONLY", "Game only")]),
        "Other.str": lsb2_game([("O", "Other")])})
    zip_of(game / tables / "RU.zip", {
        "InGame.str": lsb2_game([("LAP", "КРУГ"), ("BEST", "ЛУЧШИЙ"), ("POS", "{0} из {1}"), ("NL", "Строка\nперенос"),
                                 ("SP", " Круг "), ("ONLY", "Только игра")]),
        "Other.str": lsb2_game([("O", "Другое")])})
    zip_of(game / "Cinematics.zip", {"Subtitles/Intro.xml": subtitles_xml("Hello there.", "Bye", ""),
                                     "Subtitles/Intro_RU.xml": subtitles_xml("Привет.", "Пока", "")})
    pack = work / "pack"
    media = pack / "media"
    (media / tables / "RU").mkdir(parents=True)
    in_game = lsb2_game([("LAP", "КОЛО"), ("BEST", "НАЙКРАЩИЙ"), ("POS", "{0} із {1}"), ("NL", "Рядок\nперенос"),
                         ("SP", " Коло "), ("ONLY", "Лише гра")])
    (media / tables / "RU" / "InGame.str").write_bytes(in_game)
    (media / "Cinematics" / "Subtitles").mkdir(parents=True)
    (media / "Cinematics" / "Subtitles" / "Intro_RU.xml").write_bytes(subtitles_xml("Привіт тобі.", "Бувай", ""))
    (pack / "language.ini").write_text("[Language]\nName=Українська\nSlot=ru-RU\n", encoding="utf-16")
    pristine = work / "pristine"
    shutil.copytree(pack, pristine)

    fixes = work / "uk-fixes.tsv"
    fixes.write_text(
        "# corrections\n" + HEADER +
        "both\tInGame.str\tBEST\tBEST\tНАЙКРАЩИЙ\tНАЙКРАЩЕ\tagreement\n"
        "fh2\tInGame.str\tPOS\t{0} of {1}\t{0} із {1}\t{0} з {1}\tcalque\n"
        "fh2\tInGame.str\tNL\tLine\\nbreak\tРядок\\nперенос\tРядок\\nрозрив\tmeaning\n"
        "fh2\tInGame.str\tSP\t Lap \t Коло \t Кружок \tterminology\n"
        "fh2\tInGame.str\tNOPE\tNope\tНі\tНемає\tmeaning\n"
        "fh2\tInGame.str\tONLY\tSomething else\tЛише гра\tТільки гра\tmeaning\n"
        "fh2\tCinematics/Subtitles/Intro_RU.xml\tKey[0]\tHello there.\tПривіт тобі.\tПривіт, \"друже\" & ко.\tmeaning\n"
        "fh2ff\tInGame.str\tLAP\tLAP\tКОЛО\tКРУГ\tmeaning\n", encoding="utf-8")

    # fix: in place, with the game's English checked.
    result = run("fix", pack, "--fixes", fixes, "--media", game)
    check(result.returncode == 0, f"fix: runs ({result.stdout.strip() or result.stderr.strip()})")
    out = result.stdout
    rows = rows_of(media / tables / "RU" / "InGame.str")
    check(rows["BEST"] == "НАЙКРАЩЕ", "fix: a fix of game both applies")
    check(rows["POS"] == "{0} з {1}", "fix: a fix keeps its placeholders ({0}, {1})")
    check(rows["NL"] == "Рядок\nрозрив" and rows["SP"] == " Кружок ",
          "fix: \\n in a cell is a line break; leading and trailing spaces kept")
    check(rows["LAP"] == "КОЛО", "fix: a fix of the other game (fh2ff) is not applied to fh2")
    check("InGame.str NOPE: unknown key" in out, "fix: an unknown key is reported")
    check(rows["ONLY"] == "Лише гра" and "InGame.str ONLY: the fix's English is not the game's" in out,
          "fix: a fix whose English is not the game's is reported and not applied")
    check("5 applied" in out and "2 not applied" in out, f"fix: the summary counts ({out.splitlines()[0]})")
    xml = (media / "Cinematics" / "Subtitles" / "Intro_RU.xml").read_bytes()
    check(tool.subtitle_values(xml) == ["Привіт, \"друже\" & ко.", "Бувай", ""] and b"&amp;" in xml and b"&quot;" in xml,
          "fix: a subtitle Key[n] fix applies, XML-escaped")
    before, after = tool.Lsb2(in_game), tool.Lsb2((media / tables / "RU" / "InGame.str").read_bytes())
    fixed = {"BEST", "POS", "NL", "SP"}
    check(all(after.text(k) == before.text(k) for k in before.ids if k not in fixed) and
          after.data[after.offsets[1]:] == before.data[before.offsets[1]:],
          "fix: untouched rows and the other sections stay as they were")
    again = run("fix", pack, "--fixes", fixes, "--media", game, "--strict")
    check(again.returncode == 1 and "0 applied, 5 already in the pack" in again.stdout,
          "fix: a second run applies nothing (already applied); --strict exits 1 on what is not applied")

    # Byte-exact round trip: fixes that change nothing leave the table's bytes as they are.
    noop = work / "noop.tsv"
    noop.write_text(HEADER + "fh2\tInGame.str\tLAP\tLAP\tКОЛО\tКОЛО\tmeaning\n", encoding="utf-8")
    copy = work / "copy"
    shutil.copytree(pristine, copy)
    run("fix", copy, "--fixes", noop)
    check((copy / "media" / tables / "RU" / "InGame.str").read_bytes() == in_game,
          "fix: a pack no fix changes keeps its table byte for byte")
    check(tool.Lsb2(in_game).replaced({}) == in_game, "fix: an LSB2 table rebuilt with no change is byte-exact")

    # convert --fixes (a fork pack: files at the pack's root).
    fork = work / "fork" / "uk"
    shutil.copytree(pristine / "media", fork)
    shutil.copy2(pristine / "language.ini", fork / "language.ini")
    converted = work / "mods" / "Ukrainian"
    result = run("convert", fork, converted, "--id", "uk", "--fixes", fixes, "--media", game)
    check(result.returncode == 0 and rows_of(converted / "media" / tables / "RU" / "InGame.str")["BEST"] == "НАЙКРАЩЕ",
          f"convert --fixes: the converted pack has the fixes ({result.stdout.strip()[-120:]})")
    check((fork / tables / "RU" / "InGame.str").read_bytes() == in_game, "convert --fixes: the fork pack is only read")

    # build --fixes: a translation mod's zips; fixes go to the pack's loose tables.
    modded = work / "modded"
    zip_of(modded / tables / "RU.zip", {"InGame.str": in_game})
    built = work / "built"
    result = run("build", game, modded, built, "--slot", "ru-RU", "--name", "Українська", "--id", "uk",
                 "--fixes", fixes)
    check(result.returncode == 0 and rows_of(built / "media" / tables / "RU" / "InGame.str")["POS"] == "{0} з {1}",
          f"build --fixes: the built pack has the fixes ({result.stdout.strip()[-120:]})")

    # port --fixes --game fh2ff: rows of game both and fh2ff, checked against the target's English.
    target = work / "ff" / "media"
    zip_of(target / tables / "EN.zip", {"InGame.str": lsb2_game([("LAP", "LAP"), ("BEST", "BEST")])})
    zip_of(target / tables / "RU.zip", {"InGame.str": lsb2_game([("LAP", "КРУГ"), ("BEST", "ЛУЧШИЙ")])})
    ported = work / "ported"
    result = run("port", pristine, game, target, ported, "--game", "fh2ff", "--id", "uk-fh2ff", "--fixes", fixes)
    rows = rows_of(ported / "media" / tables / "RU" / "InGame.str") if result.returncode == 0 else {}
    check(rows.get("BEST") == "НАЙКРАЩЕ" and rows.get("LAP") == "КРУГ",
          f"port --fixes: fixes of both and fh2ff apply to the ported pack ({result.stdout.strip()[-160:]})")

    bad = work / "bad.tsv"
    bad.write_text(HEADER + "fh3\tInGame.str\tLAP\tLAP\tКОЛО\tКРУГ\tmeaning\n", encoding="utf-8")
    result = run("fix", pack, "--fixes", bad)
    check(result.returncode != 0 and "game 'fh3'" in result.stderr, "fix: an unknown game in the TSV is refused")

    # The real pack: every table round-trips; a fix of one row leaves the others byte for byte.
    uk = fh2_mount / "languages" / "uk" if fh2_mount else None
    if not uk or not (uk / tables / "RU").is_dir():
        skip("fixes: the real Ukrainian pack (no --fh2 Mount with languages\\uk)")
        return
    exact, total = 0, 0
    for path in sorted((uk / tables / "RU").glob("*.str")):
        data = path.read_bytes()
        try:
            table = tool.Lsb2(data)
        except ValueError:
            continue
        total += 1
        exact += table.replaced({}) == data
    check(total and exact == total, f"fixes: the real pack's tables round-trip byte for byte ({exact}/{total})")
    real = work / "real"
    (real / tables / "RU").mkdir(parents=True)
    shutil.copy2(uk / "language.ini", real / "language.ini")
    source = uk / tables / "RU" / "InGame.str"
    shutil.copy2(source, real / tables / "RU" / "InGame.str")
    original = tool.Lsb2(source.read_bytes())
    key = "IDS_Best" if "IDS_Best" in original.ids else sorted(original.ids)[0]
    old = original.text(key)
    one = work / "one.tsv"
    one.write_text(HEADER + f"fh2\tInGame.str\t{key}\t-\t{tool.escape(old)}\t{tool.escape(old + '!')}\tmeaning\n",
                   encoding="utf-8")
    result = run("fix", real, "--fixes", one)
    changed = tool.Lsb2((real / tables / "RU" / "InGame.str").read_bytes())
    shared = {k for k, h in original.ids.items() if h == original.ids[key]}
    check(result.returncode == 0 and changed.text(key) == old + "!" and
          all(changed.text(k) == original.text(k) for k in original.ids if k not in shared),
          f"fixes: a fix of the real InGame.str {key} changes that row only")


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--work", type=Path, default=HERE.parent / "out" / "test-runs" / "fixes")
    parser.add_argument("--fh2", type=Path, default=Path.home() / "FH2" / "Anthem_1.0.0.64_x64__8wekyb3d8bbwe" / "Mount")
    options = parser.parse_args()
    failures = []

    def check(ok, what):
        print(("  ok   " if ok else "  FAIL ") + what)
        if not ok:
            failures.append(what)

    test_fixes(check, lambda what: print("  skip " + what), options.work.resolve(), options.fh2)
    print(f"{len(failures)} failure(s)" if failures else "all tests passed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
