"""Draws Custom Radio's station logos (the art in plugins/custom-radio/logo/).

Usage: py plugins/custom-radio/tools/make_logo.py [--output <folder>] [--preview <png>]

Needs Pillow and Windows' Bahnschrift font; the build does not run this, it
converts the committed PNGs (build.py). The logos are original artwork: a
record with a single eighth note in front of it, and the station name in white
beside it, on a transparent 196x96 canvas like the game's own station logos,
in their art area (x 2..188, y 2..89: room for the drop shadow the build adds,
as the game's logos have; straight alpha here, the build premultiplies it).

  my_music.png     MY MUSIC     (the default name)
  moja_muzyka.png  MOJA MUZYKA  (the default name with Polish game text)
  moya_muzyka.png  МОЯ МУЗИКА   (the default name with Language Support's
                                 Ukrainian pack in the ru-RU slot)
  badge.png        the badge alone, right-aligned (any other station name)
"""

import argparse
from pathlib import Path

import math

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

HERE = Path(__file__).resolve().parent
WIDTH, HEIGHT = 196, 96
# The art area of the game's logos: their art ends ~7 px before the right
# edge and ~7 px above the bottom, where the drop shadow the build adds
# (tools/logo_texture.py shadow, 2 px right, 3 px down, blurred) still fits.
ART_LEFT, ART_TOP, ART_RIGHT, ART_BOTTOM = 2, 2, 188, 89
SCALE = 8  # drawn at 8x, then reduced: smooth edges
FONT = Path(r"C:\Windows\Fonts\bahnschrift.ttf")
# The badge's label and accent bar: the yellow of the HUD's track title.
ACCENT = (255, 226, 0)
WHITE = (255, 255, 255)


def ellipse_points(cx, cy, rx, ry, angle, steps=96):
    a = math.radians(angle)
    points = []
    for i in range(steps):
        t = 2 * math.pi * i / steps
        x, y = rx * math.cos(t), ry * math.sin(t)
        points.append((cx + x * math.cos(a) - y * math.sin(a), cy + x * math.sin(a) + y * math.cos(a)))
    return points


def bezier(p0, p1, p2, p3, steps=40):
    out = []
    for i in range(steps + 1):
        t = i / steps
        u = 1 - t
        out.append((u ** 3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t ** 3 * p3[0],
                    u ** 3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t ** 3 * p3[1]))
    return out


def note_mask(size):
    """A single eighth note (head, stem, flag) as a mask, in badge units of size/100."""
    u = size / 100.0
    mask = Image.new("L", (size, size), 0)
    draw = ImageDraw.Draw(mask)
    stem_right, stem_width = 80 * u, 6 * u
    head_y = 80 * u
    draw.polygon(ellipse_points(stem_right - 10.5 * u, head_y, 13 * u, 9.2 * u, -24), fill=255)
    draw.rectangle((stem_right - stem_width, 14 * u, stem_right, head_y - 2 * u), fill=255)
    # The flag: out from the top of the stem, curling down to the right.
    outer = bezier((stem_right, 14 * u), (stem_right + 4 * u, 30 * u), (stem_right + 20 * u, 32 * u),
                   (stem_right + 15 * u, 56 * u))
    inner = bezier((stem_right + 11 * u, 52 * u), (stem_right + 14 * u, 38 * u), (stem_right + 2 * u, 34 * u),
                   (stem_right, 30 * u))
    draw.polygon(outer + inner, fill=255)
    return mask


def badge(size):
    """The badge at `size` px: a record (white rim, grooves, yellow label) with a white eighth note in front."""
    u = size / 100.0
    art = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(art)
    disc = (2 * u, 2 * u, 78 * u, 78 * u)  # the record, up and left; the note overlaps its lower right
    cx, cy = 40 * u, 40 * u
    draw.ellipse(disc, outline=WHITE, width=round(7 * u))
    for radius, width in ((27 * u, 2.2 * u), (21 * u, 2.2 * u)):
        draw.arc((cx - radius, cy - radius, cx + radius, cy + radius), 200, 340, fill=WHITE, width=round(width))
        draw.arc((cx - radius, cy - radius, cx + radius, cy + radius), 20, 160, fill=WHITE, width=round(width))
    draw.ellipse((cx - 13 * u, cy - 13 * u, cx + 13 * u, cy + 13 * u), fill=ACCENT + (255,))
    draw.ellipse((cx - 3 * u, cy - 3 * u, cx + 3 * u, cy + 3 * u), fill=(0, 0, 0, 0))
    # The note, with a transparent gap around it where it covers the record.
    note = note_mask(size)
    gap = note.filter(ImageFilter.MaxFilter(_odd(7 * u)))
    art.putalpha(ImageChops.subtract(art.getchannel("A"), gap))
    white = Image.new("RGBA", (size, size), WHITE + (255,))
    white.putalpha(note)
    art.alpha_composite(white)
    return art


def _odd(value):
    value = max(3, int(round(value)))
    return value if value % 2 else value + 1


def font(size, variation):
    face = ImageFont.truetype(str(FONT), size)
    face.set_variation_by_name(variation)
    return face


def fit_text(draw, text, variation, width, height):
    """The largest font of `variation` that fits `text` into width x height (ink box)."""
    size = height * 2
    while size > 4:
        face = font(size, variation)
        left, top, right, bottom = draw.textbbox((0, 0), text, font=face)
        if right - left <= width and bottom - top <= height:
            return face, (left, top, right, bottom)
        size -= 1
    raise ValueError(text)


def logo(top_word, bottom_word):
    """Badge on the left, the two words stacked on the right: the small one above the big one."""
    s = SCALE
    canvas = Image.new("RGBA", (WIDTH * s, HEIGHT * s), (0, 0, 0, 0))
    size = 84 * s
    middle = (ART_TOP + ART_BOTTOM) * s // 2
    canvas.alpha_composite(badge(size), (ART_LEFT * s, middle - size // 2))
    draw = ImageDraw.Draw(canvas)
    text_left, text_right = 92 * s, ART_RIGHT * s
    width = text_right - text_left
    big, big_box = fit_text(draw, bottom_word, b"Bold Condensed", width, 40 * s)
    small, small_box = fit_text(draw, top_word, b"Bold SemiCondensed", width, 19 * s)
    big_height, small_height = big_box[3] - big_box[1], small_box[3] - small_box[1]
    gap = 7 * s
    top = middle - (small_height + gap + big_height) // 2
    # Right-aligned, like the game's own logos.
    draw.text((text_right - (small_box[2] - small_box[0]) - small_box[0], top - small_box[1]), top_word,
              font=small, fill=WHITE)
    draw.text((text_right - (big_box[2] - big_box[0]) - big_box[0], top + small_height + gap - big_box[1]),
              bottom_word, font=big, fill=WHITE)
    # An accent bar under the small word, from the badge to the text's right edge.
    bar_y = top + small_height + gap // 2 - s
    small_left = text_right - (small_box[2] - small_box[0])
    draw.rectangle((text_left, bar_y, small_left - 4 * s, bar_y + 2 * s), fill=ACCENT + (255,))
    return canvas.resize((WIDTH, HEIGHT), Image.LANCZOS)


def badge_logo():
    """The badge alone at the right edge (other station names)."""
    s = SCALE
    canvas = Image.new("RGBA", (WIDTH * s, HEIGHT * s), (0, 0, 0, 0))
    size = 84 * s
    canvas.alpha_composite(badge(size), (ART_RIGHT * s - size, (ART_TOP + ART_BOTTOM) * s // 2 - size // 2))
    return canvas.resize((WIDTH, HEIGHT), Image.LANCZOS)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", type=Path, default=HERE.parent / "logo")
    parser.add_argument("--preview", type=Path, help="also write the logos on a dark HUD-like background")
    options = parser.parse_args()
    options.output.mkdir(parents=True, exist_ok=True)
    images = {"my_music.png": logo("MY", "MUSIC"), "moja_muzyka.png": logo("MOJA", "MUZYKA"),
              "moya_muzyka.png": logo("МОЯ", "МУЗИКА"), "badge.png": badge_logo()}
    for name, image in images.items():
        image.save(options.output / name, optimize=True)
        print(options.output / name)
    if options.preview:
        sheet = Image.new("RGBA", (WIDTH * 2 + 48, (HEIGHT + 16) * len(images) + 16), (18, 20, 24, 255))
        for row, image in enumerate(images.values()):
            sheet.alpha_composite(image, (16, 16 + row * (HEIGHT + 16)))
            sheet.alpha_composite(image.resize((WIDTH, HEIGHT)), (WIDTH + 32, 16 + row * (HEIGHT + 16)))
        sheet.save(options.preview)
        print(options.preview)


if __name__ == "__main__":
    main()
