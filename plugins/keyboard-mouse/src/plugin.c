// Keyboard & Mouse for Forza Horizon 2: an Extra Options mod-loader plugin.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Installed as mods\Keyboard & Mouse\plugins\KeyboardMouse.dll beside Extra
// Options 0.4.4 on Azimuth 1.0.4 (or 1.0.2). Playing with a keyboard and
// mouse without a virtual controller: keys answer the game's own input
// actions (kbm_input.c), the mouse turns the driving look and drives a
// pointer in menus, the map, photo mode and the car viewer; a KEYBOARD &
// MOUSE tile on the pause menu's Settings hub opens a page with the options
// and one row per key (kbm_menu.c, kbm_rebind.c, settings_page.c). Also the
// controller-missing pause fix ([Controller] PauseOnlyOnUnplug).
//
// From FH2PluginInitialize (on Extra Options' loader worker, after the game
// started) it reads KeyboardMouse.ini, identifies the Azimuth host, checks
// every game site byte for byte and chains onto whatever is there (Extra
// Options' hooks, other plugins'); on any mismatch that part changes nothing
// and the log (FH2_ExtraOptions.log, "KBM...:" lines) says why.
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "controller_pause.h"
#include "eo_bindings.h"
#include "fh2_game.h"
#include "fh2_plugin.h"
#include "host.h"
#include "host_pad.h"
#include "kbm_game.h"
#include "kbm_input.h"
#include "kbm_menu.h"
#include "kbm_pitch.h"
#include "kbm_win32.h"
#include "patch.h"
#include "settings.h"
#include "settings_page.h"

#define PLUGIN_EXPORT __declspec(dllexport)

static wchar_t ini_path[PATH_CAPACITY];
static PluginSettings settings;

// KeyboardMouse.ini beside the DLL (common/include/fh2_game.h).
static int make_ini_path(const wchar_t *plugin_path) {
    return fh2_plugin_ini_path(plugin_path, ini_path, ARRAY_COUNT(ini_path));
}

static void keep_loaded(void) {
    HMODULE self = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                       (LPCWSTR)(void *)keep_loaded, &self);
}

// What runs: the controller pause option (game only), then keyboard & mouse
// (needs a supported Azimuth host).
static void start(unsigned char *game, uint32_t adapter) {
    int pause_installed = 0;
    if (!settings.controller_valid)
        log_line("KBM: %s; [Controller] is ignored.", settings.controller_problem);
    if (settings.pause_only_on_unplug) {
        keep_loaded();
        pause_installed = controller_pause_install(game, 1);
    }
    if (!settings.kbm_valid) {
        log_line("KBM: REFUSED: %s; keyboard & mouse is not installed.", settings.kbm_problem);
        return;
    }
    if (!settings.kbm.enabled) {
        log_append("KBM: keyboard & mouse is off ([KeyboardMouse] Enabled=0).\r\n");
        return;
    }
    char description[96];
    unsigned char *host = azimuth_host_base();
    AzimuthBuild build = azimuth_host_build(host);
    azimuth_host_describe(host, description, sizeof(description));
    if (adapter != FH2_PLUGIN_AZIMUTH || build == AZIMUTH_UNKNOWN) {
        log_line("KBM: the host is not Azimuth 1.0.4 or 1.0.2 (%s%s); keyboard & mouse is not installed%s.",
                 adapter != FH2_PLUGIN_AZIMUTH ? "not an Azimuth adapter, " : "", description,
                 pause_installed ? " (the controller pause option is)" : "");
        return;
    }
    if (!kbm_win32_check_host()) {
        log_line("KBM: Azimuth %s's window, prompt and cursor globals are not as inspected (%s); keyboard & mouse "
                 "is not installed.", azimuth_host_name(build), description);
        return;
    }
    log_line("KBM: host Azimuth %s (%s).", azimuth_host_name(build), description);
    eo_bindings_load(NULL);
    eo_bindings_report(&settings.kbm);
    keep_loaded();  // callbacks into this DLL stay in the game
    kbm_pitch_install(game);
    if (!kbm_install(game, &settings.kbm) && !kbm_game->is_fh2 && settings.kbm.mode != KBM_MODE_PAD) {
        // Another build whose game-level input could not be hooked: the keys
        // still reach it through Azimuth's gamepad readings.
        log_line("KBM: %s: the game-level input hooks are not installed (see above); host-level keyboard "
                 "instead (keys merged into the gamepad readings; no mouse look, pointer, prompts or page).",
                 kbm_game->name);
        host_pad_install(&settings.kbm);
        return;
    }
    // The tile and page also come with Mode=PAD: they let the mode be changed back.
    const char *resource = NULL, *reset = NULL;
    const char *problem = settings_page_check(game, &resource, &reset);
    if (problem) {
        log_line("KBM MENU: %s; no KEYBOARD & MOUSE tile (KeyboardMouse.ini still sets every option).", problem);
        return;
    }
    if (kbm_menu_install(game) && settings_page_install(game))
        log_line("KBM MENU: XAML resource call 0x16ebb37 chained onto %s; Video page reset chained onto %s.", resource,
                 reset);
}

// Another game on Azimuth (Fast & Furious, ...): none of Forza Horizon 2's
// sites exist there, so only the host level is used: keys merged into
// Azimuth's gamepad readings (host_pad.c). No game byte is patched.
static void start_host_only(const FH2PluginContext *context) {
    IMAGE_NT_HEADERS64 nt;
    IMAGE_DOS_HEADER dos;
    unsigned char *game = (unsigned char *)context->game_image;
    uint32_t stamp = memory_read(game, &dos, sizeof(dos)) && memory_read(game + dos.e_lfanew, &nt, sizeof(nt))
                         ? nt.FileHeader.TimeDateStamp
                         : 0;
    log_line("KBM: the game is not Forza Horizon 2 1.0.0.64 or Fast & Furious 1.0.0.6 (SizeOfImage 0x%lx, "
             "TimeDateStamp 0x%08lx): host-level keyboard only (no menu pointer, page, prompts or game hooks).",
             (unsigned long)context->game_image_size, (unsigned long)stamp);
    settings_read(ini_path, &settings);
    if (!settings.kbm_valid) {
        log_line("KBM: REFUSED: %s; keyboard & mouse is not installed.", settings.kbm_problem);
        return;
    }
    if (!settings.kbm.enabled) {
        log_append("KBM: keyboard & mouse is off ([KeyboardMouse] Enabled=0).\r\n");
        return;
    }
    char description[96];
    unsigned char *host = azimuth_host_base();
    AzimuthBuild build = azimuth_host_build(host);
    azimuth_host_describe(host, description, sizeof(description));
    if (context->adapter != FH2_PLUGIN_AZIMUTH || build == AZIMUTH_UNKNOWN || !kbm_win32_check_host()) {
        log_line("KBM: the host is not a verified Azimuth 1.0.4 or 1.0.2 (%s); keyboard is not installed.",
                 description);
        return;
    }
    log_line("KBM: host Azimuth %s (%s).", azimuth_host_name(build), description);
    keep_loaded();
    host_pad_install(&settings.kbm);
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);
    return TRUE;
}

PLUGIN_EXPORT BOOL WINAPI FH2PluginInitialize(const FH2PluginContext *context) {
    if (!context || context->size < sizeof(FH2PluginContext) || context->api_version < 1) return FALSE;
    log_set_host(context->log_utf8);
    if (!context->plugin_path || !context->game_image || !make_ini_path(context->plugin_path)) {
        log_append("KBM: the loader gave no plugin path or game image; nothing is installed.\r\n");
        return FALSE;
    }
    kbm_set_ini_path(ini_path);
    log_line("KBM: Keyboard & Mouse " KBM_VERSION " for Extra Options %s.",
             context->extra_options_version ? context->extra_options_version : "?");
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS64 nt;
    unsigned char *image = (unsigned char *)context->game_image;
    uint32_t stamp = memory_read(image, &dos, sizeof(dos)) && memory_read(image + dos.e_lfanew, &nt, sizeof(nt))
                         ? nt.FileHeader.TimeDateStamp
                         : 0;
    const KbmGame *build = kbm_game_find((uint32_t)context->game_image_size, stamp);
    if (!build) {
        kbm_game_select(NULL);
        start_host_only(context);
        return TRUE;
    }
    kbm_game_select(build);
    if (!build->is_fh2)
        log_line("KBM: %s (SizeOfImage 0x%lx, TimeDateStamp 0x%08lx): Forza Horizon 2's game-level keyboard & mouse, "
                 "with the sites found again in this build; what is not found stays off and the log says why.",
                 build->name, (unsigned long)context->game_image_size, (unsigned long)stamp);
    settings_read(ini_path, &settings);
    start((unsigned char *)context->game_image, context->adapter);
    return TRUE;
}

#ifdef KBM_TEST
PLUGIN_EXPORT const KbmSettings *KbmTestActiveSettings(void) { return &settings.kbm; }
// The Azimuth image the plugin checks instead of the test process' own.
PLUGIN_EXPORT void KbmTestUseHost(unsigned char *base) { azimuth_host_set_test_base(base); }
#endif
