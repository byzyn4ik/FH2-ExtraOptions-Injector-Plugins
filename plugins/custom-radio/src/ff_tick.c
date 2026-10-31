// Custom Radio: the key tick on Forza Horizon 2 Presents Fast & Furious.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// On Forza Horizon 2 the Xtra Options runtime (Extra Options' core) calls
// FH2PluginVehicleUpdate from the local car's update, ForzaCar::Update
// (vtable 0x2c5cb80 slot 0x80 -> 0x2a9400). Fast & Furious runs on
// forza_injector without that runtime, so nothing calls it there; this file
// hooks the same slot of Fast & Furious' ForzaCar vtable (0x2c9ca30 + 0x80 ->
// 0x2af960, ff_sites.h) itself and, on the game thread, for the local car
// only (found as the runtime finds it: the player manager's local player and
// its car), calls the plugin's tick. The pad buttons are the selected
// controller's, as the runtime reads them (Fast & Furious' controllers are
// 0x1480 bytes, so the manager's selected controller sits 0xd80 later; the
// layout Keyboard & Mouse uses there). Every function's first bytes and the
// slot are checked first; the slot is swapped with a compare-exchange.
#include "ff_tick.h"

#include "ff_sites.h"
#include "hooks.h"

enum {
    CONTROLLER_FIRST = 0x10,
    CONTROLLER_STRIDE = 0x1480,
    CONTROLLER_COUNT = 8,
    MANAGER_SELECTED = 0xab28 + 0xd80,
    CONTROLLER_CONNECTED = 0x04,
    CONTROLLER_BUTTONS = 0xc0,
    CAR_RELEASE_SLOT = 3
};

typedef void (*CarUpdateFn)(void *, float, float, unsigned);
typedef void *(*LocalPlayerFn)(void *, int);
typedef void *(*PlayerCarFn)(void *, void **);
typedef void (*ReleaseFn)(void *);

static unsigned char *image;
static CarUpdateFn original;
static FfTickFn tick;

static int has_vtable(const void *object, uint32_t rva) {
    void *table;
    return object && memory_read(object, &table, sizeof(table)) && table == image + rva;
}

static void release_car(void *car) {
    void **table;
    void *method;
    if (car && memory_read(car, &table, sizeof(table)) && table &&
        memory_read(table + CAR_RELEASE_SLOT, &method, sizeof(method)) && method) {
        ((ReleaseFn)method)(car);
    }
}

// The local player's car (a reference the caller releases), or NULL.
static void *local_car(void) {
    void *manager = NULL, *car = NULL;
    if (!memory_read(image + FF_RVA_PLAYER_MANAGER, &manager, sizeof(manager)) || !manager) {
        return NULL;
    }
    void *player = ((LocalPlayerFn)(image + FF_FN_LOCAL_PLAYER))(manager, 0);
    if (!player) {
        return NULL;
    }
    ((PlayerCarFn)(image + FF_FN_PLAYER_CAR))(player, &car);
    if (car && !has_vtable(car, FF_RVA_FORZA_CAR_VTABLE)) {
        release_car(car);
        return NULL;
    }
    return car;
}

unsigned ff_tick_pad_buttons(const unsigned char *base) {
    unsigned char *manager = NULL, *selected = NULL, connected = 0;
    unsigned short buttons = 0;
    void *table = NULL;
    if (!base || !memory_read(base + FF_RVA_CONTROLLER_MANAGER, &manager, sizeof(manager)) || !manager ||
        !memory_read(manager, &table, sizeof(table)) || table != base + FF_RVA_CONTROLLER_MANAGER_VTABLE ||
        !memory_read(manager + MANAGER_SELECTED, &selected, sizeof(selected))) {
        return 0;
    }
    uintptr_t first = (uintptr_t)manager + CONTROLLER_FIRST, at = (uintptr_t)selected;
    if (at < first || at >= first + CONTROLLER_COUNT * CONTROLLER_STRIDE || (at - first) % CONTROLLER_STRIDE ||
        !memory_read(selected + CONTROLLER_CONNECTED, &connected, 1) || connected != 1 ||
        !memory_read(selected + CONTROLLER_BUTTONS, &buttons, sizeof(buttons))) {
        return 0;
    }
    return buttons;
}

static void car_update(void *car, float dt, float frame_dt, unsigned frame) {
    void *local = local_car();
    if (local && local == car && tick) {
        tick(ff_tick_pad_buttons(image));
    }
    release_car(local);
    original(car, dt, frame_dt, frame);
}

static int guard(uint32_t rva, const char *bytes, size_t size) {
    unsigned char code[16];
    return size <= sizeof(code) && memory_read(image + rva, code, size) && memory_equal(code, bytes, size);
}

int ff_tick_install(unsigned char *base, FfTickFn callback) {
    void *value = NULL;
    image = base;
    if (!guard(FF_FN_CAR_UPDATE, FF_FN_CAR_UPDATE_GUARD, sizeof(FF_FN_CAR_UPDATE_GUARD) - 1) ||
        !guard(FF_FN_LOCAL_PLAYER, FF_FN_LOCAL_PLAYER_GUARD, sizeof(FF_FN_LOCAL_PLAYER_GUARD) - 1) ||
        !guard(FF_FN_PLAYER_CAR, FF_FN_PLAYER_CAR_GUARD, sizeof(FF_FN_PLAYER_CAR_GUARD) - 1)) {
        log_append("CUSTOM RADIO: Fast & Furious' car update or local player code is not as expected; "
                   "the keys are off (the D-pad still reaches the station).\r\n");
        return 0;
    }
    if (!memory_read(image + FF_SLOT_CAR_UPDATE, &value, sizeof(value)) || !value ||
        (value != image + FF_FN_CAR_UPDATE && !memory_is_code(value))) {
        log_append("CUSTOM RADIO: ForzaCar's update slot 0x2c9cab0 holds neither the game's update nor code; "
                   "the keys are off (the D-pad still reaches the station).\r\n");
        return 0;
    }
    original = (CarUpdateFn)value;
    tick = callback;
    if (!hooks_swap_pointer(FF_SLOT_CAR_UPDATE, value, (void *)(uintptr_t)car_update)) {
        log_append("CUSTOM RADIO: ForzaCar's update slot 0x2c9cab0 could not be written; the keys are off.\r\n");
        return 0;
    }
    log_append(value == image + FF_FN_CAR_UPDATE
                   ? "CUSTOM RADIO: Fast & Furious: the keys are read in the local car's update "
                     "(ForzaCar slot 0x2c9cab0).\r\n"
                   : "CUSTOM RADIO: Fast & Furious: the keys are read in the local car's update "
                     "(ForzaCar slot 0x2c9cab0, chained onto another module's hook).\r\n");
    return 1;
}

#ifdef CUSTOM_RADIO_TEST
// Test build only: the hook as it sits in the slot.
__declspec(dllexport) void *CustomRadioTestFfCarUpdate(void) {
    return (void *)(uintptr_t)car_update;
}
__declspec(dllexport) unsigned CustomRadioTestFfPadButtons(const unsigned char *base) {
    return ff_tick_pad_buttons(base);
}
#endif
