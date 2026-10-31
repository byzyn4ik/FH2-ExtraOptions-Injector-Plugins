// Custom Radio for Forza Horizon 2 Extra Options: the plugin's entry points.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// An Extra Options mod-loader plugin (tools/PLUGIN_API.md of Extra Options):
// FH2PluginInitialize reads CustomRadio.ini beside this DLL (the DLL path with
// its extension replaced) and installs the station's hooks;
// FH2PluginVehicleUpdate (the local car's update, on the game
// thread) reads the three keys while gameplay input is enabled and the game
// window has focus; FH2PluginMediaDirectory names the folder of the station's
// logos (station_logo.c).
#include "fh2_game.h"    // common/include: <DLL path>.ini
#include "fh2_plugin.h"
#include "config.h"
#include "ff_tick.h"
#include "hooks.h"
#include "radio.h"
#include "sites.h"
#include "station_logo.h"

#define CUSTOM_RADIO_VERSION "0.0.6"

static RadioConfig config;
static KeyEdges edges;
static KeyStateFn key_state;  // NULL: GetAsyncKeyState
static int focus_override = -1;  // test build: -1 = ask Windows
static int own_tick;  // Fast & Furious: the keys come from ff_tick.c, not FH2PluginVehicleUpdate
static void ff_tick(unsigned buttons);

static int game_focused(void) {
    if (focus_override >= 0) {
        return focus_override;
    }
    HWND window = GetForegroundWindow();
    DWORD process = 0;
    if (window) {
        GetWindowThreadProcessId(window, &process);
    }
    return process == GetCurrentProcessId();
}

static int fh2_join(wchar_t *output, const wchar_t *a, const wchar_t *b) {
    output[0] = 0;
    return wide_append(output, PATH_CAPACITY, a) && wide_append(output, PATH_CAPACITY, L"\\") &&
           wide_append(output, PATH_CAPACITY, b);
}

// Fast & Furious: CFileSys' resolver slot (vtable 0x2eb6ff8 + 0x210), which
// Language Support swaps from ForzaPluginEarlyInitialize (before this plugin
// starts) to mount every mod's media\ folder; 0x121a7a0 is the game's own.
#define FF_FILE_RESOLVER_SLOT 0x2eb7208u
#define FF_FILE_RESOLVER 0x121a7a0u

// The mod's media\ folder (<plugins>\..\media) when Language Support's mount
// is installed in this game (the resolver slot holds code of LangSupport.dll)
// and the folder is there; else 0.
static int ff_media_folder(const FH2PluginContext *context, wchar_t *output) {
    void *value = NULL;
    HMODULE module = NULL;
    static wchar_t path[PATH_CAPACITY], joined[PATH_CAPACITY];
    unsigned char *image = context->game_image;
    if (!memory_read(image + FF_FILE_RESOLVER_SLOT, &value, sizeof(value)) || !value ||
        value == image + FF_FILE_RESOLVER || !memory_is_code(value) ||
        !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)value, &module)) {
        return 0;
    }
    DWORD length = GetModuleFileNameW(module, path, PATH_CAPACITY);
    if (!length || length >= PATH_CAPACITY || length < 15 ||
        !wide_equal_ignore_case(path + length - 15, L"LangSupport.dll") ||
        (length > 15 && path[length - 16] != L'\\' && path[length - 16] != L'/')) {
        return 0;
    }
    if (!context->plugins_directory || !fh2_join(joined, context->plugins_directory, L"..\\media")) {
        return 0;
    }
    DWORD full = GetFullPathNameW(joined, PATH_CAPACITY, output, NULL);
    DWORD attributes = full && full < PATH_CAPACITY ? GetFileAttributesW(output) : INVALID_FILE_ATTRIBUTES;
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

// The folder of the game's launcher (Azimuth.exe): relative Folder= values start here.
static void launcher_folder(wchar_t *output, size_t capacity) {
    DWORD length = GetModuleFileNameW(NULL, output, (DWORD)capacity);
    if (!length || length >= capacity) {
        output[0] = 0;
        return;
    }
    while (length && output[length - 1] != L'\\' && output[length - 1] != L'/') {
        --length;
    }
    output[length ? length - 1 : 0] = 0;
}

static void log_keys(void) {
    for (unsigned i = 0; i < KEY_COUNT; ++i) {
        char text[64];
        unsigned key = config.keyboard[i], at = 0;
        const char *label = config_action_name(i);
        if (!key) {
            memory_copy(text, "keyboard none", 14);
        } else if (key >= VK_F1 && key <= VK_F24) {
            memory_copy(text, "keyboard F", 10);
            at = 10;
            unsigned number = key - VK_F1 + 1;
            if (number >= 10) {
                text[at++] = (char)('0' + number / 10);
            }
            text[at++] = (char)('0' + number % 10);
            text[at] = 0;
        } else {
            static const char hex[] = "0123456789ABCDEF";
            memory_copy(text, "keyboard 0x", 11);
            text[11] = hex[key >> 4 & 15];
            text[12] = hex[key & 15];
            text[13] = 0;
        }
        log_append_parts("CUSTOM RADIO: ", label, ": ", text);
    }
}

__declspec(dllexport) BOOL WINAPI FH2PluginInitialize(const FH2PluginContext *context) {
    static wchar_t ini[PATH_CAPACITY], base[PATH_CAPACITY];
    if (!context || context->size < sizeof(*context) || context->api_version < 1) {
        return FALSE;
    }
    log_bind(context->log_utf8);
    log_append("CUSTOM RADIO " CUSTOM_RADIO_VERSION ": starting.\r\n");
    const RadioGame *game = context->game_image
                                ? hooks_select(context->game_image, context->game_image_size)
                                : NULL;
    if (!game || !context->plugin_path || !fh2_plugin_ini_path(context->plugin_path, ini, ARRAY_COUNT(ini))) {
        log_append("CUSTOM RADIO: unsupported game image or plugin path; nothing installed.\r\n");
        return FALSE;
    }
    if (game->fast_furious) {
        log_append_parts("CUSTOM RADIO: ", game->name, " (SizeOfImage 0x3a89000, TimeDateStamp 0x55b62fde).",
                         NULL);
    }
    launcher_folder(base, ARRAY_COUNT(base));
    config_load(ini, base, &config);
    log_append_wide("CUSTOM RADIO: settings ", ini);
    log_append_wide("CUSTOM RADIO: music folder ", config.folder);
    log_keys();
    if (!radio_install(context->game_image, &config)) {
        return FALSE;
    }
    // Fast & Furious has no Extra Options runtime calling FH2PluginVehicleUpdate:
    // the plugin reads its keys in the local car's update itself.
    if (game->fast_furious && config.enabled && radio_installed()) {
        own_tick = ff_tick_install(context->game_image, ff_tick);
    }
    static wchar_t media[PATH_CAPACITY];
    if (config.enabled && game->fast_furious && !ff_media_folder(context, media)) {
        // Only Language Support serves mod media on Fast & Furious (each mod's
        // media\ folder, mounted from its early start): without it the logo's
        // texture would not be found, so the HUD is left as the game has it.
        log_append("CUSTOM RADIO: Fast & Furious: the station's HUD logo is off (Language Support does not mount "
                   "the mods' media\\ folders; its file resolver hook at 0x2eb7208 is not there); the station's "
                   "icon is hidden, as the game hides its own stations'.\r\n");
        station_logo_prepare(context->plugins_directory, NULL, &config);
        station_logo_install();
    } else if (config.enabled && context->plugins_directory &&
               (game->fast_furious || fh2_join(media, context->plugins_directory, LOGO_MEDIA_FOLDER))) {
        if (game->fast_furious) {
            log_append_wide("CUSTOM RADIO: Fast & Furious: Language Support mounts the mods' media; the logos from ",
                            media);
        }
        station_logo_prepare(context->plugins_directory, media, &config);
        station_logo_install();
    }
    return TRUE;
}

// The plugin's media folder (the same folder mod.json names as the mod's
// media): the station logos, below <plugins>\CustomRadio\Media. NULL when it
// is missing or the station is off.
__declspec(dllexport) const wchar_t *WINAPI FH2PluginMediaDirectory(void) {
    return station_logo_media_directory();
}

// Fast & Furious (ff_tick.c): the local car's update, on the game thread. The
// car updating and the game window focused stand for Extra Options' gameplay
// input state, which Fast & Furious has no runtime to report.
static void ff_tick(unsigned buttons) {
    if (!radio_installed()) {
        return;
    }
    int enabled = game_focused();
    unsigned down = keys_down(&config, key_state ? key_state : GetAsyncKeyState, buttons);
    radio_game_tick(keys_pressed(&edges, down, enabled), enabled);
}

__declspec(dllexport) void WINAPI FH2PluginVehicleUpdate(const FH2PluginVehicleFrame *frame) {
    if (!frame || frame->size < sizeof(*frame) || !radio_installed() || own_tick) {
        return;
    }
    int enabled = frame->input_enabled && game_focused();
    unsigned down = keys_down(&config, key_state ? key_state : GetAsyncKeyState, frame->buttons);
    radio_game_tick(keys_pressed(&edges, down, enabled), enabled);
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

#ifdef CUSTOM_RADIO_TEST
// Test build only: the key state and window focus the vehicle update sees.
__declspec(dllexport) void CustomRadioTestInput(KeyStateFn state, int focused) {
    key_state = state;
    focus_override = focused;
}
__declspec(dllexport) unsigned CustomRadioTestKeyEdges(unsigned down, int enabled) {
    static KeyEdges test_edges;
    return keys_pressed(&test_edges, down, enabled);
}
__declspec(dllexport) int CustomRadioTestConfig(const wchar_t *ini_path, const wchar_t *base_directory,
                                                RadioConfig *output, unsigned size) {
    if (size != sizeof(RadioConfig)) {
        return 0;
    }
    config_load(ini_path, base_directory, output);
    return 1;
}
__declspec(dllexport) int CustomRadioTestParseKey(const wchar_t *text, int xbox, unsigned *value) {
    return xbox ? config_parse_xbox(text, value) : config_parse_keyboard(text, value);
}
__declspec(dllexport) unsigned CustomRadioTestConfigSize(void) {
    return sizeof(RadioConfig);
}
__declspec(dllexport) void CustomRadioTestBindLog(void (*sink)(const char *message)) {
    log_bind(sink);
}
#endif
