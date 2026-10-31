// Azimuth Settings (Extra Options / forza_injector plugin).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The rows on two of the game's own settings pages (media\UI.zip, the same
// XAML in Forza Horizon 2 and Fast & Furious but for F&F's missing Radio DJ
// row on the Audio page):
//   Video (Scenes/Anthem/A_VideoOptions.xaml): frame rate ... anti-aliasing;
//   Audio (Scenes/Anthem/A_AudioOptions.xaml): audio output, volume, speech
//   and text language.
//
// The game reads every XAML resource through one call on its UIRenderer
// LoadComponent path (FH2 "call 0xaabec0" at 0x16ebb37; F&F "call 0xc795c0"
// at 0x1712d47). Extra Options (FH2: 0.3.2 and 0.4.4 alike) points that call
// at its own thunk (0x13fe354, in padding); Xtra Options or another plugin
// may chain it too. This plugin chains the call (see call_chain.h): its
// handler first calls whatever the site called, then edits the XAML that
// came back, for these two pages only:
//
//   Video  one TileMenuDataItem per row before the first </ResourceDictionary>;
//          one SpinnerControl per row after the page's own rows (Brightness,
//          Subtitles) and before Extra Options' (Realtime shadows, ...); the
//          list gets the scrollable list style and a 440 px height (it was
//          110, or 275 after Extra Options).
//   Audio  the resources likewise, plus a 0..1 slider style in 5 % steps;
//          after the last row (the page's own, then Extra Options' dialogue
//          and checkpoint rows) the rows (spinners and the volume slider);
//          the list (550 px) gets the scrollable list style.
//
// Pages built from these classes but marked "<!-- EO:StandaloneOptionsPage -->"
// (plugins' own pages, such as Weather Control's) and pages Extra Options'
// fork builds from the Video page (KEYBOARD & MOUSE, World) are left alone.
// With Azimuth Graphics Options (AzimuthGraphicsOptions.dll) in the process
// nothing is added: it shows rows for the same settings.
//
// The rows bind to properties ("AzsFrameRate", "AzsVolume", ...) registered
// in the page type's registry (FH2 Video 0x35470d0, Audio 0x35107b0; F&F
// 0x35adf70, 0x357a460) with the game's own int, float and collection
// property descriptors, the way Extra Options registers its own rows.
#pragma once
#include <cstddef>
#include <cstdint>

#include "live.h"
#include "rows.h"

namespace azs {
namespace pages {

// nullptr when the game image is a known build (by its size and PE
// TimeDateStamp) as inspected (every native function the rows use, both page
// types, and the resource call site), else what differs. *target (when given)
// describes what the call site calls now. On success the build is the one
// the functions below describe.
const char *check_game(const unsigned char *image, uint32_t size, const char **target);
const char *game_name();      // "Forza Horizon 2 1.0.0.64"
const char *game_id();        // "fh2", "fh2ff"
const wchar_t *title_id();    // Azimuth's title, "1a96545b"
uint32_t resource_call();     // the chained call site
live::GameShadows shadows();  // the build's shadow sites
// The image fh2_text_language (common/include/fh2_game.h) reads the build's
// text table manager through.
const unsigned char *text_base(const unsigned char *game);
// True when Azimuth Graphics Options' DLL is loaded in the process.
bool other_plugin_loaded();

// The page XAML with the rows added; 0 (and *why) when it is not that page as
// expected or already has them.
size_t extend_video(const char *text, size_t length, char *output, size_t capacity, Language language,
                    const LiveState &live, const char **why);
size_t extend_audio(const char *text, size_t length, char *output, size_t capacity, Language language,
                    const LiveState &live, const char **why);

// Chains the resource call; false (logged) when it cannot.
bool install(unsigned char *image, const LiveState &live, bool apply_live);

#ifdef AZS_TEST_EXPORTS
// Test build: what other_plugin_loaded answers (-1: the real check).
void test_other_plugin(int loaded);
// Test build: a site of the identified build: kind 0 native (in Native's
// order: DeleteObject ... GetResource), 1 template lea (Subtitles, Gamma,
// OnOffOptions), 2 registration lea and 3 registry (Video, Audio), 4
// collection vtable, 5 resource call, 6 text tables, 7 Extra Options' thunk.
uint32_t test_build_site(unsigned kind, unsigned index);
#endif

} // namespace pages
} // namespace azs
