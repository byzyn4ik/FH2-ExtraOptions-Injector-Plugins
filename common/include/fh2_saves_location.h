// FH2 Extra Options plugins: the saves folder Azimuth uses for a title.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Header only, C and C++, no C runtime calls, nothing to link beyond KERNEL32.
// forza_injector's forza-injector.ini [Saves] Location=Documents redirects
// Azimuth's <exe folder>\data\titles\<title>\saves to
// Documents\My Games\<game>\Saves\<title> (forza-injector
// core/adapters/azimuth_saves.c; the same as its sdk/include/forza_saves.h).
// While it is on, the game-folder saves are stale: a plugin that reads saves
// (Photo Export, the achievements' save stats, ...) takes the folder from here.
#ifndef FH2_SAVES_LOCATION_H
#define FH2_SAVES_LOCATION_H
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stddef.h>

// The injector's winmm.dll export: the redirected folder of `title`, its
// length; 0 when that title's saves are in Azimuth's own folder.
typedef unsigned (*Fh2InjectorSavesFolderFn)(const wchar_t *title, wchar_t *output, unsigned capacity);

// forza_injector's winmm.dll (beside the process executable, exporting
// ForzaInjectorStatus); NULL under stock Extra Options or anything else.
static inline HMODULE fh2_injector_module(void) {
    wchar_t path[1024];
    DWORD count = GetModuleFileNameW(NULL, path, 1024 - 16);
    if (!count || count >= 1024 - 16) return NULL;
    while (count && path[count - 1] != L'\\' && path[count - 1] != L'/') --count;
    static const wchar_t name[] = L"winmm.dll";
    for (unsigned i = 0; i < sizeof(name) / sizeof(name[0]); ++i) path[count + i] = name[i];
    HMODULE module = GetModuleHandleW(path);
    return module && GetProcAddress(module, "ForzaInjectorStatus") ? module : NULL;
}

// The injector exports the saves-location query (0.2.1 with saves in Documents, or later).
static inline Fh2InjectorSavesFolderFn fh2_injector_saves_query(void) {
    HMODULE injector = fh2_injector_module();
    return injector ? (Fh2InjectorSavesFolderFn)(void *)GetProcAddress(injector, "ForzaInjectorSavesFolder") : NULL;
}

// The folder Azimuth's saves of `title` are redirected to, with its length; 0
// when they are not redirected (no injector, an older one, Location=Game).
static inline size_t fh2_redirected_saves(const wchar_t *title, wchar_t *output, size_t capacity) {
    Fh2InjectorSavesFolderFn query = fh2_injector_saves_query();
    if (!query || !output || !capacity || capacity > 0xffffffffu) return 0;
    return query(title, output, (unsigned)capacity);
}

// <folder of the process executable>\data\titles\<title>\saves; its length, 0 on failure.
static inline size_t fh2_game_title_saves(const wchar_t *title, wchar_t *output, size_t capacity) {
    DWORD count = capacity <= 0xffffffffu ? GetModuleFileNameW(NULL, output, (DWORD)capacity) : 0;
    if (!count || count >= capacity) return 0;
    while (count && output[count - 1] != L'\\' && output[count - 1] != L'/') --count;
    static const wchar_t data[] = L"data\\titles\\", saves[] = L"\\saves";
    size_t title_length = 0;
    while (title && title[title_length]) ++title_length;
    if (!count || !title_length || count + 12 + title_length + 6 + 1 > capacity) return 0;
    for (unsigned i = 0; i < 12; ++i) output[count++] = data[i];
    for (size_t i = 0; i < title_length; ++i) output[count++] = title[i];
    for (unsigned i = 0; i < 7; ++i) output[count + i] = saves[i];
    return count + 6;
}

// Azimuth's saves folder of `title` in use: the redirected one, else the game
// folder's. Its length, 0 on failure; *redirected (optional) tells which.
static inline size_t fh2_effective_title_saves(const wchar_t *title, wchar_t *output, size_t capacity,
                                               int *redirected) {
    size_t length = fh2_redirected_saves(title, output, capacity);
    if (redirected) *redirected = length != 0;
    return length ? length : fh2_game_title_saves(title, output, capacity);
}
#endif
