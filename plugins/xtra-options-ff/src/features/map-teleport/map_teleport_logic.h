// Xtra Options (Fast & Furious), MapTeleport.dll: the parts that need no game
// (settings, the map cursor's world point, the press of the button, the
// legend caption), so the offline tests can run them on made-up data.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MAP_TELEPORT_LOGIC_H
#define MAP_TELEPORT_LOGIC_H
#include <stdint.h>

// MapViewControllerInteractive (Fast & Furious, vtable 0x2de6510):
//   +0x2c0 vec4  the cursor's map point (x, y, z): the input (0xbdf110) pans
//                x and z, ResetView sets y to 0
//   +0x3d0 vec4  the world point under the cursor that its update
//                (0xbdfbeb, 0x1962ed0) picks, a snapped map item's position
//                when the cursor sits on one; the game's own fast travel
//                (0xd6bd90) travels there
#define MT_CONTROLLER_CURSOR 0x2c0u
#define MT_CONTROLLER_PICK 0x3d0u
// The world fits in +-MT_WORLD_LIMIT metres on every axis (Fast & Furious'
// map is about 10 x 7 km around the origin).
#define MT_WORLD_LIMIT 50000.0f

enum { MT_BUTTON_X = 0, MT_BUTTON_Y = 1 };
#define MT_HOLD_DEFAULT_MS 500u
#define MT_HOLD_MAX_MS 5000u
// A press the game's handler reported but the poll never saw held (its axis
// unreadable) goes back to the game after this long.
#define MT_DEFER_GRACE_MS 250u

typedef struct {
    int button;            // MT_BUTTON_*
    unsigned hold_ms;      // 0: the button teleports on the press
    int button_invalid;    // the INI's TeleportButton was not X or Y (X used)
    int hold_clamped;      // the INI's TeleportHold was outside 0..MT_HOLD_MAX_MS
} MtSettings;

// TeleportButton ("X", "Y"; case and surrounding blanks ignored; anything
// else is X) and TeleportHold (milliseconds, clamped to 0..MT_HOLD_MAX_MS).
void mt_parse_settings(const wchar_t *button, long hold, MtSettings *out);

// Where to travel: the picked world point (+0x3d0) when it is a usable
// position, else the cursor's map point (+0x2c0 x and z, height 0).
// `controller` is a copy of the controller's first 0x3e0 bytes.
enum { MT_TARGET_NONE = 0, MT_TARGET_PICK = 1, MT_TARGET_CURSOR = 2 };
int mt_target(const unsigned char *controller, float out[4]);

// The button on the map, polled every map frame through its axis (the
// game's key translation consumes the press, which its state query then
// reports as up; the axis is not consumed), plus the game's own handler call
// for the press when one came (held back until the press resolves).
typedef struct {
    int armed;         // the button was seen up on this map: a new press counts
    int down, fired;   // held now; this press already travelled
    uint64_t since;    // ms, when this press went down
    int deferred;      // the game's handler call for this press is held back
    uint64_t deferred_at;
} MtPress;

enum {
    MT_EV_PRESSED = 1,   // a new press on the map
    MT_EV_TELEPORT = 2,  // held for hold_ms (or hold_ms is 0): travel now
    MT_EV_RELEASED = 4,  // released before it travelled
    MT_EV_NATIVE = 8,    // run the held-back game handler (a tap)
};
// held: 1 down, 0 up, -1 unknown. Returns MT_EV_* bits.
int mt_poll(MtPress *press, int held, uint64_t now, unsigned hold_ms);
// The game's handler came for the button (its key down): hold it back.
void mt_defer(MtPress *press, uint64_t now);

// The legend caption for a language code ("EN", "PL", "UK", ...; NULL or
// unknown: English).
const wchar_t *mt_caption(const char *code);
#endif
