// Xtra Options: one Extra Options feature made of byte patches, as its own
// forza_injector plugin. The patches are Extra Options' own (core/patches.json
// of forza_injector, from Extra Options 0.4.4), generated into feature_data.h
// by build.py from features/<feature>/patches.json; feature.json names the
// INI key of each patch group. The same source builds every byte-patch DLL.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// forza_injector calls ForzaPluginEarlyInitialize when the game image is
// mapped and before its code runs; each enabled group is then applied all or
// nothing (forza_patch.h), every site checked against the bytes Extra Options
// accepts there. Under stock Extra Options these patches are its own, so the
// DLL only says so.
#include "forza_patch.h"
#include "forza_profile.h"

#include "feature_data.h"  // FEATURE_NAME, FEATURE_GAME, kGroups, FEATURE_HAS_CODE (generated)
#if FEATURE_HAS_CODE
#include "feature_code.h"  // the feature's own code: feature_prepare, feature_extra
#endif

#define MAX_GROUP_PATCHES 64

static void (*host_log)(const char *message);

static void say(const char *a, const char *b, const char *c) {
    char line[256];
    size_t n = 0;
    const char *parts[] = {"XTRA OPTIONS: " FEATURE_NAME ": ", a, b, c, "\r\n"};
    for (size_t p = 0; p < sizeof(parts) / sizeof(parts[0]); ++p)
        for (const char *s = parts[p]; s && *s && n + 1 < sizeof(line); ++s) line[n++] = *s;
    line[n] = 0;
    if (host_log) host_log(line);
}

// XtraOptions.ini beside this DLL.
static int ini_path(const wchar_t *plugin_path, wchar_t *out, size_t capacity) {
    static const wchar_t name[] = L"XtraOptions.ini";
    size_t length = 0, slash = 0;
    if (!plugin_path) return 0;
    while (plugin_path[length]) {
        if (plugin_path[length] == L'\\' || plugin_path[length] == L'/') slash = length + 1;
        ++length;
    }
    if (slash + sizeof(name) / sizeof(name[0]) > capacity) return 0;
    for (size_t i = 0; i < slash; ++i) out[i] = plugin_path[i];
    for (size_t i = 0; i < sizeof(name) / sizeof(name[0]); ++i) out[slash + i] = name[i];
    return 1;
}

static void decimal(uint32_t value, char *out) {
    char digits[12];
    int n = 0;
    do digits[n++] = (char)('0' + value % 10); while ((value /= 10) && n < 11);
    for (int i = 0; i < n; ++i) out[i] = digits[n - 1 - i];
    out[n] = 0;
}

__declspec(dllexport) BOOL WINAPI ForzaPluginEarlyInitialize(const ForzaContext *context) {
    if (!context || context->size < sizeof(ForzaContext) || !context->v1) return FALSE;
    host_log = context->v1->log_utf8;
    if (forza_extra_options_loaded()) {
        say("Extra Options' winmm.dll is the loader and applies this itself; nothing changed", "", "");
        return TRUE;
    }
    const ForzaGameInfo *game = forza_game_by_image(context->v1->game_image);
    if (!game || game->game != FEATURE_GAME) {
        say("not the supported game build (", forza_game_info(FEATURE_GAME)->id, "); nothing changed");
        return TRUE;
    }
    wchar_t ini[MAX_PATH * 2];
    if (!ini_path(context->v1->plugin_path, ini, sizeof(ini) / sizeof(ini[0]))) {
        say("no plugin path; nothing changed", "", "");
        return FALSE;
    }
#if FEATURE_HAS_CONTEXT
    feature_context(context, ini);
#endif
#if FEATURE_HAS_CODE
    feature_prepare((unsigned char *)context->v1->game_image);
#endif
    static ForzaPatch patches[MAX_GROUP_PATCHES];
    for (size_t g = 0; g < sizeof(kGroups) / sizeof(kGroups[0]); ++g) {
        const FeatureGroup *group = &kGroups[g];
        if (!GetPrivateProfileIntW(group->section, group->key, group->enabled_by_default, ini)) {
            say(group->name, ": off in XtraOptions.ini", "");
            continue;
        }
        // The group's byte patches, then what the feature's code adds: one set, all or nothing.
        uint32_t count = 0, extra_count = 0;
        const ForzaPatch *extra = 0;
#if FEATURE_HAS_CODE
        extra = feature_extra(group->name, &extra_count);
#endif
        if (group->count + extra_count > MAX_GROUP_PATCHES) {
            say(group->name, ": too many sites for one group; nothing changed", "");
            continue;
        }
        for (uint32_t i = 0; i < group->count; ++i) patches[count++] = group->patches[i];
        for (uint32_t i = 0; i < extra_count; ++i) patches[count++] = extra[i];
        uint32_t failed = 0;
        ForzaPatchResult result = forza_patch_apply((unsigned char *)context->v1->game_image,
                                                    context->v1->game_image_size, patches, count, &failed);
        if (result == FORZA_PATCH_OK) {
            char text[12];
            decimal(count, text);
            say(group->name, ": applied, sites: ", text);
        } else {
            say(group->name, ": ", forza_patch_result_text(result));
            say(group->name, ": first failing site ", patches[failed].name);
        }
    }
    return TRUE;
}

// Later initialization (forza_injector): the work was done early.
__declspec(dllexport) BOOL WINAPI ForzaPluginInitialize(const ForzaContext *context) {
    return context && context->size >= sizeof(ForzaContext);
}

// Stock Extra Options: it has these patches itself.
__declspec(dllexport) BOOL WINAPI FH2PluginInitialize(const FH2PluginContext *context) {
    if (!context || context->size < sizeof(FH2PluginContext)) return FALSE;
    host_log = context->log_utf8;
    HMODULE loader = GetModuleHandleW(L"winmm.dll");
    if (!loader || !GetProcAddress(loader, "ForzaInjectorStatus"))
        say("needs forza_injector; Extra Options applies this feature itself", "", "");
    return TRUE;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}
