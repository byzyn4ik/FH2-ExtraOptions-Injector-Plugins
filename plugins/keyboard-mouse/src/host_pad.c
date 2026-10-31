// Keyboard & Mouse: host-level pad emulation (see host_pad.h).
// SPDX-License-Identifier: GPL-3.0-or-later
#include "host_pad.h"

#include "host.h"
#include "kbm_win32.h"

#include <string.h>

/* Azimuth's reading (1.0.4 0x4b18c0 / 0x4b0c40, from Windows.Gaming.Input):
   +0 timestamp, +8 buttons (WGI's GamepadButtons << 2, i.e. the Xbox One
   Windows.Xbox.Input.GamepadButtons values), +0xc left trigger, +0x10 right
   trigger, +0x14/+0x18 left stick X/Y, +0x1c/+0x20 right stick X/Y (floats,
   Y up positive). */
enum {
    PAD_MENU = 0x4,
    PAD_VIEW = 0x8,
    PAD_A = 0x10,
    PAD_B = 0x20,
    PAD_X = 0x40,
    PAD_Y = 0x80,
    PAD_UP = 0x100,
    PAD_DOWN = 0x200,
    PAD_LEFT = 0x400,
    PAD_RIGHT = 0x800,
    PAD_LB = 0x1000,
    PAD_RB = 0x2000,
    PAD_LS = 0x4000,
    PAD_RS = 0x8000,
};
enum { READING_SIZE = 0x28, OFF_BUTTONS = 8, OFF_LT = 0xc, OFF_RT = 0x10, OFF_LX = 0x14, OFF_LY = 0x18, OFF_RX = 0x1c };

/* What each binding presses. Axes: LT/RT full, left stick X -1 / +1. */
enum { EL_NONE, EL_BUTTON, EL_LT, EL_RT, EL_STEER_LEFT, EL_STEER_RIGHT };
typedef struct {
    unsigned char kind;
    unsigned short button;
} Element;

/* Forza Horizon 2's pad layout (the same in Fast & Furious): RT throttle, LT
   brake, A handbrake, B/X shift up/down, LB clutch, RS click camera, Y
   rewind, RB look back, X enter an event, D-pad left/right radio, View map,
   Menu pause; in menus A/B confirm/back, LB/RB tabs, the D-pad moves. */
static const Element elements[KBM_BINDING_COUNT] = {
    [KBM_THROTTLE] = {EL_RT, 0},
    [KBM_BRAKE] = {EL_LT, 0},
    [KBM_STEER_LEFT] = {EL_STEER_LEFT, 0},
    [KBM_STEER_RIGHT] = {EL_STEER_RIGHT, 0},
    [KBM_HANDBRAKE] = {EL_BUTTON, PAD_A},
    [KBM_SHIFT_UP] = {EL_BUTTON, PAD_B},
    [KBM_SHIFT_DOWN] = {EL_BUTTON, PAD_X},
    [KBM_CLUTCH] = {EL_BUTTON, PAD_LB},
    [KBM_CAMERA] = {EL_BUTTON, PAD_RS},
    [KBM_REWIND] = {EL_BUTTON, PAD_Y},
    [KBM_LOOK_BACK] = {EL_BUTTON, PAD_RB},
    [KBM_ACTIVATE] = {EL_BUTTON, PAD_X},
    [KBM_PHOTO_MODE] = {EL_BUTTON, PAD_UP},
    [KBM_RADIO_PREVIOUS] = {EL_BUTTON, PAD_LEFT},
    [KBM_RADIO_NEXT] = {EL_BUTTON, PAD_RIGHT},
    [KBM_MAP] = {EL_BUTTON, PAD_VIEW},
    [KBM_PAUSE] = {EL_BUTTON, PAD_MENU},
    [KBM_CONFIRM] = {EL_BUTTON, PAD_A},
    [KBM_BACK] = {EL_BUTTON, PAD_B},
    [KBM_UP] = {EL_BUTTON, PAD_UP},
    [KBM_DOWN] = {EL_BUTTON, PAD_DOWN},
    [KBM_LEFT] = {EL_BUTTON, PAD_LEFT},
    [KBM_RIGHT] = {EL_BUTTON, PAD_RIGHT},
    [KBM_TAB_LEFT] = {EL_BUTTON, PAD_LB},
    [KBM_TAB_RIGHT] = {EL_BUTTON, PAD_RB},
    [KBM_MENU_X] = {EL_BUTTON, PAD_X},
    [KBM_MENU_Y] = {EL_BUTTON, PAD_Y},
    [KBM_MENU_LEFT_TRIGGER] = {EL_LT, 0},
    [KBM_MENU_RIGHT_TRIGGER] = {EL_RT, 0},
    [KBM_DPAD_UP] = {EL_BUTTON, PAD_UP},
    [KBM_DPAD_DOWN] = {EL_BUTTON, PAD_DOWN},
    [KBM_DPAD_LEFT] = {EL_BUTTON, PAD_LEFT},
    [KBM_DPAD_RIGHT] = {EL_BUTTON, PAD_RIGHT},
};

/* A key in both groups (E: shift up / next tab, Enter: enter an event /
   confirm, ...) acts as its driving binding while a driving-only key was held
   in the last DRIVING_HOLD_MS, else as its menu binding. */
#define DRIVING_HOLD_MS 2500u
#define ESCAPE_CODE 0x01u
#define TAP_MS 60u

static KbmSettings config;
#ifdef KBM_TEST
static int test_no_thread;
__declspec(dllexport) void HostPadTestNoThread(int value) { test_no_thread = value; }
#endif
static volatile LONG installed;
static SRWLOCK lock = SRWLOCK_INIT;
static uint64_t last_driving_ms, last_tick_qpc, qpc_frequency;
static float steer;
static uint32_t pressed_at[256];
static unsigned char event_down[256];

typedef HRESULT(STDMETHODCALLTYPE *GetReadingFn)(void *, void **);
typedef HRESULT(STDMETHODCALLTYPE *GetRawReadingFn)(void *, unsigned char *);
static GetReadingFn original_reading;
static GetRawReadingFn original_raw_reading;

static int driving_only(unsigned binding) {
    return binding == KBM_THROTTLE || binding == KBM_BRAKE || binding == KBM_STEER_LEFT ||
           binding == KBM_STEER_RIGHT || binding == KBM_HANDBRAKE;
}

static int bound(const KbmSettings *settings, unsigned binding, unsigned code) {
    for (unsigned k = 0; k < KBM_KEYS_PER_BINDING; ++k)
        if (settings->keys[binding][k] == code) return 1;
    return 0;
}

static float clamp_unit(float value) {
    return value > 1.0f ? 1.0f : value < -1.0f ? -1.0f : value;
}

/* The merge, given which keys are down. */
static void merge(const KbmSettings *settings, const unsigned char *down, int driving, float dt,
                  unsigned char *reading) {
    uint32_t buttons = 0;
    int lt = 0, rt = 0, left = 0, right = 0, any = 0;
    for (unsigned code = 1; code < 256; ++code) {
        if (!down[code]) continue;
        int in_driving = 0, in_menu = 0;
        for (unsigned b = 0; b < KBM_BINDING_COUNT; ++b) {
            if (!bound(settings, b, code)) continue;
            if (kbm_config_binding_driving(b)) in_driving = 1;
            else in_menu = 1;
        }
        if (settings->escape && code == ESCAPE_CODE) {
            buttons |= driving ? PAD_MENU : PAD_B;
            any = 1;
        }
        if (!in_driving && !in_menu) continue;
        int use_driving = in_driving && (driving || !in_menu);
        for (unsigned b = 0; b < KBM_BINDING_COUNT; ++b) {
            if (kbm_config_binding_driving(b) != use_driving || !bound(settings, b, code)) continue;
            any = 1;
            switch (elements[b].kind) {
                case EL_BUTTON: buttons |= elements[b].button; break;
                case EL_LT: lt = 1; break;
                case EL_RT: rt = 1; break;
                case EL_STEER_LEFT: left = 1; break;
                case EL_STEER_RIGHT: right = 1; break;
            }
        }
    }
    /* Keyboard steering eases in and returns to the centre faster, as the PC
       Forza titles do (SteeringSpeed scales both). */
    float target = (float)(right - left);
    if (settings->invert_steering) target = -target;
    float rate = (target != 0.0f && (steer == 0.0f || (target > 0.0f) == (steer > 0.0f)) ? 4.0f : 8.0f) *
                 (float)settings->steering_speed / 100.0f;
    float step = rate * (dt > 0.1f ? 0.1f : dt);
    if (steer < target) steer = steer + step > target ? target : steer + step;
    else if (steer > target) steer = steer - step < target ? target : steer - step;
    if (!any && steer == 0.0f) return;
    uint32_t pad;
    float value;
    memory_copy(&pad, reading + OFF_BUTTONS, 4);
    pad |= buttons;
    memory_copy(reading + OFF_BUTTONS, &pad, 4);
    if (lt) {
        value = 1.0f;
        memory_copy(reading + OFF_LT, &value, 4);
    }
    if (rt) {
        value = 1.0f;
        memory_copy(reading + OFF_RT, &value, 4);
    }
    if (steer != 0.0f) {
        value = clamp_unit(steer);
        memory_copy(reading + OFF_LX, &value, 4);
    }
}

/* The current keys (Raw Input events plus the physical state; a tap shorter
   than a game tick still counts for TAP_MS), only while the game window has
   the focus and Azimuth shows no prompt. */
static void apply(unsigned char *reading) {
    if (!installed || !reading) return;
    AcquireSRWLockExclusive(&lock);
    uint32_t now = (uint32_t)GetTickCount64();
    KbmKeyEvent event;
    while (kbm_win32_next_event(&event)) {
        unsigned code = event.code & 0xff;
        event_down[code] = event.down;
        if (event.down) pressed_at[code] = event.time ? event.time : 1;
    }
    kbm_win32_take_overflow();
    long dx, dy;
    kbm_win32_take_mouse(&dx, &dy);
    kbm_win32_take_wheel();
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    float dt = last_tick_qpc && qpc_frequency ? (float)((double)(counter.QuadPart - (LONGLONG)last_tick_qpc) /
                                                        (double)qpc_frequency)
                                              : 0.0f;
    last_tick_qpc = (uint64_t)counter.QuadPart;
    unsigned char down[256] = {0};
    if (kbm_win32_focused()) {
        for (unsigned code = 1; code < 256; ++code) {
            int physical = kbm_win32_key_down(code);
            down[code] = physical == 1 || (physical < 0 && event_down[code]) ||
                         (pressed_at[code] && now - pressed_at[code] < TAP_MS);
        }
        for (unsigned b = 0; b < KBM_FIRST_MENU_BINDING; ++b) {
            if (!driving_only(b)) continue;
            for (unsigned k = 0; k < KBM_KEYS_PER_BINDING; ++k) {
                unsigned code = config.keys[b][k];
                if (code && code < 256 && down[code]) last_driving_ms = GetTickCount64();
            }
        }
    }
    int driving = last_driving_ms && GetTickCount64() - last_driving_ms < DRIVING_HOLD_MS;
    merge(&config, down, driving, dt, reading);
    ReleaseSRWLockExclusive(&lock);
}

static HRESULT STDMETHODCALLTYPE get_reading(void *gamepad, void **value) {
    HRESULT result = original_reading(gamepad, value);
    /* The reading object: vtable, reference count, then the reading at +0x10. */
    if (result >= 0 && value && *value) apply((unsigned char *)*value + 0x10);
    return result;
}

static HRESULT STDMETHODCALLTYPE get_raw_reading(void *gamepad, unsigned char *value) {
    HRESULT result = original_raw_reading(gamepad, value);
    if (result >= 0) apply(value);
    return result;
}

/* The virtual pad: with no gamepad connected Azimuth exposes none to the
   game, so nothing would read the keys. Azimuth 1.0.4 adds a
   Windows.Gaming.Input gamepad to its device list (0x8593d0, 0x58-byte
   entries) in 0x4b02c0, its GamepadAdded handler: it asks the pad only for
   IUnknown (its identity), keeps a reference, and later calls get_Vibration /
   put_Vibration (vtable +0x30/+0x38) and GetCurrentReading (+0x40). This
   object answers those with a neutral pad, so the game sees one gamepad whose
   readings the keys fill (get_reading above). Added once, when no gamepad is
   connected after the game window appears; a real pad can still join. */
#define VPAD_ADD_RVA 0x4b02c0u
#define VPAD_STATICS_USE_RVA 0x4b0715u /* mov rcx, [rip+statics] */
#define VPAD_STATICS_RVA 0x84e7d0u
#define VPAD_LIST_USE_RVA 0x4b0364u /* mov rax, [rip+begin]; mov rdx, [rip+end] */
#define VPAD_LIST_RVA 0x8593d0u
#define VPAD_IID_USE_RVA 0x4b02e6u /* lea rdx, [rip+IID_IUnknown] */
#define VPAD_IID_RVA 0x6c0aa0u

typedef struct {
    const void *const *vtable;
    volatile LONG references;
} VirtualPad;

static const GUID iid_unknown = {0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID iid_inspectable = {0xaf86e2e0, 0xb12d, 0x4c6a, {0x9c, 0x5a, 0xd7, 0xaa, 0x65, 0x10, 0x1e, 0x90}};
static const GUID iid_agile = {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};
static const GUID iid_gamepad = {0xbc7bb43c, 0x0a69, 0x3903, {0x9e, 0x9d, 0xa5, 0x0f, 0x86, 0xa4, 0x5d, 0xe5}};

static HRESULT STDMETHODCALLTYPE vpad_query(VirtualPad *self, const GUID *iid, void **out) {
    if (!out) return E_POINTER;
    if (iid && (IsEqualGUID(iid, &iid_unknown) || IsEqualGUID(iid, &iid_inspectable) ||
                IsEqualGUID(iid, &iid_agile) || IsEqualGUID(iid, &iid_gamepad))) {
        InterlockedIncrement(&self->references);
        *out = self;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE vpad_add_ref(VirtualPad *self) {
    return (ULONG)InterlockedIncrement(&self->references);
}
static ULONG STDMETHODCALLTYPE vpad_release(VirtualPad *self) {
    LONG left = InterlockedDecrement(&self->references);
    return left > 0 ? (ULONG)left : 1u; /* static: never freed */
}
static HRESULT STDMETHODCALLTYPE vpad_iids(VirtualPad *self, ULONG *count, GUID **iids) {
    (void)self;
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE vpad_class_name(VirtualPad *self, void **name) {
    (void)self;
    if (name) *name = NULL;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE vpad_trust(VirtualPad *self, int *level) {
    (void)self;
    if (level) *level = 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE vpad_get_vibration(VirtualPad *self, double *vibration) {
    (void)self;
    if (vibration) memset(vibration, 0, 4 * sizeof(double));
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE vpad_put_vibration(VirtualPad *self, double a, double b, double c, double d) {
    (void)self, (void)a, (void)b, (void)c, (void)d;
    return S_OK;
}
/* Windows.Gaming.Input.GamepadReading: timestamp, buttons, then six doubles. */
static HRESULT STDMETHODCALLTYPE vpad_reading(VirtualPad *self, unsigned char *reading) {
    (void)self;
    if (!reading) return E_POINTER;
    memset(reading, 0, 0x40);
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    uint64_t stamp = qpc_frequency ? (uint64_t)((double)counter.QuadPart * 1e6 / (double)qpc_frequency) : 0;
    memory_copy(reading, &stamp, sizeof(stamp));
    return S_OK;
}
static const void *const vpad_vtable[] = {
    (const void *)(uintptr_t)vpad_query,         (const void *)(uintptr_t)vpad_add_ref,
    (const void *)(uintptr_t)vpad_release,       (const void *)(uintptr_t)vpad_iids,
    (const void *)(uintptr_t)vpad_class_name,    (const void *)(uintptr_t)vpad_trust,
    (const void *)(uintptr_t)vpad_get_vibration, (const void *)(uintptr_t)vpad_put_vibration,
    (const void *)(uintptr_t)vpad_reading,
};
static VirtualPad virtual_pad = {vpad_vtable, 1};

static int rip_points(const unsigned char *host, unsigned use, unsigned length, unsigned target) {
    int32_t displacement;
    return memory_read(host + use + length - 4, &displacement, 4) &&
           (uint32_t)((int64_t)use + length + displacement) == target;
}

/* NULL when Azimuth 1.0.4's add routine and globals are as inspected. */
static const char *vpad_check(const unsigned char *host) {
    static const unsigned char add_entry[] = {0x4c, 0x8b, 0xdc, 0x49, 0x89, 0x5b, 0x10, 0x49, 0x89, 0x6b, 0x18,
                                              0x56, 0x57, 0x41, 0x56, 0x48, 0x81, 0xec, 0x80, 0x00, 0x00, 0x00};
    static const unsigned char statics_use[] = {0x48, 0x8b, 0x0d};
    static const unsigned char list_use[] = {0x48, 0x8b, 0x05};
    static const unsigned char iid_use[] = {0x48, 0x8d, 0x15};
    if (azimuth_host_build(host) != AZIMUTH_104) return "not Azimuth 1.0.4";
    if (!memory_equal(host + VPAD_ADD_RVA, add_entry, sizeof(add_entry))) return "its gamepad add routine differs";
    if (!memory_equal(host + VPAD_STATICS_USE_RVA, statics_use, 3) ||
        !rip_points(host, VPAD_STATICS_USE_RVA, 7, VPAD_STATICS_RVA) ||
        !memory_equal(host + VPAD_LIST_USE_RVA, list_use, 3) || !rip_points(host, VPAD_LIST_USE_RVA, 7, VPAD_LIST_RVA) ||
        !memory_equal(host + VPAD_IID_USE_RVA, iid_use, 3) || !rip_points(host, VPAD_IID_USE_RVA, 7, VPAD_IID_RVA) ||
        !memory_equal(host + VPAD_IID_RVA, &iid_unknown, sizeof(iid_unknown)))
        return "its gamepad list globals differ";
    return NULL;
}

/* Connected gamepads in Azimuth's list (entry +0x21; +0x22 is set once its
   input pump has announced the pad to the game). */
static int vpad_connected_pads(const unsigned char *host) {
    unsigned char *begin = NULL, *end = NULL;
    if (!memory_read(host + VPAD_LIST_RVA, &begin, 8) || !memory_read(host + VPAD_LIST_RVA + 8, &end, 8) || !begin ||
        end < begin || (size_t)(end - begin) % 0x58 || end - begin > 0x58 * 64)
        return 0;
    int pads = 0;
    for (unsigned char *entry = begin; entry < end; entry += 0x58) {
        unsigned char connected;
        if (memory_read(entry + 0x21, &connected, 1) && connected) ++pads;
    }
    return pads;
}

typedef void (*AddPadFn)(void *gamepad);

static DWORD WINAPI vpad_thread(void *unused) {
    (void)unused;
    unsigned char *host = azimuth_host_base();
    void *statics = NULL;
    /* Azimuth's input is up when its Gamepad statics are; then give a real pad
       a moment to be found. */
    for (unsigned waited = 0; waited < 1200; ++waited) {
        if (memory_read(host + VPAD_STATICS_RVA, &statics, 8) && statics) break;
        Sleep(100);
    }
    Sleep(3000);
    int pads = vpad_connected_pads(host);
    if (pads) {
        log_line("KBM HOST: %d gamepad(s) connected; no virtual pad (the keys go into their readings).", pads);
        return 0;
    }
    ((AddPadFn)(void *)(host + VPAD_ADD_RVA))(&virtual_pad);
    pads = vpad_connected_pads(host);
    log_line("KBM HOST: no gamepad connected: a virtual pad was added to Azimuth 1.0.4's gamepads (0x%x); %d connected "
             "(announced to the game on Azimuth's next input pump)%s.", VPAD_ADD_RVA, pads, statics ? "" : " (Windows.Gaming.Input was not found)");
    return 0;
}

static void vpad_start(void) {
    const char *problem = vpad_check(azimuth_host_base());
    if (problem) {
        log_line("KBM HOST: no virtual pad (%s); the keys need a connected gamepad.", problem);
        return;
    }
    HANDLE thread = CreateThread(NULL, 0, vpad_thread, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
}

int host_pad_install(const KbmSettings *settings) {
    memory_copy(&config, settings, sizeof(config));
    if (config.mode == KBM_MODE_PAD) {
        log_append("KBM HOST: Mode=PAD; the gamepad readings are untouched.\r\n");
        return 0;
    }
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    qpc_frequency = (uint64_t)frequency.QuadPart;
    const char *problem = kbm_win32_chain_readings((void *)(uintptr_t)get_reading, (void **)&original_reading,
                                                   (void *)(uintptr_t)get_raw_reading,
                                                   (void **)&original_raw_reading);
    if (problem) {
        log_line("KBM HOST: Azimuth's gamepad readings not chained (%s); keyboard & mouse is not installed.",
                 problem);
        return 0;
    }
    InterlockedExchange(&installed, 1);
#ifdef KBM_TEST
    if (test_no_thread) return 1;
#endif
    if (!kbm_win32_start()) log_append("KBM HOST: input thread could not start; keys stay neutral.\r\n");
    vpad_start();
    log_append("KBM HOST: keys are merged into the gamepad readings as Forza Horizon 2's pad layout (W/S triggers, "
               "A/D left stick, Space A, E/Q B/X, Tab right stick click, R Y, M View, Esc Menu/B, Enter A or X, "
               "arrows D-pad); the game's own input code is not patched.\r\n");
    return 1;
}

#ifdef KBM_TEST
/* The virtual pad's guard on a mapped Azimuth copy: NULL or why not. */
__declspec(dllexport) const char *HostPadTestCheckVirtualPad(const unsigned char *host) { return vpad_check(host); }
__declspec(dllexport) void *HostPadTestVirtualPad(void) { return &virtual_pad; }

__declspec(dllexport) void host_pad_test_merge(const KbmSettings *settings, const unsigned char *down, int driving, float dt,
                         unsigned char *reading) {
    merge(settings, down, driving, dt, reading);
}
#endif
