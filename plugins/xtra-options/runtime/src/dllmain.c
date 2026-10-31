// Extra Options runtime provider for forza_injector.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "forza_runtime.h"
#include "forza_profile.h"
#include "runtime.h"
#include "settings.h"
#include "patch_engine.h"
#include "features/forza.h"
#include "features/addons.h"
#include "support.h"

static volatile LONG provider_state; // 0 unused, 1 starting, 2 ready, -1 failed
static wchar_t config_path[PATH_CAPACITY];

static int choose_settings(const ForzaRuntimeContext *context) {
    // Keep existing installations' options and binary menu state where they
    // are. New installations use the mod's INI; state/Gamerpic stay beside the
    // launcher. No settings or save file is moved or overwritten here.
    if (!path_join(config_path, context->settings_directory, MOD_INI_NAME)) return 0;
    if (GetFileAttributesW(config_path) != INVALID_FILE_ATTRIBUTES)
        return settings_select_file(config_path);
    const wchar_t *plugin = context->plugin->v1->plugin_path;
    if (!plugin) return 0;
    size_t length = (size_t)lstrlenW(plugin), slash = length;
    while (slash && plugin[slash - 1] != 0x5c && plugin[slash - 1] != L'/') --slash;
    static const wchar_t name[] = L"XtraRuntime.ini";
    if (!slash || slash + ARRAY_COUNT(name) > ARRAY_COUNT(config_path)) return 0;
    memory_copy(config_path, plugin, slash * sizeof(wchar_t));
    memory_copy(config_path + slash, name, sizeof(name));
    return settings_select_file(config_path);
}

__declspec(dllexport) int WINAPI ForzaGameRuntimeInitialize(const ForzaRuntimeContext *context) {
    if (!context || context->size < sizeof(*context) || context->api_version != FORZA_RUNTIME_API_VERSION ||
        !context->plugin || context->plugin->size < sizeof(ForzaContext) || !context->plugin->v1 ||
        context->plugin->v1->size < sizeof(FH2PluginContext) || !context->game_executable ||
        !context->settings_directory) return 0;
    if (context->plugin->game != FORZA_GAME_FH2) return 0;
    const ForzaGameInfo *game = forza_game_by_image(context->plugin->v1->game_image);
    if (!game || game->game != FORZA_GAME_FH2) return 0;
    LONG previous = InterlockedCompareExchange(&provider_state, 1, 0);
    if (previous) return previous == 2 ? 1 : -1;
    forza_set_injector_version(context->plugin->injector_version);
    addons_set_launcher_directory(context->settings_directory);
    int ready = choose_settings(context) && verify_game_file(context->game_executable) &&
                runtime_initialize(context->plugin->v1->game_image, context->game_executable,
                                   context->settings_directory);
    InterlockedExchange(&provider_state, ready ? 2 : -1);
    if (context->plugin->v1->log_utf8) {
        context->plugin->v1->log_utf8(ready ? "XTRA RUNTIME: FH2 runtime initialized; native hooks and deferred plugin lifecycle owned by this provider.\r\n"
                                          : "XTRA RUNTIME: claimed FH2 but initialization failed; no fallback provider is allowed.\r\n");
        if (!ready) context->plugin->v1->log_utf8(status_get());
    }
    return ready ? 1 : -1;
}

__declspec(dllexport) const char *XtraRuntimeStatus(void) { return status_get(); }

// Stock Extra Options already implements these features. Its v1 loader and
// this provider's own legacy loader can see the DLL without applying it twice.
__declspec(dllexport) BOOL WINAPI FH2PluginInitialize(const FH2PluginContext *context) {
    return context && context->size >= sizeof(*context);
}
__declspec(dllexport) BOOL WINAPI ForzaPluginInitialize(const ForzaContext *context) {
    return context && context->size >= sizeof(*context) && context->v1;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}
