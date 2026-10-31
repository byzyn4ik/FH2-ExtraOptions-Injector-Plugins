// Performance for Forza Horizon 2 (Extra Options plugin).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Nitrous plugin's gauge, drawn in the same overlay as
// the FPS counter. Nothing here runs unless the Nitrous plugin is installed:
// its status comes from Nitrous.dll's NitrousStatus export (fh2_nitrous.h),
// looked up at run time; there is no link-time dependency.
//
// The gauge is Fast & Furious' own nitrous bar: its pixel shader
// (FZMaterialPresetRadialNitrousBar, ported to nos.hlsl from the DXBC in
// Fast & Furious' executable), its colour (A_Color_Nitrous_Fill #2DB2FF) and
// its layout beside the speedometer (HUDSpeedometerControl.xaml), with its
// outline texture (HUD_Nos_Outline_Radial_Bar from SpireHUD.zip) read at run
// time from the player's own Fast & Furious install; nothing of the game is
// shipped. Without that install the bar is drawn without its outline.
// (0.0.2's screen effect, edges darkened and streaks of our own, was dropped
// in 0.0.4: Fast & Furious' own motion blur and camera shake now run in the
// game's camera, through the Nitrous plugin; ScreenEffect is ignored.)
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "fh2_nitrous.h"

namespace perf::nos {

struct Options {
    bool gauge = true;          // [Nitrous] Gauge
    bool driving_only = true;   // [Nitrous] OnlyWhileDriving
    float scale = 1.f;          // [Nitrous] GaugeScale (0.5 to 3)
    float x = 0.f, y = 0.f;     // [Nitrous] GaugeOffsetX / GaugeOffsetY: HUD pixels (1080p), right / down
    std::wstring media;         // [Nitrous] FastFuriousMedia: SpireHUD.zip or a folder holding it; empty: searched
};
Options read_options(const std::wstring &ini, std::string *warnings);
bool any(const Options &options);

// The Nitrous plugin is installed beside this one (mods\Nitrous\plugins\Nitrous.dll).
bool nitrous_installed(const std::wstring &plugin_dll);

// Nitrous.dll's status; looks the export up again at most every 2 s while it is missing.
bool poll(FH2NitrousStatus *status, uint64_t now_ms);
// Tests: another status source than Nitrous.dll's export.
void set_source(FH2NitrousStatusFn source);

// ---------------------------------------------------------------- the outline texture

struct Texture {
    unsigned width = 0, height = 0;
    std::vector<uint8_t> blocks;  // BC7 blocks, linear (row pitch ((width + 3) / 4) * 16)
};
// Raw deflate (zip method 8); false on bad data or when it does not fill `size` exactly.
bool inflate(const uint8_t *input, size_t length, uint8_t *output, size_t size);
// One entry of a zip archive (stored or deflated); false when missing or bad.
bool read_zip_entry(const std::wstring &zip, const char *name, std::vector<uint8_t> *out, std::string *why);
// A Forza swatchbin of one BC7 mip, 1D- or 2D-tiled (Xbox One), made linear.
bool detile_swatchbin(const std::vector<uint8_t> &file, Texture *out, std::string *why);
// Fast & Furious' SpireHUD.zip: `configured` (the file or its folder), else
// searched beside the game's folders (…\Forza Horizon 2 Presents Fast &
// Furious\game\base\Mount\media\ui\textures\data_bound). Empty when not found.
std::wstring find_spire_hud(const std::wstring &configured, const std::wstring &host_exe);
// Loads HUD_Nos_Outline_Radial_Bar; false (and *why) when it cannot.
bool load_outline(const std::wstring &zip, Texture *out, std::string *why);

// ---------------------------------------------------------------- what to draw

enum Mode : uint32_t { kBar = 1, kOutline = 2 };
struct Quad {
    float rect[4];  // x, y, width, height in back-buffer pixels
    uint32_t mode;
};
struct Draw {
    unsigned count = 0;
    Quad quads[2];
    float ratio = 0, active = 0, level = 0, alpha = 1;  // the bar's Ratio and Active, the boost level, the bar's alpha
};
// The quads for a back buffer of width x height. Fast & Furious' layout on
// Forza Horizon 2's HUD (1920x1080 design, scaled to the height, anchored at
// the bottom-right): the speedometer is a 300x300 box 92 px from the right
// and 23 px from the bottom; the bar is that box scaled by 1.2645 about its
// centre; the outline (128x256) sits at (-23, 8) in the box.
Draw layout(unsigned width, unsigned height, const FH2NitrousStatus &status, const Options &options,
            bool outline, float seconds);

} // namespace perf::nos
