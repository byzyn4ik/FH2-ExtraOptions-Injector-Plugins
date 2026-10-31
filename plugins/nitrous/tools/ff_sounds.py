"""Fast & Furious' nitrous sounds for the Nitrous plugin, from your own install.

Usage: py tools/ff_sounds.py --ffmpeg <ffmpeg.exe> [--media <F&F media folder>] [--output <folder>]

Forza Horizon 2 Presents Fast & Furious keeps its NOS sounds in
media/audio/fmodanthem/durango/HZ2_SFX_UI.bank (FMOD Studio, an FSB5 of XMA2
samples): HZ2_SFX_NOS_Start_V3, HZ2_SFX_NOS_Sustain_V4 and HZ2_SFX_NOS_Stop_V3,
the samples of its /Master/NOS/NOS event. XMA2 needs a decoder Windows does not
have, so the plugin does not read the bank at run time: this tool cuts the
three samples out of the bank, wraps each in an XMA2 RIFF header and decodes it
with ffmpeg (its xma2 decoder) into 48 kHz 16-bit stereo WAV files:

  <output>/nos_start.wav, nos_sustain.wav, nos_stop.wav

Put them in mods/Nitrous/plugins/NitrousSounds/ (the default --output when
--mods is given). Nothing of the game is in this repository or its releases;
the files are made on your machine from your copy of the game.

--media defaults to the first of
  D:/Games/Forza Horizon 2 Presents Fast & Furious/game/base/Mount/media
  ~/FH2/Forza Horizon 2 Presents Fast & Furious/Spire_*/Mount/media
"""
# SPDX-License-Identifier: GPL-3.0-or-later

import argparse
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

BANK = Path("audio/fmodanthem/durango/HZ2_SFX_UI.bank")
SAMPLES = {"HZ2_SFX_NOS_Start_V3": "nos_start.wav", "HZ2_SFX_NOS_Sustain_V4": "nos_sustain.wav",
           "HZ2_SFX_NOS_Stop_V3": "nos_stop.wav"}
RATES = {1: 8000, 2: 11000, 3: 11025, 4: 16000, 5: 22050, 6: 24000, 7: 32000, 8: 44100, 9: 48000}
XMA = 10  # FSB5 mode


def fsb5_samples(data):
    """The FSB5 in a bank: [(name, channels, rate, sample count, raw bytes)]."""
    at = data.find(b"FSB5")
    if at < 0:
        raise SystemExit("no FSB5 sound bank in the file")
    version, count, headers_size, names_size, data_size, mode = struct.unpack_from("<6I", data, at + 4)
    if mode != XMA:
        raise SystemExit(f"the FSB5 is not XMA (mode {mode})")
    header = at + (0x3C if version == 1 else 0x40)
    p, samples = header, []
    for _ in range(count):
        value = struct.unpack_from("<Q", data, p)[0]
        p += 8
        more, frequency, channels = value & 1, (value >> 1) & 0xF, ((value >> 5) & 1) + 1
        offset, frames = ((value >> 6) & 0xFFFFFFF) * 16, value >> 34
        rate = RATES.get(frequency, frequency)
        while more:
            chunk = struct.unpack_from("<I", data, p)[0]
            p += 4
            more, size, kind = chunk & 1, (chunk >> 1) & 0xFFFFFF, (chunk >> 25) & 0x7F
            if kind == 1:
                channels = data[p]
            elif kind == 2:
                rate = struct.unpack_from("<I", data, p)[0]
            p += size
        samples.append([channels, rate, frames, offset])
    names = []
    if names_size:
        table = header + headers_size
        for i in range(count):
            o = struct.unpack_from("<I", data, table + 4 * i)[0]
            names.append(data[table + o:data.index(b"\0", table + o)].decode("latin-1"))
    base = header + headers_size + names_size
    out = []
    for i, (channels, rate, frames, offset) in enumerate(samples):
        end = samples[i + 1][3] if i + 1 < count else data_size
        name = names[i].split(" ")[0] if names else str(i)
        out.append((name, channels, rate, frames, data[base + offset:base + end]))
    return out


def xma2_wav(channels, rate, frames, raw):
    """An XMA2 RIFF (WAVE_FORMAT_XMA2, 0x166) around FSB5 XMA data: 32 KB blocks, 2 KB packets."""
    streams = (channels + 1) // 2
    mask = {1: 4, 2: 3}.get(channels, 0)
    blocks = max(1, len(raw) // 0x8000)
    extra = struct.pack("<HIIIIIIIBBH", streams, mask, frames, 0x8000, 0, frames, 0, 0, 0, 4, blocks)
    fmt = struct.pack("<HHIIHHH", 0x166, channels, rate, rate * channels * 2, 2048, 16, len(extra)) + extra
    body = b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", len(raw)) + raw
    return b"RIFF" + struct.pack("<I", len(body)) + body


def find_media():
    candidates = [Path("D:/Games/Forza Horizon 2 Presents Fast & Furious/game/base/Mount/media")]
    candidates += sorted((Path.home() / "FH2" / "Forza Horizon 2 Presents Fast & Furious").glob("Spire_*/Mount/media"))
    return next((c for c in candidates if (c / BANK).is_file()), None)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--ffmpeg", type=Path, required=True, help="ffmpeg.exe with the xma2 decoder")
    parser.add_argument("--media", type=Path, help="Fast & Furious' media folder")
    parser.add_argument("--output", type=Path, help="where the WAV files go")
    parser.add_argument("--mods", type=Path, help="the game's mods folder: --output mods/Nitrous/plugins/NitrousSounds")
    options = parser.parse_args()
    media = options.media or find_media()
    if not media or not (media / BANK).is_file():
        raise SystemExit(f"Fast & Furious' {BANK} was not found; give --media")
    output = options.output or (options.mods / "Nitrous/plugins/NitrousSounds" if options.mods else Path("NitrousSounds"))
    output.mkdir(parents=True, exist_ok=True)
    found = {name: rest for name, *rest in fsb5_samples((media / BANK).read_bytes()) if name in SAMPLES}
    missing = sorted(set(SAMPLES) - set(found))
    if missing:
        raise SystemExit(f"{media / BANK} lacks {', '.join(missing)}")
    with tempfile.TemporaryDirectory() as temp:
        for name, target in SAMPLES.items():
            channels, rate, frames, raw = found[name]
            source = Path(temp) / (name + ".xma.wav")
            source.write_bytes(xma2_wav(channels, rate, frames, raw))
            subprocess.run([str(options.ffmpeg), "-v", "error", "-y", "-i", str(source), "-ar", "48000", "-ac", "2",
                            "-c:a", "pcm_s16le", str(output / target)], check=True)
            print(f"{output / target}  ({name}: {channels} ch, {rate} Hz, {frames / rate:.2f} s)")


if __name__ == "__main__":
    sys.exit(main())
