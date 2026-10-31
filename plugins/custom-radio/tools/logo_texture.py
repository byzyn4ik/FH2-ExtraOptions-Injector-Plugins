"""Custom Radio's station logos as game textures (no third-party modules).

The game's radio logos are 196x96 swatchbin textures in BC7 (format 0x2e,
2D-tiled, one mip, premultiplied alpha), in Forza Horizon 2 and Fast &
Furious alike; the game draws an RGBA8 texture of that size as a white box.
The logos here are written exactly like the game's own: the same header and
block layout, the same baked drop shadow, BC7 mode 6 blocks (src/texture.c
writes Logo= and the built-in logos the same way, byte for byte; the tests
compare the two).

read_png(path)          -> (width, height, straight RGBA bytes); 8-bit RGB or RGBA PNGs
premultiply(rgba)       -> premultiplied RGBA bytes
shadow(rgba, w, h)      -> premultiplied RGBA with the game's logo drop shadow added
encode(rgba, w, h)      -> BC7 logo swatchbin bytes (premultiplied RGBA in; at most 256x128)
decode(data)            -> (width, height, premultiplied RGBA bytes) of a BC7 logo swatchbin (mode 6 blocks)
encode_rgba8, decode_rgba8: the same for the game's RGBA8 UI textures
"""

from pathlib import Path
import struct
import zlib

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
HEADER_SIZE = 83
FORMAT_RGBA8_2D = 0xAE
# The header of the game's RGBA8 UI textures (for example its Infographics
# icons) with the sizes and dimensions zeroed: "burG" file, "BCXT" texture and
# "HCXT" layout chunks, one mip level, format 0xae.
HEADER = bytes.fromhex(
    "62757247" "01000100" "53000000" "00000000"
    "42435854" "02000100" "28000000" "53000000"
    "00000000" "00000000" "48435854" "30020800"
    "00000000" "00000000" "00000000" "00000001"
    "00000000" "01000000" "00000100" "010100ae"
    "010098")
assert len(HEADER) == HEADER_SIZE


def read_png(path):
    """An 8-bit, non-interlaced RGB or RGBA PNG: (width, height, RGBA bytes, straight alpha)."""
    data = Path(path).read_bytes()
    if data[:8] != PNG_SIGNATURE:
        raise ValueError(f"{path}: not a PNG file")
    at, chunks, header = 8, [], None
    while at < len(data):
        length, kind = struct.unpack_from(">I4s", data, at)
        body = data[at + 8:at + 8 + length]
        at += 12 + length
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            chunks.append(body)
        elif kind == b"IEND":
            break
    width, height, depth, colour, _, _, interlace = header
    if depth != 8 or colour not in (2, 6) or interlace:
        raise ValueError(f"{path}: only 8-bit RGB/RGBA non-interlaced PNGs are read here")
    channels = 4 if colour == 6 else 3
    raw = zlib.decompress(b"".join(chunks))
    stride = width * channels
    rows, previous, at = [], bytearray(stride), 0
    for _ in range(height):
        kind, line = raw[at], bytearray(raw[at + 1:at + 1 + stride])
        at += 1 + stride
        for i in range(stride):
            left = line[i - channels] if i >= channels else 0
            up = previous[i]
            corner = previous[i - channels] if i >= channels else 0
            if kind == 1:
                line[i] = (line[i] + left) & 255
            elif kind == 2:
                line[i] = (line[i] + up) & 255
            elif kind == 3:
                line[i] = (line[i] + (left + up) // 2) & 255
            elif kind == 4:
                p = left + up - corner
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - corner)
                line[i] = (line[i] + (left if pa <= pb and pa <= pc else up if pb <= pc else corner)) & 255
        rows.append(bytes(line))
        previous = line
    pixels = b"".join(rows)
    if channels == 3:
        pixels = b"".join(pixels[i:i + 3] + b"\xff" for i in range(0, len(pixels), 3))
    return width, height, pixels


def premultiply(rgba):
    out = bytearray(rgba)
    for i in range(0, len(out), 4):
        alpha = out[i + 3]
        for c in range(3):
            out[i + c] = (out[i + c] * alpha + 127) // 255
    return bytes(out)


# The game's radio logos carry a soft black drop shadow baked into the texture
# (fitted against their art: offset 2 right and 3 down, a Gaussian blur of
# sigma ~1.75, 30% opacity), which keeps the white art readable over a bright
# road or sky. The plugin's logos get the same shadow (src/texture.c
# texture_shadow does exactly this, byte for byte).
SHADOW_DX, SHADOW_DY, SHADOW_OPACITY = 2, 3, 77
SHADOW_KERNEL = (1, 5, 15, 33, 54, 64, 54, 33, 15, 5, 1)  # sum 280
# The art area of the game's logos, which leaves room for the shadow: a PNG of
# another size than 196x96 is fitted into x 1..188, y 2..88 (src/texture.c).
ART_RIGHT, ART_TOP, ART_WIDTH, ART_HEIGHT = 189, 2, 188, 87


def shadow(rgba, width, height):
    """Premultiplied RGBA with the game's logo drop shadow (black) under it."""
    radius = len(SHADOW_KERNEL) // 2
    total = sum(SHADOW_KERNEL) ** 2 * 255
    alpha = rgba[3::4]
    across = [0] * (width * height)
    for y in range(height - SHADOW_DY):
        for x in range(width):
            s = 0
            for i, k in enumerate(SHADOW_KERNEL):
                sx = x + i - radius - SHADOW_DX
                if 0 <= sx < width:
                    s += k * alpha[y * width + sx]
            across[(y + SHADOW_DY) * width + x] = s
    out = bytearray(rgba)
    for y in range(height):
        for x in range(width):
            v = 0
            for j, k in enumerate(SHADOW_KERNEL):
                sy = y + j - radius
                if 0 <= sy < height:
                    v += k * across[sy * width + x]
            s = (v * SHADOW_OPACITY + total // 2) // total
            a = out[(y * width + x) * 4 + 3]
            out[(y * width + x) * 4 + 3] = a + (s * (255 - a) + 127) // 255
    return bytes(out)


def _bit(value, n):
    return (value >> n) & 1


def tiled_index(x, y, pitch):
    """The element index of pixel x, y (GCN 2D thin tiling, as src/texture.c)."""
    micro = (_bit(x, 0) | _bit(y, 0) << 1 | _bit(x, 1) << 2 | _bit(y, 1) << 3 | _bit(x, 2) << 4 | _bit(y, 2) << 5)
    pipe = (_bit(x, 3) ^ _bit(x, 4) ^ _bit(y, 3)) | (_bit(x, 4) ^ _bit(y, 4)) << 1 | (_bit(x, 5) ^ _bit(y, 5)) << 2
    bank = (_bit(x, 6) ^ _bit(y, 4) ^ _bit(y, 5)) | (_bit(x, 7) ^ _bit(y, 3)) << 1
    tile = (y >> 5) * (pitch >> 6) + (x >> 6)
    return tile * 2048 + (micro | (pipe | bank << 3) * 64)


def padded(width, height):
    return -(-width // 64) * 64, -(-height // 32) * 32


def encode_rgba8(rgba, width, height):
    """An RGBA8 2D-tiled swatchbin (the game's RGBA8 UI textures; the tests write the game's back with it)."""
    pitch, rows = padded(width, height)
    data = bytearray(pitch * rows * 4)
    for y in range(height):
        for x in range(width):
            at = tiled_index(x, y, pitch) * 4
            data[at:at + 4] = rgba[(y * width + x) * 4:(y * width + x) * 4 + 4]
    header = bytearray(HEADER)
    struct.pack_into("<I", header, 0x0C, HEADER_SIZE + len(data))
    struct.pack_into("<II", header, 0x20, len(data), len(data))
    struct.pack_into("<HH", header, 0x40, width, height)
    struct.pack_into("<HH", header, 0x46, width, height)
    return bytes(header) + bytes(data)


def decode_rgba8(data):
    """An RGBA8 2D-tiled swatchbin (a single mip): (width, height, premultiplied RGBA)."""
    if data[:4] != b"burG" or data[0x4F] != FORMAT_RGBA8_2D:
        raise ValueError("not an RGBA8 2D-tiled swatchbin")
    width, height = struct.unpack_from("<HH", data, 0x40)
    pitch, _ = padded(width, height)
    tiles = data[HEADER_SIZE:]
    out = bytearray(width * height * 4)
    for y in range(height):
        for x in range(width):
            at = tiled_index(x, y, pitch) * 4
            out[(y * width + x) * 4:(y * width + x) * 4 + 4] = tiles[at:at + 4]
    return width, height, bytes(out)


# ------------------------------------------------------------------ BC7 logos

FORMAT_BC7_2D = 0x2E
# The header of the game's radio logos (HUD/HUD_RadioIcon_XS.swatchbin) with the sizes and dimensions zeroed.
HEADER_BC7 = HEADER[:0x4F] + bytes([FORMAT_BC7_2D, 0x01, 0x00, 0xB8])
WEIGHTS = [0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64]


def block_index(x, y):
    """The block index of block x, y (GCN 2D thin tiling of 16-byte elements, as src/texture.c)."""
    micro = (_bit(x, 0) | _bit(y, 0) << 1 | _bit(x, 1) << 2 | _bit(y, 1) << 3 | _bit(x, 2) << 4 | _bit(y, 2) << 5)
    pipe = (_bit(x, 3) ^ _bit(x, 4) ^ _bit(y, 3)) | (_bit(x, 4) ^ _bit(y, 4)) << 1 | (_bit(x, 5) ^ _bit(y, 5)) << 2
    bank = (_bit(x, 6) ^ _bit(y, 4) ^ _bit(y, 5)) | (_bit(x, 7) ^ _bit(y, 3)) << 1
    return (micro & 15) | (pipe | bank << 3) << 4 | (micro >> 4) << 9


def _quantise(value, p):
    return max(0, min(127, (value - p + 1) >> 1))


def _assign(pixels, low, high):
    """The best of the four p-bit choices for endpoints low, high: (error, a, b, p0, p1, indices)."""
    best = None
    for p0 in (0, 1):
        for p1 in (0, 1):
            a = [_quantise(v, p0) for v in low]
            b = [_quantise(v, p1) for v in high]
            palette = [[((64 - w) * (a[c] << 1 | p0) + w * (b[c] << 1 | p1) + 32) >> 6 for c in range(4)]
                       for w in WEIGHTS]
            index, error = [], 0
            for pixel in pixels:
                errors = [sum((entry[c] - pixel[c]) ** 2 for c in range(4)) for entry in palette]
                nearest = errors.index(min(errors))
                index.append(nearest)
                error += errors[nearest]
            if best is None or error < best[0]:
                best = (error, a, b, p0, p1, index)
    return best


def _least_squares(pixels, index):
    """The endpoints that fit `pixels` best for these indices (integer least squares), or None."""
    a = b = c = 0
    x0, x1 = [0] * 4, [0] * 4
    for pixel, k in zip(pixels, index):
        w = WEIGHTS[k]
        u = 64 - w
        a, b, c = a + u * u, b + u * w, c + w * w
        for ch in range(4):
            x0[ch] += u * pixel[ch]
            x1[ch] += w * pixel[ch]
    det = a * c - b * b
    if det <= 0:
        return None

    def solve(numerator):
        return 0 if numerator <= 0 else min(255, (numerator + det // 2) // det)

    return ([solve(64 * (c * x0[ch] - b * x1[ch])) for ch in range(4)],
            [solve(64 * (a * x1[ch] - b * x0[ch])) for ch in range(4)])


def encode_block(pixels):
    """16 premultiplied RGBA tuples -> a BC7 mode 6 block (as src/texture.c encode_block).

    Endpoint candidates: the per-channel minimum and maximum, and the two pixels
    farthest apart (the first such pair); the better one, then refined twice by
    least squares while that lowers the error."""
    low = [min(p[c] for p in pixels) for c in range(4)]
    high = [max(p[c] for p in pixels) for c in range(4)]
    best = _assign(pixels, low, high)
    far, pair = -1, (0, 0)
    for i in range(16):
        for j in range(i + 1, 16):
            d = sum((pixels[i][c] - pixels[j][c]) ** 2 for c in range(4))
            if d > far:
                far, pair = d, (i, j)
    trial = _assign(pixels, list(pixels[pair[0]]), list(pixels[pair[1]]))
    if trial[0] < best[0]:
        best = trial
    for _ in range(2):
        ends = _least_squares(pixels, best[5])
        if ends is None:
            break
        trial = _assign(pixels, *ends)
        if trial[0] >= best[0]:
            break
        best = trial
    _, a, b, p0, p1, index = best
    if index[0] >= 8:
        a, b, p0, p1, index = b, a, p1, p0, [15 - i for i in index]
    value, at = 0x40, 7
    fields = []
    for c in range(4):
        fields += [(a[c], 7), (b[c], 7)]
    fields += [(p0, 1), (p1, 1)] + [(k, 3 if i == 0 else 4) for i, k in enumerate(index)]
    for field, count in fields:
        value |= field << at
        at += count
    return value.to_bytes(16, "little")


def decode_block(block):
    """A BC7 mode 6 block -> 16 RGBA tuples (None for another mode)."""
    value = int.from_bytes(block, "little")
    if value & 0x7F != 0x40:
        return None
    at = 7

    def take(count):
        nonlocal at
        field = (value >> at) & ((1 << count) - 1)
        at += count
        return field

    ends = [(take(7), take(7)) for _ in range(4)]
    p0, p1 = take(1), take(1)
    out = []
    for i in range(16):
        w = WEIGHTS[take(3 if i == 0 else 4)]
        out.append(tuple(((64 - w) * (e0 << 1 | p0) + w * (e1 << 1 | p1) + 32) >> 6 for e0, e1 in ends))
    return out


def encode(rgba, width, height):
    """Premultiplied RGBA (width x height, at most 256x128) -> a BC7 logo swatchbin."""
    blocks_wide, blocks_high = 64, 32
    assert width <= blocks_wide * 4 and height <= blocks_high * 4
    data = bytearray(blocks_wide * blocks_high * 16)
    for by in range(-(-height // 4)):
        for bx in range(-(-width // 4)):
            pixels = []
            for i in range(16):
                x, y = bx * 4 + i % 4, by * 4 + i // 4
                pixels.append(tuple(rgba[(y * width + x) * 4:(y * width + x) * 4 + 4]) if x < width and y < height
                              else (0, 0, 0, 0))
            at = block_index(bx, by) * 16
            data[at:at + 16] = encode_block(pixels)
    header = bytearray(HEADER_BC7)
    struct.pack_into("<I", header, 0x0C, HEADER_SIZE + len(data))
    struct.pack_into("<II", header, 0x20, len(data), len(data))
    struct.pack_into("<HH", header, 0x40, width, height)
    struct.pack_into("<HH", header, 0x46, width, height)
    return bytes(header) + bytes(data)


def decode(data):
    """A BC7 logo swatchbin of mode 6 blocks (as encode writes): (width, height, premultiplied RGBA)."""
    if data[:4] != b"burG" or data[0x4F] != FORMAT_BC7_2D:
        raise ValueError("not a BC7 2D-tiled swatchbin")
    width, height = struct.unpack_from("<HH", data, 0x40)
    blocks = data[HEADER_SIZE:]
    out = bytearray(width * height * 4)
    for by in range(-(-height // 4)):
        for bx in range(-(-width // 4)):
            at = block_index(bx, by) * 16
            pixels = decode_block(blocks[at:at + 16])
            if pixels is None:
                raise ValueError(f"block {bx},{by}: not BC7 mode 6")
            for i, pixel in enumerate(pixels):
                x, y = bx * 4 + i % 4, by * 4 + i // 4
                if x < width and y < height:
                    out[(y * width + x) * 4:(y * width + x) * 4 + 4] = bytes(pixel)
    return width, height, bytes(out)


# --------------------------------------------- any BC7 block (the game's logos)

# The game's own logos use every BC7 mode; the tests read them with this
# (BC7's partition and anchor tables, as in the format's specification).
_PARTITIONS2 = (
    0xCCCC, 0x8888, 0xEEEE, 0xECC8, 0xC880, 0xFEEC, 0xFEC8, 0xEC80, 0xC800, 0xFFEC, 0xFE80, 0xE800, 0xFFE8, 0xFF00,
    0xFFF0, 0xF000, 0xF710, 0x008E, 0x7100, 0x08CE, 0x008C, 0x7310, 0x3100, 0x8CCE, 0x088C, 0x3110, 0x6666, 0x366C,
    0x17E8, 0x0FF0, 0x718E, 0x399C, 0xAAAA, 0xF0F0, 0x5A5A, 0x33CC, 0x3C3C, 0x55AA, 0x9696, 0xA55A, 0x73CE, 0x13C8,
    0x324C, 0x3BDC, 0x6996, 0xC33C, 0x9966, 0x0660, 0x0272, 0x04E4, 0x4E40, 0x2720, 0xC936, 0x936C, 0x39C6, 0x639C,
    0x9336, 0x9CC6, 0x817E, 0xE718, 0xCCF0, 0x0FCC, 0x7744, 0xEE22)
_PARTITIONS3 = (
    "0011001102210222", "0001001122112221", "0000200122112211", "0222002200110111", "0000000011221122",
    "0011001100220022", "0022002200111111", "0011001122112211", "0000000011112222", "0000111111112222",
    "0000111122222222", "0012001200120012", "0112011201120112", "0122012201220122", "0011011211221222",
    "0011200122002220", "0001001101120112", "0111001120012200", "0000112211221122", "0222002200220111",
    "0001000122212221", "0000001101220122", "0001001102220222", "0000110012201220", "0122001100110000",
    "0012001101120000", "0110122112210110", "0000011012211221", "0022110211020022", "0110011020022002",
    "0011012201220011", "0000200022112221", "0000000211221222", "0222002200120011", "0011001200220222",
    "0120012001200120", "0000111122220000", "0120120120120120", "0120201212010201", "0011220011220011",
    "0011112222000011", "0101010122222222", "0000000021212121", "0022112200221122", "0022001100220011",
    "0220122102201221", "0101222222220101", "0000212121212121", "0101010101012222", "0222011102220111",
    "0002111200021112", "0000211221122112", "0222011101110222", "0002111211120002", "0110011001102222",
    "0000000021122112", "0110011022222222", "0022001100110022", "0022112211220022", "0000000000002112",
    "0002000100020001", "0222122202221222", "0101222222222222", "0111201122012220")
_ANCHOR2 = (15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 2, 8, 2, 2, 8, 8, 15, 2, 8, 2, 2, 8, 8,
            2, 2, 15, 15, 6, 8, 2, 8, 15, 15, 2, 8, 2, 2, 2, 15, 15, 6, 6, 2, 6, 8, 15, 15, 2, 2, 15, 15, 15, 15, 15,
            2, 2, 15)
_ANCHOR3A = (3, 3, 15, 15, 8, 3, 15, 15, 8, 8, 6, 6, 6, 5, 3, 3, 3, 3, 8, 15, 3, 3, 6, 10, 5, 8, 8, 6, 8, 5, 15, 15,
             8, 15, 3, 5, 6, 10, 8, 15, 15, 3, 15, 5, 15, 15, 15, 15, 3, 15, 5, 5, 5, 8, 5, 10, 5, 10, 8, 13, 15, 12,
             3, 3)
_ANCHOR3B = (15, 8, 8, 3, 15, 15, 3, 8, 15, 15, 15, 15, 15, 15, 15, 8, 15, 8, 15, 3, 15, 8, 15, 8, 3, 15, 6, 10, 15,
             15, 10, 8, 15, 3, 15, 10, 10, 8, 9, 10, 6, 15, 8, 15, 3, 6, 6, 8, 15, 3, 15, 15, 15, 15, 15, 15, 15, 15, 15,
             15, 3, 15, 15, 8)
_INDEX_WEIGHTS = {2: (0, 21, 43, 64), 3: (0, 9, 18, 27, 37, 46, 55, 64), 4: tuple(WEIGHTS)}
# subsets, partition bits, rotation bits, index selection bits, colour bits, alpha bits,
# a p-bit per endpoint, a p-bit per subset, index bits, second index bits
_MODES = ((3, 4, 0, 0, 4, 0, 1, 0, 3, 0), (2, 6, 0, 0, 6, 0, 0, 1, 3, 0), (3, 6, 0, 0, 5, 0, 0, 0, 2, 0),
          (2, 6, 0, 0, 7, 0, 1, 0, 2, 0), (1, 0, 2, 1, 5, 6, 0, 0, 2, 3), (1, 0, 2, 0, 7, 8, 0, 0, 2, 2),
          (1, 0, 0, 0, 7, 7, 1, 0, 4, 0), (2, 6, 0, 0, 5, 5, 1, 0, 2, 0))


def decode_any_block(block):
    """Any BC7 block -> 16 RGBA tuples (a reserved mode: transparent black)."""
    value = int.from_bytes(block, "little")
    at = 0

    def take(count):
        nonlocal at
        field = (value >> at) & ((1 << count) - 1)
        at += count
        return field

    mode = 0
    while mode < 8 and not take(1):
        mode += 1
    if mode == 8:
        return [(0, 0, 0, 0)] * 16
    subsets, partition_bits, rotation_bits, select_bits, colour_bits, alpha_bits, end_p, shared_p, bits1, bits2 = \
        _MODES[mode]
    partition, rotation, select = take(partition_bits), take(rotation_bits), take(select_bits)
    count = subsets * 2
    ends = [[0, 0, 0, 255] for _ in range(count)]
    for c in range(3):
        for e in range(count):
            ends[e][c] = take(colour_bits)
    if alpha_bits:
        for e in range(count):
            ends[e][3] = take(alpha_bits)
    if end_p:
        pbits = [take(1) for _ in range(count)]
    elif shared_p:
        shared = [take(1) for _ in range(subsets)]
        pbits = [shared[e // 2] for e in range(count)]
    else:
        pbits = None
    for e in range(count):
        for c in range(4 if alpha_bits else 3):
            v, n = ends[e][c], colour_bits if c < 3 else alpha_bits
            if pbits is not None:
                v, n = v << 1 | pbits[e], n + 1
            ends[e][c] = (v << (8 - n)) | (v >> (2 * n - 8))
    if subsets == 1:
        subset, anchors = [0] * 16, (0,)
    elif subsets == 2:
        subset, anchors = [(_PARTITIONS2[partition] >> i) & 1 for i in range(16)], (0, _ANCHOR2[partition])
    else:
        subset = [int(k) for k in _PARTITIONS3[partition]]
        anchors = (0, _ANCHOR3A[partition], _ANCHOR3B[partition])
    first = [take(bits1 - (i in anchors)) for i in range(16)]
    second = [take(bits2 - (i == 0)) for i in range(16)] if bits2 else None
    out = []
    for i in range(16):
        e0, e1 = ends[2 * subset[i]], ends[2 * subset[i] + 1]
        if second is None:
            wc = wa = _INDEX_WEIGHTS[bits1][first[i]]
        elif select:
            wc, wa = _INDEX_WEIGHTS[bits2][second[i]], _INDEX_WEIGHTS[bits1][first[i]]
        else:
            wc, wa = _INDEX_WEIGHTS[bits1][first[i]], _INDEX_WEIGHTS[bits2][second[i]]
        pixel = [((64 - wc) * e0[c] + wc * e1[c] + 32) >> 6 for c in range(3)]
        pixel.append(((64 - wa) * e0[3] + wa * e1[3] + 32) >> 6)
        if rotation:
            pixel[3], pixel[rotation - 1] = pixel[rotation - 1], pixel[3]
        out.append(tuple(pixel))
    return out


def decode_any(data):
    """A BC7 2D-tiled swatchbin of any BC7 modes (the game's logos): (width, height, premultiplied RGBA)."""
    if data[:4] != b"burG" or data[0x4F] != FORMAT_BC7_2D:
        raise ValueError("not a BC7 2D-tiled swatchbin")
    width, height = struct.unpack_from("<HH", data, 0x40)
    blocks = data[HEADER_SIZE:]
    out = bytearray(width * height * 4)
    for by in range(-(-height // 4)):
        for bx in range(-(-width // 4)):
            at = block_index(bx, by) * 16
            for i, pixel in enumerate(decode_any_block(blocks[at:at + 16])):
                x, y = bx * 4 + i % 4, by * 4 + i // 4
                if x < width and y < height:
                    out[(y * width + x) * 4:(y * width + x) * 4 + 4] = bytes(pixel)
    return width, height, bytes(out)
