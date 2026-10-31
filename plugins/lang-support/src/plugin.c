// Language Support for Forza Horizon 2: an Extra Options mod-loader plugin.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Installed as mods\Language Support\plugins\LangSupport.dll beside Extra
// Options 0.4.4 on Azimuth 1.0.4. It ports the language support of the Extra
// Options fork (0.3.2-openbox: features/custom_language.c and
// features/voice_subtitles.c) to a stock Extra Options:
//
//   - the language pack is an ordinary mod folder (mods\<pack>\ with
//     language.ini and media\, built by tools/make_language_pack.py). Extra
//     Options mounts its media\ itself, on the game's first file lookup, so
//     the pack's string tables, fonts and subtitles are in place before the
//     game reads them; the plugin only finds the pack and reports it;
//   - voice subtitles: when the launcher runs the game in the pack's slot, the
//     pack's VoiceSubtitles.str is shown for spoken lines that have no
//     subtitles (voice_subtitles.c).
//
// From FH2PluginInitialize it checks the game image; with any mismatch it
// changes nothing and says why in FH2_ExtraOptions.log ("LANG SUPPORT: ...").
// Settings: LangSupport.ini beside the DLL.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "fh2_game.h"
#include "fh2_plugin.h"
#include "lang_support.h"
#include "ls_forza.h"

#define PLUGIN_EXPORT __declspec(dllexport)

static void (*host_log)(const char *message);

void say(const char *format, ...) {
    char text[700], line[760];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    snprintf(line, sizeof(line), "LANG SUPPORT: %s.\r\n", text);
    if (host_log) host_log(line);
}

static void narrow(const wchar_t *text, char *output, int capacity) {
    if (!WideCharToMultiByte(CP_UTF8, 0, text ? text : L"", -1, output, capacity, NULL, NULL)) output[0] = 0;
}

// `path` without its last `levels` components; 0 when it has too few.
static int parent(const wchar_t *path, unsigned levels, wchar_t *output, size_t capacity) {
    if (!path || wcslen(path) >= capacity) return 0;
    wcscpy(output, path);
    for (unsigned i = 0; i < levels; ++i) {
        wchar_t *slash = wcsrchr(output, L'\\'), *other = wcsrchr(output, L'/');
        if (other > slash) slash = other;
        if (!slash || slash == output) return 0;
        *slash = 0;
    }
    return 1;
}

typedef struct {
    wchar_t pack[260];
    int voice_subtitles;
} Settings;

static void read_settings(const wchar_t *plugin_path, Settings *settings) {
    wchar_t ini[LANG_PATH];
    settings->pack[0] = 0;
    settings->voice_subtitles = 1;
    if (!fh2_plugin_ini_path(plugin_path, ini, ARRAY_COUNT(ini))) return;
    GetPrivateProfileStringW(L"LangSupport", L"Pack", L"", settings->pack, ARRAY_COUNT(settings->pack), ini);
    // An inline "; comment" is not part of the value.
    wchar_t *comment = wcschr(settings->pack, L';');
    if (comment) *comment = 0;
    for (size_t n = wcslen(settings->pack); n && (settings->pack[n - 1] == L' ' || settings->pack[n - 1] == L'\t');)
        settings->pack[--n] = 0;
    settings->voice_subtitles = GetPrivateProfileIntW(L"LangSupport", L"VoiceSubtitles", 1, ini) != 0;
}

// The fork's FH2_ExtraOptions.ini [Language] Custom: a stock Extra Options
// refuses an INI with a key it does not know.
static void warn_fork_setting(const wchar_t *launcher) {
    wchar_t path[LANG_PATH], value[64];
    if (_snwprintf(path, ARRAY_COUNT(path), L"%ls\\FH2_ExtraOptions.ini", launcher) < 0 ||
        GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
        return;
    if (GetPrivateProfileStringW(L"Language", L"Custom", L"", value, ARRAY_COUNT(value), path))
        say("warning: FH2_ExtraOptions.ini has [Language] Custom, a key of the Extra Options fork; Extra Options "
            "0.4.4 refuses an INI with a key it does not know, so remove the line (the pack is now a mod folder)");
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);
    return TRUE;
}

// The game the plugin runs in: Forza Horizon 2 (Extra Options 0.4.4, or
// forza_injector with the Xtra Options runtime, which mounts the pack's media)
// or Fast & Furious (forza_injector; ff_media.c mounts it).
typedef struct {
    int ff;
    const char *id, *title, *name;
} Game;

static const Game fh2_game = {0, "fh2", "1A96545B", "Forza Horizon 2"};
static const Game ff_game = {1, "fh2ff", "5A9771A4", "Fast & Furious"};

// What ForzaPluginEarlyInitialize did on Fast & Furious, for the later report.
static const char *ff_media_problem = "the plugin was not loaded early (forza_injector calls "
                                      "ForzaPluginEarlyInitialize before the game's code runs)";

static BOOL initialize(const FH2PluginContext *context, const Game *game_info) {
    if (!context || context->size < sizeof(FH2PluginContext) || context->api_version < 1) return FALSE;
    host_log = context->log_utf8;
    if (!context->plugin_path || !context->game_image) {
        say("the loader gave no plugin path or game image; nothing is installed");
        return FALSE;
    }
    const char *eo = context->extra_options_version ? context->extra_options_version : "?";
    if (game_info->ff) {
        say("plugin " LANG_SUPPORT_VERSION " on Fast & Furious (forza_injector)");
    } else {
        say("plugin " LANG_SUPPORT_VERSION " for Extra Options %s", eo);
        if (strcmp(eo, EXTRA_OPTIONS_VERSION)) {
            say("Extra Options %s is not " EXTRA_OPTIONS_VERSION ", the version this plugin is built for; nothing is "
                "installed", eo);
            return TRUE;
        }
    }
    Settings settings;
    read_settings(context->plugin_path, &settings);

    // mods\<mod>\plugins\LangSupport.dll; mods\ is beside Azimuth.exe (Mezcal: in Mount).
    wchar_t mods[LANG_PATH], launcher[LANG_PATH];
    if (!parent(context->plugin_path, 3, mods, ARRAY_COUNT(mods)) ||
        !parent(context->plugin_path, 4, launcher, ARRAY_COUNT(launcher))) {
        say("the plugin is not in mods\\<mod>\\plugins\\; nothing is installed");
        return TRUE;
    }
    if (!game_info->ff) warn_fork_setting(launcher);

    LangPack pack;
    char name[600], language[200], slot[40], archive[16], text[100];
    const char *problem = pack_find(mods, settings.pack, game_info->id, game_info->title, &pack);
    if (problem) {
        narrow(settings.pack, name, sizeof(name));
        say("%s%s%s; nothing to do", settings.pack[0] ? name : "", settings.pack[0] ? ": " : "", problem);
        return TRUE;
    }
    narrow(pack.name, name, sizeof(name));
    narrow(pack.language, language, sizeof(language));
    narrow(pack.slot, slot, sizeof(slot));
    narrow(pack.archive, archive, sizeof(archive));
    say("language pack mods\\%s: %s in the %s slot (string tables %s)", name, language, slot, archive);
    int order = pack_load_order(mods, pack.name);
    if (!order) {
        say("mods\\loadorder.txt comments the pack out (# %s): %s does not mount its files and voice subtitles are "
            "off", name, game_info->ff ? "the media mount" : "Extra Options");
        return TRUE;
    }
    wchar_t media[LANG_PATH], stringtables[LANG_PATH];
    if (_snwprintf(media, ARRAY_COUNT(media), L"%ls\\media", pack.folder) < 0 ||
        _snwprintf(stringtables, ARRAY_COUNT(stringtables), L"%ls\\media\\stripped\\stringtables", pack.folder) < 0) {
        say("the pack's path is too long; nothing is installed");
        return TRUE;
    }
    DWORD attributes = GetFileAttributesW(media);
    int has_media = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
    if (!has_media) {
        say("warning: the pack has no media\\ folder; there are no files of it to mount");
    } else if (game_info->ff) {
        int mounted = 0;
        for (unsigned i = 0; !ff_media_problem && i < ff_media_count(); ++i)
            if (!_wcsicmp(ff_media_name(i), pack.name)) mounted = 1;
        if (mounted)
            say("the pack's media\\ is mounted over the game's files (the game uses them in the %s slot; files it "
                "opens from mods are logged as \"LANG SUPPORT: media: the game opens ...\")", slot);
        else
            say("warning: the pack's media\\ is not mounted (%s): the game shows its own %s text",
                ff_media_problem ? ff_media_problem : "[LangSupport] ModMedia=0 or not in the mount", slot);
    } else {
        say("Extra Options mounts the pack's media\\ as a mod%s (below mediaoverride and mediapc); the game uses "
            "its files in the %s slot", order < 0 ? " (not in loadorder.txt yet: it adds new folders itself)" : "",
            slot);
    }
    if (!settings.voice_subtitles) {
        say("[LangSupport] VoiceSubtitles=0; voice subtitles are off");
        return TRUE;
    }

    // The language the game runs in: the launcher's choice (Azimuth), the Windows locale (Mezcal).
    wchar_t running[LOCALE_NAME_MAX_LENGTH];
    if (context->adapter == FH2_PLUGIN_AZIMUTH) {
        const unsigned char *host = azimuth_host();
        AzimuthBuild build = azimuth_build(host);
        if (build == AZIMUTH_UNKNOWN) {
            say("the host is not Azimuth 1.0.4 or 1.0.2; voice subtitles are not installed");
            return TRUE;
        }
        wchar_t azimuth_ini[LANG_PATH];
        _snwprintf(azimuth_ini, ARRAY_COUNT(azimuth_ini), L"%ls\\data\\azimuth.ini", launcher);
        azimuth_ini[ARRAY_COUNT(azimuth_ini) - 1] = 0;
        int named = launcher_language(azimuth_ini, running, ARRAY_COUNT(running));
        narrow(running, text, sizeof(text));
        say("Azimuth %s runs the game in %s%s", azimuth_name(build), running[0] ? text : "an unknown language",
            named ? "" : " (the Windows locale: azimuth.ini names no language)");
    } else {
        if (!GetUserDefaultLocaleName(running, ARRAY_COUNT(running))) running[0] = 0;
        narrow(running, text, sizeof(text));
        say("Mezcal: the game runs in the Windows locale, %s", running[0] ? text : "unknown");
    }
    if (_wcsicmp(running, pack.slot)) {
        say("choose %s in the launcher to play in the pack's language; voice subtitles are off", slot);
        return TRUE;
    }
    unsigned char *game = (unsigned char *)context->game_image;
    problem = voice_check(game, context->game_image_size);
    if (problem) {
        say("%s; voice subtitles are not installed and the game is left as it is", problem);
        return TRUE;
    }
    const char *why = NULL;
    unsigned lines = voice_install(game, context->game_image_size, stringtables, pack.archive, &why);
    if (!lines) {
        say("%s; voice subtitles are not installed and the game is left as it is", why ? why : "?");
        return TRUE;
    }
    say("voice subtitles installed (%s): %u lines; speech hooks 0x%x, 0x%x, 0x%x, 0x%x and 0x%x jump to the hook "
        "code at 0x%llx", voice_game_name(), lines, voice_site(0), voice_site(1), voice_site(2), voice_site(3),
        voice_site(4), (unsigned long long)(uintptr_t)voice_cave());
    return TRUE;
}

PLUGIN_EXPORT BOOL WINAPI FH2PluginInitialize(const FH2PluginContext *context) {
    if (context && context->size >= sizeof(FH2PluginContext) &&
        ff_game_is((const unsigned char *)context->game_image, context->game_image_size))
        return initialize(context, &ff_game);
    return initialize(context, &fh2_game);
}

// forza_injector, once the game image is mapped and before its code runs: on
// Fast & Furious, the mods' media\ mount (ff_media.c), which must be in place
// before the game's first file lookup. Nothing on another game.
PLUGIN_EXPORT BOOL WINAPI ForzaPluginEarlyInitialize(const LsForzaContext *context) {
    if (!context || context->size < sizeof(LsForzaContext) || !context->v1) return FALSE;
    const FH2PluginContext *v1 = context->v1;
    if (!ff_game_is((const unsigned char *)v1->game_image, v1->game_image_size)) return TRUE;
    host_log = v1->log_utf8;
    wchar_t mods[LANG_PATH], ini[LANG_PATH];
    if (!v1->plugin_path || !parent(v1->plugin_path, 3, mods, ARRAY_COUNT(mods))) {
        ff_media_problem = "the plugin is not in mods\\<mod>\\plugins\\";
        say("media: %s; the mods' media\\ is not mounted", ff_media_problem);
        return TRUE;
    }
    if (fh2_plugin_ini_path(v1->plugin_path, ini, ARRAY_COUNT(ini)) &&
        !GetPrivateProfileIntW(L"LangSupport", L"ModMedia", 1, ini)) {
        ff_media_problem = "[LangSupport] ModMedia=0";
        say("media: [LangSupport] ModMedia=0; the mods' media\\ is not mounted");
        return TRUE;
    }
    unsigned count = ff_media_scan(mods);
    for (unsigned i = 0; i < count; ++i) {
        char name[600];
        narrow(ff_media_name(i), name, sizeof(name));
        say("media: mods\\%s\\media is served on Fast & Furious (load order %u of %u)", name, i + 1, count);
    }
    ff_media_problem = ff_media_install((unsigned char *)v1->game_image, v1->game_image_size, v1->log_utf8);
    if (ff_media_problem)
        say("media: %s; the mods' media\\ is not mounted and the game is left as it is", ff_media_problem);
    else
        say("media: the game's file resolver (CFileSys vtable slot 0x2eb7208) mounts them on its first lookup");
    return TRUE;
}

// forza_injector calls this in place of FH2PluginInitialize.
PLUGIN_EXPORT BOOL WINAPI ForzaPluginInitialize(const LsForzaContext *context) {
    if (!context || context->size < sizeof(LsForzaContext) || !context->v1) return FALSE;
    return FH2PluginInitialize(context->v1);
}

#ifdef LANG_SUPPORT_TEST
// Test build only (out\tests\LangSupportTest.dll): the same code with entry
// points for tests\run_tests.py and tests\combined_test.cpp.
PLUGIN_EXPORT void LangTestUseHost(const unsigned char *base) { azimuth_set_test_host(base); }
PLUGIN_EXPORT const wchar_t *LangTestSlotArchive(const wchar_t *slot) { return slot_archive(slot); }
PLUGIN_EXPORT int LangTestBuild(const unsigned char *base) { return (int)azimuth_build(base); }
PLUGIN_EXPORT const char *LangTestCheck(const unsigned char *game, uint32_t size) { return voice_check(game, size); }
PLUGIN_EXPORT void *LangTestCave(void) { return voice_cave(); }
PLUGIN_EXPORT int LangTestLoadOrder(const wchar_t *mods, const wchar_t *name) { return pack_load_order(mods, name); }

PLUGIN_EXPORT int LangTestLauncherLanguage(const wchar_t *ini, wchar_t *language, unsigned capacity) {
    return launcher_language(ini, language, capacity);
}

// The pack's folder name and slot ("name|slot"), or the problem; game: NULL (any), "fh2", "fh2ff".
PLUGIN_EXPORT const char *LangTestFindPack(const wchar_t *mods, const wchar_t *wanted, wchar_t *found,
                                           unsigned capacity, const char *game) {
    LangPack pack;
    const char *title = !game ? NULL : !strcmp(game, "fh2") ? "1A96545B" : !strcmp(game, "fh2ff") ? "5A9771A4" : "";
    const char *problem = pack_find(mods, wanted, game, title, &pack);
    found[0] = 0;
    if (!problem) _snwprintf(found, capacity, L"%ls|%ls|%ls", pack.name, pack.slot, pack.language);
    if (capacity) found[capacity - 1] = 0;
    return problem;
}

PLUGIN_EXPORT unsigned LangTestTable(const wchar_t *stringtables, const wchar_t *archive, const char **why) {
    return voice_test_table(stringtables, archive, why);
}

PLUGIN_EXPORT int LangTestEntry(unsigned index, unsigned long long *hash, const wchar_t **text, unsigned *duration) {
    return voice_test_entry(index, hash, text, duration);
}

// fh2_game.h's game filter and language search, as every plugin's captions use them.
PLUGIN_EXPORT int LangTestModForGame(const wchar_t *folder, const char *id, const char *title) {
    return fh2_mod_for_game(folder, id, title);
}
PLUGIN_EXPORT const char *LangTestRunningGame(const wchar_t *mods) {
    static char id[8], title[12], both[24];
    fh2_running_game(mods, id, title);
    _snprintf(both, sizeof(both), "%s|%s", id, title);
    both[sizeof(both) - 1] = 0;
    return both;
}
PLUGIN_EXPORT const char *LangTestPackLanguage(const wchar_t *mods, const char *game_code) {
    static char code[4];
    fh2_pack_language(mods, game_code, code);
    return code;
}

// ff_media.c
PLUGIN_EXPORT int LangTestFfGame(const unsigned char *game, uint32_t size) { return ff_game_is(game, size); }
PLUGIN_EXPORT const char *LangTestFfCheck(const unsigned char *game, uint32_t size) {
    return ff_media_check(game, size);
}
PLUGIN_EXPORT void LangTestFfReset(void) { ff_media_test_reset(); }
// The mods the mount serves, "a|b|c".
PLUGIN_EXPORT unsigned LangTestFfScan(const wchar_t *mods, wchar_t *names, unsigned capacity) {
    unsigned count = ff_media_scan(mods), used = 0;
    names[0] = 0;
    for (unsigned i = 0; i < count; ++i) {
        int n = _snwprintf(names + used, capacity - used, i ? L"|%ls" : L"%ls", ff_media_name(i));
        if (n < 0) break;
        used += (unsigned)n;
    }
    return count;
}
PLUGIN_EXPORT const char *LangTestFfInstall(unsigned char *game, uint32_t size, void (*log)(const char *)) {
    return ff_media_install(game, size, log);
}
PLUGIN_EXPORT void LangTestFfFunctions(void *allocator, void *volume_constructor, void *resolver,
                                       void *file_constructor, void *async_open) {
    ff_media_test_functions(allocator, volume_constructor, resolver, file_constructor, async_open);
}
PLUGIN_EXPORT void *LangTestFfVolume(unsigned index) { return ff_media_test_volume(index); }
PLUGIN_EXPORT void *LangTestFfCreateFile2(void) { return ff_media_test_create_file2(); }
#endif
