// Xtra Options (Fast & Furious), MapTeleport.dll: settings, target point,
// the press of the button and the legend caption (no game calls; see
// map_teleport_logic.h).
// SPDX-License-Identifier: GPL-3.0-or-later
#include <wchar.h>

#include "map_teleport_logic.h"

static int finite(float value) {
    union {
        float f;
        uint32_t u;
    } bits = {value};
    return (bits.u & 0x7f800000u) != 0x7f800000u;
}

static int usable(float value) {
    return finite(value) && value > -MT_WORLD_LIMIT && value < MT_WORLD_LIMIT;
}

void mt_parse_settings(const wchar_t *button, long hold, MtSettings *out) {
    out->button = MT_BUTTON_X;
    out->button_invalid = 0;
    out->hold_clamped = 0;
    const wchar_t *p = button ? button : L"";
    while (*p == L' ' || *p == L'\t') ++p;
    wchar_t c = *p;
    const wchar_t *rest = c ? p + 1 : p;
    while (*rest == L' ' || *rest == L'\t') ++rest;
    if ((c == L'X' || c == L'x') && !*rest) {
        out->button = MT_BUTTON_X;
    } else if ((c == L'Y' || c == L'y') && !*rest) {
        out->button = MT_BUTTON_Y;
    } else {
        out->button_invalid = 1;
    }
    if (hold < 0) {
        out->hold_ms = 0;
        out->hold_clamped = 1;
    } else if (hold > (long)MT_HOLD_MAX_MS) {
        out->hold_ms = MT_HOLD_MAX_MS;
        out->hold_clamped = 1;
    } else {
        out->hold_ms = (unsigned)hold;
    }
}

int mt_target(const unsigned char *controller, float out[4]) {
    float pick[4], cursor[4];
    const unsigned char *a = controller + MT_CONTROLLER_PICK, *b = controller + MT_CONTROLLER_CURSOR;
    for (unsigned i = 0; i < sizeof(pick); ++i) ((unsigned char *)pick)[i] = a[i];
    for (unsigned i = 0; i < sizeof(cursor); ++i) ((unsigned char *)cursor)[i] = b[i];
    // A never-updated pick is all zeros: the world origin is not a target.
    int pick_zero = pick[0] == 0.0f && pick[1] == 0.0f && pick[2] == 0.0f;
    if (usable(pick[0]) && usable(pick[1]) && usable(pick[2]) && !pick_zero) {
        out[0] = pick[0];
        out[1] = pick[1];
        out[2] = pick[2];
        out[3] = 1.0f;
        return MT_TARGET_PICK;
    }
    if (usable(cursor[0]) && usable(cursor[2]) && !(cursor[0] == 0.0f && cursor[2] == 0.0f)) {
        out[0] = cursor[0];
        out[1] = 0.0f;
        out[2] = cursor[2];
        out[3] = 1.0f;
        return MT_TARGET_CURSOR;
    }
    return MT_TARGET_NONE;
}

void mt_defer(MtPress *press, uint64_t now) {
    if (!press->deferred) {
        press->deferred = 1;
        press->deferred_at = now;
    }
}

int mt_poll(MtPress *press, int held, uint64_t now, unsigned hold_ms) {
    int events = 0;
    if (held > 0) {
        if (!press->down && press->armed) {
            press->down = 1;
            press->fired = 0;
            press->since = now;
            events |= MT_EV_PRESSED;
        }
        if (press->down && !press->fired && (now >= press->since ? now - press->since : 0) >= hold_ms) {
            press->fired = 1;
            press->deferred = 0;  // it travels: the game's handler never runs for this press
            events |= MT_EV_TELEPORT;
        }
    } else {
        if (press->down && !press->fired) events |= MT_EV_RELEASED;
        press->down = 0;
        press->fired = 0;
        if (held == 0) press->armed = 1;
    }
    if (press->deferred && !press->down) {
        uint64_t age = now >= press->deferred_at ? now - press->deferred_at : 0;
        // A tap (released before the hold), or a press the poll never saw held: the game's own action.
        if ((events & MT_EV_RELEASED) || age >= MT_DEFER_GRACE_MS) {
            press->deferred = 0;
            events |= MT_EV_NATIVE;
        }
    }
    return events;
}

typedef struct {
    const char *code;
    const wchar_t *text;
} Caption;

static const Caption captions[] = {
    {"EN", L"Teleport (hold)"},
    {"PL", L"Teleport (przytrzymaj)"},
    {"UK", L"Телепорт (утримуйте)"},
    {"RU", L"Телепорт (удерживайте)"},
    {"DE", L"Teleportieren (halten)"},
    {"FR", L"Téléporter (maintenir)"},
    {"ES", L"Teletransporte (mantener)"},
    {"MX", L"Teletransporte (mantener)"},
    {"IT", L"Teletrasporto (tieni premuto)"},
    {"PT", L"Teletransporte (manter)"},
    {"BR", L"Teletransporte (segurar)"},
    {"NL", L"Teleporteren (ingedrukt houden)"},
    {"SV", L"Teleportera (håll in)"},
    {"NO", L"Teleporter (hold inne)"},
    {"DA", L"Teleportér (hold nede)"},
    {"FI", L"Teleporttaa (pidä pohjassa)"},
    {"CS", L"Teleport (podržte)"},
    {"TR", L"Işınlan (basılı tut)"},
    {"JA", L"テレポート（長押し）"},
    {"KO", L"순간이동 (길게 누르기)"},
    {"CHT", L"傳送（按住）"},
    {"CHS", L"传送（按住）"},
};

const wchar_t *mt_caption(const char *code) {
    for (unsigned i = 0; code && i < sizeof(captions) / sizeof(captions[0]); ++i) {
        const char *a = captions[i].code, *b = code;
        while (*a && *a == *b) ++a, ++b;
        if (!*a && !*b) return captions[i].text;
    }
    return captions[0].text;
}
