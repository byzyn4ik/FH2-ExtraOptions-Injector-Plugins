// Custom Radio for Forza Horizon 2 Extra Options: the station logo texture.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CUSTOM_RADIO_TEXTURE_H
#define CUSTOM_RADIO_TEXTURE_H
#include "support.h"

// The game's radio logos are 196x96 swatchbin textures (the HUD's station
// icon is a 196x96 FZMaterial). The plugin writes them as BC7 2D-tiled
// swatchbins with premultiplied alpha, stored exactly as the game's own radio
// logos (format 0x2e); build.py writes the built-in logos the same way.
enum {
    LOGO_WIDTH = 196,
    LOGO_HEIGHT = 96,
    LOGO_BLOCKS_WIDE = 64,  // BC7 4x4 blocks: the width (49 blocks) padded to 64
    LOGO_BLOCKS_HIGH = 32,  // the height (24 blocks) padded to 32
    SWATCH_HEADER = 83,
    LOGO_SWATCH_SIZE = SWATCH_HEADER + LOGO_BLOCKS_WIDE * LOGO_BLOCKS_HIGH * 16,
    LOGO_MIN_SIDE = 8,
    LOGO_MAX_SIDE = 4096,
    LOGO_MAX_FILE = 32 * 1024 * 1024,
    // The art area of the game's logos (room for their drop shadow below and
    // right): a PNG of another size than 196x96 is fitted into x 1..188, y 2..88.
    LOGO_ART_RIGHT = 189,
    LOGO_ART_TOP = 2,
    LOGO_ART_WIDTH = 188,
    LOGO_ART_HEIGHT = 87
};

// `pixels`: LOGO_WIDTH x LOGO_HEIGHT premultiplied RGBA, rows top down.
// `output`: LOGO_SWATCH_SIZE bytes.
void texture_encode(const unsigned char *pixels, unsigned char *output);

// Adds the game's logo drop shadow (black, 2 px right, 3 px down, blurred,
// 30%) under the art of `pixels` (LOGO_WIDTH x LOGO_HEIGHT premultiplied RGBA).
void texture_shadow(unsigned char *pixels);

// Reads a PNG (Windows Imaging Component) and makes it the logo: a 196x96
// PNG as it is, any other size scaled with its aspect kept into the art area
// (against its right edge, centred in it vertically), like the game's own
// logos; then the drop shadow. Writes `output` (LOGO_SWATCH_SIZE bytes). NULL on success, else why the file is refused; `width`/`height`
// get the PNG's size when it was read.
const char *texture_from_png(const wchar_t *path, unsigned char *output, unsigned *width, unsigned *height);
#endif
