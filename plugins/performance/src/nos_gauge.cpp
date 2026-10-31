// Performance for Forza Horizon 2 (Extra Options plugin).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Nitrous gauge's CPU side (nos_gauge.h): settings, the Nitrous plugin's
// status, Fast & Furious' outline texture (zip, deflate, swatchbin) and the
// layout. The GPU side is in monitor.cpp, the shader in nos.hlsl.
#include "nos_gauge.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "common.h"

namespace perf::nos {

// ---------------------------------------------------------------- settings

namespace {
std::wstring value(const std::wstring &ini, const wchar_t *key) {
    wchar_t text[1024];
    GetPrivateProfileStringW(L"Nitrous", key, L"", text, 1024, ini.c_str());
    std::wstring out = text;
    while (!out.empty() && (out.back() == L' ' || out.back() == L'\t')) out.pop_back();
    size_t start = out.find_first_not_of(L" \t");
    return start == std::wstring::npos ? std::wstring() : out.substr(start);
}

void flag(const std::wstring &ini, const wchar_t *key, const char *name, bool &out, std::string *warnings) {
    std::wstring v = value(ini, key);
    if (v == L"0" || v == L"1")
        out = v == L"1";
    else if (!v.empty() && warnings)
        *warnings += std::string(warnings->empty() ? "" : "; ") + "[Nitrous] " + name + "=" + narrow(v) +
                     " is not valid, " + (out ? "1" : "0") + " is used";
}

void number(const std::wstring &ini, const wchar_t *key, const char *name, float low, float high, float &out,
            std::string *warnings) {
    std::wstring v = value(ini, key);
    if (v.empty()) return;
    wchar_t *end = nullptr;
    float parsed = wcstof(v.c_str(), &end);
    if (!end || *end || !std::isfinite(parsed)) {
        if (warnings)
            *warnings += std::string(warnings->empty() ? "" : "; ") + "[Nitrous] " + name + "=" + narrow(v) +
                         " is not a number, " + std::to_string(out) + " is used";
        return;
    }
    out = std::min(high, std::max(low, parsed));
}
} // namespace

Options read_options(const std::wstring &ini, std::string *warnings) {
    Options o;
    flag(ini, L"Gauge", "Gauge", o.gauge, warnings);
    flag(ini, L"OnlyWhileDriving", "OnlyWhileDriving", o.driving_only, warnings);
    number(ini, L"GaugeScale", "GaugeScale", 0.5f, 3.f, o.scale, warnings);
    number(ini, L"GaugeOffsetX", "GaugeOffsetX", -1920.f, 1920.f, o.x, warnings);
    number(ini, L"GaugeOffsetY", "GaugeOffsetY", -1080.f, 1080.f, o.y, warnings);
    o.media = value(ini, L"FastFuriousMedia");
    return o;
}

bool any(const Options &options) { return options.gauge; }

namespace {
std::wstring parent(const std::wstring &path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}
bool is_file(const std::wstring &path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
} // namespace

bool nitrous_installed(const std::wstring &plugin_dll) {
    // <mods>\Performance\plugins\Performance.dll -> <mods>\Nitrous\plugins\Nitrous.dll
    const std::wstring mods = parent(parent(parent(plugin_dll)));
    return !mods.empty() && is_file(mods + L"\\Nitrous\\plugins\\Nitrous.dll");
}

// ---------------------------------------------------------------- status

namespace {
FH2NitrousStatusFn status_export;
uint64_t next_lookup;
} // namespace

void set_source(FH2NitrousStatusFn source) { status_export = source; }

bool poll(FH2NitrousStatus *status, uint64_t now_ms) {
    if (!status_export && now_ms >= next_lookup) {
        next_lookup = now_ms + 2000;
        HMODULE module = GetModuleHandleW(L"Nitrous.dll");
        if (module) status_export = (FH2NitrousStatusFn)(void *)GetProcAddress(module, "NitrousStatus");
    }
    if (!status_export) return false;
    status->size = sizeof(*status);
    return status_export(status) != FALSE;
}

// ---------------------------------------------------------------- inflate

namespace {
struct Bits {
    const uint8_t *at, *end;
    uint32_t buffer = 0;
    int count = 0;
    bool bad = false;
    uint32_t take(int n) {
        while (count < n) {
            if (at >= end) {
                bad = true;
                return 0;
            }
            buffer |= uint32_t(*at++) << count;
            count += 8;
        }
        uint32_t v = buffer & ((1u << n) - 1);
        buffer >>= n;
        count -= n;
        return v;
    }
};

struct Huffman {
    uint16_t counts[16];
    uint16_t symbols[320];
};

bool build(Huffman &h, const uint8_t *lengths, int n) {
    memset(h.counts, 0, sizeof(h.counts));
    for (int i = 0; i < n; ++i) ++h.counts[lengths[i]];
    h.counts[0] = 0;
    uint16_t offsets[16] = {};
    for (int i = 1; i < 15; ++i) offsets[i + 1] = uint16_t(offsets[i] + h.counts[i]);
    for (int i = 0; i < n; ++i)
        if (lengths[i]) h.symbols[offsets[lengths[i]]++] = uint16_t(i);
    return true;
}

int decode(Bits &bits, const Huffman &h) {
    int code = 0, first = 0, index = 0;
    for (int length = 1; length < 16; ++length) {
        code |= int(bits.take(1));
        const int count = h.counts[length];
        if (code - count < first) return h.symbols[index + (code - first)];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
        if (bits.bad) return -1;
    }
    return -1;
}

const uint16_t kLengthBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                  31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistanceBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t kDistanceExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
} // namespace

bool inflate(const uint8_t *input, size_t length, uint8_t *output, size_t size) {
    Bits bits{input, input + length};
    size_t out = 0;
    bool last = false;
    while (!last) {
        last = bits.take(1) != 0;
        const uint32_t type = bits.take(2);
        if (bits.bad) return false;
        if (type == 0) {
            bits.buffer = 0;
            bits.count = 0;
            if (bits.end - bits.at < 4) return false;
            const unsigned n = bits.at[0] | bits.at[1] << 8, check = bits.at[2] | bits.at[3] << 8;
            bits.at += 4;
            if ((n ^ 0xffffu) != check || size_t(bits.end - bits.at) < n || size - out < n) return false;
            memcpy(output + out, bits.at, n);
            bits.at += n;
            out += n;
            continue;
        }
        if (type == 3) return false;
        Huffman literals, distances;
        uint8_t lengths[320] = {};
        if (type == 1) {
            for (int i = 0; i < 288; ++i) lengths[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
            build(literals, lengths, 288);
            for (int i = 0; i < 30; ++i) lengths[i] = 5;
            build(distances, lengths, 30);
        } else {
            const int nlit = int(bits.take(5)) + 257, ndist = int(bits.take(5)) + 1, nlen = int(bits.take(4)) + 4;
            static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            uint8_t code_lengths[19] = {};
            for (int i = 0; i < nlen; ++i) code_lengths[order[i]] = uint8_t(bits.take(3));
            Huffman lencode;
            build(lencode, code_lengths, 19);
            int i = 0;
            while (i < nlit + ndist) {
                const int symbol = decode(bits, lencode);
                if (symbol < 0 || bits.bad) return false;
                if (symbol < 16) {
                    lengths[i++] = uint8_t(symbol);
                    continue;
                }
                uint8_t repeat_value = 0;
                int repeat;
                if (symbol == 16) {
                    if (!i) return false;
                    repeat_value = lengths[i - 1];
                    repeat = 3 + int(bits.take(2));
                } else if (symbol == 17) {
                    repeat = 3 + int(bits.take(3));
                } else {
                    repeat = 11 + int(bits.take(7));
                }
                if (i + repeat > nlit + ndist) return false;
                while (repeat--) lengths[i++] = repeat_value;
            }
            if (!lengths[256]) return false;
            build(literals, lengths, nlit);
            build(distances, lengths + nlit, ndist);
        }
        for (;;) {
            const int symbol = decode(bits, literals);
            if (symbol < 0 || bits.bad) return false;
            if (symbol < 256) {
                if (out >= size) return false;
                output[out++] = uint8_t(symbol);
                continue;
            }
            if (symbol == 256) break;
            const int l = symbol - 257;
            if (l >= 29) return false;
            const size_t n = kLengthBase[l] + bits.take(kLengthExtra[l]);
            const int d = decode(bits, distances);
            if (d < 0 || d >= 30 || bits.bad) return false;
            const size_t distance = kDistanceBase[d] + bits.take(kDistanceExtra[d]);
            if (bits.bad || distance > out || size - out < n) return false;
            for (size_t k = 0; k < n; ++k, ++out) output[out] = output[out - distance];
        }
    }
    return out == size;
}

// ---------------------------------------------------------------- zip and swatchbin

namespace {
uint32_t u16(const uint8_t *p) { return p[0] | p[1] << 8; }
uint32_t u32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24; }
} // namespace

bool read_zip_entry(const std::wstring &zip, const char *name, std::vector<uint8_t> *out, std::string *why) {
    HANDLE file = CreateFileW(zip.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        *why = "cannot open " + narrow(zip);
        return false;
    }
    LARGE_INTEGER size{};
    std::vector<uint8_t> data;
    DWORD got = 0;
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart > 22 && size.QuadPart < (64ll << 20);
    if (ok) {
        data.resize(size_t(size.QuadPart));
        ok = ReadFile(file, data.data(), DWORD(data.size()), &got, nullptr) && got == data.size();
    }
    CloseHandle(file);
    if (!ok) {
        *why = "cannot read " + narrow(zip);
        return false;
    }
    size_t end = std::string::npos;
    for (size_t at = data.size() - 22 + 1; at-- > 0 && data.size() - at < 0x10000 + 22;)
        if (u32(&data[at]) == 0x06054b50u) {
            end = at;
            break;
        }
    if (end == std::string::npos) {
        *why = narrow(zip) + " is not a zip archive";
        return false;
    }
    const size_t entries = u16(&data[end + 10]);
    size_t at = u32(&data[end + 16]);
    const size_t name_length = strlen(name);
    for (size_t i = 0; i < entries; ++i) {
        if (at + 46 > data.size() || u32(&data[at]) != 0x02014b50u) break;
        const uint32_t method = u16(&data[at + 10]), packed = u32(&data[at + 20]), plain = u32(&data[at + 24]);
        const size_t n = u16(&data[at + 28]), extra = u16(&data[at + 30]), comment = u16(&data[at + 32]);
        const size_t local = u32(&data[at + 42]);
        if (at + 46 + n > data.size()) break;
        if (n == name_length && _strnicmp((const char *)&data[at + 46], name, n) == 0) {
            if (local + 30 > data.size() || u32(&data[local]) != 0x04034b50u) break;
            const size_t start = local + 30 + u16(&data[local + 26]) + u16(&data[local + 28]);
            if (start + packed > data.size() || plain > (16u << 20)) break;
            out->assign(plain, 0);
            if (method == 0 && packed == plain) {
                memcpy(out->data(), &data[start], plain);
                return true;
            }
            if (method == 8 && inflate(&data[start], packed, out->data(), plain)) return true;
            *why = std::string(name) + " in " + narrow(zip) + " cannot be unpacked (method " + std::to_string(method) + ")";
            return false;
        }
        at += 46 + n + extra + comment;
    }
    *why = std::string(name) + " is not in " + narrow(zip);
    return false;
}

bool detile_swatchbin(const std::vector<uint8_t> &file, Texture *out, std::string *why) {
    if (file.size() < 0x53 || memcmp(file.data(), "burG", 4) != 0 || memcmp(&file[0x10], "BCXT", 4) != 0) {
        *why = "not a swatchbin";
        return false;
    }
    const uint32_t header = u32(&file[0x08]), data_size = u32(&file[0x20]);
    const unsigned width = u16(&file[0x40]), height = u16(&file[0x42]);
    const uint8_t format = file[0x4f];
    if (format != 0x2e && format != 0x2d) {
        *why = "the swatchbin is not BC7 (format 0x" + hex(format) + ")";
        return false;
    }
    if (!width || !height || width > 4096 || height > 4096 || header + uint64_t(data_size) > file.size()) {
        *why = "the swatchbin's size fields are bad";
        return false;
    }
    const unsigned ew = (width + 3) / 4, eh = (height + 3) / 4;
    out->width = width;
    out->height = height;
    out->blocks.assign(size_t(ew) * eh * 16, 0);
    for (unsigned ey = 0; ey < eh; ++ey)
        for (unsigned ex = 0; ex < ew; ++ex) {
            auto bit = [](unsigned v, unsigned n) { return (v >> n) & 1u; };
            const unsigned m = bit(ex, 0) | bit(ey, 0) << 1 | bit(ex, 1) << 2 | bit(ey, 1) << 3 | bit(ex, 2) << 4 |
                               bit(ey, 2) << 5;
            size_t source;
            if (format == 0x2d) {  // 1D tiled: 8x8 blocks of blocks
                const unsigned pw = (ew + 7) & ~7u;
                source = (size_t((ey >> 3) * (pw >> 3) + (ex >> 3))) * 64 + m;
            } else {  // 2D tiled: 64x32 macro tiles, pipes and banks
                const unsigned pw = (ew + 63) & ~63u;
                const unsigned lo = m & 15, hi = m >> 4;
                const unsigned pipe = (bit(ex, 3) ^ bit(ex, 4) ^ bit(ey, 3)) | (bit(ex, 4) ^ bit(ey, 4)) << 1 |
                                      (bit(ex, 5) ^ bit(ey, 5)) << 2;
                const unsigned bank = (bit(ex, 6) ^ bit(ey, 4) ^ bit(ey, 5)) | (bit(ex, 7) ^ bit(ey, 3)) << 1;
                source = size_t((ey >> 5) * (pw >> 6) + (ex >> 6)) * 2048 + (lo | (pipe | bank << 3 | hi << 5) * 16);
            }
            const size_t from = header + source * 16;
            if (source * 16 + 16 > data_size || from + 16 > file.size()) {
                *why = "the swatchbin's data is shorter than its tiling";
                return false;
            }
            memcpy(&out->blocks[(size_t(ey) * ew + ex) * 16], &file[from], 16);
        }
    return true;
}

std::wstring find_spire_hud(const std::wstring &configured, const std::wstring &host_exe) {
    static const wchar_t *const kInside[] = {L"", L"\\SpireHUD.zip", L"\\ui\\textures\\data_bound\\SpireHUD.zip",
                                             L"\\media\\ui\\textures\\data_bound\\SpireHUD.zip",
                                             L"\\Mount\\media\\ui\\textures\\data_bound\\SpireHUD.zip",
                                             L"\\game\\base\\Mount\\media\\ui\\textures\\data_bound\\SpireHUD.zip"};
    if (!configured.empty()) {
        for (const wchar_t *inside : kInside)
            if (is_file(configured + inside)) return configured + inside;
        return std::wstring();
    }
    static const wchar_t *const kGames[] = {L"\\Forza Horizon 2 Presents Fast & Furious",
                                            L"\\Forza Horizon 2 Fast and Furious"};
    std::wstring folder = parent(host_exe);
    for (int up = 0; up < 5 && !folder.empty(); ++up, folder = parent(folder))
        for (const wchar_t *game : kGames)
            for (size_t i = 2; i < sizeof(kInside) / sizeof(kInside[0]); ++i)
                if (is_file(folder + game + kInside[i])) return folder + game + kInside[i];
    return std::wstring();
}

bool load_outline(const std::wstring &zip, Texture *out, std::string *why) {
    std::vector<uint8_t> file;
    return read_zip_entry(zip, "HUD_Nos_Outline_Radial_Bar.swatchbin", &file, why) && detile_swatchbin(file, out, why);
}

// ---------------------------------------------------------------- layout

Draw layout(unsigned width, unsigned height, const FH2NitrousStatus &status, const Options &options, bool outline,
            float seconds) {
    Draw d;
    (void)seconds;
    if (!width || !height) return d;
    if (options.driving_only && !(status.flags & FH2_NITROUS_DRIVING)) return d;
    const float level = std::min(1.f, std::max(0.f, status.level));
    d.level = level;
    if (!options.gauge) return d;
    const float s = float(height) / 1080.f * options.scale;
    const float box_x = float(width) - (92.f + 300.f) * s + options.x * float(height) / 1080.f;
    const float box_y = float(height) - (23.f + 300.f) * s + options.y * float(height) / 1080.f;
    const float bar = 300.f * 1.2645f * s, centre_x = box_x + 150.f * s, centre_y = box_y + 150.f * s;
    d.quads[d.count++] = {{centre_x - bar / 2, centre_y - bar / 2, bar, bar}, kBar};
    if (outline) d.quads[d.count++] = {{box_x - 23.f * s, box_y + 8.f * s, 128.f * s, 256.f * s}, kOutline};
    d.ratio = std::min(1.f, std::max(0.f, status.remaining));
    d.active = level;
    d.alpha = (status.flags & FH2_NITROUS_BLOCKED)                                          ? 0.35f
              : (status.flags & FH2_NITROUS_REFILLING) && !(status.flags & FH2_NITROUS_RUN) ? 0.8f
                                                                                             : 1.f;
    // A skill's refill lights the bar's tip (the shader's Active glow) for a moment.
    if ((status.flags & FH2_NITROUS_GAINED) && d.active < 0.6f) d.active = 0.6f;
    return d;
}

} // namespace perf::nos
