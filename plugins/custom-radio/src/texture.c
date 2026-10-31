// Custom Radio for Forza Horizon 2 Extra Options: the station logo texture.
// SPDX-License-Identifier: GPL-3.0-or-later
#define COBJMACROS
#include "texture.h"

#include <wincodec.h>

// --- swatchbin ----------------------------------------------------------------

/* The game's radio logos (media\UI\Textures\Anthem.zip HUD/HUD_RadioIcon_*,
   the same in Forza Horizon 2 and Fast & Furious) are BC7 swatchbins: format
   0x2e (BC7, 2D-tiled), one mip, flags 0xb8, 196x96 stored as 64x32 blocks
   of 16 bytes (32768 bytes; the width padded to 64 blocks, the height to 32),
   premultiplied alpha, a soft drop shadow baked in (texture_shadow). The
   game draws an RGBA8 texture of that size as a white box, so the logos are written exactly like its own: this header, with
   only the pixels different. "burG" file, "BCXT" texture and "HCXT" layout
   chunks; sizes, the width and height (twice), one mip level. */
static const unsigned char header_template[SWATCH_HEADER] = {
    'b', 'u', 'r', 'G', 0x01, 0x00, 0x01, 0x00, 0x53, 0x00, 0x00, 0x00, 0, 0, 0, 0,          // 0x00
    'B', 'C', 'X', 'T', 0x02, 0x00, 0x01, 0x00, 0x28, 0x00, 0x00, 0x00, 0x53, 0x00, 0x00, 0x00,  // 0x10
    0, 0, 0, 0, 0, 0, 0, 0, 'H', 'C', 'X', 'T', 0x30, 0x02, 0x08, 0x00,                        // 0x20
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x00, 0x00, 0x01,                                // 0x30
    0, 0, 0, 0, 0x01, 0x00, 0, 0, 0, 0, 0x01, 0x00, 0x01, 0x01, 0x00, 0x2e,                    // 0x40
    0x01, 0x00, 0xb8};                                                                          // 0x50

static void put_u16(unsigned char *at, unsigned value) {
    at[0] = (unsigned char)value;
    at[1] = (unsigned char)(value >> 8);
}

static void put_u32(unsigned char *at, unsigned value) {
    put_u16(at, value & 0xffff);
    put_u16(at + 2, value >> 16);
}

#define BIT(value, n) (((value) >> (n)) & 1u)

/* The block index of block x, y in the tiled data (GCN 2D thin tiling of
   16-byte elements, read off the game's own logos): 4x4-block groups of 256
   bytes (x0 y0 x1 y1), interleaved by pipe (x3^x4^y3, x4^y4, x5^y5) and bank
   (x6^y4^y5, x7^y3), then the rest of each 8x8-block micro tile (x2 y2). The
   logo's 64x32 blocks are one macro tile. */
static unsigned block_index(unsigned x, unsigned y) {
    unsigned micro = BIT(x, 0) | BIT(y, 0) << 1 | BIT(x, 1) << 2 | BIT(y, 1) << 3 | BIT(x, 2) << 4 | BIT(y, 2) << 5;
    unsigned pipe = (BIT(x, 3) ^ BIT(x, 4) ^ BIT(y, 3)) | (BIT(x, 4) ^ BIT(y, 4)) << 1 | (BIT(x, 5) ^ BIT(y, 5)) << 2;
    unsigned bank = (BIT(x, 6) ^ BIT(y, 4) ^ BIT(y, 5)) | (BIT(x, 7) ^ BIT(y, 3)) << 1;
    return (micro & 15) | (pipe | bank << 3) << 4 | (micro >> 4) << 9;
}

/* BC7 mode 6 (one subset, RGBA endpoints of 7 bits plus a p-bit each, 4-bit
   indices). Endpoint candidates: the block's per-channel minimum and maximum,
   and its two pixels farthest apart (the first such pair); the better one is
   refined by least squares (at most twice, while the error drops). For each
   pair of endpoints the four p-bit choices are tried, each pixel takes the
   nearest of the 16 palette colours (the first on a tie), the smallest total
   error wins. tools/logo_texture.py does exactly the same, so the two write
   the same bytes. */
static const unsigned char weights[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};

static unsigned quantise(unsigned value, unsigned p) {
    int q = ((int)value - (int)p + 1) >> 1;
    return q < 0 ? 0 : q > 127 ? 127 : (unsigned)q;
}

typedef struct {
    uint64_t low, high;
    unsigned at;
} Bits;

static void put_bits(Bits *bits, uint64_t value, unsigned count) {
    for (unsigned i = 0; i < count; ++i, ++bits->at) {
        uint64_t bit = (value >> i) & 1;
        if (bits->at < 64) {
            bits->low |= bit << bits->at;
        } else {
            bits->high |= bit << (bits->at - 64);
        }
    }
}

typedef struct {
    unsigned error, a[4], b[4], p0, p1;
    unsigned char index[16];
} Fit;

// The best of the four p-bit choices for the endpoints low, high.
static void assign(const unsigned char pixels[16][4], const unsigned low[4], const unsigned high[4], Fit *best) {
    best->error = 0xffffffffu;
    for (unsigned p0 = 0; p0 < 2; ++p0) {
        for (unsigned p1 = 0; p1 < 2; ++p1) {
            Fit fit;
            unsigned palette[16][4];
            fit.error = 0;
            fit.p0 = p0;
            fit.p1 = p1;
            for (unsigned c = 0; c < 4; ++c) {
                fit.a[c] = quantise(low[c], p0);
                fit.b[c] = quantise(high[c], p1);
            }
            for (unsigned k = 0; k < 16; ++k) {
                for (unsigned c = 0; c < 4; ++c) {
                    unsigned e0 = fit.a[c] << 1 | p0, e1 = fit.b[c] << 1 | p1;
                    palette[k][c] = ((64 - weights[k]) * e0 + weights[k] * e1 + 32) >> 6;
                }
            }
            for (unsigned i = 0; i < 16; ++i) {
                unsigned nearest = 0, nearest_error = 0xffffffffu;
                for (unsigned k = 0; k < 16; ++k) {
                    unsigned sum = 0;
                    for (unsigned c = 0; c < 4; ++c) {
                        int d = (int)palette[k][c] - (int)pixels[i][c];
                        sum += (unsigned)(d * d);
                    }
                    if (sum < nearest_error) {
                        nearest_error = sum;
                        nearest = k;
                    }
                }
                fit.index[i] = (unsigned char)nearest;
                fit.error += nearest_error;
            }
            if (fit.error < best->error) {
                memory_copy(best, &fit, sizeof(fit));
            }
        }
    }
}

static unsigned solve(int64_t numerator, int64_t det) {
    if (numerator <= 0) {
        return 0;
    }
    int64_t value = (numerator + det / 2) / det;
    return value > 255 ? 255 : (unsigned)value;
}

// The endpoints that fit the pixels best for these indices (integer least squares); 0 when there are none.
static int least_squares(const unsigned char pixels[16][4], const unsigned char index[16], unsigned low[4],
                         unsigned high[4]) {
    int64_t a = 0, b = 0, c = 0, x0[4] = {0, 0, 0, 0}, x1[4] = {0, 0, 0, 0};
    for (unsigned i = 0; i < 16; ++i) {
        int64_t w = weights[index[i]], u = 64 - w;
        a += u * u;
        b += u * w;
        c += w * w;
        for (unsigned ch = 0; ch < 4; ++ch) {
            x0[ch] += u * pixels[i][ch];
            x1[ch] += w * pixels[i][ch];
        }
    }
    int64_t det = a * c - b * b;
    if (det <= 0) {
        return 0;
    }
    for (unsigned ch = 0; ch < 4; ++ch) {
        low[ch] = solve(64 * (c * x0[ch] - b * x1[ch]), det);
        high[ch] = solve(64 * (a * x1[ch] - b * x0[ch]), det);
    }
    return 1;
}

static void encode_block(const unsigned char pixels[16][4], unsigned char *output) {
    unsigned low[4], high[4];
    Fit best, trial;
    for (unsigned c = 0; c < 4; ++c) {
        low[c] = 255;
        high[c] = 0;
        for (unsigned i = 0; i < 16; ++i) {
            low[c] = pixels[i][c] < low[c] ? pixels[i][c] : low[c];
            high[c] = pixels[i][c] > high[c] ? pixels[i][c] : high[c];
        }
    }
    assign(pixels, low, high, &best);
    // The two pixels farthest apart (the first such pair; 0 and 1 when all are equal).
    unsigned distance = 0, first = 0, second = 1;
    int found = 0;
    for (unsigned i = 0; i < 16; ++i) {
        for (unsigned j = i + 1; j < 16; ++j) {
            unsigned d = 0;
            for (unsigned c = 0; c < 4; ++c) {
                int delta = (int)pixels[i][c] - (int)pixels[j][c];
                d += (unsigned)(delta * delta);
            }
            if (!found || d > distance) {
                found = 1;
                distance = d;
                first = i;
                second = j;
            }
        }
    }
    for (unsigned c = 0; c < 4; ++c) {
        low[c] = pixels[first][c];
        high[c] = pixels[second][c];
    }
    assign(pixels, low, high, &trial);
    if (trial.error < best.error) {
        memory_copy(&best, &trial, sizeof(best));
    }
    for (unsigned round = 0; round < 2; ++round) {
        if (!least_squares(pixels, best.index, low, high)) {
            break;
        }
        assign(pixels, low, high, &trial);
        if (trial.error >= best.error) {
            break;
        }
        memory_copy(&best, &trial, sizeof(best));
    }
    if (best.index[0] >= 8) {
        // The first index is stored in 3 bits: swap the endpoints so it fits.
        unsigned swap[4], p = best.p0;
        memory_copy(swap, best.a, sizeof(swap));
        memory_copy(best.a, best.b, sizeof(swap));
        memory_copy(best.b, swap, sizeof(swap));
        best.p0 = best.p1;
        best.p1 = p;
        for (unsigned i = 0; i < 16; ++i) {
            best.index[i] = (unsigned char)(15 - best.index[i]);
        }
    }
    Bits bits = {0, 0, 0};
    put_bits(&bits, 0x40, 7);  // mode 6
    for (unsigned c = 0; c < 4; ++c) {
        put_bits(&bits, best.a[c], 7);
        put_bits(&bits, best.b[c], 7);
    }
    put_bits(&bits, best.p0, 1);
    put_bits(&bits, best.p1, 1);
    for (unsigned i = 0; i < 16; ++i) {
        put_bits(&bits, best.index[i], i ? 4 : 3);
    }
    for (unsigned i = 0; i < 8; ++i) {
        output[i] = (unsigned char)(bits.low >> (8 * i));
        output[8 + i] = (unsigned char)(bits.high >> (8 * i));
    }
}

/* The game's radio logos carry a soft black drop shadow in the texture
   (fitted against their art: 2 px right, 3 px down, a Gaussian blur of sigma
   ~1.75, 30% opacity); it keeps the white art readable over a bright road or
   sky. The same shadow here, in integers (tools/logo_texture.py shadow does
   exactly this): the art's alpha moved, blurred with an 11-tap kernel
   (sum 280) across and down, scaled to 77/255, under the art. */
enum { SHADOW_DX = 2, SHADOW_DY = 3, SHADOW_OPACITY = 77, SHADOW_RADIUS = 5, SHADOW_TAPS = 2 * SHADOW_RADIUS + 1 };
static const unsigned shadow_kernel[SHADOW_TAPS] = {1, 5, 15, 33, 54, 64, 54, 33, 15, 5, 1};

void texture_shadow(unsigned char *pixels) {
    static unsigned across[LOGO_WIDTH * LOGO_HEIGHT];
    const uint64_t total = 280ull * 280ull * 255ull;
    memory_zero(across, sizeof(across));
    for (unsigned y = 0; y + SHADOW_DY < LOGO_HEIGHT; ++y) {
        for (unsigned x = 0; x < LOGO_WIDTH; ++x) {
            unsigned sum = 0;
            for (unsigned i = 0; i < SHADOW_TAPS; ++i) {
                int sx = (int)x + (int)i - SHADOW_RADIUS - SHADOW_DX;
                if (sx >= 0 && sx < LOGO_WIDTH) {
                    sum += shadow_kernel[i] * pixels[(y * LOGO_WIDTH + (unsigned)sx) * 4 + 3];
                }
            }
            across[(y + SHADOW_DY) * LOGO_WIDTH + x] = sum;
        }
    }
    for (unsigned y = 0; y < LOGO_HEIGHT; ++y) {
        for (unsigned x = 0; x < LOGO_WIDTH; ++x) {
            uint64_t sum = 0;
            for (unsigned j = 0; j < SHADOW_TAPS; ++j) {
                int sy = (int)y + (int)j - SHADOW_RADIUS;
                if (sy >= 0 && sy < LOGO_HEIGHT) {
                    sum += (uint64_t)shadow_kernel[j] * across[(unsigned)sy * LOGO_WIDTH + x];
                }
            }
            unsigned s = (unsigned)((sum * SHADOW_OPACITY + total / 2) / total);
            unsigned char *alpha = pixels + (y * LOGO_WIDTH + x) * 4 + 3;
            *alpha = (unsigned char)(*alpha + (s * (255u - *alpha) + 127) / 255);
        }
    }
}

void texture_encode(const unsigned char *pixels, unsigned char *output) {
    unsigned data = LOGO_BLOCKS_WIDE * LOGO_BLOCKS_HIGH * 16;
    unsigned char block[16][4];
    memory_copy(output, header_template, SWATCH_HEADER);
    put_u32(output + 0x0c, SWATCH_HEADER + data);
    put_u32(output + 0x20, data);
    put_u32(output + 0x24, data);
    put_u16(output + 0x40, LOGO_WIDTH);
    put_u16(output + 0x42, LOGO_HEIGHT);
    put_u16(output + 0x46, LOGO_WIDTH);
    put_u16(output + 0x48, LOGO_HEIGHT);
    unsigned char *blocks = output + SWATCH_HEADER;
    memory_zero(blocks, data);  // the padding blocks stay zero, as in the game's logos
    for (unsigned by = 0; by * 4 < LOGO_HEIGHT; ++by) {
        for (unsigned bx = 0; bx * 4 < LOGO_WIDTH; ++bx) {
            for (unsigned i = 0; i < 16; ++i) {
                unsigned x = bx * 4 + i % 4, y = by * 4 + i / 4;
                if (x < LOGO_WIDTH && y < LOGO_HEIGHT) {
                    memory_copy(block[i], pixels + (y * LOGO_WIDTH + x) * 4, 4);
                } else {
                    memory_zero(block[i], 4);
                }
            }
            encode_block((const unsigned char(*)[4])block, blocks + block_index(bx, by) * 16);
        }
    }
}

// --- PNG through the Windows Imaging Component ---------------------------------

// Our own copies of the GUIDs (no uuid library is linked).
static const GUID clsid_imaging_factory = {0xcacaf262, 0x9370, 0x4615, {0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a}};
static const GUID iid_imaging_factory = {0xec5ec8a9, 0xc395, 0x4314, {0x9c, 0x77, 0x54, 0xd7, 0xa9, 0x35, 0xff, 0x70}};
// 32bppPBGRA: premultiplied, the format WIC's scaler works in.
static const GUID format_pbgra = {0x6fddc324, 0x4e03, 0x4bfe, {0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x10}};

typedef HRESULT(WINAPI *CoInitializeExFn)(void *, DWORD);
typedef void(WINAPI *CoUninitializeFn)(void);
typedef HRESULT(WINAPI *CoCreateInstanceFn)(const GUID *, void *, DWORD, const GUID *, void **);

static const char *read_signature(const wchar_t *path) {
    static const unsigned char png[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    unsigned char head[8];
    DWORD got = 0;
    LARGE_INTEGER size;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return "the file was not found or cannot be read";
    }
    int ok = GetFileSizeEx(file, &size) && ReadFile(file, head, sizeof(head), &got, NULL);
    CloseHandle(file);
    if (!ok) {
        return "the file cannot be read";
    }
    if (size.QuadPart > LOGO_MAX_FILE) {
        return "the file is larger than 32 MB";
    }
    if (got != sizeof(head) || !memory_equal(head, png, sizeof(head))) {
        return "it is not a PNG file";
    }
    return NULL;
}

// The fitted, scaled image (premultiplied RGBA) into the 196x96 canvas.
static const char *decode(IWICImagingFactory *factory, const wchar_t *path, unsigned char *canvas, unsigned *width,
                          unsigned *height) {
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *converter = NULL;
    IWICBitmapScaler *scaler = NULL;
    unsigned char *pixels = NULL;
    const char *why = NULL;
    UINT source_width = 0, source_height = 0;
    if (FAILED(IWICImagingFactory_CreateDecoderFromFilename(factory, path, NULL, GENERIC_READ,
                                                            WICDecodeMetadataCacheOnDemand, &decoder)) ||
        FAILED(IWICBitmapDecoder_GetFrame(decoder, 0, &frame)) ||
        FAILED(IWICBitmapFrameDecode_GetSize(frame, &source_width, &source_height))) {
        why = "the PNG could not be decoded";
        goto done;
    }
    *width = source_width;
    *height = source_height;
    if (source_width < LOGO_MIN_SIDE || source_height < LOGO_MIN_SIDE || source_width > LOGO_MAX_SIDE ||
        source_height > LOGO_MAX_SIDE) {
        why = "its size is outside 8x8 to 4096x4096 pixels";
        goto done;
    }
    // 196x96 as it is; else fitted into the art area with the aspect kept (rounded, at least one pixel).
    int exact = source_width == LOGO_WIDTH && source_height == LOGO_HEIGHT;
    unsigned box_width = exact ? LOGO_WIDTH : LOGO_ART_WIDTH, box_height = exact ? LOGO_HEIGHT : LOGO_ART_HEIGHT;
    unsigned fit_width = box_width, fit_height = (source_height * box_width + source_width / 2) / source_width;
    if (fit_height > box_height) {
        fit_height = box_height;
        fit_width = (source_width * box_height + source_height / 2) / source_height;
    }
    fit_width = fit_width ? fit_width : 1;
    fit_height = fit_height ? fit_height : 1;
    // Premultiplied before scaling, so edges blend against transparency.
    GUID scaled_format = {0};
    if (FAILED(IWICImagingFactory_CreateFormatConverter(factory, &converter)) ||
        FAILED(IWICFormatConverter_Initialize(converter, (IWICBitmapSource *)frame, &format_pbgra,
                                              WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom)) ||
        FAILED(IWICImagingFactory_CreateBitmapScaler(factory, &scaler)) ||
        FAILED(IWICBitmapScaler_Initialize(scaler, (IWICBitmapSource *)converter, fit_width, fit_height,
                                           WICBitmapInterpolationModeFant)) ||
        FAILED(IWICBitmapScaler_GetPixelFormat(scaler, &scaled_format)) ||
        !memory_equal(&scaled_format, &format_pbgra, sizeof(GUID))) {
        why = "the PNG could not be converted";
        goto done;
    }
    unsigned stride = fit_width * 4;
    pixels = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)stride * fit_height);
    if (!pixels || FAILED(IWICBitmapScaler_CopyPixels(scaler, NULL, stride, stride * fit_height, pixels))) {
        why = "the PNG could not be converted";
        goto done;
    }
    memory_zero(canvas, LOGO_WIDTH * LOGO_HEIGHT * 4);
    unsigned left = (exact ? LOGO_WIDTH : LOGO_ART_RIGHT) - fit_width,
             top = (exact ? 0 : LOGO_ART_TOP) + (box_height - fit_height) / 2, visible = 0;
    for (unsigned y = 0; y < fit_height; ++y) {
        for (unsigned x = 0; x < fit_width; ++x) {
            const unsigned char *from = pixels + y * stride + x * 4;  // B G R A
            unsigned char *to = canvas + ((top + y) * LOGO_WIDTH + left + x) * 4;
            to[0] = from[2];
            to[1] = from[1];
            to[2] = from[0];
            to[3] = from[3];
            visible |= from[3];
        }
    }
    if (!visible) {
        why = "the image is fully transparent";
    }
done:
    if (pixels) {
        HeapFree(GetProcessHeap(), 0, pixels);
    }
    if (scaler) {
        IWICBitmapScaler_Release(scaler);
    }
    if (converter) {
        IWICFormatConverter_Release(converter);
    }
    if (frame) {
        IWICBitmapFrameDecode_Release(frame);
    }
    if (decoder) {
        IWICBitmapDecoder_Release(decoder);
    }
    return why;
}

const char *texture_from_png(const wchar_t *path, unsigned char *output, unsigned *width, unsigned *height) {
    *width = *height = 0;
    const char *why = read_signature(path);
    if (why) {
        return why;
    }
    HMODULE ole = LoadLibraryW(L"ole32.dll");
    CoInitializeExFn initialize = ole ? (CoInitializeExFn)(void *)GetProcAddress(ole, "CoInitializeEx") : NULL;
    CoUninitializeFn uninitialize = ole ? (CoUninitializeFn)(void *)GetProcAddress(ole, "CoUninitialize") : NULL;
    CoCreateInstanceFn create = ole ? (CoCreateInstanceFn)(void *)GetProcAddress(ole, "CoCreateInstance") : NULL;
    if (!initialize || !uninitialize || !create) {
        return "the Windows Imaging Component is not available";
    }
    HRESULT com = initialize(NULL, COINIT_MULTITHREADED);  // S_FALSE / RPC_E_CHANGED_MODE: already set up
    IWICImagingFactory *factory = NULL;
    unsigned char *canvas = HeapAlloc(GetProcessHeap(), 0, LOGO_WIDTH * LOGO_HEIGHT * 4);
    if (!canvas) {
        why = "out of memory";
    } else if (FAILED(create(&clsid_imaging_factory, NULL, CLSCTX_INPROC_SERVER, &iid_imaging_factory,
                             (void **)&factory))) {
        why = "the Windows Imaging Component is not available";
    } else {
        why = decode(factory, path, canvas, width, height);
        IWICImagingFactory_Release(factory);
    }
    if (!why) {
        texture_shadow(canvas);
        texture_encode(canvas, output);
    }
    if (canvas) {
        HeapFree(GetProcessHeap(), 0, canvas);
    }
    if (SUCCEEDED(com)) {
        uninitialize();
    }
    return why;
}
