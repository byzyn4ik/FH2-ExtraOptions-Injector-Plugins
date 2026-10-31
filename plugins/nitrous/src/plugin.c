// Nitrous for Forza Horizon 2: an Extra Options mod-loader plugin.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Installed as mods\Nitrous\plugins\Nitrous.dll (loaded by Extra Options 0.4.4
// or forza_injector's Xtra Options). Forza Horizon 2 Presents Fast & Furious
// gave the player a nitrous boost; Forza Horizon 2's car physics still has the
// same nitrous model, unused (nitrous.c). While a key or a pad button is held
// (with the throttle fully down), this plugin asks the player's car for it on
// every local-car update (FH2PluginVehicleUpdate), with Fast & Furious' tuning
// by default, and refills the tank while it is not used. Only the player's car
// is touched; on any other game or build the plugin does nothing and says why.
// Settings: Nitrous.ini beside the DLL. Log lines start with "NITROUS:".
//
// Exports for other plugins (Keyboard & Mouse could bind its own key to it):
//   void WINAPI NitrousHold(BOOL held): held = TRUE asks for nitrous for the
//     next 250 ms (call it on every frame the key is down), FALSE releases it
//     at once. Any thread.
//   BOOL WINAPI NitrousTank(float *remaining, BOOL *boosting): the player car's
//     tank (0..1) and whether it boosts now; FALSE before the first armed car.
//   BOOL WINAPI NitrousUsage(unsigned *boosts, float *seconds): boosts begun and
//     seconds boosted since the game started (Fast & Furious' NitrousUsed).
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "fh2_game.h"
#include "fh2_nitrous.h"
#include "fh2_plugin.h"
#include "effects.h"
#include "hooks.h"
#include "nitrous.h"
#include "rules.h"

#define PLUGIN_EXPORT __declspec(dllexport)
#define HOLD_LAPSE_MS 250u

static void (*host_log)(const char *message);
static const unsigned char *game_image;
static volatile LONG ready;
static unsigned key_code = VK_LSHIFT, pad_mask = 0x40;  // Left Shift (Fast & Furious' key); XInput's left stick click
static volatile LONGLONG external_until;
static int last_status = -1;
static unsigned logged_arms, logged_boosts;
static volatile LONGLONG last_driving_ms;  // the last update with gameplay input
static NitrousTuning active_tuning = {1.6f, 20.0f, 0.5f, 60.0f};
// Fast & Furious' NitrousTelemetryComponent counts the boost ("NitrousUsed");
// NitrousUsage hands the same to other plugins (achievements).
static volatile LONG usage_boosts;
static volatile float usage_seconds;
// Where nitrous is allowed and how the tank fills (rules.h), from the ETX events.
static NitrousRules rules;
static volatile LONG place = NITROUS_FREE_ROAM, pending_ms, logged_place = -1, logged_skills;
static volatile LONGLONG gained_until, crash_block_until;
static NitrousCrash crash;
static unsigned logged_crashes;
// The end of a boost (hooks.h): the eased build-up and the flames' tail.
static float ending_envelope, ease_out = 1.5f, exhaust_tail = 0.35f;
#ifdef NITROUS_TEST
static int test_keys = -1;  // -1: the real keyboard; else the test's key state
static ULONGLONG test_now;
#endif

static void say(const char *format, ...) {
    char text[1024], line[1100];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    text[sizeof(text) - 1] = 0;
    snprintf(line, sizeof(line), "NITROUS: %s.\r\n", text);
    if (host_log) host_log(line);
}

static ULONGLONG now_ms(void) {
#ifdef NITROUS_TEST
    if (test_now) return test_now;
#endif
    return GetTickCount64();
}

// ---------------------------------------------------------------- settings

typedef struct {
    const wchar_t *name;
    unsigned value;
} InputName;

static const InputName key_names[] = {
    {L"None", 0},          {L"Space", VK_SPACE},     {L"Tab", VK_TAB},         {L"CapsLock", VK_CAPITAL},
    {L"Shift", VK_SHIFT},  {L"LShift", VK_LSHIFT},   {L"RShift", VK_RSHIFT},   {L"Ctrl", VK_CONTROL},
    {L"LCtrl", VK_LCONTROL}, {L"RCtrl", VK_RCONTROL}, {L"Alt", VK_MENU},       {L"LAlt", VK_LMENU},
    {L"RAlt", VK_RMENU},   {L"Enter", VK_RETURN},    {L"Backspace", VK_BACK},  {L"Insert", VK_INSERT},
    {L"Delete", VK_DELETE}, {L"Home", VK_HOME},      {L"End", VK_END},         {L"PageUp", VK_PRIOR},
    {L"PageDown", VK_NEXT}, {L"Up", VK_UP},          {L"Down", VK_DOWN},       {L"Left", VK_LEFT},
    {L"Right", VK_RIGHT},  {L"MouseLeft", VK_LBUTTON}, {L"MouseRight", VK_RBUTTON},
    {L"MouseMiddle", VK_MBUTTON}, {L"Mouse4", VK_XBUTTON1}, {L"Mouse5", VK_XBUTTON2}};

// XInput's wButtons bits, as FH2PluginVehicleFrame::buttons carries them.
static const InputName button_names[] = {
    {L"None", 0},       {L"DPadUp", 0x1},  {L"DPadDown", 0x2},    {L"DPadLeft", 0x4},   {L"DPadRight", 0x8},
    {L"Menu", 0x10},    {L"View", 0x20},   {L"LeftStick", 0x40},  {L"LS", 0x40},        {L"L3", 0x40},
    {L"RightStick", 0x80}, {L"RS", 0x80},  {L"R3", 0x80},         {L"LB", 0x100},       {L"RB", 0x200},
    {L"A", 0x1000},     {L"B", 0x2000},    {L"X", 0x4000},        {L"Y", 0x8000}};

static void trim(wchar_t *text) {
    size_t start = 0, length = wcslen(text);
    while (text[start] == L' ' || text[start] == L'\t') ++start;
    while (length > start && (text[length - 1] == L' ' || text[length - 1] == L'\t')) --length;
    memmove(text, text + start, (length - start) * sizeof(wchar_t));
    text[length - start] = 0;
}

// A key: a letter or digit, F1..F24, a name above, or a virtual-key code (0x..).
static int parse_key(const wchar_t *text, unsigned *code) {
    if (!text[0]) {
        *code = 0;
        return 1;
    }
    if (!text[1] && ((text[0] >= L'A' && text[0] <= L'Z') || (text[0] >= L'0' && text[0] <= L'9'))) {
        *code = (unsigned)text[0];
        return 1;
    }
    if (!text[1] && text[0] >= L'a' && text[0] <= L'z') {
        *code = (unsigned)(text[0] - L'a' + L'A');
        return 1;
    }
    if ((text[0] == L'F' || text[0] == L'f') && text[1] >= L'1' && text[1] <= L'9') {
        unsigned n = (unsigned)(text[1] - L'0');
        if (text[2] >= L'0' && text[2] <= L'9' && !text[3]) n = n * 10 + (unsigned)(text[2] - L'0');
        else if (text[2]) n = 0;
        if (n >= 1 && n <= 24) {
            *code = VK_F1 + n - 1;
            return 1;
        }
    }
    for (size_t i = 0; i < sizeof(key_names) / sizeof(key_names[0]); ++i) {
        if (CompareStringOrdinal(text, -1, key_names[i].name, -1, TRUE) == CSTR_EQUAL) {
            *code = key_names[i].value;
            return 1;
        }
    }
    if (text[0] == L'0' && (text[1] == L'x' || text[1] == L'X') && text[2]) {
        wchar_t *end = NULL;
        unsigned long value = wcstoul(text + 2, &end, 16);
        if (end && !*end && value > 0 && value < 256) {
            *code = (unsigned)value;
            return 1;
        }
    }
    return 0;
}

static int parse_button(const wchar_t *text, unsigned *mask) {
    if (!text[0]) {
        *mask = 0;
        return 1;
    }
    for (size_t i = 0; i < sizeof(button_names) / sizeof(button_names[0]); ++i) {
        if (CompareStringOrdinal(text, -1, button_names[i].name, -1, TRUE) == CSTR_EQUAL) {
            *mask = button_names[i].value;
            return 1;
        }
    }
    return 0;
}

static float read_float(const wchar_t *ini, const wchar_t *key, float fallback) {
    wchar_t text[64];
    GetPrivateProfileStringW(L"Nitrous", key, L"", text, 64, ini);
    trim(text);
    if (!text[0]) return fallback;
    wchar_t *end = NULL;
    float value = wcstof(text, &end);
    if (!end || *end || !(value == value)) {
        char name[64];
        WideCharToMultiByte(CP_UTF8, 0, key, -1, name, sizeof(name), NULL, NULL);
        say("[Nitrous] %s is not a number; %g is used", name, (double)fallback);
        return fallback;
    }
    return value;
}

static int read_settings(const wchar_t *ini, NitrousTuning *tuning) {
    if (!GetPrivateProfileIntW(L"Nitrous", L"Enabled", 1, ini)) return 0;
    wchar_t text[64];
    char shown[64];
    GetPrivateProfileStringW(L"Nitrous", L"Key", L"LShift", text, 64, ini);
    trim(text);
    if (!parse_key(text, &key_code)) {
        WideCharToMultiByte(CP_UTF8, 0, text, -1, shown, sizeof(shown), NULL, NULL);
        say("[Nitrous] Key=%s is not a key; LShift is used", shown);
        key_code = VK_LSHIFT;
    }
    GetPrivateProfileStringW(L"Nitrous", L"PadButton", L"LeftStick", text, 64, ini);
    trim(text);
    if (!parse_button(text, &pad_mask)) {
        WideCharToMultiByte(CP_UTF8, 0, text, -1, shown, sizeof(shown), NULL, NULL);
        say("[Nitrous] PadButton=%s is not a button; LeftStick is used", shown);
        pad_mask = 0x40;
    }
    tuning->torque_scale = read_float(ini, L"TorqueScale", 1.6f);
    tuning->duration = read_float(ini, L"Duration", 20.0f);
    tuning->buildup = read_float(ini, L"BuildupTime", 0.5f);
    tuning->recharge = read_float(ini, L"RechargeTime", 60.0f);
    return 1;
}

// ---------------------------------------------------------------- input

static int game_has_focus(void) {
    HWND window = GetForegroundWindow();
    DWORD process = 0;
    if (window) GetWindowThreadProcessId(window, &process);
    return process == GetCurrentProcessId();
}

static int key_held(void) {
#ifdef NITROUS_TEST
    if (test_keys >= 0) return key_code && test_keys;
#endif
    return key_code && game_has_focus() && (GetAsyncKeyState((int)key_code) & 0x8000);
}

PLUGIN_EXPORT void WINAPI NitrousHold(BOOL held) {
    InterlockedExchange64(&external_until, held ? (LONGLONG)(now_ms() + HOLD_LAPSE_MS) : 0);
}

PLUGIN_EXPORT BOOL WINAPI NitrousTank(float *remaining, BOOL *boosting) {
    const NitrousState *now = nitrous_state();
    if (!InterlockedCompareExchange(&ready, 0, 0) || !now->arms) return FALSE;
    if (remaining) *remaining = now->remaining;
    if (boosting) *boosting = now->active;
    return TRUE;
}

// Since the game started: boosts (runs) begun and seconds boosted; FALSE before the plugin is ready.
PLUGIN_EXPORT BOOL WINAPI NitrousUsage(unsigned *boosts, float *seconds) {
    if (!InterlockedCompareExchange(&ready, 0, 0)) return FALSE;
    if (boosts) *boosts = (unsigned)InterlockedCompareExchange(&usage_boosts, 0, 0);
    if (seconds) *seconds = usage_seconds;
    return TRUE;
}

PLUGIN_EXPORT BOOL WINAPI NitrousStatus(FH2NitrousStatus *status) {
    const NitrousState *now = nitrous_state();
    if (!status || status->size < sizeof(FH2NitrousStatus) || !InterlockedCompareExchange(&ready, 0, 0) || !now->arms)
        return FALSE;
    FH2NitrousStatus out = {0};
    out.size = sizeof(out);
    out.remaining = now->remaining;
    out.level = now->level > ending_envelope ? now->level : ending_envelope;
    out.run_seconds = now->run ? now->run_seconds : 0.0f;
    out.fov_degrees = effects_fov_degrees();
    LONGLONG driving = InterlockedCompareExchange64(&last_driving_ms, 0, 0);
    out.flags = (now->active ? FH2_NITROUS_BOOSTING : 0) | (now->held ? FH2_NITROUS_HELD : 0) |
                (driving && (LONGLONG)now_ms() - driving < 500 ? FH2_NITROUS_DRIVING : 0) |
                (now->run ? FH2_NITROUS_RUN : 0) |
                (!now->active && !now->held && now->remaining < 1.0f && active_tuning.recharge > 0.0f
                     ? FH2_NITROUS_REFILLING
                     : 0) |
                (rules_allowed(&rules, (int)place) ? 0 : FH2_NITROUS_BLOCKED) |
                ((LONGLONG)now_ms() < InterlockedCompareExchange64(&gained_until, 0, 0) ? FH2_NITROUS_GAINED : 0);
    *status = out;
    return TRUE;
}

// ---------------------------------------------------------------- telemetry

// Forza Horizon 2's ETX events (hooks.c): where the player is, skills, chains.
static void on_event(const char *event, unsigned count, const NitrousEtxData *data) {
    const LONG before = InterlockedCompareExchange(&place, 0, 0);
    const int after = rules_place_after((int)before, event);
    if (after != before) InterlockedExchange(&place, after);
    if (!rules.skill_recharge || count < 4 || !data) return;
    float seconds = 0.0f;
    if (!strcmp(event, "SkillAchieved")) {  // field 3: the skill's name (UTF-16)
        wchar_t name[64] = {0};
        const unsigned size = data[3].size < sizeof(name) - 2 ? data[3].size : (unsigned)sizeof(name) - 2;
        if (!data[3].pointer || !fh2_safe_read((const void *)(uintptr_t)data[3].pointer, name, size)) return;
        const int group = rules_skill_group(name);
        seconds = rules.skill[group];
        if (seconds > 0.0f && InterlockedIncrement(&logged_skills) <= 20) {
            char shown[64];
            WideCharToMultiByte(CP_UTF8, 0, name, -1, shown, sizeof(shown), NULL, NULL);
            say("skill %s (%s): +%.1f s of nitrous", shown, rules_skill_key(group), (double)seconds);
        }
    } else if (!strcmp(event, "SkillChainCompleted")) {  // field 3: the chain's points
        int points = 0;
        if (data[3].size != 4 || !fh2_safe_read((const void *)(uintptr_t)data[3].pointer, &points, 4) || points <= 0)
            return;
        seconds = rules.chain_per_1000 * (float)points / 1000.0f;
    }
    if (seconds > 0.0f) {
        InterlockedExchangeAdd(&pending_ms, (LONG)(seconds * 1000.0f + 0.5f));
        InterlockedExchange64(&gained_until, (LONGLONG)now_ms() + 600);
    }
}

static const char *place_name(int where) {
    return where == NITROUS_IN_RACE ? "a race" : where == NITROUS_IN_OTHER_EVENT ? "an event (showcase, bucket list, "
                                                                                  "rivals, online)" : "free roam";
}

// ---------------------------------------------------------------- plugin

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);
    return TRUE;
}

PLUGIN_EXPORT BOOL WINAPI FH2PluginInitialize(const FH2PluginContext *context) {
    if (!context || context->size < sizeof(FH2PluginContext) || context->api_version < 1) return FALSE;
    host_log = context->log_utf8;
    InterlockedExchange(&ready, 0);
    nitrous_reset();
    wchar_t ini[MAX_PATH + 8];
    if (!fh2_plugin_ini_path(context->plugin_path, ini, sizeof(ini) / sizeof(ini[0]))) {
        say("the plugin's path is too long or missing; no nitrous");
        return FALSE;
    }
    NitrousTuning tuning = {1.6f, 20.0f, 0.5f, 60.0f};
    if (!read_settings(ini, &tuning)) {
        say("[Nitrous] Enabled=0; no nitrous");
        return TRUE;
    }
    say("plugin " NITROUS_VERSION " for Extra Options %s",
        context->extra_options_version ? context->extra_options_version : "?");
    if (context->game_image_size == NITROUS_FF_IMAGE_SIZE) {
        say("this is Forza Horizon 2 Presents Fast & Furious, which has its own nitrous; the plugin does nothing");
        return TRUE;
    }
    const char *why = nitrous_check_game((const unsigned char *)context->game_image, context->game_image_size);
    if (why) {
        say("not Forza Horizon 2 1.0.0.64: %s; the plugin does nothing", why);
        return TRUE;
    }
    const char *rule_warning = NULL;
    rules_read(ini, &rules, &rule_warning);
    if (rule_warning) say("[Nitrous]/[SkillRecharge] %s is not valid; its default is used", rule_warning);
    if (!rules.auto_recharge) tuning.recharge = 0.0f;
    nitrous_configure(&tuning);
    game_image = (const unsigned char *)context->game_image;
    InterlockedExchange(&place, NITROUS_FREE_ROAM);
    InterlockedExchange(&pending_ms, 0);
    InterlockedExchange(&logged_place, -1);
    InterlockedExchange(&logged_skills, 0);
    logged_crashes = 0;
    NitrousCrash still = {0};
    crash = still;
    ending_envelope = 0.0f;
    hooks_ending_frame(0, 0.0f, 0.0f, 0.0f, 0.0f);
    ease_out = effects_number(ini, L"EaseOut", 1.5f, 0.0f, 10.0f);
    exhaust_tail = effects_number(ini, L"ExhaustTail", 0.35f, 0.0f, 5.0f);
    const int telemetry = hooks_etx_prepare((const unsigned char *)context->game_image, on_event);
    say("mode %s: tank refills %s%s; allowed in races %s, free roam %s, other events %s; crash penalty %.1f s "
        "(from %.0f km/h lost)%s; %s",
        rules.mode == NITROUS_MODE_UNDERGROUND ? "Underground" : rules.mode == NITROUS_MODE_CUSTOM ? "Custom" : "Arcade",
        rules.auto_recharge ? "over time" : "not over time", rules.skill_recharge ? ", from skills" : "",
        rules.in_races ? "yes" : "no", rules.free_roam ? "yes" : "no", rules.in_other_events ? "yes" : "no",
        (double)rules.crash_penalty, (double)rules.crash_impact, rules.crash_cancels ? ", a crash ends the boost" : "",
        telemetry ? "game telemetry read for races and skills" : "the game's telemetry import differs: no race or skill rules");
    last_status = -1;
    logged_arms = logged_boosts = 0;
    last_driving_ms = 0;
    active_tuning = tuning;
    wchar_t folder[MAX_PATH + 8];
    lstrcpynW(folder, context->plugin_path, MAX_PATH + 8);
    wchar_t *slash = wcsrchr(folder, L'\\');
    if (slash) *slash = 0;
    // Fast & Furious' exhaust flames and camera layers, in the game's own code (hooks.c).
    NitrousEffects wanted;
    effects_read(ini, &wanted, NULL);
    int layers = 0;
    if (slash && wanted.camera_layers) {
        wchar_t xml[MAX_PATH + 64];
        _snwprintf(xml, MAX_PATH + 63, L"%ls\\..\\media\\Camera\\CameraPhysics.xml", folder);
        xml[MAX_PATH + 63] = 0;
        const DWORD attributes = GetFileAttributesW(xml);
        layers = attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
        if (!layers)
            say("camera: no media\\Camera\\CameraPhysics.xml in the mod (tools\\ff_camera.py makes it from Fast & "
                "Furious); the plugin's FOV kick instead");
    }
    const int game_camera = (hooks_start(game_image, wanted.flames, layers, wanted.rumble, say) & 2) != 0;
    if (!wanted.flames) say("[Effects] ExhaustFlames=0: no exhaust flames");
    if (!wanted.rumble) say("[Effects] Rumble=0: no nitrous rumble");
    effects_start(ini, slash ? folder : NULL, game_image, game_camera, say);
    InterlockedExchange(&ready, 1);
    say("Forza Horizon 2 1.0.0.64: the physics' own nitrous is used; key 0x%02x, pad buttons 0x%04x; torque x%.2f, "
        "%.1f s tank, %.2f s build-up, refill %.0f s%s",
        key_code, pad_mask, (double)tuning.torque_scale, (double)tuning.duration, (double)tuning.buildup,
        (double)tuning.recharge, tuning.recharge > 0.0f ? "" : " (never)");
    return TRUE;
}

PLUGIN_EXPORT void WINAPI FH2PluginVehicleUpdate(const FH2PluginVehicleFrame *frame) {
    if (!InterlockedCompareExchange(&ready, 0, 0) || !frame || frame->size < sizeof(FH2PluginVehicleFrame) ||
        !frame->car)
        return;
    int enabled = frame->input_enabled != 0;
    int held = 0;
    if (enabled) {
        held = (pad_mask && (frame->buttons & pad_mask)) || key_held() ||
               (LONGLONG)now_ms() < InterlockedCompareExchange64(&external_until, 0, 0);
    }
    hooks_etx_ensure();
    const int where = (int)InterlockedCompareExchange(&place, 0, 0);
    const int allowed = rules_allowed(&rules, where);
    if (InterlockedExchange(&logged_place, where) != where)
        say("now in %s: nitrous %s", place_name(where), allowed ? "allowed" : "not allowed");
    if (!allowed || (LONGLONG)now_ms() < crash_block_until) held = 0;
    const LONG gained = InterlockedExchange(&pending_ms, 0);
    if (gained) nitrous_adjust_tank((float)gained / 1000.0f);
    int status = nitrous_frame(game_image, frame->car, held, frame->delta_seconds);
    if (status == NITROUS_ARMED && logged_arms < 8) {
        ++logged_arms;
        say("armed the player's car (dynamics %p)%s", nitrous_state()->dynamics,
            nitrous_state()->arms > 1 ? " again (a new car, or the game rebuilt its engine)" : "");
    }
    const NitrousState *now = nitrous_state();
    if (status < NITROUS_NOT_LOCAL_CAR) {
        if (enabled) InterlockedExchange64(&last_driving_ms, (LONGLONG)now_ms());
        if (now->started) InterlockedIncrement(&usage_boosts);
        if (now->active && frame->delta_seconds > 0.0f && frame->delta_seconds < 0.5f)
            usage_seconds += frame->delta_seconds;
        if (now->started && logged_boosts < 40) {
            ++logged_boosts;
            say("boost started (tank %.0f %%, throttle %.2f)", (double)(now->remaining * 100.0f), (double)now->throttle);
        }
        if (now->ended) {
            if (logged_boosts < 40) {
                ++logged_boosts;
                char detail[160] = "";
                int n = 0;
                if (now->ended == NITROUS_END_THROTTLE || now->ended == NITROUS_END_GEAR_CHANGE)
                    n = snprintf(detail, sizeof(detail), " (throttle %.2f)", (double)now->ended_throttle);
                if (now->ended_bridged && n >= 0 && (size_t)n < sizeof(detail))
                    snprintf(detail + n, sizeof(detail) - (size_t)n, "; %u shorter cut%s bridged, build-up kept",
                             now->ended_bridged, now->ended_bridged == 1 ? "" : "s");
                say("boost ended after %.1f s (tank %.0f %%): %s%s", (double)now->ended_seconds,
                    (double)(now->remaining * 100.0f), nitrous_end_text(now->ended), detail);
            }
            nitrous_clear_ended();
        }
    }
    if (status < NITROUS_NOT_LOCAL_CAR) {
        const float dt = frame->delta_seconds > 0.0f && frame->delta_seconds < 0.5f ? frame->delta_seconds : 0.0f;
        unsigned char *iface = (unsigned char *)now->dynamics + NITROUS_IFACE_OFFSET;
        hooks_set_player(iface);
        // Crashes: the speed lost within 0.15 s (rules.c).
        float speed = 0.0f;
        if ((rules.crash_penalty > 0.0f || rules.crash_cancels) && fh2_safe_read(iface + NITROUS_IFACE_SPEED, &speed, 4) &&
            rules_crash_step(&crash, speed, dt, rules.crash_impact)) {
            if (rules.crash_penalty > 0.0f) nitrous_adjust_tank(-rules.crash_penalty);
            if (rules.crash_cancels && now->run) InterlockedExchange64(&crash_block_until, (LONGLONG)now_ms() + 600);
            if (logged_crashes++ < 20)
                say("crash: -%.1f s of nitrous%s", (double)rules.crash_penalty,
                    rules.crash_cancels && now->run ? ", the boost ends" : "");
        }
        // The end of a boost: the build-up eased out, the flames' tail, one pop.
        ending_envelope = hooks_ending_frame(now->active, active_tuning.buildup > 0.0f ? now->buildup_time / active_tuning.buildup : 1.0f,
                                             dt, ease_out, exhaust_tail);
    }
    effects_frame(now, frame->delta_seconds, enabled);
    // Fast & Furious' rumble input: IsNitrousActive * (1 - the tank left).
    hooks_set_rumble(status < NITROUS_NOT_LOCAL_CAR && now->active ? 1.0f - now->remaining : 0.0f);
    if (status >= NITROUS_NOT_LOCAL_CAR && status != last_status) {
        say(status == NITROUS_NOT_LOCAL_CAR ? "the local car is not a ForzaCar; left alone"
            : status == NITROUS_NO_DYNAMICS ? "the local car has no known CCarDynamics; left alone"
                                            : "the car's nitrous fields cannot be read; left alone");
    }
    last_status = status;
}

#ifdef NITROUS_TEST
// Test build only (tests\NitrousTest.dll): the same code with entry points
// for tests\run_tests.py. Nothing here runs in the shipped DLL.
PLUGIN_EXPORT void NitrousTestKeys(int state) { test_keys = state; }
PLUGIN_EXPORT void NitrousTestEvent(const char *event, unsigned count, const NitrousEtxData *data) {
    on_event(event, count, data);
}
PLUGIN_EXPORT void NitrousTestClock(unsigned long long ms) { test_now = ms; }
PLUGIN_EXPORT unsigned NitrousTestKey(void) { return key_code; }
PLUGIN_EXPORT unsigned NitrousTestPad(void) { return pad_mask; }
PLUGIN_EXPORT int NitrousTestReady(void) { return (int)ready; }
PLUGIN_EXPORT const char *NitrousTestCheck(const void *image, size_t size) {
    const char *why = nitrous_check_game((const unsigned char *)image, size);
    return why ? why : "";
}
PLUGIN_EXPORT int NitrousTestParseKey(const wchar_t *text) {
    unsigned code = 0;
    wchar_t copy[64];
    lstrcpynW(copy, text, 64);
    trim(copy);
    return parse_key(copy, &code) ? (int)code : -1;
}
PLUGIN_EXPORT int NitrousTestParseButton(const wchar_t *text) {
    unsigned mask = 0;
    return parse_button(text, &mask) ? (int)mask : -1;
}
PLUGIN_EXPORT int NitrousTestFrame(const void *image, void *car, int held, float dt) {
    return nitrous_frame((const unsigned char *)image, car, held, dt);
}
PLUGIN_EXPORT void NitrousTestConfigure(float scale, float duration, float buildup, float recharge) {
    NitrousTuning tuning = {scale, duration, buildup, recharge};
    nitrous_configure(&tuning);
}
PLUGIN_EXPORT void NitrousTestReset(void) { nitrous_reset(); }
PLUGIN_EXPORT void NitrousTestStateCopy(NitrousState *out) { *out = *nitrous_state(); }
PLUGIN_EXPORT void NitrousTestClearEnded(void) { nitrous_clear_ended(); }
PLUGIN_EXPORT const char *NitrousTestEndText(int reason) { return nitrous_end_text(reason); }
PLUGIN_EXPORT const char *NitrousTestEffectsRead(const wchar_t *ini, NitrousEffects *out) {
    const char *warning = NULL;
    effects_read(ini, out, &warning);
    return warning ? warning : "";
}
PLUGIN_EXPORT void NitrousTestCameraStep(NitrousCamera *camera, int boosting, int started, float dt) {
    nitrous_camera_step(camera, boosting, started, dt);
}
PLUGIN_EXPORT float NitrousTestCameraDegrees(const NitrousCamera *camera, const NitrousEffects *effects, int mounted) {
    return nitrous_camera_degrees(camera, effects, mounted);
}
PLUGIN_EXPORT float NitrousTestFovFactor(float base, float kick) { return nitrous_fov_factor(base, kick); }
PLUGIN_EXPORT void NitrousTestPlayerRender(NitrousPlayer *player, short *out, unsigned frames, int run, int started,
                                           float volume) {
    nitrous_player_render(player, out, frames, run, started, volume);
}
PLUGIN_EXPORT int NitrousTestPlayerIdle(const NitrousPlayer *player) { return nitrous_player_idle(player); }
PLUGIN_EXPORT int NitrousTestLoadWav(const wchar_t *path, NitrousClip *clip) { return nitrous_load_wav(path, clip); }
PLUGIN_EXPORT void NitrousTestSynth(NitrousSynth *synth, int reset, short *out, unsigned count, float level, int burst,
                                    float volume) {
    if (reset) nitrous_synth_reset(synth);
    nitrous_synth_render(synth, out, count, level, burst, volume);
}
PLUGIN_EXPORT float NitrousTestFovDegrees(void) { return effects_fov_degrees(); }
PLUGIN_EXPORT void NitrousTestState(void **dynamics, unsigned *arms, float *remaining, int *active) {
    const NitrousState *now = nitrous_state();
    *dynamics = now->dynamics;
    *arms = now->arms;
    *remaining = now->remaining;
    *active = now->active;
}
#endif
