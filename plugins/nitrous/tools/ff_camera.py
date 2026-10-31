"""Fast & Furious' nitrous camera for the Nitrous plugin, from your own install.

Usage: py tools/ff_camera.py [--media <F&F media folder>] [--fh2-media <FH2 media folder>]
                             [--mods <the game's mods folder> | --output <file>]

Forza Horizon 2 Presents Fast & Furious' media/Camera.zip holds its
CameraPhysics.xml: Forza Horizon 2's camera layers plus 52 that follow the
boost (InputType "Nitrous" and "NitrousImpulse": the FOV kick, shake, offsets
and motion blur of its follow, cockpit, hood and bumper cameras). Forza
Horizon 2's parser knows neither input name, so this tool writes Fast &
Furious' file with them renamed to two input types Forza Horizon 2 parses but
never computes ("ShoulderYaw" for Nitrous, "ShoulderRoll" for NitrousImpulse);
the plugin fills those two as Fast & Furious computes its inputs (hooks.c).

With --mods the file goes to mods/Nitrous/media/Camera/CameraPhysics.xml,
where the mod loader serves it as game:\\media\\Camera\\CameraPhysics.xml.
When Forza Horizon 2's own Camera.zip is found (--fh2-media, or beside the
game) the tool checks that Fast & Furious' file is Forza Horizon 2's plus the
nitrous layers. Nothing of either game is in this repository or its
releases; the file is made on your machine from your copy of the game.

--media defaults to the first of
  D:/Games/Forza Horizon 2 Presents Fast & Furious/game/base/Mount/media
  ~/FH2/Forza Horizon 2 Presents Fast & Furious/Spire_*/Mount/media
"""
# SPDX-License-Identifier: GPL-3.0-or-later

import argparse
from pathlib import Path
import re
import sys
import zipfile

ARCHIVE = "Camera.zip"
MEMBER = "CameraPhysics.xml"
CARRIERS = {b"Nitrous": b"ShoulderYaw", b"NitrousImpulse": b"ShoulderRoll"}
LAYERS = {b"Nitrous": 35, b"NitrousImpulse": 17}  # Fast & Furious 1.0.0.6
INPUT = re.compile(rb'InputType="(\w+)"')


def find_media(candidates):
    return next((c for c in candidates if (c / ARCHIVE).is_file()), None)


def ff_candidates():
    out = [Path("D:/Games/Forza Horizon 2 Presents Fast & Furious/game/base/Mount/media")]
    out += sorted((Path.home() / "FH2" / "Forza Horizon 2 Presents Fast & Furious").glob("Spire_*/Mount/media"))
    return out


def fh2_candidates(mods):
    out = []
    if mods:
        out += [mods.parent / "game/base/Mount/media", mods.parent / "Mount/media", mods.parent / "media"]
    out += sorted((Path.home() / "FH2").glob("Anthem_1.0.0.64_*/Mount/media"))
    return out


def convert(text):
    """Fast & Furious' CameraPhysics.xml with the nitrous inputs on the carriers; (bytes, counts)."""
    counts = dict.fromkeys(CARRIERS, 0)
    used = {m.group(1) for m in INPUT.finditer(text)}
    clash = sorted(c.decode() for c in CARRIERS.values() if c in used)
    if clash:
        raise SystemExit(f"{MEMBER} already uses {', '.join(clash)}; the carriers would mix")

    def swap(match):
        name = match.group(1)
        if name in CARRIERS:
            counts[name] += 1
            return b'InputType="' + CARRIERS[name] + b'"'
        return match.group(0)

    return INPUT.sub(swap, text), counts


def strip_nitrous(text):
    """The file without its nitrous layers and layer counts, whitespace normalised (to compare with FH2's)."""
    lines = [l.strip() for l in text.splitlines() if not re.search(rb'InputType="Nitrous', l)]
    return [re.sub(rb'NumLayers="\d+"', b"", l) for l in lines if l]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--media", type=Path, help="Fast & Furious' media folder (holding Camera.zip)")
    parser.add_argument("--fh2-media", type=Path, help="Forza Horizon 2's media folder, for the check")
    parser.add_argument("--mods", type=Path, help="the game's mods folder: writes Nitrous/media/Camera/CameraPhysics.xml")
    parser.add_argument("--output", type=Path, help="the file to write (instead of --mods)")
    options = parser.parse_args(argv)
    media = options.media or find_media(ff_candidates())
    if not media or not (media / ARCHIVE).is_file():
        raise SystemExit(f"Fast & Furious' {ARCHIVE} was not found; give --media")
    source = zipfile.ZipFile(media / ARCHIVE).read(MEMBER)
    converted, counts = convert(source)
    if counts != LAYERS:
        print(f"note: {counts} nitrous layers, Fast & Furious 1.0.0.6 has {LAYERS}", file=sys.stderr)
    if not any(counts.values()):
        raise SystemExit(f"{media / ARCHIVE}: {MEMBER} has no nitrous layers (is this Fast & Furious?)")
    fh2 = options.fh2_media or find_media(fh2_candidates(options.mods))
    if fh2 and (fh2 / ARCHIVE).is_file():
        own = zipfile.ZipFile(fh2 / ARCHIVE).read(MEMBER)
        if strip_nitrous(source) != strip_nitrous(own):
            raise SystemExit(f"Fast & Furious' {MEMBER} is not Forza Horizon 2's ({fh2 / ARCHIVE}) plus the nitrous "
                             "layers; not written")
        print(f"checked: Fast & Furious' {MEMBER} is Forza Horizon 2's plus {sum(counts.values())} nitrous layers")
    else:
        print("note: Forza Horizon 2's Camera.zip was not found; the file is not compared with it", file=sys.stderr)
    if options.output:
        target = options.output
    elif options.mods:
        target = options.mods / "Nitrous" / "media" / "Camera" / MEMBER
    else:
        target = Path(MEMBER)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(converted)
    print(f"{target}  ({counts[b'Nitrous']} Nitrous layers on ShoulderYaw, {counts[b'NitrousImpulse']} "
          f"NitrousImpulse on ShoulderRoll)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
