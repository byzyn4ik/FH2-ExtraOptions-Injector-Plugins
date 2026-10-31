// Azimuth Settings (Extra Options / forza_injector plugin).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The rows and their keys in Azimuth 1.x's data\azimuth.ini. Every key and
// value is the one Azimuth 1.0.4's INI reader takes (azimuth.exe 0xabe70; its
// defaults 0x9d70; its writer 0xb5980; 1.0.2 reads the same keys):
//
//   [title-<id>]  (the running game's title: 1a96545b FH2, 5a9771a4 F&F)
//     frame_rate         30..1000 (1..4 digits); absent: the title's own rate
//     vsync              on | off                                  (on)
//     resolution         360..4320, the picture's height in lines (1080)
//     aspect             16:9 | display (the display's own shape)  (16:9)
//     hdr                on | off                                  (off)
//     antialiasing       off | msaax2 | msaax4 | coverage          (msaax4)
//     graphics           low | default | high | ultra (anything else: custom)
//     shadow_detail      low | default | high | ultra              (default)
//     shadow_distance    default | high | ultra                    (default)
//     shadow_redraw      default | high | ultra                    (default)
//     reflections, scenery_detail, car_detail
//                        low | default | high | ultra              (default)
//     particles          low | default                             (default)
//     motion_blur        off | default | high | ultra              (default)
//     texture_filtering  default | 16x                             (default)
//     audio              stereo | system | multichannel            (stereo)
//     volume             0..100                                    (100)
//     speech, language   a language code (letters and '-'); absent: speech as
//                        the text, the text as Windows' language
//   [launcher]
//     display            windowed | fullscreen
//
// Graphics is a preset of the nine detail keys: Azimuth fills them from the
// preset, then each detail key that is present overrides it; its launcher
// writes all nine with the preset. A preset level a detail does not have
// moves towards Default (Azimuth 0x4140): texture filtering High and Ultra
// are 16x, particles High and Ultra are Default, shadow distance and redraw
// Low are Default, motion blur Low is Off.
//
// Video page: frame rate ... anti-aliasing. Audio page: audio output, volume,
// speech and text language. Frame rate offers what Azimuth's own launcher
// offers (live.h, frame_rates): 30 and the steps up to the display's refresh
// rate; a saved rate between two choices shows as the lower one.
// Not rows: [launcher]'s HDR calibration (hdr_peak, hdr_paper_white,
// hdr_black), wheel and player settings (Azimuth's launcher, for every game).
#pragma once
#include <string>
#include <vector>

namespace azs {

enum Row {
    // Video page
    kFrameRate,
    kVSync,
    kResolution,
    kAspect,
    kDisplay,
    kHdr,
    kPreset,
    kShadowDetail, // the nine details, in Azimuth's order (detail index = row - kShadowDetail)
    kShadowDistance,
    kShadowRedraw,
    kReflections,
    kSceneryDetail,
    kCarDetail,
    kParticles,
    kMotionBlur,
    kTextureFiltering,
    kAntiAliasing,
    // Audio page
    kAudioOutput,
    kVolume,
    kSpeech,
    kLanguage,
    kRowCount
};

constexpr unsigned kDetailCount = 9;

enum Page { kVideoPage, kAudioPage };
enum Language { kEnglish, kPolish, kUkrainian };

// What applies while the game runs (see live.h); the rows' descriptions say so.
struct LiveState {
    bool frame_rate, display, shadow_distance, shadow_rate, volume;
};

namespace rows {

// The running game's title ("1a96545b"): the [title-<id>] section.
void set_title(const std::wstring &title_id);
const std::wstring &section();
// Azimuth's own frame rate for the title (without frame_rate); 30 by default.
void set_title_rate(int rate);
// data\azimuth.ini; also fixes the language rows' choices (a saved code that
// is not one of them is added as it is).
void set_file(const std::wstring &azimuth_ini);
const std::wstring &file();
// Empty when data\azimuth.ini has Azimuth 1.x's layout, else what is wrong.
std::string check_layout();

// The Frame rate row's choices (frames per second, ascending, from 30).
void set_frame_rates(const std::vector<int> &rates);
bool frame_rates_set();
const std::vector<int> &frame_rates();
int frame_rate_value(int index);

// Rows a page shows can be turned off as a group ([Video] / [Audio] Enabled).
void enable_page(Page page, bool on);
bool shown(unsigned row);
Page page(unsigned row);
// The Volume row is a slider (0..100 as 0.0..1.0); the others are spinners.
bool slider(unsigned row);
unsigned count(unsigned row);
// The row's choice index (Volume: 0..100).
int get(unsigned row);
// Writes the row's keys; false if the value is out of range, unchanged or
// cannot be written.
bool set(unsigned row, int value);
// The level (0 low ... 3 ultra) Azimuth uses for a detail (0..8): its key,
// else the preset's, else Default.
int detail_level(unsigned detail);
// The level Azimuth's preset `level` (0..3) gives a detail, as Azimuth 0x4140.
int preset_detail(unsigned level, unsigned detail);

const char *property(unsigned row);   // binding name, "AzsFrameRate"
const char *collection(unsigned row); // its choices, "AzsFrameRateChoices" (nullptr for a slider)
const wchar_t *choice(unsigned row, unsigned index, Language language);
const char *name(unsigned row, Language language);
std::string description(unsigned row, Language language, const LiveState &live);
// The INI key a row writes ("frame_rate"; the preset row: "graphics").
const wchar_t *key(unsigned row);

} // namespace rows
} // namespace azs
