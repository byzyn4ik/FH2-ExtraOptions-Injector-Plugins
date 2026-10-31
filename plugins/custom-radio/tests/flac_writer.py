"""A minimal FLAC writer for the tests: verbatim (uncompressed) subframes, any channel count up to 8,
8/16/24-bit samples, a STREAMINFO and an optional VORBIS_COMMENT block. Valid FLAC that any decoder
reads (Windows' Media Foundation FLAC decoder in the tests)."""
# SPDX-License-Identifier: GPL-3.0-or-later

import struct

BLOCK = 256


def crc8(data):
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def crc16(data):
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x8005) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def utf8_number(value):
    if value < 0x80:
        return bytes([value])
    if value < 0x800:
        return bytes([0xC0 | value >> 6, 0x80 | value & 0x3F])
    if value < 0x10000:
        return bytes([0xE0 | value >> 12, 0x80 | value >> 6 & 0x3F, 0x80 | value & 0x3F])
    return bytes([0xF0 | value >> 18, 0x80 | value >> 12 & 0x3F, 0x80 | value >> 6 & 0x3F, 0x80 | value & 0x3F])


def flac(channels, rate, bits, comments=None):
    """FLAC bytes for `channels` (lists of signed `bits`-bit samples, all the same length)."""
    count = len(channels[0])
    size_code = {8: 1, 16: 4, 24: 6}[bits]
    info = struct.pack(">HH", BLOCK, BLOCK) + b"\0\0\0\0\0\0"
    packed = rate << 44 | (len(channels) - 1) << 41 | (bits - 1) << 36 | count
    info += packed.to_bytes(8, "big") + bytes(16)
    blocks = [(0, info)]
    if comments is not None:
        body = struct.pack("<I", 4) + b"test" + struct.pack("<I", len(comments))
        for entry in comments:
            data = entry.encode("utf-8")
            body += struct.pack("<I", len(data)) + data
        blocks.append((4, body))
    out = bytearray(b"fLaC")
    for i, (kind, body) in enumerate(blocks):
        out += bytes([kind | (0x80 if i == len(blocks) - 1 else 0)]) + len(body).to_bytes(3, "big") + body
    width = bits // 8
    for frame, start in enumerate(range(0, count, BLOCK)):
        n = min(BLOCK, count - start)
        header = bytearray([0xFF, 0xF8, 0x60, (len(channels) - 1) << 4 | size_code << 1])
        header += utf8_number(frame) + bytes([n - 1])
        header.append(crc8(header))
        body = bytearray(header)
        for channel in channels:
            body.append(0x02)  # verbatim subframe, no wasted bits
            for sample in channel[start:start + n]:
                body += (sample & ((1 << bits) - 1)).to_bytes(width, "big")
        body += struct.pack(">H", crc16(body))
        out += body
    return bytes(out)
