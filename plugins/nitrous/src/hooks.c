// Nitrous for Forza Horizon 2: Fast & Furious' nitrous tests in Forza Horizon
// 2's exhaust-flame and camera-input code (hooks.h has what each does).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Each hook takes over a function's entry: its first 8 bytes (8-aligned) are
// swapped at once for a jump to a page within 2 GB, which jumps on to the
// handler; the handler calls the function's original first instructions
// (copied to that page, none of them rip-relative) followed by a jump back.
// A function whose first bytes are not Forza Horizon 2 1.0.0.64's (another
// module's hook) is left alone.
#include "hooks.h"

#include <math.h>
#include <string.h>

static const char *etx_check(const unsigned char *image);

typedef void (*CarEffectUpdate)(unsigned char *self, float delta_seconds, void **dynamics);
typedef void (*CameraInput)(unsigned char *layer, void *target, void *context, float external);
typedef void (*RumbleUpdate)(unsigned char *self, float delta_seconds, const void *input, int wheel);
typedef void *(*Getter)(void *self);
typedef unsigned long (*Release)(void *self);

typedef struct {
    unsigned rva;
    unsigned char copy;  // whole instructions moved to the trampoline (at least 5 bytes)
    unsigned char size;  // bytes compared
    unsigned char bytes[16];
    const char *what;
} Entry;

static const Entry entries[4] = {
    // mov rax,rsp; mov [rax+0x18],r8 | push rsi; push rdi; push r12; push r14; push r15; (sub rsp,0x80)
    {NITROUS_RVA_BACKFIRE_UPDATE, 7, 16,
     {0x48, 0x8b, 0xc4, 0x4c, 0x89, 0x40, 0x18, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48},
     "the exhaust backfire's update (0xf7d8a0) differs"},
    // mov [rsp+0x18],r8 | push rdi; sub rsp,0x30; mov qword [rsp+0x20],-2
    {NITROUS_RVA_BURBLE_UPDATE, 5, 16,
     {0x4c, 0x89, 0x44, 0x24, 0x18, 0x57, 0x48, 0x83, 0xec, 0x30, 0x48, 0xc7, 0x44, 0x24, 0x20, 0xfe},
     "the exhaust burble's update (0xf7dd70) differs"},
    // push rbx; push rbp; push rsi; sub rsp,0x110 | vmovaps [rsp+0x100],xmm6
    {NITROUS_RVA_CAMERA_INPUT, 11, 16,
     {0x40, 0x53, 0x55, 0x56, 0x48, 0x81, 0xec, 0x10, 0x01, 0x00, 0x00, 0xc5, 0xf8, 0x29, 0xb4, 0x24},
     "the camera layers' input (0x1037df0) differs"},
    // mov rax,rsp; mov [rax+0x10],rbx | push rsi; sub rsp,0xa0; cmp byte [rcx+0xc],0
    {NITROUS_RVA_RUMBLE_UPDATE, 7, 16,
     {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x10, 0x56, 0x48, 0x81, 0xec, 0xa0, 0x00, 0x00, 0x00, 0x80},
     "the pad rumble's update (0x1076010) differs"},
};
// The camera input's switch: movsxd rax,[rsi+0x48]; cmp eax,0x2c; ja ... (the
// carriers' cases, 0x17 and 0x18, go to the one that writes 0: 0x103808b).
static const unsigned char camera_switch[] = {0x48, 0x63, 0x46, 0x48, 0x83, 0xf8, 0x2c, 0x0f, 0x87, 0x2c, 0x02, 0x00, 0x00};
static const unsigned camera_switch_rva = 0x1037e52u, camera_table_rva = 0x1038558u, camera_zero_case = 0x103808bu;

static int same(const unsigned char *at, const unsigned char *expected, size_t size) {
    unsigned char actual[32];
    if (size > sizeof(actual) || !fh2_safe_read(at, actual, size)) return 0;
    for (size_t i = 0; i < size; ++i)
        if (actual[i] != expected[i]) return 0;
    return 1;
}

const char *hooks_check(const unsigned char *image) {
    if (!image) return "no game image";
    for (unsigned i = 0; i < 4; ++i)
        if (!same(image + entries[i].rva, entries[i].bytes, entries[i].size)) return entries[i].what;
    if (!same(image + camera_switch_rva, camera_switch, sizeof(camera_switch)))
        return "the camera layers' input switch (0x1037e52) differs";
    int32_t cases[2];
    if (!fh2_safe_read(image + camera_table_rva + 4 * NITROUS_CARRIER_NITROUS, cases, sizeof(cases)) ||
        (uint32_t)cases[0] != camera_zero_case || (uint32_t)cases[1] != camera_zero_case)
        return "the camera input types 0x17 and 0x18 are computed by the game (0x1038558)";
    return etx_check(image);
}

// ---------------------------------------------------------------- the handlers

static const void *iface_vtable;  // CCarDynamics' interface vtable (the game's; the tests' own)
static CarEffectUpdate backfire_original, burble_original;
static CameraInput camera_original;
static RumbleUpdate rumble_original;
static volatile float rumble_input;
// The end of a boost (the player's car only).
static const void *volatile player_iface;
static volatile float ending_envelope;  // the eased build-up, 0..1
// The flames' tail and its pop run on the wall clock: they end even when no
// more local-car updates come (a pause, a menu, another car).
static volatile LONGLONG flames_until, pop_at;  // ms (0: none)
static volatile LONG pending_pop;
static volatile float rumble_last;      // the rumble input of the last boosting update
static ULONGLONG hooks_clock;           // tests: a fixed clock (0: the real one)

static LONGLONG clock_ms(void) { return (LONGLONG)(hooks_clock ? hooks_clock : GetTickCount64()); }

void hooks_set_player(const void *iface) { player_iface = iface; }
void hooks_set_ending(float envelope, int flames) {
    ending_envelope = envelope > 0.0f ? (envelope < 1.0f ? envelope : 1.0f) : 0.0f;
    if (!flames) InterlockedExchange64(&flames_until, 0);
}
static void start_tail(float seconds) {
    const LONGLONG now = clock_ms(), end = now + (LONGLONG)(seconds * 1000.0f + 0.5f);
    InterlockedExchange64(&flames_until, seconds > 0.0f ? end : 0);
    InterlockedExchange64(&pop_at, end);
    InterlockedExchange(&pending_pop, 1);
}
void hooks_set_pop(void) { start_tail(0.0f); }

float hooks_ending_frame(int active, float buildup, float dt, float ease_out, float tail) {
    static float envelope;
    static int was_active;
    if (!active && !(dt > 0.0f) && !(ease_out > 0.0f) && !(tail > 0.0f)) {  // a reset (a new start)
        envelope = 0.0f;
        was_active = 0;
        InterlockedExchange(&pending_pop, 0);
        InterlockedExchange64(&flames_until, 0);
        hooks_set_ending(0.0f, 0);
        return 0.0f;
    }
    if (!(dt > 0.0f && dt < 0.5f)) dt = 0.0f;
    if (active) {
        envelope = buildup > 0.0f ? (buildup < 1.0f ? buildup : 1.0f) : 0.0f;
        InterlockedExchange64(&flames_until, 0);  // boosting again: no tail, no pop pending
        InterlockedExchange(&pending_pop, 0);
    } else {
        envelope = ease_out > 0.0f ? envelope - dt / ease_out : 0.0f;
        if (envelope < 0.0f) envelope = 0.0f;
        if (was_active) start_tail(tail > 0.0f ? tail : 0.0f);  // however the boost ended
    }
    was_active = active != 0;
    ending_envelope = envelope;
    return envelope;
}

// Fast & Furious' ControllerRumble.xml <Nitrous>: [wheel][motor].
static const NitrousCurve rumble_curves[2][2] = {
    {{{0.001f, 0.7f, 1.0f}, {0.1f, 0.2f, 0.1f}}, {{0.001f, 0.7f, 1.0f}, {0.1f, 0.1f, 1.0f}}},
    {{{0.001f, 0.7f, 1.0f}, {0.2f, 0.2f, 0.1f}}, {{0.001f, 0.7f, 1.0f}, {0.1f, 0.1f, 1.0f}}},
};

// Fast & Furious 0x109b460: flat below the first and above the last point.
float nitrous_curve(const NitrousCurve *c, float x) {
    if (!(x > c->x[0])) return c->y[0];
    if (x >= c->x[2]) return c->y[2];
    if (x <= c->x[1]) return c->y[0] + (x - c->x[0]) / (c->x[1] - c->x[0]) * (c->y[1] - c->y[0]);
    return c->y[1] + (x - c->x[1]) / (c->x[2] - c->x[1]) * (c->y[2] - c->y[1]);
}

void hooks_set_rumble(float input) {
    rumble_input = input > 0.0f && input <= 1.0f ? input : input > 1.0f ? 1.0f : 0.0f;
    if (rumble_input > 0.0f) rumble_last = rumble_input;
}

// Fast & Furious 0x109c4d0's Nitrous term, after Forza Horizon 2's sum.
static void rumble_hook(unsigned char *self, float delta_seconds, const void *input, int wheel) {
    rumble_original(self, delta_seconds, input, wheel);
    float x = rumble_input, scale = 1.0f;
    if (!(x > 0.0f)) {  // after a boost: the last input, fading with the envelope
        x = rumble_last;
        scale = ending_envelope;
        if (!(x > 0.0f) || !(scale > 0.0f)) return;
    }
    if (self[0xc]) return;
    const float full = 65535.0f * *(const float *)(self + 8);
    if (!(full > 0.0f)) return;
    for (unsigned motor = 0; motor < 2; ++motor) {
        uint32_t *value = (uint32_t *)(self + 4 * motor);
        if (*value >= 0xffffu) continue;  // a forced full rumble (its timer) or already full
        float sum = (float)*value + nitrous_curve(&rumble_curves[(wheel & 0xff) != 0][motor], x) * scale * full;
        if (sum > full) sum = full;
        *value = (uint32_t)sum;
    }
}

static void *boosting_dynamics(void **dynamics, int *active) {
    void *iface = NULL;
    const void *vtable = NULL;
    *active = 0;
    if (!dynamics || !fh2_safe_read(dynamics, &iface, sizeof(iface)) || !iface ||
        !fh2_safe_read(iface, &vtable, sizeof(vtable)) || vtable != iface_vtable)
        return NULL;
    *active = *(const int *)((const unsigned char *)iface + NITROUS_IFACE_ACTIVE) != 0;
    return iface;
}

static void release(void *iface) { ((Release)(*(void ***)iface)[3])(iface); }

// Fast & Furious 0xfa3c40: while boosting, a backfire (2) on every update.
static void backfire_hook(unsigned char *self, float delta_seconds, void **dynamics) {
    int active;
    void *iface = boosting_dynamics(dynamics, &active);
    if (iface && (active || (iface == player_iface && clock_ms() < InterlockedCompareExchange64(&flames_until, 0, 0)))) {
        *(int *)(self + NITROUS_BACKFIRE_STATE) = 2;
        release(iface);
        return;
    }
    // Fast & Furious clears the state first (0xfa3ca6); Forza Horizon 2's update
    // returns before clearing it for a car with no backfire level (+8 == 0),
    // which would keep our 2, and the flames, for good.
    *(int *)(self + NITROUS_BACKFIRE_STATE) = 0;
    backfire_original(self, delta_seconds, dynamics);
}

// Fast & Furious 0xfa4130: while boosting, a pop (1) on every update.
static void burble_hook(unsigned char *self, float delta_seconds, void **dynamics) {
    int active;
    void *iface = boosting_dynamics(dynamics, &active);
    if (iface && !active && iface == player_iface && InterlockedCompareExchange(&pending_pop, 0, 0) &&
        clock_ms() >= InterlockedCompareExchange64(&pop_at, 0, 0) && InterlockedExchange(&pending_pop, 0)) {
        burble_original(self, delta_seconds, dynamics);  // the game's own update, then the last pop
        *(int *)(self + NITROUS_BURBLE_STATE) = 1;
        return;
    }
    if (iface && active) {
        *(int *)(self + NITROUS_BURBLE_STATE) = 1;
        release(iface);
        return;
    }
    burble_original(self, delta_seconds, dynamics);
}

static float speed_ramp(float speed_mps) {
    const float low = NITROUS_CAMERA_MIN_MPH * 0.44704f, high = NITROUS_CAMERA_MAX_MPH * 0.44704f;
    if (!(speed_mps > low)) return 0.0f;
    if (speed_mps >= high) return 1.0f;
    return (speed_mps - low) / (high - low);
}

float nitrous_camera_nitrous(float speed_mps, float boost_seconds, float buildup_seconds) {
    float buildup = buildup_seconds > 0.0f ? boost_seconds / buildup_seconds : 0.0f;
    if (!(buildup > 0.0f)) buildup = 0.0f;
    if (buildup > 1.0f) buildup = 1.0f;
    return speed_ramp(speed_mps) * buildup;
}

float nitrous_camera_impulse(float speed_mps, int active, float *previous) {
    const float pulse = active && *previous == 0.0f ? 1.0f : 0.0f;
    *previous = active ? 1.0f : 0.0f;
    return speed_ramp(speed_mps) * pulse;
}

// Fast & Furious 0x105dfc0 / 0x105e014, on the carriers' layers.
static void camera_hook(unsigned char *layer, void *target, void *context, float external) {
    camera_original(layer, target, context, external);
    const int type = *(const int *)(layer + NITROUS_CAMERA_INPUT_TYPE);
    if ((type != NITROUS_CARRIER_NITROUS && type != NITROUS_CARRIER_IMPULSE) || !target) return;
    void *iface = ((Getter)(*(void ***)target)[0x70 / 8])(target);  // as the game's function does
    const void *vtable = NULL;
    if (!iface || !fh2_safe_read(iface, &vtable, sizeof(vtable)) || vtable != iface_vtable) return;
    const unsigned char *at = (const unsigned char *)iface;
    const float speed = *(const float *)(at + NITROUS_IFACE_SPEED);
    if (type == NITROUS_CARRIER_NITROUS) {
        float value = nitrous_camera_nitrous(speed, *(const float *)(at + NITROUS_IFACE_BUILDUP),
                                             *(const float *)(at + NITROUS_IFACE_BUILDUP_TIME));
        if (iface == player_iface) {  // the eased end of a boost
            const float eased = nitrous_camera_nitrous(speed, ending_envelope, 1.0f);
            if (eased > value) value = eased;
        }
        *(float *)layer = value;
    }
    else
        *(float *)layer =
            nitrous_camera_impulse(speed, *(const int *)(at + NITROUS_IFACE_ACTIVE) != 0, (float *)(layer + 4));
}

// ---------------------------------------------------------------- installing

// Takes over `entry`'s function; its original instructions are then at *original.
static const char *take_over(unsigned char *image, const Entry *entry, void *handler, void **original) {
    unsigned char *target = image + entry->rva;
    if (((uintptr_t)target & 7) != 0) return "not 8-aligned";
    if (!same(target, entry->bytes, entry->size)) return entry->what;
    unsigned char *page = fh2_allocate_near(target);
    if (!page) return "no free memory within 2 GB of the game";
    // [0] jmp [rip+0]; dq handler   [16] the original instructions; jmp [rip+0]; dq target + copy
    unsigned char *code = page;
    uintptr_t address = (uintptr_t)handler;
    code[0] = 0xff, code[1] = 0x25, code[2] = code[3] = code[4] = code[5] = 0;
    fh2_copy_bytes(code + 6, &address, 8);
    unsigned char *trampoline = page + 16;
    fh2_copy_bytes(trampoline, target, entry->copy);
    unsigned char *back = trampoline + entry->copy;
    address = (uintptr_t)(target + entry->copy);
    back[0] = 0xff, back[1] = 0x25, back[2] = back[3] = back[4] = back[5] = 0;
    fh2_copy_bytes(back + 6, &address, 8);
    DWORD protection, ignored;
    if (!VirtualProtect(page, 64, PAGE_EXECUTE_READ, &protection)) {
        VirtualFree(page, 0, MEM_RELEASE);
        return "the jump page cannot be made executable";
    }
    FlushInstructionCache(GetCurrentProcess(), page, 64);
    *original = trampoline;
    LONG64 before, after;
    fh2_copy_bytes(&before, target, 8);
    unsigned char patched[8];
    fh2_copy_bytes(patched, target, 8);
    const int32_t displacement = (int32_t)(page - (target + 5));
    patched[0] = 0xe9;
    fh2_copy_bytes(patched + 1, &displacement, 4);
    fh2_copy_bytes(&after, patched, 8);
    if (!VirtualProtect(target, 8, PAGE_EXECUTE_READWRITE, &protection)) {
        VirtualFree(page, 0, MEM_RELEASE);
        return "the function cannot be written";
    }
    const LONG64 seen = InterlockedCompareExchange64((volatile LONG64 *)target, after, before);
    VirtualProtect(target, 8, protection, &ignored);
    FlushInstructionCache(GetCurrentProcess(), target, 8);
    if (seen != before) {
        VirtualFree(page, 0, MEM_RELEASE);
        return "another thread changed the function meanwhile";
    }
    return NULL;
}

int hooks_start(const unsigned char *image, int flames, int camera, int rumble, void (*say)(const char *format, ...)) {
    static int installed;
    if (!image) return installed;
    iface_vtable = image + NITROUS_RVA_DYNAMICS_IFACE_VTABLE;
    unsigned char *game = (unsigned char *)image;
    if (flames && !(installed & 1)) {
        const char *why = take_over(game, &entries[0], (void *)backfire_hook, (void **)&backfire_original);
        if (!why) why = take_over(game, &entries[1], (void *)burble_hook, (void **)&burble_original);
        if (!why) installed |= 1;
        // A failed burble leaves the backfire hooked: flames without the pops.
        if (say)
            say(why ? "exhaust flames: %s; the backfire hook is %s"
                    : "exhaust flames: Fast & Furious' (the exhaust backfires on every update while boosting)%s",
                why ? why : "", why ? (backfire_original ? "in place (flames, no pops)" : "not installed") : "");
    }
    if (camera && !(installed & 2)) {
        // The carriers must be input types the game parses and leaves at 0.
        int32_t cases[2];
        const char *why = !same(image + camera_switch_rva, camera_switch, sizeof(camera_switch))
                              ? "the camera layers' input switch (0x1037e52) differs"
                          : !fh2_safe_read(image + camera_table_rva + 4 * NITROUS_CARRIER_NITROUS, cases, sizeof(cases)) ||
                                  (uint32_t)cases[0] != camera_zero_case || (uint32_t)cases[1] != camera_zero_case
                              ? "the camera input types 0x17 and 0x18 are computed by the game (0x1038558)"
                              : NULL;
        if (!why) why = take_over(game, &entries[2], (void *)camera_hook, (void **)&camera_original);
        if (!why) installed |= 2;
        if (say && why) say("camera: Fast & Furious' nitrous layers cannot be fed: %s", why);
    }
    if (rumble && !(installed & 4)) {
        const char *why = take_over(game, &entries[3], (void *)rumble_hook, (void **)&rumble_original);
        if (!why) installed |= 4;
        if (say)
            say(why ? "pad rumble: %s; none while boosting"
                    : "pad rumble: Fast & Furious' nitrous curves (stronger as the tank empties)%s",
                why ? why : "");
    }
    return installed;
}

// ---------------------------------------------------------------- telemetry

typedef ULONG (*EtxWrite)(const void *event, const void *provider, unsigned long long registration, ULONG count,
                          const NitrousEtxData *data);
static const unsigned char *etx_image;
static void **etx_slot;
static NitrousEtxSink etx_sink;
static EtxWrite volatile etx_previous;
static void *etx_first;
static volatile LONG etx_installed, etx_busy;

// The event's name, when it is one of the game's descriptors.
static const char *etx_name(const void *event) {
    const unsigned char *table = etx_image + NITROUS_RVA_ETX_TABLE;
    const unsigned char *at = (const unsigned char *)event;
    if (at < table || at >= table + NITROUS_ETX_EVENTS * 0x38u || (size_t)(at - table) % 0x38u) return NULL;
    const char *name = NULL;
    return fh2_safe_read(at + 0x10, &name, sizeof(name)) ? name : NULL;
}

static ULONG etx_hook(const void *event, const void *provider, unsigned long long registration, ULONG count,
                      const NitrousEtxData *data) {
    const NitrousEtxSink sink = etx_sink;
    if (sink) {
        const char *name = etx_name(event);
        if (name) sink(name, data ? count : 0, data);
    }
    return etx_previous(event, provider, registration, count, data);
}

// EtwPlus.dll!EtxEventWrite is imported at the slot (the import name table says so).
static const char *etx_check(const unsigned char *image) {
    const char *first = NULL;
    char name[19];
    if (!fh2_safe_read(image + NITROUS_RVA_ETX_TABLE + 0x10, &first, sizeof(first)) ||
        first != (const char *)image + NITROUS_RVA_ETX_FIRST_NAME || !fh2_safe_read(first, name, sizeof(name)) ||
        memcmp(name, "AnnaRecommendation", 19))
        return "the telemetry event table (0x2cb9280) differs";
    uint32_t pe = 0, directory = 0;
    if (!fh2_safe_read(image + 0x3c, &pe, 4) || pe > 4096 || !fh2_safe_read(image + pe + 24 + 120, &directory, 4) ||
        !directory)
        return "the game has no import table";
    for (unsigned i = 0; i < 512; ++i) {
        uint32_t entry[5];
        if (!fh2_safe_read(image + directory + 20 * i, entry, sizeof(entry)) || !entry[3]) break;
        char dll[16] = {0};
        if (!fh2_safe_read(image + entry[3], dll, 11) || _strnicmp(dll, "EtwPlus.dll", 11)) continue;
        for (unsigned k = 0; k < 4096; ++k) {
            unsigned long long thunk = 0;
            if (!fh2_safe_read(image + entry[0] + 8 * k, &thunk, 8) || !thunk) break;
            char function[14] = {0};
            if ((thunk >> 63) || !fh2_safe_read(image + (uint32_t)thunk + 2, function, 14) ||
                memcmp(function, "EtxEventWrite", 14))
                continue;
            return entry[4] + 8 * k == NITROUS_RVA_ETX_SLOT ? NULL : "EtxEventWrite is not imported at 0x1dd0060";
        }
        return "EtwPlus.dll!EtxEventWrite is not imported";
    }
    return "EtwPlus.dll is not imported";
}

int hooks_etx_prepare(const unsigned char *image, NitrousEtxSink sink) {
    if (!image || etx_check(image)) return 0;
    etx_image = image;
    etx_sink = sink;
    etx_slot = (void **)(image + NITROUS_RVA_ETX_SLOT);
    return 1;
}

int hooks_etx_ensure(void) {
    if (!etx_slot) return 0;
    void *current = NULL;
    if (!fh2_safe_read(etx_slot, &current, sizeof(current)) || !current) return 0;
    if (current == (void *)etx_hook) return 1;
    if (InterlockedCompareExchange(&etx_installed, 0, 0) && current != etx_first) return 1;  // wrapped after us
    if ((const unsigned char *)current >= etx_image && (const unsigned char *)current < etx_image + NITROUS_FH2_IMAGE_SIZE)
        return 0;  // not resolved yet (the name's RVA)
    if (!fh2_is_code(current) || InterlockedCompareExchange(&etx_busy, 1, 0)) return 0;
    etx_previous = (EtxWrite)current;
    const int ok = fh2_chain_slot(etx_slot, current, (void *)etx_hook) == FH2_CHAINED;
    if (ok) {
        if (!etx_first) etx_first = current;
        InterlockedExchange(&etx_installed, 1);
    }
    InterlockedExchange(&etx_busy, 0);
    return ok;
}

#ifdef NITROUS_TEST// Test build only: the handlers on fake objects, with fake originals.
__declspec(dllexport) void NitrousTestHooksSetup(const void *vtable, void *backfire, void *burble, void *camera) {
    iface_vtable = vtable;
    backfire_original = (CarEffectUpdate)backfire;
    burble_original = (CarEffectUpdate)burble;
    camera_original = (CameraInput)camera;
}
__declspec(dllexport) void NitrousTestRumbleSetup(void *original) { rumble_original = (RumbleUpdate)original; }
__declspec(dllexport) void NitrousTestRumble(unsigned char *self, float dt, const void *input, int wheel, float x) {
    hooks_set_rumble(x);
    rumble_hook(self, dt, input, wheel);
}
__declspec(dllexport) void NitrousTestHooksClock(unsigned long long ms) { hooks_clock = ms; }
__declspec(dllexport) float NitrousTestEndingFrame(int active, float buildup, float dt, float ease_out, float tail) {
    return hooks_ending_frame(active, buildup, dt, ease_out, tail);
}
__declspec(dllexport) void NitrousTestEnding(const void *player, float envelope, int flames, int pop) {
    hooks_set_player(player);
    hooks_set_ending(envelope, flames);
    if (pop) hooks_set_pop();
}
__declspec(dllexport) unsigned NitrousTestEtx(const void *image, const void *event, void *sink, void *previous) {
    etx_image = (const unsigned char *)image;
    etx_sink = (NitrousEtxSink)sink;
    etx_previous = (EtxWrite)previous;
    return (unsigned)etx_hook(event, NULL, 0, 0, NULL);
}
__declspec(dllexport) float NitrousTestCurve(int wheel, int motor, float x) {
    return nitrous_curve(&rumble_curves[wheel != 0][motor != 0], x);
}
__declspec(dllexport) void NitrousTestBackfire(unsigned char *self, float dt, void **dynamics) {
    backfire_hook(self, dt, dynamics);
}
__declspec(dllexport) void NitrousTestBurble(unsigned char *self, float dt, void **dynamics) {
    burble_hook(self, dt, dynamics);
}
__declspec(dllexport) void NitrousTestCameraInput(unsigned char *layer, void *target, void *context, float external) {
    camera_hook(layer, target, context, external);
}
__declspec(dllexport) float NitrousTestCameraNitrous(float speed, float boost, float buildup) {
    return nitrous_camera_nitrous(speed, boost, buildup);
}
__declspec(dllexport) float NitrousTestCameraImpulse(float speed, int active, float *previous) {
    return nitrous_camera_impulse(speed, active, previous);
}
__declspec(dllexport) const char *NitrousTestHooksCheck(const void *image) {
    const char *why = hooks_check((const unsigned char *)image);
    return why ? why : "";
}
#endif
