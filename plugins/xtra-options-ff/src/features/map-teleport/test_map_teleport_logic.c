// Offline test of MapTeleport.dll's game-free parts: settings, the target
// point taken from a made-up MapViewControllerInteractive, the press of the
// button (poll and held-back game handler), the legend caption. Built and
// run by build.py --test.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "map_teleport_logic.c"

static int failures;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "  ok  " : "  FAIL", what);
    if (!ok) ++failures;
}

static void put(unsigned char *controller, unsigned offset, float x, float y, float z) {
    float v[4] = {x, y, z, 0.0f};
    memcpy(controller + offset, v, sizeof(v));
}

static int near(float a, float b) {
    return fabsf(a - b) < 1e-4f;
}

int main(void) {
    MtSettings s;
    printf("settings\n");
    mt_parse_settings(L"X", 500, &s);
    check(s.button == MT_BUTTON_X && s.hold_ms == 500 && !s.button_invalid && !s.hold_clamped, "X, 500 ms");
    mt_parse_settings(L" y ", 0, &s);
    check(s.button == MT_BUTTON_Y && s.hold_ms == 0 && !s.button_invalid, "' y ', 0 ms (teleport on the press)");
    mt_parse_settings(L"A", 9000, &s);
    check(s.button == MT_BUTTON_X && s.button_invalid && s.hold_ms == MT_HOLD_MAX_MS && s.hold_clamped,
          "A is refused (X), 9000 ms clamped to 5000");
    mt_parse_settings(L"XY", -3, &s);
    check(s.button == MT_BUTTON_X && s.button_invalid && s.hold_ms == 0 && s.hold_clamped, "XY refused, -3 clamped to 0");
    mt_parse_settings(L"", 250, &s);
    check(s.button_invalid && s.hold_ms == 250, "empty button refused");

    printf("target from a made-up map controller\n");
    unsigned char controller[0x3e0];
    float t[4];
    memset(controller, 0, sizeof(controller));
    put(controller, MT_CONTROLLER_CURSOR, 1200.0f, 0.0f, -3400.0f);
    put(controller, MT_CONTROLLER_PICK, 1203.5f, 41.25f, -3398.0f);
    check(mt_target(controller, t) == MT_TARGET_PICK && near(t[0], 1203.5f) && near(t[1], 41.25f) &&
              near(t[2], -3398.0f) && near(t[3], 1.0f),
          "the picked world point (+0x3d0) with its height");
    put(controller, MT_CONTROLLER_PICK, 0.0f, 0.0f, 0.0f);
    check(mt_target(controller, t) == MT_TARGET_CURSOR && near(t[0], 1200.0f) && near(t[1], 0.0f) &&
              near(t[2], -3400.0f),
          "no pick yet (zeros): the cursor's x and z (+0x2c0, +0x2c8), height 0");
    put(controller, MT_CONTROLLER_PICK, NAN, 3.0f, 5.0f);
    check(mt_target(controller, t) == MT_TARGET_CURSOR, "a NaN pick: the cursor");
    put(controller, MT_CONTROLLER_PICK, 1.0e9f, 3.0f, 5.0f);
    check(mt_target(controller, t) == MT_TARGET_CURSOR, "a pick outside the world: the cursor");
    put(controller, MT_CONTROLLER_CURSOR, INFINITY, 0.0f, 10.0f);
    check(mt_target(controller, t) == MT_TARGET_NONE, "neither usable: no target");
    put(controller, MT_CONTROLLER_CURSOR, 0.0f, 0.0f, 0.0f);
    put(controller, MT_CONTROLLER_PICK, 0.0f, 0.0f, 0.0f);
    check(mt_target(controller, t) == MT_TARGET_NONE, "an untouched controller (all zeros): no target");
    put(controller, MT_CONTROLLER_PICK, -4999.0f, -12.0f, 4999.0f);
    check(mt_target(controller, t) == MT_TARGET_PICK && near(t[0], -4999.0f) && near(t[1], -12.0f),
          "negative coordinates and height are kept");

    printf("press of the button (polled every map frame)\n");
    MtPress p = {0};
    check(mt_poll(&p, 1, 1000, 500) == 0, "held when the map opens: not armed, nothing");
    check(mt_poll(&p, 0, 1016, 500) == 0 && p.armed, "released: armed");
    check(mt_poll(&p, 1, 1032, 500) == MT_EV_PRESSED, "a new press");
    check(mt_poll(&p, 1, 1500, 500) == 0, "held 468 ms: wait");
    check(mt_poll(&p, 1, 1532, 500) == MT_EV_TELEPORT, "held 500 ms: travel");
    check(mt_poll(&p, 1, 1600, 500) == 0, "still held: once only");
    check(mt_poll(&p, 0, 1650, 500) == 0, "released after travelling: nothing");
    check(mt_poll(&p, 1, 2000, 500) == MT_EV_PRESSED && mt_poll(&p, 0, 2200, 500) == MT_EV_RELEASED,
          "a tap without the game's handler: pressed, released");

    printf("the game's handler held back\n");
    p = (MtPress){1, 0, 0, 0, 0, 0};
    mt_defer(&p, 3000);
    check(mt_poll(&p, 1, 3010, 500) == MT_EV_PRESSED && p.deferred, "handler came, then the press is seen: waits");
    check(mt_poll(&p, 0, 3150, 500) == (MT_EV_RELEASED | MT_EV_NATIVE), "a tap: released, the game's handler runs");
    p = (MtPress){1, 0, 0, 0, 0, 0};
    mt_defer(&p, 4000);
    mt_poll(&p, 1, 4005, 500);
    check(mt_poll(&p, 1, 4505, 500) == MT_EV_TELEPORT && !p.deferred, "held: travel, the game's handler never runs");
    check(mt_poll(&p, 0, 4600, 500) == 0, "and not on the release either");
    p = (MtPress){1, 0, 0, 0, 0, 0};
    mt_defer(&p, 5000);
    check(mt_poll(&p, -1, 5100, 500) == 0, "the axis unreadable: wait a moment");
    check(mt_poll(&p, -1, 5000 + MT_DEFER_GRACE_MS, 500) == MT_EV_NATIVE,
          "never seen held: the game's handler runs after the grace");
    p = (MtPress){1, 0, 0, 0, 0, 0};
    mt_defer(&p, 6000);
    check(mt_poll(&p, 1, 6000, 0) == (MT_EV_PRESSED | MT_EV_TELEPORT) && !p.deferred,
          "hold 0: travel on the press, the handler dropped");
    p = (MtPress){1, 0, 0, 0, 0, 0};
    mt_defer(&p, 7000);
    mt_defer(&p, 7100);
    check(p.deferred_at == 7000, "a repeated key down keeps the first one");

    printf("legend caption\n");
    check(!wcscmp(mt_caption("EN"), L"Teleport (hold)") && !wcscmp(mt_caption(NULL), L"Teleport (hold)") &&
              !wcscmp(mt_caption("XX"), L"Teleport (hold)"),
          "English, also for no or an unknown language");
    check(!wcscmp(mt_caption("UK"), L"Телепорт "
                                     L"(утримуйте)"),
          "Ukrainian: Телепорт (утримуйте)");
    check(!wcscmp(mt_caption("PL"), L"Teleport (przytrzymaj)") && mt_caption("CHT") != mt_caption("CH"),
          "Polish; codes match exactly");

    printf(failures ? "%d failure(s)\n" : "all logic tests passed\n", failures);
    return failures ? 1 : 0;
}
