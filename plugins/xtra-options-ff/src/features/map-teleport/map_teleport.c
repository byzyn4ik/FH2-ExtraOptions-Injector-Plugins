// Xtra Options (Fast & Furious), MapTeleport.dll: hold X on the world map to
// travel to the map cursor, with the game's own fast travel, free of charge.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fast & Furious 1.0.0.6 (forza_durango_release_final.exe 0x55b62fde); every
// address below is checked by patches.json (code around it, byte for byte),
// by the slot patches here (the slot's own value) or at run time (the
// CUserString vtable's slots) before it is used; on another build nothing
// changes and the log says why.
//
// The map (MapScene, vtable 0x2e48a70) binds the pad's X to 0xda2a20 (UI key
// 0x43 at 0xd7e411, through a member-function closure, vtable 0x2e52f30) and
// Y to 0xd7a1f0 (key 0x44, closure vtable 0x2e52ff0): X is the game's fast
// travel (a snapped festival site or house; anywhere only once every
// fast-travel board is smashed, at a price), Y sets or clears the waypoint.
//
//   - MapScene's update (slot 153, 0xda0080) is hooked. After the game's
//     update it polls the button through its axis (0x1095250, UI context 7,
//     action 0x8e for X / 0x91 for Y, on the controller the map itself reads:
//     0xd43e60 -> slot 0x140). Not its state query (0x10948e0): the game's
//     key translation consumes the press (consume mask +0x1e8, read by
//     0x1095100), so the state reads "up" right after the press, and
//     Keyboard & Mouse answers a consumed key as up too; the axis is not
//     consumed. Held for TeleportHold ms anywhere on the map: travel.
//   - The closure's invoke slot (vtable +0x10) is hooked: when the game's
//     handler for the press comes (the key down), it is held back until the
//     press resolves; released before TeleportHold, it runs (a tap stays the
//     game's), held, it never runs.
//   - The legend: the map's button bar (0xd44610(page)) gets X's text
//     (0xba1e90(bar, 2 = XDescription, text), as the map sets its other
//     buttons each frame) in the game's language, a CUserString (vtable
//     0x2ef91c0) made a UI string by 0x1431f40. Keyboard & Mouse draws its
//     keycap over the X icon like any other prompt.
//   - Travel: the game's fast travel to a position, FastTravelModule
//     0x561d90(module, &position, cost 0) (the module from 0x90e9b0 on the
//     entity root 0x352b3b0), to the world point under the cursor
//     (controller +0x3d0, the point the game's own fast travel uses). The
//     game then moves the car to the nearest road node facing along it and
//     waits for streaming; the map closes the way the game's fast-travel
//     confirmation closes it (0xd6bd90).
//   - Only in free roam: the event context (0x219530 on the root, +0x60,
//     2 = free roam); elsewhere the button stays the game's.
#include "forza_patch.h"
#include "forza_plugin.h"
#include "../../../../../common/include/fh2_game.h" /* the repository's (the SDK has another fh2_game.h) */

#include "feature_code.h"
#include "map_teleport_logic.h"

#define IMAGE_SIZE 0x3a89000u

#define MAP_SCENE_VTABLE 0x2e48a70u
#define MAP_SCENE_UPDATE_SLOT (MAP_SCENE_VTABLE + 153u * 8u) /* 0x2e48f38 */
#define MAP_SCENE_UPDATE 0xda0080u
#define MAP_SCENE_INTERACTIVE_SLOT 0x598u /* 0xd7ebf0 */
#define MAP_SCENE_ACTIVE 0xd7ed80u
#define MAP_SCENE_CONTROLLER 0x428u
#define MAP_SCENE_TRANSITION 0x310u
#define MAP_CONTROLLER_VTABLE 0x2de6510u /* MapViewControllerInteractive */
#define MAP_CONTROLLER_COPY 0x3e0u

#define X_CLOSURE_INVOKE_SLOT (0x2e52f30u + 0x10u)
#define X_CLOSURE_INVOKE 0xeff220u
#define X_HANDLER 0xda2a20u
#define Y_CLOSURE_INVOKE_SLOT (0x2e52ff0u + 0x10u)
#define Y_CLOSURE_INVOKE 0xdb8270u

#define ACTIVE_USER 0xd43e60u
#define USER_CONTROLLER_SLOT 0x140u
#define ACTION_AXIS 0x1095250u
#define CONTEXT_UI 7u
#define ACTION_X_PRESS 0x8eu /* Forza Horizon 2's 0x8d; Fast & Furious' UI actions sit one higher */
#define ACTION_Y_PRESS 0x91u

#define HELP_BAR 0xd44610u
#define HELP_BAR_SET 0xba1e90u
#define HELP_SLOT_X 2 /* 0xb68300: 0 A, 1 B, 2 X (XDescription 0x354eb78), 3 Y */
#define UI_STRING_FROM_LOC 0x1431f40u
#define UI_STRING_FREE 0x25ed30u
#define USER_STRING_VTABLE 0x2ef91c0u /* CUserString: +8 text, +0x10 capacity, +0x14 references */
#define TEXT_TABLES 0x36cc1b0u        /* Forza Horizon 2's 0x3662990 (FH2_TEXT_TABLES) */

#define ENTITY_ROOT 0x352b3b0u
#define EVENT_CONTEXT_GET 0x219530u
#define EVENT_CONTEXT_VALUE 0x60u
#define EVENT_CONTEXT_FREE_ROAM 2
#define FAST_TRAVEL_GET 0x90e9b0u
#define FAST_TRAVEL_VTABLE 0x2cd88d8u /* EntityModel::FastTravelModule */
#define FAST_TRAVEL_ENABLED 0x68u
#define FAST_TRAVEL_TO_POSITION 0x561d90u
#define CLOSE_PAGE 0x1449ed0u
#define CLOSE_PAGE_TRANSITION 0x35907f8u
#define CLOSE_PAGE_DEFAULT 0x35907e8u

#define FREE_ROAM_RECHECK_MS 1000u

typedef void (*UpdateFn)(void *page);
typedef unsigned char (*InvokeFn)(void *closure, int *key);
typedef char (*PageCheckFn)(void *page);
typedef void **(*ActiveUserFn)(void **out);
typedef void *(*UserControllerFn)(void *user, int index);
typedef void (*ReleaseFn)(void *object);
typedef float (*AxisFn)(void *controller, unsigned context, unsigned action);
typedef struct {
    void *object, *counter;
} Shared;
typedef Shared *(*SharedGetFn)(void *root, Shared *out);
typedef void (*TravelFn)(void *module, const float *position, int cost);
typedef void (*ClosePageFn)(void *page, void *transition);
typedef void (*TransitionFn)(void *transition, void *page);
typedef void *(*HelpBarFn)(void *page);
typedef void (*HelpBarSetFn)(void *bar, int slot, void **text);
typedef void **(*UiStringFn)(void **out, void **loc);

static unsigned char *image;
static void (*host_log)(const char *message);
static MtSettings settings = {MT_BUTTON_X, MT_HOLD_DEFAULT_MS, 0, 0};

static unsigned char update_original[8], update_hook_bytes[8];
static unsigned char invoke_original[8], invoke_hook_bytes[8];
static const unsigned char *const update_accepted[] = {update_original, update_hook_bytes};
static const unsigned char *const invoke_accepted[] = {invoke_original, invoke_hook_bytes};
static ForzaPatch slot_patches[2];

/* The map being polled (UI thread). */
static void *map_page;
static MtPress press;
static void *deferred_closure;
static int deferred_key;
static int free_roam_now;
static uint64_t free_roam_checked;
static DWORD invoke_thread, update_thread;
static int threads_differ;

/* The legend caption: a CUserString the game reads like its own and never
   frees (capacity 0 keeps its text, the reference count never reaches 0). */
typedef struct {
    void *vtable;
    const wchar_t *text;
    uint32_t capacity;
    volatile LONG references;
} UserString;
static UserString caption = {0, 0, 0, 0x10000000};
static int legend_state; /* 0 not checked, 1 usable, -1 not */
static void pick_caption(void);

/* ---------- log ---------- */

static void say(const char *a, const char *b, const char *c) {
    char line[320];
    size_t n = 0;
    const char *parts[] = {"XTRA OPTIONS: Map Teleport: ", a, b, c, "\r\n"};
    for (size_t p = 0; p < sizeof(parts) / sizeof(parts[0]); ++p)
        for (const char *s = parts[p]; s && *s && n + 1 < sizeof(line); ++s) line[n++] = *s;
    line[n] = 0;
    if (host_log) host_log(line);
}

static void hex_text(uint64_t value, char *out) {
    static const char digits[] = "0123456789abcdef";
    char tmp[17];
    int n = 0;
    do tmp[n++] = digits[value & 15]; while ((value >>= 4) && n < 16);
    out[0] = '0';
    out[1] = 'x';
    for (int i = 0; i < n; ++i) out[2 + i] = tmp[n - 1 - i];
    out[2 + n] = 0;
}

static void number_text(long value, char *out) {
    char tmp[24];
    int n = 0, negative = value < 0;
    unsigned long v = negative ? (unsigned long)(-value) : (unsigned long)value;
    do tmp[n++] = (char)('0' + v % 10); while ((v /= 10) && n < 22);
    int at = 0;
    if (negative) out[at++] = '-';
    while (n) out[at++] = tmp[--n];
    out[at] = 0;
}

static void position_text(const float p[3], char *out) {
    size_t at = 0;
    for (int i = 0; i < 3; ++i) {
        char part[24];
        number_text((long)(p[i] < 0 ? p[i] - 0.5f : p[i] + 0.5f), part);
        if (i) out[at++] = ' ';
        for (const char *s = part; *s; ++s) out[at++] = *s;
    }
    out[at] = 0;
}

static const char *button_name(void) {
    return settings.button == MT_BUTTON_X ? "X" : "Y";
}

/* ---------- memory ---------- */

static int read_memory(const void *address, void *out, size_t size) {
    SIZE_T got = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), address, out, size, &got) && got == size;
}

static void *read_pointer(const void *address) {
    void *value = NULL;
    return read_memory(address, &value, sizeof(value)) ? value : NULL;
}

static int has_vtable(const void *object, uint32_t rva) {
    return object && read_pointer(object) == (void *)(image + rva);
}

static int in_image_code(const void *address) {
    return (const unsigned char *)address >= image + 0x1000u && (const unsigned char *)address < image + IMAGE_SIZE;
}

static void release_shared(Shared *value) {
    unsigned char *counter = value->counter;
    if (counter && InterlockedDecrement((volatile LONG *)(counter + 8)) == 0) {
        void **vtable = *(void ***)counter;
        ((ReleaseFn)vtable[0])(counter);
        if (InterlockedDecrement((volatile LONG *)(counter + 12)) == 0) ((ReleaseFn)vtable[1])(counter);
    }
    value->object = value->counter = NULL;
}

/* ---------- game state ---------- */

static void *entity_root(void) {
    return read_pointer(image + ENTITY_ROOT);
}

/* 1 in free roam, 0 elsewhere, -1 unknown (no root or context yet). */
static int free_roam(void) {
    void *root = entity_root();
    if (!root || !read_pointer((unsigned char *)root + 8)) return -1;
    Shared context = {0};
    ((SharedGetFn)(void *)(image + EVENT_CONTEXT_GET))(root, &context);
    int value = -1;
    if (!context.object || !read_memory((unsigned char *)context.object + EVENT_CONTEXT_VALUE, &value, 4)) value = -1;
    release_shared(&context);
    return value < 0 ? -1 : value == EVENT_CONTEXT_FREE_ROAM;
}

/* The fast travel module (owned reference in *out), or NULL. */
static void *fast_travel_module(Shared *out) {
    void *root = entity_root();
    *out = (Shared){0};
    if (!root || !read_pointer((unsigned char *)root + 8)) return NULL;
    ((SharedGetFn)(void *)(image + FAST_TRAVEL_GET))(root, out);
    if (!out->object) {
        release_shared(out);
        return NULL;
    }
    return out->object;
}

/* The map page a closure of the teleport button calls, or NULL. */
static void *closure_page(void *closure) {
    void *page = NULL;
    if (settings.button == MT_BUTTON_X) {
        /* member-function closure: +8 the function, +0x10 this adjustment, +0x20 the object */
        void *function = read_pointer((unsigned char *)closure + 8);
        int adjust = 0;
        void *object = read_pointer((unsigned char *)closure + 0x20);
        if (function != image + X_HANDLER || !read_memory((unsigned char *)closure + 0x10, &adjust, 4) || !object)
            return NULL;
        page = (unsigned char *)object + adjust;
    } else {
        /* lambda closure: +8 the page (0xdb8270 passes closure + 8 to 0xd7a1f0, which reads [rcx]) */
        page = read_pointer((unsigned char *)closure + 8);
    }
    return has_vtable(page, MAP_SCENE_VTABLE) ? page : NULL;
}

/* 1 down, 0 up, -1 unknown: the button's axis on the controller the map reads. */
static int button_held(void) {
    void *user = NULL;
    ((ActiveUserFn)(void *)(image + ACTIVE_USER))(&user);
    if (!user) return -1;
    void *vtable = read_pointer(user), *method = vtable ? read_pointer((unsigned char *)vtable + USER_CONTROLLER_SLOT) : NULL;
    void *controller = in_image_code(method) ? ((UserControllerFn)method)(user, 0) : NULL;
    void *release = vtable ? read_pointer((unsigned char *)vtable + 0x18) : NULL;
    int held = -1;
    if (controller) {
        unsigned action = settings.button == MT_BUTTON_X ? ACTION_X_PRESS : ACTION_Y_PRESS;
        float value = ((AxisFn)(void *)(image + ACTION_AXIS))(controller, CONTEXT_UI, action);
        held = value > 0.5f || value < -0.5f;
    }
    if (in_image_code(release)) ((ReleaseFn)release)(user);
    return held;
}

static int map_page_active(void *page) {
    void *vtable = read_pointer(page);
    void *interactive = vtable ? read_pointer((unsigned char *)vtable + MAP_SCENE_INTERACTIVE_SLOT) : NULL;
    return in_image_code(interactive) && ((PageCheckFn)(void *)(image + MAP_SCENE_ACTIVE))(page) &&
           ((PageCheckFn)interactive)(page);
}

static void close_map(void *page) {
    void *transition = NULL;
    if (read_memory((unsigned char *)page + MAP_SCENE_TRANSITION, &transition, sizeof(transition)) && transition) {
        ((ClosePageFn)(void *)(image + CLOSE_PAGE))(page, image + CLOSE_PAGE_TRANSITION);
        return;
    }
    void **vtable = read_pointer(image + CLOSE_PAGE_DEFAULT);
    void *method = vtable ? read_pointer(vtable + 2) : NULL;
    if (in_image_code(method)) ((TransitionFn)method)(image + CLOSE_PAGE_DEFAULT, page);
}

/* ---------- the legend ---------- */

static int legend_usable(void) {
    if (legend_state) return legend_state > 0;
    void **vtable = (void **)(image + USER_STRING_VTABLE);
    void *text = read_pointer(vtable + 7), *add = read_pointer(vtable + 2), *release = read_pointer(vtable + 3);
    legend_state = text == image + 0x1e0830u && add == image + 0x75e3e0u && release == image + 0x758780u ? 1 : -1;
    if (legend_state < 0) say("the CUserString vtable is not the expected one; no legend entry", "", "");
    return legend_state > 0;
}

static void set_legend(void *page) {
    if (settings.button != MT_BUTTON_X || !legend_usable()) return; /* Y's text is the map's own every frame */
    void *bar = ((HelpBarFn)(void *)(image + HELP_BAR))(page);
    if (!bar) return;
    void *loc = &caption, *ui = NULL;
    ((UiStringFn)(void *)(image + UI_STRING_FROM_LOC))(&ui, &loc);
    if (!ui) return;
    /* the bar takes its own reference (released by 0xba1e90); ours is dropped after */
    InterlockedIncrement((volatile LONG *)((unsigned char *)ui + 0x10));
    void *argument = ui;
    ((HelpBarSetFn)(void *)(image + HELP_BAR_SET))(bar, HELP_SLOT_X, &argument);
    if (InterlockedDecrement((volatile LONG *)((unsigned char *)ui + 0x10)) == 0)
        ((ReleaseFn)(void *)(image + UI_STRING_FREE))(ui);
}

/* ---------- travel ---------- */

static void teleport(void *page) {
    int roam = free_roam();
    if (roam != 1) {
        say(roam < 0 ? "not travelling: the game's event context is unknown" : "not travelling: not in free roam",
            "", "");
        return;
    }
    if (!map_page_active(page)) {
        say("not travelling: the map is no longer the active page", "", "");
        return;
    }
    void *controller = read_pointer((unsigned char *)page + MAP_SCENE_CONTROLLER);
    unsigned char copy[MAP_CONTROLLER_COPY];
    if (!has_vtable(controller, MAP_CONTROLLER_VTABLE) || !read_memory(controller, copy, sizeof(copy))) {
        say("not travelling: the map has no cursor controller", "", "");
        return;
    }
    float target[4] __attribute__((aligned(16)));
    int source = mt_target(copy, target);
    if (source == MT_TARGET_NONE) {
        say("not travelling: the map cursor has no usable world position", "", "");
        return;
    }
    Shared module_ref;
    void *module = fast_travel_module(&module_ref);
    if (!module || !has_vtable(module, FAST_TRAVEL_VTABLE)) {
        char vt[24];
        hex_text(module ? (uint64_t)((unsigned char *)read_pointer(module) - image) : 0, vt);
        say("not travelling: no fast travel module (vtable ", vt, ")");
        release_shared(&module_ref);
        return;
    }
    unsigned char enabled = 0;
    read_memory((unsigned char *)module + FAST_TRAVEL_ENABLED, &enabled, 1);
    char where[96];
    position_text(target, where);
    say(source == MT_TARGET_PICK ? "travelling to the point under the map cursor, " : "travelling to the map cursor, ",
        where, enabled ? " (the game's fast travel, free)"
                       : " (the game's fast travel, free; the game has not enabled fast travel yet)");
    ((TravelFn)(void *)(image + FAST_TRAVEL_TO_POSITION))(module, target, 0);
    release_shared(&module_ref);
    close_map(page);
}

/* ---------- hooks ---------- */

static void run_deferred(const char *why) {
    void *closure = deferred_closure;
    int key = deferred_key;
    deferred_closure = NULL;
    if (!closure) return;
    say(button_name(), why, "; the game's handler runs");
    (*(InvokeFn *)invoke_original)(closure, &key);
}

static unsigned char invoke_hook(void *closure, int *key) {
    InvokeFn original = *(InvokeFn *)invoke_original;
    invoke_thread = GetCurrentThreadId();
    void *page = closure_page(closure);
    if (!page || threads_differ || page != map_page || free_roam_now != 1) return original(closure, key);
    if (deferred_closure) return 1; /* the same press again (a repeat): still held back */
    deferred_closure = closure;
    deferred_key = key ? *key : 0;
    mt_defer(&press, GetTickCount64());
    say(button_name(), " down on the map: the game's handler waits for the release or the hold", "");
    return 1;
}

static void update_hook(void *page) {
    (*(UpdateFn *)update_original)(page);
    uint64_t now = GetTickCount64();
    if (page != map_page) {
        /* a new map: start unarmed (a button already held does not count) */
        if (deferred_closure) deferred_closure = NULL;
        map_page = page;
        press = (MtPress){0};
        free_roam_checked = 0;
        update_thread = GetCurrentThreadId();
    }
    if (threads_differ) return;
    if (update_thread != GetCurrentThreadId() || (invoke_thread && invoke_thread != update_thread)) {
        /* Not expected: the map's keys and its update share the UI thread. Never call the
           game's handler from another thread; the button stays the game's from now on. */
        threads_differ = 1;
        deferred_closure = NULL;
        say("the map's button handler and its update run on different threads; teleport off, the button stays "
            "the game's", "", "");
        return;
    }
    if (!free_roam_checked || now - free_roam_checked >= FREE_ROAM_RECHECK_MS) {
        int roam = free_roam();
        if (!free_roam_checked)
            say(roam == 1 ? "map open in free roam; hold " : "map open outside free roam; the game keeps ",
                button_name(), roam == 1 ? " to travel" : "");
        free_roam_now = roam;
        free_roam_checked = now ? now : 1;
    }
    if (free_roam_now != 1) {
        if (deferred_closure) run_deferred(": outside free roam");
        press = (MtPress){0};
        return;
    }
    pick_caption();
    set_legend(page);
    int held = button_held();
    int events = mt_poll(&press, held, now, settings.hold_ms);
    if (events & MT_EV_PRESSED) {
        char ms[16];
        number_text((long)settings.hold_ms, ms);
        say(button_name(), " pressed on the map; travel after (ms) ", ms);
    }
    if (events & MT_EV_RELEASED) {
        char ms[16];
        number_text((long)(now - press.since), ms);
        say(button_name(), " released after (ms) ", ms);
    }
    if (events & MT_EV_TELEPORT) {
        deferred_closure = NULL;
        teleport(page);
    }
    if (events & MT_EV_NATIVE) run_deferred(held < 0 ? ": its state is unreadable" : ": a tap");
}

/* ---------- feature_code.h ---------- */

void feature_context(const ForzaContext *context, const wchar_t *ini) {
    host_log = context->v1->log_utf8;
    wchar_t button[16];
    GetPrivateProfileStringW(L"Map", L"TeleportButton", L"X", button, sizeof(button) / sizeof(button[0]), ini);
    long hold_ms = (long)(int)GetPrivateProfileIntW(L"Map", L"TeleportHold", (int)MT_HOLD_DEFAULT_MS, ini);
    mt_parse_settings(button, hold_ms, &settings);
    if (settings.button_invalid) say("[Map] TeleportButton is not X or Y; X used", "", "");
    if (settings.hold_clamped) say("[Map] TeleportHold is outside 0..5000 ms; clamped", "", "");
}

static void pointer_bytes(unsigned char out[8], const void *pointer) {
    uintptr_t value = (uintptr_t)pointer;
    for (unsigned i = 0; i < 8; ++i) out[i] = (unsigned char)(value >> (8 * i));
}

void feature_prepare(unsigned char *game_image) {
    image = game_image;
    int x = settings.button == MT_BUTTON_X;
    pointer_bytes(update_original, image + MAP_SCENE_UPDATE);
    pointer_bytes(update_hook_bytes, (const void *)update_hook);
    pointer_bytes(invoke_original, image + (x ? X_CLOSURE_INVOKE : Y_CLOSURE_INVOKE));
    pointer_bytes(invoke_hook_bytes, (const void *)invoke_hook);
    slot_patches[0] = (ForzaPatch){"map_scene_update_slot", MAP_SCENE_UPDATE_SLOT, 8, update_hook_bytes,
                                   update_accepted, 2};
    slot_patches[1] = (ForzaPatch){x ? "map_x_closure_invoke_slot" : "map_y_closure_invoke_slot",
                                   x ? X_CLOSURE_INVOKE_SLOT : Y_CLOSURE_INVOKE_SLOT, 8, invoke_hook_bytes,
                                   invoke_accepted, 2};
    caption.vtable = image + USER_STRING_VTABLE;
    caption.text = mt_caption(NULL);
    char text[16];
    number_text((long)settings.hold_ms, text);
    say(x ? "button X, hold (ms) " : "button Y, hold (ms) ", text, "");
}

/* The caption's language: the game's text language read like fh2_text_language reads FH2's (the text
   table manager, Fast & Furious' 0x36cc1b0, +0x30), a Language Support pack in its slot counting. */
static void pick_caption(void) {
    static int picked;
    if (picked) return;
    char code[4];
    if (fh2_text_language(image + TEXT_TABLES - FH2_TEXT_TABLES, code)) {
        caption.text = mt_caption(code);
        picked = 1;
        say("legend language ", code, "");
    }
}

const ForzaPatch *feature_extra(const char *group, uint32_t *count) {
    const char *name = "MAP_TELEPORT";
    unsigned i = 0;
    while (group[i] && group[i] == name[i]) ++i;
    if (group[i] || name[i] || !image) {
        *count = 0;
        return 0;
    }
    *count = 2;
    return slot_patches;
}
