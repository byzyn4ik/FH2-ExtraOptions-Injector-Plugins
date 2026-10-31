// Xtra Features: the World menu (Forza Horizon 2 and Fast & Furious).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A WORLD tile in the pause menu's Settings hub (A_PauseOptions), in the lower
// half of the Team Credits column, opens the native Video page rebuilt as a
// WORLD page with two rows: Time of day (Game, Sunrise, Midday, Sunset,
// Midnight) and Weather (Game, Sunny, Wet, Rain, Storm). The choices apply at
// once, on the game thread, and are kept in XtraFeatures.ini [WorldMenu].
// From our Extra Options fork (features/world_menu.c, time_weather.c,
// video_options_xaml.c), and Keyboard & Mouse's page (settings_page.c).
//
// 1. The hub. PauseOptionsMenu's vtable slot holds its page init, which binds
//    each tile with the binder (page, "PART_Button_<tile>", &{handler, 0});
//    the binder returns quietly when no element has that name. The slot is
//    chained: the previous init, then PART_Button_XFWorld bound to world_tile,
//    which notes the request and runs the Video tile's own handler (it starts
//    the transition to the Video page).
// 2. The XAML. The game reads every XAML resource through one call (FH2
//    0x16ebb37, Fast & Furious 0x1712d47). It is chained: the previous target
//    (the game's reader, Extra Options' or another plugin's hook) runs first,
//    then the hub gets the tile, and a Video page read within a few seconds
//    of the tile becomes the WORLD page (marked EO:StandaloneOptionsPage so
//    other plugins leave it alone).
// 3. The rows bind to int properties ("XfTimeOfDay", "XfWeather") and their
//    choice lists, linked into the VideoOptions type with the page's own
//    Subtitles / OnOffOptions descriptors as templates, as Extra Options does
//    for its rows.
// 4. The apply. The open world's per-frame update ticks the weather manager
//    [world+0x488] and the time-of-day manager [world+0x480] with
//    "mov rcx,[rsi+manager]; call rel32". Both calls are chained to handlers
//    that apply the choice (SetTime with the game's remap; SetDirectControl /
//    ClearDirectControl, the game's own weather override) and then tick. The
//    world exists only in the open world; menus and loads skip them.
//
// Fast & Furious 1.0.0.6 has the same code at its own addresses (found by
// the code around each site, Keyboard & Mouse's tools/port_sites.py and
// xtra-options-ff/tools/port_feature.py). Every site is checked byte for byte
// first; on any difference nothing is changed and the log says why.
#include <stdio.h>
#include <string.h>

#include "fh2_game.h"  // fh2_text_language, FH2_TEXT_TABLES
#include "xtra.h"

enum {
    MAX_XAML = 1 << 20,
    EXTRA_ROOM = 0x10000,
    REQUEST_WINDOW_MS = 5000,  // the WORLD page must be read this soon after the tile
};

enum {
    // Natives (allocate, free, object init/cleanup/delete) and the XAML reader.
    S_ALLOCATE, S_FREE, S_OBJECT_INIT, S_OBJECT_CLEANUP, S_DELETE_OBJECT, S_RESOURCE_CALL, S_GET_RESOURCE,
    // The VideoOptions type and its property machinery.
    S_TYPE_INIT, S_REGISTRY, S_PAGE_LEA, S_PAGE_CALL, S_REGISTER_PROPERTY, S_INT_TEMPLATE, S_INT_TEMPLATE_NAME,
    S_LIST_TEMPLATE, S_LIST_TEMPLATE_NAME, S_BOX_INT, S_PROPERTY_CHANGED, S_COLLECTION_TYPE, S_ALLOCATE_TYPED,
    S_COLLECTION_INIT, S_MAKE_STRING, S_INSERT_ITEM, S_ITEM_VTABLE0, S_ITEM_VTABLE1, S_ITEM_VTABLE2,
    S_ITEM_VTABLE3, S_ITEM_VTABLE4,
    // The Settings hub.
    S_HUB_SLOT, S_HUB_INIT, S_HUB_BIND, S_VIDEO_TILE,
    // The open world's update and the time of day / weather managers.
    S_WEATHER_LOAD, S_TIME_LOAD, S_WEATHER_TICK, S_TIME_TICK, S_SET_TIME, S_SET_DIRECT, S_CLEAR_DIRECT,
    S_TEXT_TABLES, S_EXTRA_OPTIONS_THUNK,
    SITE_COUNT
};

typedef struct {
    const char *name;
    uint32_t image_size, stamp;
    uint32_t rva[SITE_COUNT];
} Build;

static const Build builds[] = {
    {"Forza Horizon 2 1.0.0.64", 0x3a18000, 0x565501ed,
     {0x3229c0, 0x31c7b0, 0x143eb80, 0x1442050, 0x257730, 0x16ebb37, 0xaabec0,
      0xeca710, 0x35470d0, 0xeca808, 0xeca80f, 0x155ee30, 0xed81f0, 0xed825b,
      0xed77d0, 0xed782e, 0x4702d0, 0xcf9c60, 0x1476110, 0x14424e0,
      0xaefd60, 0x13fe8e0, 0xb52170, 0x2db73f8, 0x2db7470, 0x2db74d0,
      0x2db74e8, 0x2db7508,
      0x2e15940, 0xdc8a60, 0xd9fc10, 0xdcfcd0,
      0x300715, 0x30092c, 0x19848b0, 0x197b170, 0x197aec0, 0x1981860, 0x1981c80,
      0x3662990, 0x13fe354}},
    {"Forza Horizon 2 Presents Fast & Furious 1.0.0.6", 0x3a89000, 0x55b62fde,
     {0x3286d0, 0x322a70, 0x1465c90, 0x1469160, 0x25ed30, 0x1712d47, 0xc795c0,
      0xee4ca0, 0x35adf70, 0xee4d98, 0xee4d9f, 0x1586070, 0xef8ac0, 0xef8b2b,
      0xef7aa0, 0xef7afe, 0x479fa0, 0xd51ae0, 0x1499af0, 0x14695f0,
      0xb2a7d0, 0x1425a40, 0xb8afc0, 0x2dffcf8, 0x2dffd70, 0x2dffdd0,
      0x2dffde8, 0x2dffe08,
      0x2e5c1f0, 0xde2e00, 0xdc2510, 0xde78c0,
      0x306fb5, 0x3071cc, 0x19adbd0, 0x19a4140, 0x19a3e90, 0x19aab60, 0x19aaf80,
      0x36cc1b0, 0}},
};
static const unsigned item_offsets[] = {0, 0x30, 0x38, 0x40, 0x48};

// The code each site must hold, the same in both builds (the bytes before any
// relocated field: rip displacements and call targets differ between them).
typedef struct {
    unsigned site;
    const char *name;
    unsigned length;
    const unsigned char *bytes;
} SiteCode;
#define SITE(site, name, ...) \
    {site, name, sizeof((const unsigned char[]){__VA_ARGS__}), (const unsigned char[]){__VA_ARGS__}}
static const SiteCode site_code[] = {
    SITE(S_ALLOCATE, "allocate", 0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x8b, 0x15),
    SITE(S_FREE, "free", 0x48, 0x85, 0xc9, 0x74, 0x1d, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b),
    SITE(S_OBJECT_INIT, "object init", 0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8d),
    SITE(S_OBJECT_CLEANUP, "object cleanup", 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8d, 0x05),
    SITE(S_DELETE_OBJECT, "delete object", 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0xe8),
    SITE(S_GET_RESOURCE, "resource reader", 0x48, 0x83, 0xec, 0x18, 0x48, 0x8b, 0x41, 0x38, 0xc7, 0x04, 0x24, 0x00),
    SITE(S_TYPE_INIT, "VideoOptions type set-up", 0x40, 0x53, 0x48, 0x81, 0xec, 0xd0, 0x00, 0x00, 0x00, 0x48, 0xc7,
         0x44, 0x24, 0x20, 0xfe, 0xff),
    SITE(S_REGISTER_PROPERTY, "register property", 0x48, 0x8b, 0x41, 0x40, 0x48, 0x89, 0x42, 0x08, 0x48, 0x89, 0x51,
         0x40, 0xc3),
    SITE(S_INT_TEMPLATE, "int property template", 0x40, 0x57, 0x48, 0x83, 0xec, 0x30, 0x48, 0xc7, 0x44, 0x24, 0x20,
         0xfe, 0xff, 0xff, 0xff, 0x48),
    SITE(S_LIST_TEMPLATE, "list property template", 0x40, 0x57, 0x48, 0x83, 0xec, 0x30, 0x48, 0xc7, 0x44, 0x24, 0x20,
         0xfe, 0xff, 0xff, 0xff, 0x48),
    SITE(S_BOX_INT, "box int", 0x40, 0x53, 0x48, 0x83, 0xec, 0x30, 0x48, 0x8b, 0xd9, 0xc7, 0x44, 0x24),
    SITE(S_PROPERTY_CHANGED, "property changed", 0x40, 0x57, 0x48, 0x83, 0xec, 0x60, 0x48, 0xc7, 0x44, 0x24, 0x20,
         0xfe),
    SITE(S_COLLECTION_TYPE, "collection type", 0x48, 0x8d, 0x05),
    SITE(S_ALLOCATE_TYPED, "typed allocate", 0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b),
    SITE(S_COLLECTION_INIT, "collection init", 0x48, 0x89, 0x4c, 0x24, 0x08, 0x55, 0x56, 0x57, 0x48, 0x83, 0xec, 0x30),
    SITE(S_MAKE_STRING, "make string", 0x40, 0x57, 0x48, 0x83, 0xec, 0x30, 0x48, 0xc7, 0x44, 0x24, 0x28, 0xfe),
    SITE(S_INSERT_ITEM, "insert item", 0x48, 0x8b, 0xc4, 0x57, 0x48, 0x81, 0xec, 0x80, 0x00, 0x00, 0x00, 0x48),
    SITE(S_HUB_INIT, "Settings hub init", 0x48, 0x8b, 0xc4, 0x55, 0x48, 0x8d, 0x68, 0xd8, 0x48, 0x81, 0xec, 0x20,
         0x01, 0x00, 0x00, 0x48),
    SITE(S_HUB_BIND, "Settings hub tile binder", 0x48, 0x8b, 0xc4, 0x57, 0x48, 0x81, 0xec, 0x90, 0x00, 0x00, 0x00,
         0x48, 0xc7, 0x40, 0xa0, 0xfe),
    SITE(S_VIDEO_TILE, "Video tile handler", 0x40, 0x53, 0x48, 0x83, 0xec, 0x30, 0x48, 0x8b, 0xd9, 0x48, 0x8b, 0x89,
         0x10, 0x04, 0x00, 0x00),
    // mov rcx,[rsi+0x488]; call <weather tick> / mov rcx,[rsi+0x480]; call <time tick>
    SITE(S_WEATHER_LOAD, "the world update's weather tick", 0x48, 0x8b, 0x8e, 0x88, 0x04, 0x00, 0x00, 0xe8),
    SITE(S_TIME_LOAD, "the world update's time-of-day tick", 0x48, 0x8b, 0x8e, 0x80, 0x04, 0x00, 0x00, 0xe8),
    SITE(S_WEATHER_TICK, "weather tick", 0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x30),
    SITE(S_TIME_TICK, "time-of-day tick", 0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x78, 0x10, 0x55,
         0x48, 0x8d, 0x68, 0xa1),
    // SetTime(tod, seconds, remap): cmp byte [rcx+0x25],0 (the clock's stop flag is +0x24).
    SITE(S_SET_TIME, "set time of day", 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x80, 0x79, 0x25, 0x00, 0x48, 0x8b, 0xd9,
         0x75, 0x0a, 0x45),
    SITE(S_SET_DIRECT, "weather direct control", 0x40, 0x53, 0x48, 0x81, 0xec, 0xa0, 0x00, 0x00, 0x00, 0xc5, 0xf8,
         0x10, 0x81, 0xe0, 0x00, 0x00),
    SITE(S_CLEAR_DIRECT, "weather control release", 0x48, 0x8b, 0xc4, 0x48, 0x83, 0xec, 0x68, 0xc5, 0xf8, 0x57, 0xc0,
         0x48, 0x8d, 0x50, 0xb8, 0xc6),
};

static const Build *build;
static unsigned char *image;

static uint32_t rva(unsigned site) { return build->rva[site]; }
static void *at(unsigned site) { return image + build->rva[site]; }

static uint32_t image_stamp(const unsigned char *game) {
    int32_t header = 0;
    uint32_t stamp = 0;
    if (!fh2_safe_read(game + 0x3c, &header, sizeof(header)) || header <= 0 || header > 0x1000 ||
        !fh2_safe_read(game + header + 8, &stamp, sizeof(stamp)))
        return 0;
    return stamp;
}

static const Build *find_build(const unsigned char *game, uint32_t size) {
    for (unsigned i = 0; game && i < ARRAY_COUNT(builds); ++i)
        if (size == builds[i].image_size && image_stamp(game) == builds[i].stamp) return &builds[i];
    return NULL;
}

static int in_image(const unsigned char *game, const void *address) {
    return (const unsigned char *)address >= game && (const unsigned char *)address < game + build->image_size;
}

static const unsigned char *call_target(const unsigned char *site) {
    unsigned char code[5];
    int32_t displacement;
    if (!fh2_safe_read(site, code, sizeof(code)) || code[0] != 0xe8) return NULL;
    memcpy(&displacement, code + 1, 4);
    return site + 5 + displacement;
}

static uint32_t rip_target(const unsigned char *game, uint32_t lea) {
    int32_t displacement = 0;
    fh2_safe_read(game + lea + 3, &displacement, 4);
    return (uint32_t)((int64_t)lea + 7 + displacement);
}

// The call's current target is the game's function, Extra Options' thunk (FH2)
// or another module's code; *native gets what to chain onto.
static const char *call_ok(const unsigned char *game, unsigned site, unsigned native_site, char *why, size_t size) {
    const unsigned char *called = call_target(game + rva(site));
    if (called == game + rva(native_site)) return NULL;
    if (rva(S_EXTRA_OPTIONS_THUNK) && called == game + rva(S_EXTRA_OPTIONS_THUNK)) {
        unsigned char thunk[12];
        void *hook = NULL;
        if (fh2_safe_read(called, thunk, sizeof(thunk)) && thunk[0] == 0x48 && thunk[1] == 0xb8 && thunk[10] == 0xff &&
            thunk[11] == 0xe0) {
            memcpy(&hook, thunk + 2, sizeof(hook));
            if (!in_image(game, hook) && fh2_is_code(hook)) return NULL;
        }
    } else if (called && !in_image(game, called) && fh2_is_code(called)) {
        return NULL;
    }
    snprintf(why, size, "the call at 0x%x goes somewhere unexpected", (unsigned)rva(site));
    return why;
}

// Where a jump another module wrote at the start of a game function goes:
// jmp rel32, jmp qword [rip+d] or mov rax/r11,imm64; jmp rax/r11. NULL when the
// code is no such jump or it goes into the game itself or to no code.
static const void *hook_target(const unsigned char *game, const unsigned char *function) {
    unsigned char code[16];
    const void *target = NULL;
    if (!fh2_safe_read(function, code, sizeof(code))) return NULL;
    if (code[0] == 0xe9) {
        int32_t displacement;
        memcpy(&displacement, code + 1, 4);
        target = function + 5 + displacement;
    } else if (code[0] == 0xff && code[1] == 0x25) {
        int32_t displacement;
        memcpy(&displacement, code + 2, 4);
        if (!fh2_safe_read(function + 6 + displacement, &target, sizeof(target))) return NULL;
    } else if (code[0] == 0x48 && code[1] == 0xb8 && code[10] == 0xff && code[11] == 0xe0) {
        memcpy(&target, code + 2, sizeof(target));
    } else if (code[0] == 0x49 && code[1] == 0xbb && code[10] == 0x41 && code[11] == 0xff && code[12] == 0xe3) {
        memcpy(&target, code + 2, sizeof(target));
    }
    return target && !in_image(game, target) && fh2_is_code(target) ? target : NULL;
}

// Game functions another mod may hook at their start (FH2: Extra Options'
// Weather Control hooks the time-of-day tick): the World menu only calls
// through them, so their own code or such a hook will do.
static int may_be_hooked(unsigned site) { return site == S_TIME_TICK; }
static int time_tick_hooked;

const char *world_menu_check(const unsigned char *game, uint32_t size, char *why, size_t why_size) {
    build = find_build(game, size);
    if (!build) {
        snprintf(why, why_size, "not a supported build (image size 0x%x)", (unsigned)size);
        return why;
    }
    for (unsigned i = 0; i < ARRAY_COUNT(site_code); ++i) {
        const SiteCode *code = &site_code[i];
        if (!fh2_bytes_equal(game + rva(code->site), code->bytes, code->length)) {
            if (may_be_hooked(code->site) && hook_target(game, game + rva(code->site))) {
                time_tick_hooked = 1;
                continue;
            }
            snprintf(why, why_size, "the game's %s at 0x%x is not as inspected", code->name, (unsigned)rva(code->site));
            return why;
        }
    }
    unsigned char next[10];
    static const unsigned char after_call[] = {0x4c, 0x89, 0x3f, 0x48, 0x8b, 0x08, 0x48, 0x89, 0x4f, 0x08};
    if (!fh2_safe_read(game + rva(S_RESOURCE_CALL) + 5, next, sizeof(next)) || memcmp(next, after_call, sizeof(next))) {
        snprintf(why, why_size, "the code after the XAML resource call 0x%x is not as inspected",
                 (unsigned)rva(S_RESOURCE_CALL));
        return why;
    }
    // lea rcx,[registry]; call RegisterProperty in the type set-up.
    unsigned char lea[3];
    if (!fh2_safe_read(game + rva(S_PAGE_LEA), lea, 3) || lea[0] != 0x48 || lea[1] != 0x8d || lea[2] != 0x0d ||
        rip_target(game, rva(S_PAGE_LEA)) != rva(S_REGISTRY) ||
        call_target(game + rva(S_PAGE_CALL)) != game + rva(S_REGISTER_PROPERTY)) {
        snprintf(why, why_size, "the VideoOptions type set-up does not register on 0x%x", (unsigned)rva(S_REGISTRY));
        return why;
    }
    static const struct {
        unsigned site;
        const char *name;
    } templates[] = {{S_INT_TEMPLATE_NAME, "Subtitles"}, {S_LIST_TEMPLATE_NAME, "OnOffOptions"}};
    for (unsigned i = 0; i < ARRAY_COUNT(templates); ++i) {
        if (!fh2_safe_read(game + rva(templates[i].site), lea, 3) || lea[0] != 0x48 || lea[1] != 0x8d ||
            lea[2] != 0x05 ||
            !fh2_bytes_equal(game + rip_target(game, rva(templates[i].site)), templates[i].name,
                             strlen(templates[i].name) + 1)) {
            snprintf(why, why_size, "the property templates do not name Subtitles and OnOffOptions");
            return why;
        }
    }
    for (unsigned i = S_ITEM_VTABLE0; i <= S_ITEM_VTABLE4; ++i) {
        void *first = read_pointer(game + rva(i));
        if (!first || !in_image(game, first)) {
            snprintf(why, why_size, "the list item vtable at 0x%x is not as inspected", (unsigned)rva(i));
            return why;
        }
    }
    void *hub = read_pointer(game + rva(S_HUB_SLOT));
    if (!hub || (hub != game + rva(S_HUB_INIT) && (in_image(game, hub) || !fh2_is_code(hub)))) {
        snprintf(why, why_size, "the Settings hub init slot 0x%x holds neither the game's init nor a hook",
                 (unsigned)rva(S_HUB_SLOT));
        return why;
    }
    const char *problem;
    if ((problem = call_ok(game, S_RESOURCE_CALL, S_GET_RESOURCE, why, why_size)) != NULL) return problem;
    // The tick calls: +7 after the manager load.
    if (call_target(game + rva(S_WEATHER_LOAD) + 7) != game + rva(S_WEATHER_TICK) &&
        !(call_target(game + rva(S_WEATHER_LOAD) + 7) &&
          !in_image(game, call_target(game + rva(S_WEATHER_LOAD) + 7)))) {
        snprintf(why, why_size, "the weather tick call at 0x%x goes somewhere unexpected",
                 (unsigned)rva(S_WEATHER_LOAD) + 7);
        return why;
    }
    if (call_target(game + rva(S_TIME_LOAD) + 7) != game + rva(S_TIME_TICK) &&
        !(call_target(game + rva(S_TIME_LOAD) + 7) && !in_image(game, call_target(game + rva(S_TIME_LOAD) + 7)))) {
        snprintf(why, why_size, "the time-of-day tick call at 0x%x goes somewhere unexpected",
                 (unsigned)rva(S_TIME_LOAD) + 7);
        return why;
    }
    return NULL;
}

const char *world_menu_build_name(void) { return build ? build->name : "?"; }

// ---------------------------------------------------------------- choices

enum { TIME_GAME, TIME_SUNRISE, TIME_MIDDAY, TIME_SUNSET, TIME_MIDNIGHT, TIME_COUNT };
enum { WEATHER_GAME, WEATHER_SUNNY, WEATHER_WET, WEATHER_RAIN, WEATHER_STORM, WEATHER_COUNT };
enum { ROW_TIME, ROW_WEATHER, ROW_COUNT };
enum { CHOICE_COUNT = 5 };

typedef enum { LANG_EN, LANG_PL, LANG_UK, LANG_COUNT } Lang;
typedef struct {
    const char *title, *tile;
    const char *row[ROW_COUNT];
    const char *description[ROW_COUNT];
    const wchar_t *choice[ROW_COUNT][CHOICE_COUNT];
} Captions;

// "\xc5\x9a" is the capital S with acute (UTF-8; the page is read as UTF-8 after its byte-order mark).
static const Captions captions[LANG_COUNT] = {
    {"WORLD", "WORLD", {"Time of day", "Weather"},
     {"Holds the time of day in free roam. Game: the game's own clock.",
      "Holds the weather in free roam. Game: the game's own weather."},
     {{L"Game", L"Sunrise", L"Midday", L"Sunset", L"Midnight"}, {L"Game", L"Sunny", L"Wet", L"Rain", L"Storm"}}},
    {"\xc5\x9a" "WIAT", "\xc5\x9a" "WIAT", {"Pora dnia", "Pogoda"},
     {"Utrzymuje por\xc4\x99 dnia w swobodnej je\xc5\xbc" "dzie. Gra: w\xc5\x82" "asny zegar gry.",
      "Utrzymuje pogod\xc4\x99 w swobodnej je\xc5\xbc" "dzie. Gra: w\xc5\x82" "asna pogoda gry."},
     {{L"Gra", L"Wschód słońca", L"Południe", L"Zachód słońca", L"Północ"},
      {L"Gra", L"Słonecznie", L"Mokro", L"Deszcz", L"Burza"}}},
    {"\xd0\xa1\xd0\x92\xd0\x86\xd0\xa2", "\xd0\xa1\xd0\x92\xd0\x86\xd0\xa2",
     {"\xd0\xa7\xd0\xb0\xd1\x81 \xd0\xb4\xd0\xbe\xd0\xb1\xd0\xb8", "\xd0\x9f\xd0\xbe\xd0\xb3\xd0\xbe\xd0\xb4\xd0\xb0"},
     {"\xd0\xa2\xd1\x80\xd0\xb8\xd0\xbc\xd0\xb0\xd1\x94 \xd1\x87\xd0\xb0\xd1\x81 \xd0\xb4\xd0\xbe\xd0\xb1\xd0\xb8 "
      "\xd0\xb2 \xd0\xb2\xd1\x96\xd0\xbb\xd1\x8c\xd0\xbd\xd1\x96\xd0\xb9 \xd1\x97\xd0\xb7\xd0\xb4\xd1\x96. "
      "\xd0\x93\xd1\x80\xd0\xb0: \xd0\xb2\xd0\xbb\xd0\xb0\xd1\x81\xd0\xbd\xd0\xb8\xd0\xb9 "
      "\xd0\xb3\xd0\xbe\xd0\xb4\xd0\xb8\xd0\xbd\xd0\xbd\xd0\xb8\xd0\xba \xd0\xb3\xd1\x80\xd0\xb8.",
      "\xd0\xa2\xd1\x80\xd0\xb8\xd0\xbc\xd0\xb0\xd1\x94 \xd0\xbf\xd0\xbe\xd0\xb3\xd0\xbe\xd0\xb4\xd1\x83 "
      "\xd0\xb2 \xd0\xb2\xd1\x96\xd0\xbb\xd1\x8c\xd0\xbd\xd1\x96\xd0\xb9 \xd1\x97\xd0\xb7\xd0\xb4\xd1\x96. "
      "\xd0\x93\xd1\x80\xd0\xb0: \xd0\xb2\xd0\xbb\xd0\xb0\xd1\x81\xd0\xbd\xd0\xb0 "
      "\xd0\xbf\xd0\xbe\xd0\xb3\xd0\xbe\xd0\xb4\xd0\xb0 \xd0\xb3\xd1\x80\xd0\xb8."},
     {{L"Гра", L"Схід сонця", L"Полудень",
       L"Захід сонця", L"Північ"},
      {L"Гра", L"Сонячно", L"Мокро",
       L"Дощ", L"Гроза"}}},
};

static Lang language(void) {
    char code[4];
    const unsigned char *base = image ? image + rva(S_TEXT_TABLES) - FH2_TEXT_TABLES : NULL;
    if (!base || !fh2_text_language(base, code)) return LANG_EN;
    if (!strcmp(code, "PL")) return LANG_PL;
    if (!strcmp(code, "UK")) return LANG_UK;
    return LANG_EN;
}

static const char *const property_names[ROW_COUNT] = {"XfTimeOfDay", "XfWeather"};
static const char *const list_names[ROW_COUNT] = {"XfTimeOfDayChoices", "XfWeatherChoices"};
static const wchar_t *const ini_keys[ROW_COUNT] = {L"TimeOfDay", L"Weather"};

static wchar_t ini_path[MAX_PATH * 2];
// Written by the page (UI thread); read by the tick handlers (game thread).
static volatile LONG wanted[ROW_COUNT];

static void save_choice(unsigned row) {
    wchar_t value[8];
    if (!ini_path[0]) return;
    swprintf(value, ARRAY_COUNT(value), L"%ld", (long)wanted[row]);
    WritePrivateProfileStringW(L"WorldMenu", ini_keys[row], value, ini_path);
}

// --------------------------------------------------------- time and weather

// The game's own weather override (its SetWeatherControl node).
typedef struct {
    uint8_t has_preset, _a[3];
    int32_t preset;
    uint8_t has_severity, _b[3];
    int32_t severity;  // 0..3: Clear, Cloudy, Threatening, Storm
    uint8_t has_atmosphere, _c[3];
    float atmosphere;
    uint8_t has_environment, _d[3];
    float environment;  // road wetness
    uint8_t has_precipitation, _e[3];
    float precipitation;
    uint8_t has_extra, _f[3];
    float extra;
    uint8_t b30, b31, b32, _g;
    float f34;
    uint8_t b38, _h[3];
} WeatherControl;
_Static_assert(sizeof(WeatherControl) == 0x3c, "WeatherControl layout");
#define CONTROL(severity, environment, precipitation) \
    {0, {0}, 0, 1, {0}, severity, 0, {0}, 0, 1, {0}, environment, 1, {0}, precipitation, 0, {0}, 0, 0, 0, 1, 0, 1.0f, 1, {0}}
static const WeatherControl controls[WEATHER_COUNT] = {
    {0},                     // game: ClearDirectControl
    CONTROL(0, 0, 0),        // sunny
    CONTROL(1, 1.0f, 0),     // wet: cloudy, wet roads
    CONTROL(2, 1.0f, 0.6f),  // rain
    CONTROL(3, 1.0f, 1.0f),  // storm
};
// Seconds after midnight; the game remaps them as its SetTimeOfDay node does.
static const float seconds[TIME_COUNT] = {0, 23400.0f, 43200.0f, 70200.0f, 0};

typedef uint8_t (*Tick)(void *manager, float dt, void *context);
static Tick previous_time_tick, previous_weather_tick;
// Game thread only: the managers last seen and the choices applied to them.
static void *time_manager, *weather_manager;
static int applied_time, applied_weather;

static uint8_t time_tick(void *tod, float dt, void *context) {
    int choice = (int)wanted[ROW_TIME];
    if (tod && choice >= 0 && choice < TIME_COUNT) {
        unsigned char *stopped = (unsigned char *)tod + 0x24;
        if (tod != time_manager) {  // a new world starts on the game's clock
            time_manager = tod;
            applied_time = TIME_GAME;
        }
        if (choice != TIME_GAME && (choice != applied_time || !*stopped)) {  // again when an event restarts it
            ((void (*)(void *, float, int))at(S_SET_TIME))(tod, seconds[choice], 1);
            *stopped = 1;
        } else if (choice == TIME_GAME && applied_time != TIME_GAME) {
            *stopped = 0;
        }
        if (choice != applied_time) say("WorldMenu: time of day %d applied.", choice);
        applied_time = choice;
    }
    return previous_time_tick(tod, dt, context);
}

static uint8_t weather_tick(void *wx, float dt, void *context) {
    int choice = (int)wanted[ROW_WEATHER];
    if (wx && choice >= 0 && choice < WEATHER_COUNT) {
        const unsigned char *active = (const unsigned char *)wx + 0xe0;
        if (wx != weather_manager) {
            weather_manager = wx;
            applied_weather = WEATHER_GAME;
        }
        // Again when an event clears the control (atmosphere and precipitation released).
        if (choice != WEATHER_GAME && (choice != applied_weather || (!active[0x10] && !active[0x20])))
            ((void (*)(void *, const WeatherControl *))at(S_SET_DIRECT))(wx, &controls[choice]);
        else if (choice == WEATHER_GAME && applied_weather != WEATHER_GAME)
            ((void (*)(void *))at(S_CLEAR_DIRECT))(wx);
        if (choice != applied_weather) say("WorldMenu: weather %d applied.", choice);
        applied_weather = choice;
    }
    return previous_weather_tick(wx, dt, context);
}

// ------------------------------------------------------------- properties

typedef struct {
    void **vtable;
    unsigned char header[0x20];
    void *getter, *setter, *unused;
    const char *name;
    void *type;
    uint64_t flags;
} NativeProperty;
typedef struct Link {
    NativeProperty *property;
    struct Link *next;
} Link;
typedef struct {
    void **vtable;
    unsigned char header[0x20];
    char *bytes;
    int length;
    int capacity;
} NativeResource;
_Static_assert(sizeof(NativeProperty) == 0x58, "native property descriptor");
_Static_assert(offsetof(NativeResource, bytes) == 0x28, "native resource buffer");

static NativeProperty value_properties[ROW_COUNT], list_properties[ROW_COUNT];
static Link value_links[ROW_COUNT], list_links[ROW_COUNT];
static void *choices[ROW_COUNT];
static volatile LONG registered, told_hub, told_page, told_layout, told_memory, told_properties;
static SRWLOCK registration_lock = SRWLOCK_INIT;
static void *resource_vtable[7];

static void release_object(void *object) {
    if (object && InterlockedDecrement((volatile LONG *)((unsigned char *)object + 0x10)) == 0)
        ((void (*)(void *))at(S_DELETE_OBJECT))(object);
}

static void add_reference(void *object) {
    if (object) InterlockedIncrement((volatile LONG *)((unsigned char *)object + 0x10));
}

static void *make_choices(unsigned row, Lang lang) {
    void *type = ((void *(*)(void *))at(S_COLLECTION_TYPE))(NULL);
    void *items = ((void *(*)(size_t, void *))at(S_ALLOCATE_TYPED))(0x58, type);
    if (!items) return NULL;
    ((void (*)(void *))at(S_COLLECTION_INIT))(items);
    for (unsigned i = 0; i < ARRAY_COUNT(item_offsets); ++i)
        *(void **)((unsigned char *)items + item_offsets[i]) = at(S_ITEM_VTABLE0 + i);
    for (unsigned index = 0; index < CHOICE_COUNT; ++index) {
        void *label = NULL;
        ((void **(*)(void **, const wchar_t *))at(S_MAKE_STRING))(&label, captions[lang].choice[row][index]);
        if (!label) {
            release_object(items);
            return NULL;
        }
        ((void (*)(void *, int, void *))at(S_INSERT_ITEM))(items, (int)index, label);
        release_object(label);
    }
    return items;
}

static void **get_value(void **output, NativeProperty *property, void *page) {
    (void)page;
    *output = NULL;
    unsigned row = (unsigned)(property - value_properties);
    if (row >= ROW_COUNT) return output;
    int value = (int)wanted[row];
    return ((void **(*)(void **, const void *))at(S_BOX_INT))(output, &value);
}

static void **get_choices(void **output, NativeProperty *property, void *page) {
    (void)page;
    *output = NULL;
    unsigned row = (unsigned)(property - list_properties);
    if (row >= ROW_COUNT) return output;
    *output = choices[row];
    add_reference(*output);
    return output;
}

static void set_value(NativeProperty *property, void *page, void *boxed) {
    int value;
    unsigned row = (unsigned)(property - value_properties);
    if (!boxed || row >= ROW_COUNT || !fh2_safe_read((unsigned char *)boxed + 0x28, &value, sizeof(value)) ||
        value < 0 || value >= CHOICE_COUNT || value == (int)wanted[row])
        return;
    InterlockedExchange(&wanted[row], value);
    save_choice(row);
    ((void (*)(void *, void *))at(S_PROPERTY_CHANGED))(page, property);
}

static void make_property(NativeProperty *property, const NativeProperty *template_property, const char *name,
                          void *getter, void *setter) {
    ((void (*)(void *))at(S_OBJECT_INIT))(property);
    property->vtable = template_property->vtable;
    memcpy((unsigned char *)property + 0x28, (const unsigned char *)template_property + 0x28, 0x30);
    property->name = name;
    property->getter = getter;
    property->setter = setter;
}

// On the UI thread, before the page binds: the choice lists, then the
// properties linked into the VideoOptions type (once per process).
static int register_properties(void) {
    if (registered) return 1;
    AcquireSRWLockExclusive(&registration_lock);
    if (!registered) {
        ((void (*)(void))at(S_TYPE_INIT))();
        NativeProperty *int_template = ((NativeProperty * (*)(void)) at(S_INT_TEMPLATE))();
        NativeProperty *list_template = ((NativeProperty * (*)(void)) at(S_LIST_TEMPLATE))();
        int ok = int_template && int_template->vtable && int_template->name &&
                 !strcmp(int_template->name, "Subtitles") && list_template && list_template->vtable &&
                 list_template->name && !strcmp(list_template->name, "OnOffOptions");
        Lang lang = language();
        for (unsigned row = 0; ok && row < ROW_COUNT; ++row)
            if (!(choices[row] = make_choices(row, lang))) ok = 0;
        if (!ok) {
            for (unsigned row = 0; row < ROW_COUNT; ++row) {
                release_object(choices[row]);
                choices[row] = NULL;
            }
            if (!InterlockedExchange(&told_properties, 1))
                say("WorldMenu: the Video page's property templates looked wrong at run time; no WORLD page.");
        } else {
            for (unsigned row = 0; row < ROW_COUNT; ++row) {
                make_property(&value_properties[row], int_template, property_names[row],
                              (void *)(uintptr_t)get_value, (void *)(uintptr_t)set_value);
                value_links[row].property = &value_properties[row];
                ((void (*)(void *, Link *))at(S_REGISTER_PROPERTY))(at(S_REGISTRY), &value_links[row]);
                make_property(&list_properties[row], list_template, list_names[row], (void *)(uintptr_t)get_choices,
                              NULL);
                list_links[row].property = &list_properties[row];
                ((void (*)(void *, Link *))at(S_REGISTER_PROPERTY))(at(S_REGISTRY), &list_links[row]);
            }
            InterlockedExchange(&registered, 1);
        }
    }
    ReleaseSRWLockExclusive(&registration_lock);
    return registered != 0;
}

// ------------------------------------------------------------------ XAML

typedef struct {
    char *bytes;
    size_t length, capacity;
    int valid;
} Buffer;

static void put(Buffer *buffer, const char *text, size_t length) {
    if (!buffer->valid || length >= buffer->capacity - buffer->length) {
        buffer->valid = 0;
        return;
    }
    memcpy(buffer->bytes + buffer->length, text, length);
    buffer->length += length;
}

static void put_text(Buffer *buffer, const char *text) { put(buffer, text, strlen(text)); }

static void put_attribute(Buffer *buffer, const char *text) {
    for (; *text; ++text) {
        const char *escaped = *text == '&'   ? "&amp;"
                              : *text == '<' ? "&lt;"
                              : *text == '>' ? "&gt;"
                              : *text == '"' ? "&quot;"
                                             : NULL;
        put(buffer, escaped ? escaped : text, escaped ? strlen(escaped) : 1);
    }
}

static const char *find_in(const char *begin, const char *end, const char *needle) {
    size_t n = strlen(needle);
    if (!begin || !end || end < begin || (size_t)(end - begin) < n) return NULL;
    for (const char *p = begin; p + n <= end; ++p)
        if (*p == *needle && !memcmp(p, needle, n)) return p;
    return NULL;
}

static const char *find_unique(const char *begin, const char *end, const char *needle) {
    const char *first = find_in(begin, end, needle);
    return first && !find_in(first + strlen(needle), end, needle) ? first : NULL;
}

static const char standalone_marker[] = "<!-- EO:StandaloneOptionsPage -->";
static const char video_class[] = "x:Class=\"ForzaUI.A_VideoOptions\"";
static const char hub_class[] = "x:Class=\"ForzaUI.A_PauseOptions\"";

// The hub tile: the game's tile look with its 2x1 picture (558x276) clipped
// to its middle 276x276, as the Exit Game plugin squares a wide tile (an
// FZMaterial draws at its own size, so it must be clipped), and Team Credits'
// picture (1x2) showing its bottom half, where its drawn part is.
#define XF_TILE_HEAD(key)                                                                                           \
    "<ControlTemplate x:Key=\"" key "\" TargetType=\"{x:Type Button}\">"                                           \
    "<Grid Style=\"{StaticResource A_Style_FrameworkElement_DefaultTransition}\""                                  \
    " local:UIPageTransitionManager.TransitionIndex=\"{Binding (local:UIPageTransitionManager.TransitionIndex),"    \
    " RelativeSource={RelativeSource TemplatedParent}}\">"                                                          \
    "<Grid Style=\"{StaticResource A_Style_Grid_HiddenWhenLocked}\">"                                               \
    "<local:FZMaterial Style=\"{StaticResource A_Style_FZMaterial_TileShadow}\"/></Grid>"                           \
    "<Grid Style=\"{StaticResource A_Style_Grid_MotionBlur}\">"                                                     \
    "<local:FZMaterial Style=\"{StaticResource A_Style_FZMaterial_TileBackground}\"/>"
#define XF_TILE_PICTURE                                                                                             \
    "<local:FZMaterial Style=\"{StaticResource A_Style_FZMaterial_LockableImage}\""                                 \
    " ImageSource=\"{Binding DataContext.Icon, RelativeSource={RelativeSource TemplatedParent}}\""
#define XF_TILE_TAIL                                                                                                \
    "<Grid Margin=\"{StaticResource A_Thickness_TilePadding}\">"                                                    \
    "<local:FZTextBlock x:Name=\"NameText\""                                                                        \
    " Text=\"{Binding DataContext.Heading, RelativeSource={RelativeSource TemplatedParent}}\""                      \
    " Style=\"{StaticResource A_Style_FZTextBlock_TileTitle}\""                                                     \
    " MaxWidth=\"{Binding Width, RelativeSource={RelativeSource TemplatedParent}}\"/>"                              \
    "<local:FZMaterial Style=\"{StaticResource A_Style_Tile_LockIcon}\"/></Grid>"                                   \
    "<local:FZMaterial Style=\"{StaticResource A_Style_FZMaterial_TileSelectionBar}\"/>"                            \
    "</Grid></Grid></ControlTemplate>"
static const char hub_templates[] =
    XF_TILE_HEAD("XF_ControlTemplate_SquareImageTile") "<Grid ClipToBounds=\"True\">" XF_TILE_PICTURE
    " Width=\"558\" Height=\"276\" HorizontalAlignment=\"Left\" VerticalAlignment=\"Top\""
    " Margin=\"-141,0,0,0\" StretchDirection=\"Both\"/></Grid>" XF_TILE_TAIL
    XF_TILE_HEAD("XF_ControlTemplate_BottomHalfTile") "<Grid ClipToBounds=\"True\">" XF_TILE_PICTURE
    " Width=\"276\" Height=\"558\" HorizontalAlignment=\"Left\" VerticalAlignment=\"Top\""
    " Margin=\"0,-282,0,0\" StretchDirection=\"Both\"/></Grid>" XF_TILE_TAIL;
static const char world_item_head[] = "<local:TileMenuDataItem x:Key=\"XF_MenuDataItem_World\" Heading=\"";
static const char world_item_tail[] =
    "\" Icon=\"/ForzaUI;component/Textures/Anthem/PauseMenuTileBackgrounds/FestivalRoutes.png\"/>";
static const char world_tile_xaml[] =
    "<Button x:Name=\"PART_Button_XFWorld\" local:TileMenuNavigationManager.TileState=\"Enabled\""
    " DataContext=\"{StaticResource XF_MenuDataItem_World}\""
    " local:TileMenuNavigationManager.GridX=\"4\" local:TileMenuNavigationManager.GridY=\"1\""
    " Style=\"{DynamicResource A_Style_Button_Tile_1x1}\""
    " Canvas.Left=\"{StaticResource A_Double_TileCoord_X_4}\""
    " Canvas.Top=\"{StaticResource A_Double_TileCoord_Y_1}\""
    " Template=\"{StaticResource XF_ControlTemplate_SquareImageTile}\""
    " local:UIPageTransitionManager.TransitionIndex=\"6\" local:BindingProxy.C=\"DisableMotionBlur\"/>";
static const char credits_tall[] = "Style=\"{DynamicResource A_Style_Button_Tile_1x2}\"";
static const char credits_look[] = "Template=\"{StaticResource A_ControlTemplate_VerticalImageTile}\"";

int world_menu_is_hub_xaml(const char *text, size_t length) {
    return text && find_in(text, text + length, hub_class) &&
           find_in(text, text + length, "x:Name=\"PART_Button_TeamCredits\"");
}

typedef struct {
    const char *at;
    size_t skip;
    int what;  // 0 resources, 1 credits size, 2 credits look, 3 the tile
} Edit;

// The Settings hub with the WORLD tile in the lower cell of the Team Credits
// column (the page's navigation grid is fixed at 5 x 2 cells): Team Credits
// keeps the upper cell.
size_t world_menu_hub_xaml(const char *text, size_t length, char *output, size_t capacity, const char **why) {
    const char *end = text + length;
    const char *resources = find_unique(text, end, "</ResourceDictionary>");
    const char *tiles = find_unique(text, end, "</local:AutoSizeCanvas>");
    const char *credits = find_unique(text, end, "x:Name=\"PART_Button_TeamCredits\"");
    const char *start = credits;
    while (start && start > text && *start != '<') --start;
    const char *credits_end = credits ? find_in(credits, end, "/>") : NULL;
    const char *size = start && credits_end ? find_unique(start, credits_end, credits_tall) : NULL;
    const char *look = start && credits_end ? find_unique(start, credits_end, credits_look) : NULL;
    const char *fail = NULL;
    if (find_in(text, end, "PART_Button_XFWorld"))
        fail = "it already has the WORLD tile";
    else if (!resources || !tiles || !credits_end || !size || !look)
        fail = "Team Credits is not the 1x2 tile of the hub";
    else if (resources >= start || credits_end >= tiles)
        fail = "the hub's resources and tiles are not in the inspected order";
    else if (find_in(text, end, "local:TileMenuNavigationManager.GridX=\"4\" local:TileMenuNavigationManager.GridY=\"1\""))
        fail = "the hub already has a tile below Team Credits";
    if (fail) {
        if (why) *why = fail;
        return 0;
    }
    Edit edits[] = {{resources, 0, 0}, {size, sizeof(credits_tall) - 1, 1}, {look, sizeof(credits_look) - 1, 2},
                    {tiles, 0, 3}};
    Buffer out = {output, 0, capacity, 1};
    size_t from = 0;
    // FH2's XML reader only decodes UTF-8 after its byte-order mark.
    if (length < 3 || memcmp(text, "\xef\xbb\xbf", 3)) put(&out, "\xef\xbb\xbf", 3);
    const Captions *c = &captions[language()];
    for (unsigned i = 0; i < ARRAY_COUNT(edits); ++i) {
        put(&out, text + from, (size_t)(edits[i].at - text) - from);
        switch (edits[i].what) {
        case 0:
            put_text(&out, hub_templates);
            put_text(&out, world_item_head);
            put_attribute(&out, c->tile);
            put_text(&out, world_item_tail);
            break;
        case 1: put_text(&out, "Style=\"{DynamicResource A_Style_Button_Tile_1x1}\""); break;
        case 2: put_text(&out, "Template=\"{StaticResource XF_ControlTemplate_BottomHalfTile}\""); break;
        default: put_text(&out, world_tile_xaml); break;
        }
        from = (size_t)(edits[i].at - text) + edits[i].skip;
    }
    put(&out, text + from, length - from);
    if (!out.valid) {
        if (why) *why = "the hub with the tile does not fit its buffer";
        return 0;
    }
    output[out.length] = 0;
    return out.length;
}

static void put_row(Buffer *out, unsigned row, int resource, Lang lang) {
    put_text(out, resource ? "<local:TileMenuDataItem x:Key=\"XF_"
                  : row    ? "<local:SpinnerControl Tag=\"{StaticResource XF_"
                           : "<local:SpinnerControl x:Name=\"FirstItem\" Tag=\"{StaticResource XF_");
    put_text(out, property_names[row]);
    put_text(out, resource ? "\" Heading=\"" : "}\" Style=\"{StaticResource A_Style_SpinnerControl_Default}\" Label=\"");
    put_attribute(out, captions[lang].row[row]);
    if (resource) {
        put_text(out, "\" Description=\"");
        put_attribute(out, captions[lang].description[row]);
        put_text(out, "\"/>");
        return;
    }
    put_text(out, "\" SelectedIndex=\"{Binding ");
    put_text(out, property_names[row]);
    put_text(out, "}\" ItemsSource=\"{Binding ");
    put_text(out, list_names[row]);
    put_text(out, "}\"/>");
}

// The native Video page (as the game and other plugins left it) with its own
// title, the brightness preview hidden and its option list holding only the
// two WORLD rows (the native list height fits two 55-pixel rows).
size_t world_menu_page_xaml(const char *text, size_t length, char *output, size_t capacity, const char **why) {
    static const char title[] = "Content=\"{local:Loc ScreenTitles.IDS_VideoOptions}\"";
    static const char panel[] = "<StackPanel";
    static const char list_open[] = "<local:CommonListBox";
    static const char list_name[] = "x:Name=\"OptionsList\"";
    static const char list_close[] = "</local:CommonListBox>";
    const char *end = text + length, *fail = NULL;
    const char *heading = NULL, *resources = NULL, *name = NULL, *preview = NULL, *list = NULL, *rows_end = NULL,
               *opening_end = NULL, *height = NULL, *height_end = NULL;
    if (!find_in(text, end, video_class) || find_in(text, end, standalone_marker)) {
        fail = "not the Video page";
    } else {
        heading = find_unique(text, end, title);
        resources = find_in(text, end, "</ResourceDictionary>");
        name = find_unique(text, end, list_name);
        if (!heading || !resources || !name || resources >= heading || heading >= name)
            fail = "no title, resources and option list in the inspected order";
    }
    if (!fail) {
        // The brightness preview: the black StackPanel between the title and the list.
        const char *black = find_in(heading, name, "Background=\"Black\"");
        preview = black;
        while (preview && preview > heading && memcmp(preview, panel, sizeof(panel) - 1)) --preview;
        list = name;
        while (list > text && *list != '<') --list;
        rows_end = find_in(name, end, list_close);
        opening_end = rows_end ? find_in(name, rows_end, ">") : NULL;
        if (!black || preview <= heading || find_in(preview, black, ">") || find_in(preview, black, "Visibility="))
            fail = "no brightness preview";
        else if ((size_t)(end - list) < sizeof(list_open) - 1 || memcmp(list, list_open, sizeof(list_open) - 1) ||
                 !opening_end || opening_end[-1] == '/' || find_in(opening_end, rows_end, list_open))
            fail = "the option list is not as inspected";
    }
    if (!fail) {
        height = find_in(list, opening_end, " Height=\"");
        height_end = height ? find_in(height + 9, opening_end, "\"") : NULL;
        if (height && !height_end) fail = "the option list's height is not as inspected";
    }
    if (fail) {
        if (why) *why = fail;
        return 0;
    }
    const char *items = opening_end + 1, *after_name = name + sizeof(list_name) - 1;
    Lang lang = language();
    Buffer out = {output, 0, capacity, 1};
    size_t index = 0;
    put(&out, "\xef\xbb\xbf", 3);
    if (length >= 3 && !memcmp(text, "\xef\xbb\xbf", 3)) index = 3;
    put_text(&out, standalone_marker);
    put_text(&out, "\r\n");
    while (index < length && out.valid) {
        const char *p = text + index;
        if (p == resources)
            for (unsigned row = 0; row < ROW_COUNT; ++row) put_row(&out, row, 1, lang);
        if (p == after_name && !height) put_text(&out, " Height=\"110\"");
        if (height && p == height) {
            put_text(&out, " Height=\"110\"");  // two 55-pixel rows
            index = (size_t)(height_end + 1 - text);
        } else if (p == heading) {
            put_text(&out, "Content=\"");
            put_attribute(&out, captions[lang].title);
            put_text(&out, "\"");
            index += sizeof(title) - 1;
        } else if (p == preview) {
            put_text(&out, "<StackPanel Visibility=\"Collapsed\"");
            index += sizeof(panel) - 1;
        } else if (p == items) {
            for (unsigned row = 0; row < ROW_COUNT; ++row) put_row(&out, row, 0, lang);
            index = (size_t)(rows_end - text);
        } else {
            put(&out, p, 1);
            ++index;
        }
    }
    if (!out.valid) {
        if (why) *why = "the page does not fit its buffer";
        return 0;
    }
    output[out.length] = 0;
    return out.length;
}

// ---------------------------------------------------------------- hooks

typedef void **(*ReadResource)(void *reader, void **output);
typedef void (*PageFunction)(void *page);
typedef struct {
    void *method;
    int32_t adjust, unused;
} MemberHandler;

static ReadResource previous_reader;
static PageFunction previous_hub_init;
static volatile LONG64 requested_at;

static void world_tile(void *page) {
    LONG64 now = (LONG64)GetTickCount64();
    InterlockedExchange64(&requested_at, now ? now : 1);
    ((PageFunction)at(S_VIDEO_TILE))(page);
}

static int take_request(void) {
    LONG64 when = InterlockedExchange64(&requested_at, 0);
    return when && (ULONGLONG)GetTickCount64() - (ULONGLONG)when < REQUEST_WINDOW_MS;
}

static void hub_init(void *page) {
    previous_hub_init(page);
    if (page) {
        MemberHandler handler = {(void *)(uintptr_t)world_tile, 0, 0};
        ((void (*)(void *, const char *, const MemberHandler *))at(S_HUB_BIND))(page, "PART_Button_XFWorld", &handler);
    }
}

static void *delete_resource(NativeResource *resource, unsigned flags) {
    ((void (*)(void *))at(S_OBJECT_CLEANUP))(resource);
    if (flags & 1) ((void (*)(void *))at(S_FREE))(resource);
    return resource;
}

static void **read_resource(void *reader, void **output) {
    previous_reader(reader, output);
    NativeResource *original = output ? (NativeResource *)*output : NULL;
    if (!original || !original->bytes || original->length <= 0 || original->length > MAX_XAML) return output;
    size_t length = (size_t)original->length;
    const char *text = original->bytes;
    int hub = world_menu_is_hub_xaml(text, length);
    int page = !hub && find_in(text, text + length, video_class) &&
               !find_in(text, text + length, standalone_marker) && take_request();
    if (!hub && !page) return output;
    if (page && !register_properties()) return output;
    size_t capacity = length + EXTRA_ROOM;
    NativeResource *replacement = ((void *(*)(size_t))at(S_ALLOCATE))(sizeof(NativeResource) + capacity);
    if (!replacement) {
        if (!InterlockedExchange(&told_memory, 1)) say("WorldMenu: no memory for the page copy; left as it is.");
        return output;
    }
    char *bytes = (char *)(replacement + 1);
    const char *why = NULL;
    size_t made = hub ? world_menu_hub_xaml(text, length, bytes, capacity, &why)
                      : world_menu_page_xaml(text, length, bytes, capacity, &why);
    if (!made) {
        ((void (*)(void *))at(S_FREE))(replacement);
        if (!InterlockedExchange(&told_layout, 1) && (!why || strcmp(why, "it already has the WORLD tile")))
            say("WorldMenu: the %s is not as inspected (%s); left as it is.", hub ? "Settings hub" : "Video page",
                why ? why : "?");
        return output;
    }
    // As Extra Options does: the game's resource object with this buffer,
    // owned by the parser through the native reference count.
    ((void (*)(void *))at(S_OBJECT_INIT))(replacement);
    memcpy(resource_vtable, original->vtable - 1, sizeof(resource_vtable));
    resource_vtable[1] = (void *)(uintptr_t)delete_resource;  // vtable slot 0, after the type information
    replacement->vtable = resource_vtable + 1;
    replacement->bytes = bytes;
    replacement->length = (int)made;
    replacement->capacity = (int)capacity;
    *output = replacement;
    release_object(original);
    if (hub ? !InterlockedExchange(&told_hub, 1) : !InterlockedExchange(&told_page, 1))
        say(hub ? "WorldMenu: WORLD tile placed below Team Credits in the Settings hub."
                : "WorldMenu: WORLD page built from the Video page (time of day, weather).");
    return output;
}

// chain_call, or, when the call's displacement cannot be swapped atomically
// (Fast & Furious' weather tick call: its four bytes cross a 16-byte
// boundary), the same jump written with every other thread suspended and none
// of them inside the call (patch.c write_code).
static const char *chain_tick_call(unsigned char *site, const void *native, const unsigned char *game, void *handler,
                                   void **previous) {
    const char *problem = chain_call(site, native, game, handler, previous);
    if (!problem || strcmp(problem, "its displacement crosses a 16-byte boundary")) return problem;
    unsigned char before[5], after[5];
    const unsigned char *target = call_target(site);
    if (!target || !fh2_safe_read(site, before, sizeof(before))) return "it is not a call";
    unsigned char *jump = fh2_allocate_near(site);
    if (!jump) return "there is no free memory within 2 GB of it";
    unsigned char code[14] = {0xff, 0x25, 0, 0, 0, 0};  // jmp qword [rip+0]; dq handler
    uintptr_t address = (uintptr_t)handler;
    memcpy(code + 6, &address, 8);
    memcpy(jump, code, sizeof(code));
    DWORD protection;
    if (!VirtualProtect(jump, sizeof(code), PAGE_EXECUTE_READ, &protection)) {
        VirtualFree(jump, 0, MEM_RELEASE);
        return "the jump could not be made executable";
    }
    FlushInstructionCache(GetCurrentProcess(), jump, sizeof(code));
    int32_t displacement = (int32_t)(jump - (site + 5));
    after[0] = 0xe8;
    memcpy(after + 1, &displacement, 4);
    *previous = (void *)target;  // before the write: another thread may call through it at once
    if ((problem = write_code(site, before, after, sizeof(after))) != NULL) {
        VirtualFree(jump, 0, MEM_RELEASE);
        return problem;
    }
    return NULL;
}

int world_menu_install(unsigned char *game, const wchar_t *ini) {
    image = game;
    if (ini) lstrcpynW(ini_path, ini, ARRAY_COUNT(ini_path));
    for (unsigned row = 0; row < ROW_COUNT; ++row) {
        int value = ini_path[0] ? (int)GetPrivateProfileIntW(L"WorldMenu", ini_keys[row], 0, ini_path) : 0;
        InterlockedExchange(&wanted[row], value >= 0 && value < CHOICE_COUNT ? value : 0);
    }
    char why[200];
    const char *problem;
    // The ticks first: alone they apply only what the INI holds.
    if ((problem = chain_tick_call(game + rva(S_TIME_LOAD) + 7, game + rva(S_TIME_TICK), game,
                              (void *)(uintptr_t)time_tick, (void **)&previous_time_tick)) != NULL ||
        (problem = chain_tick_call(game + rva(S_WEATHER_LOAD) + 7, game + rva(S_WEATHER_TICK), game,
                              (void *)(uintptr_t)weather_tick, (void **)&previous_weather_tick)) != NULL) {
        say("WorldMenu: the world update's ticks could not be chained (%s); nothing more is installed.", problem);
        return 0;
    }
    if ((problem = chain_slot((void **)(game + rva(S_HUB_SLOT)), game + rva(S_HUB_INIT), game,
                              (void *)(uintptr_t)hub_init, (void **)&previous_hub_init)) != NULL) {
        say("WorldMenu: the Settings hub init slot 0x%x could not be chained (%s); no WORLD tile.",
            (unsigned)rva(S_HUB_SLOT), problem);
        return 0;
    }
    // The resource call may go to Extra Options' thunk (FH2): chained as any target.
    const void *native = call_target(game + rva(S_RESOURCE_CALL));
    if ((problem = chain_call(game + rva(S_RESOURCE_CALL), native, game, (void *)(uintptr_t)read_resource,
                              (void **)&previous_reader)) != NULL) {
        snprintf(why, sizeof(why), "%s", problem);
        say("WorldMenu: the XAML resource call 0x%x could not be chained (%s); no WORLD tile.",
            (unsigned)rva(S_RESOURCE_CALL), why);
        return 0;
    }
    say("WorldMenu: %s: WORLD tile in the Settings hub; time of day %ld, weather %ld from the INI.", build->name,
        (long)wanted[ROW_TIME], (long)wanted[ROW_WEATHER]);
    if (time_tick_hooked)
        say("WorldMenu: the time-of-day tick 0x%x is hooked by another mod (e.g. Extra Options' Weather Control); "
            "the World menu calls through its hook, and the last one to set the time or weather wins.",
            (unsigned)rva(S_TIME_TICK));
    return 1;
}

#ifdef XTRA_TEST
__declspec(dllexport) size_t XtraTestWorldHub(const char *text, size_t length, char *out, size_t capacity) {
    const char *why = NULL;
    return world_menu_hub_xaml(text, length, out, capacity, &why);
}
__declspec(dllexport) size_t XtraTestWorldPage(const char *text, size_t length, char *out, size_t capacity) {
    const char *why = NULL;
    return world_menu_page_xaml(text, length, out, capacity, &why);
}
__declspec(dllexport) const char *XtraTestWorldCheck(const unsigned char *game, uint32_t size) {
    static char why[200];
    const char *problem = world_menu_check(game, size, why, sizeof(why));
    return problem ? problem : world_menu_build_name();
}
#endif
