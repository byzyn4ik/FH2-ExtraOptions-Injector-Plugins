// FH2 Extra Options plugins: the game's text language and the plugin's INI.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Header only, C and C++, no C runtime calls.
#ifndef FH2_GAME_H
#define FH2_GAME_H
#include "fh2_guard.h"

// The game's text table manager (forza_durango_release_final.exe, image size
// 0x3a18000); its language code is a std::string at +0x30. The same read
// Extra Options and its Weather Control plugin use.
#define FH2_TEXT_TABLES 0x3662990u

// The language code the game's text uses ("EN", "PL", "CHT", ...: 2 or 3
// letters, upper case) into code[4]; its length, or 0 (code empty) when it
// cannot be read.
static inline size_t fh2_game_language(const unsigned char *game, char code[4]) {
    void *manager = NULL;
    unsigned char text[32];
    size_t length, capacity;
    code[0] = 0;
    if (!game || !fh2_safe_read(game + FH2_TEXT_TABLES, &manager, sizeof(manager)) || !manager ||
        !fh2_safe_read((const unsigned char *)manager + 0x30, text, sizeof(text)))
        return 0;
    fh2_copy_bytes(&length, text + 16, sizeof(length));
    fh2_copy_bytes(&capacity, text + 24, sizeof(capacity));
    // An inline (short) std::string of 2 or 3 letters.
    if (capacity != 15 || length < 2 || length > 3 || text[length] != 0) return 0;
    for (size_t i = 0; i < length; ++i) {
        char c = (char)text[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c < 'A' || c > 'Z') {
            code[0] = 0;
            return 0;
        }
        code[i] = c;
    }
    code[length] = 0;
    return length;
}

// ---------------------------------------------------------------------------
// Language packs (the Language Support plugin): a pack is a mod folder in
// mods\ with a language.ini ([Language] Slot=ru-RU, an Azimuth language code)
// and a mod.json whose "id" names its language ("uk"). Extra Options mounts
// its media\ over the slot's string tables, so while the game runs in that
// slot its text is the pack's language, not the slot's.
//
// fh2_text_language: the language of the game's text as a code for the
// plugins' own captions: the pack's id in capitals ("UK") when a pack fills
// the game's slot, else the game's code (fh2_game_language). The pack is the
// one Language Support uses: LangSupport.ini [LangSupport] Pack= (a folder
// name in mods\) when set, else the one folder of mods\ with a language.ini
// (with several, the one whose Slot is the game's). It does not count when
// its Slot is another language, when loadorder.txt comments it out
// ("# Ukrainian"), or when its id is not 2 or 3 letters.

// Paths here are short (mods\<folder>\<file>); the helpers keep every frame
// well under a page (Custom Radio links no C runtime, so no __chkstk) and are
// not inlined into each other for that reason.
#define FH2_LANG_PATH 520
#if defined(__GNUC__) || defined(__clang__)
#define FH2_NOINLINE __attribute__((noinline))
#else
#define FH2_NOINLINE __declspec(noinline)
#endif

// Azimuth language code (language.ini Slot) -> the game's text code.
static inline const char *fh2_slot_code(const wchar_t *slot) {
    static const struct {
        const wchar_t *slot;
        const char *code;
    } slots[] = {{L"en-US", "EN"}, {L"en-GB", "GB"}, {L"de-DE", "DE"}, {L"fr-FR", "FR"}, {L"it-IT", "IT"},
                 {L"es-ES", "ES"}, {L"es-MX", "MX"}, {L"pt-BR", "BR"}, {L"pl-PL", "PL"}, {L"ru-RU", "RU"},
                 {L"ja-JP", "JP"}, {L"ko-KR", "KO"}, {L"zh-TW", "CHT"}, {L"zh-HK", "CHT"}, {L"nl-NL", "NL"},
                 {L"cs-CZ", "CZ"}, {L"hu-HU", "HU"}};
    for (size_t i = 0; slot && i < sizeof(slots) / sizeof(slots[0]); ++i)
        if (CompareStringOrdinal(slot, -1, slots[i].slot, -1, TRUE) == CSTR_EQUAL) return slots[i].code;
    return NULL;
}

static inline int fh2_codes_equal(const char *a, const char *b) {
    size_t i = 0;
    for (; a[i] && b[i]; ++i) {
        char x = a[i] >= 'a' && a[i] <= 'z' ? (char)(a[i] - 'a' + 'A') : a[i];
        char y = b[i] >= 'a' && b[i] <= 'z' ? (char)(b[i] - 'a' + 'A') : b[i];
        if (x != y) return 0;
    }
    return a[i] == b[i];
}

// output = a + "\" + b (+ "\" + c); 0 when it does not fit.
static inline int fh2_join_path(wchar_t *output, size_t capacity, const wchar_t *a, const wchar_t *b,
                                const wchar_t *c) {
    const wchar_t *parts[3];
    size_t length = 0;
    parts[0] = a;
    parts[1] = b;
    parts[2] = c;
    for (int p = 0; p < 3 && parts[p]; ++p) {
        if (p) {
            if (length + 1 >= capacity) return 0;
            output[length++] = L'\\';
        }
        for (const wchar_t *s = parts[p]; *s; ++s) {
            if (length + 1 >= capacity) return 0;
            output[length++] = *s;
        }
    }
    if (length >= capacity) return 0;
    output[length] = 0;
    return 1;
}

static inline int fh2_file_exists(const wchar_t *path) {
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

// A whole small file (at most capacity - 1 bytes), zero-terminated; its size or -1.
static inline long fh2_read_small_file(const wchar_t *path, char *buffer, size_t capacity) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                              OPEN_EXISTING, 0, NULL);
    LARGE_INTEGER size;
    DWORD got = 0;
    long result = -1;
    if (file == INVALID_HANDLE_VALUE) return -1;
    if (GetFileSizeEx(file, &size) && size.QuadPart >= 0 && (unsigned long long)size.QuadPart < capacity &&
        ReadFile(file, buffer, (DWORD)size.QuadPart, &got, NULL) && got == (DWORD)size.QuadPart) {
        buffer[got] = 0;
        result = (long)got;
    }
    CloseHandle(file);
    return result;
}

// 1 when mods\loadorder.txt comments the folder out ("# <name>"), else 0.
static FH2_NOINLINE int fh2_mod_disabled(const wchar_t *mods, const wchar_t *name) {
    wchar_t path[FH2_LANG_PATH];
    char wanted[780];
    int wanted_length, disabled = 0;
    char *text;
    long size;
    if (!fh2_join_path(path, FH2_LANG_PATH, mods, L"loadorder.txt", NULL)) return 0;
    wanted_length = WideCharToMultiByte(CP_UTF8, 0, name, -1, wanted, (int)sizeof(wanted), NULL, NULL) - 1;
    if (wanted_length <= 0) return 0;
    // Extra Options reads at most 64 KiB of load order.
    text = (char *)HeapAlloc(GetProcessHeap(), 0, 65537);
    if (!text) return 0;
    size = fh2_read_small_file(path, text, 65537);
    for (long at = 0; size > 0 && at < size;) {
        long start = at, end;
        while (at < size && text[at] != '\n') ++at;
        end = at++;
        while (end > start && (text[end - 1] == '\r' || text[end - 1] == ' ' || text[end - 1] == '\t')) --end;
        if (start == 0 && end - start >= 3 && (unsigned char)text[0] == 0xef && (unsigned char)text[1] == 0xbb &&
            (unsigned char)text[2] == 0xbf)
            start = 3;
        while (start < end && (text[start] == ' ' || text[start] == '\t')) ++start;
        int commented = start < end && text[start] == '#';
        if (commented) {
            ++start;
            while (start < end && (text[start] == ' ' || text[start] == '\t')) ++start;
        }
        if (end - start != wanted_length) continue;
        int same = 1;
        for (long i = 0; i < wanted_length && same; ++i) {
            char x = text[start + i], y = wanted[i];
            x = x >= 'A' && x <= 'Z' ? (char)(x - 'A' + 'a') : x;
            y = y >= 'A' && y <= 'Z' ? (char)(y - 'A' + 'a') : y;
            same = x == y;
        }
        if (same) {
            disabled = commented;
            break;
        }
    }
    HeapFree(GetProcessHeap(), 0, text);
    return disabled;
}

// The pack's language: mod.json's "id" ("uk", "uk-UA" -> "UK"); 2 or 3 letters.
static FH2_NOINLINE size_t fh2_pack_id(const wchar_t *folder, char code[4]) {
    wchar_t path[FH2_LANG_PATH];
    char *json;
    long size;
    size_t result = 0;
    code[0] = 0;
    if (!fh2_join_path(path, FH2_LANG_PATH, folder, L"mod.json", NULL)) return 0;
    json = (char *)HeapAlloc(GetProcessHeap(), 0, 8192);
    if (!json) return 0;
    size = fh2_read_small_file(path, json, 8192);
    for (long i = 0; size > 0 && i + 4 <= size; ++i) {
        if (json[i] != '"' || json[i + 1] != 'i' || json[i + 2] != 'd' || json[i + 3] != '"') continue;
        long at = i + 4;
        while (at < size && (json[at] == ' ' || json[at] == '\t' || json[at] == '\r' || json[at] == '\n')) ++at;
        if (at >= size || json[at] != ':') continue;
        ++at;
        while (at < size && (json[at] == ' ' || json[at] == '\t' || json[at] == '\r' || json[at] == '\n')) ++at;
        if (at >= size || json[at] != '"') break;
        ++at;
        size_t length = 0;
        for (; at < size && length < 4; ++at, ++length) {
            char c = json[at];
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            if (c < 'A' || c > 'Z') break;
            if (length < 3) code[length] = c;
        }
        // Letters, then the end of the string or a region ("-UA", "_UA").
        if (length < 2 || length > 3 || at >= size || (json[at] != '"' && json[at] != '-' && json[at] != '_')) {
            code[0] = 0;
            break;
        }
        code[length] = 0;
        result = length;
        break;
    }
    HeapFree(GetProcessHeap(), 0, json);
    return result;
}

// ---------------------------------------------------------------------------
// Which game a mod is for. forza_injector runs Forza Horizon 2 ("fh2", title
// 1A96545B) and Fast & Furious ("fh2ff", title 5A9771A4) from the same kind
// of install; a mod's mod.json "games" lists the game ids (or title ids) it
// is for ([] or no "games": every game) and "enabled": false turns it off.
// A language pack made for one game is not taken in the other.

static inline int fh2_json_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// The JSON string at json[at] ('"' first) equals `text` ignoring case; *end past it.
static inline int fh2_json_string_is(const char *json, long size, long at, const char *text, long *end) {
    long i = at + 1;
    size_t k = 0;
    int same = text != NULL;
    while (i < size && json[i] != '"') {
        char c = json[i];
        if (c == '\\' && i + 1 < size) {
            ++i;
            same = 0;  // ids hold no escapes
        }
        if (same) {
            char x = c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
            char y = text[k] >= 'A' && text[k] <= 'Z' ? (char)(text[k] - 'A' + 'a') : text[k];
            if (!text[k] || x != y)
                same = 0;
            else
                ++k;
        }
        ++i;
    }
    *end = i < size ? i + 1 : size;
    return same && !text[k];
}

// 1 when <folder>\mod.json lets the mod load in the game `id` (title
// `title`): no mod.json, no "games", "games": [] or one naming the game;
// "enabled": false gives 0. With id NULL or "" (the game is not known) only
// "enabled" counts.
static FH2_NOINLINE int fh2_mod_for_game(const wchar_t *folder, const char *id, const char *title) {
    wchar_t *path;
    char *json;
    long size, at, end;
    int allowed = 1;
    path = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, FH2_LANG_PATH * sizeof(wchar_t) + 65537);
    if (!path) return 1;
    json = (char *)(path + FH2_LANG_PATH);
    size = fh2_join_path(path, FH2_LANG_PATH, folder, L"mod.json", NULL) ? fh2_read_small_file(path, json, 65537) : -1;
    for (at = 0; size > 0 && at < size; ++at) {
        int games, enabled, listed = 0, named = 0;
        if (json[at] != '"') continue;
        games = fh2_json_string_is(json, size, at, "games", &end);
        enabled = !games && fh2_json_string_is(json, size, at, "enabled", &end);
        at = end;
        while (at < size && fh2_json_space(json[at])) ++at;
        if ((!games && !enabled) || at >= size || json[at] != ':') {
            --at;  // a string value or another key: go on after it
            continue;
        }
        ++at;
        while (at < size && fh2_json_space(json[at])) ++at;
        if (enabled) {
            if (at + 5 <= size && json[at] == 'f' && json[at + 1] == 'a' && json[at + 2] == 'l' &&
                json[at + 3] == 's' && json[at + 4] == 'e')
                allowed = 0;
            --at;
            continue;
        }
        if (at >= size || json[at] != '[') {
            --at;
            continue;
        }
        for (++at; at < size && json[at] != ']';) {
            if (json[at] != '"') {
                ++at;
                continue;
            }
            ++listed;
            if ((id && id[0] && fh2_json_string_is(json, size, at, id, &end)) ||
                (title && title[0] && fh2_json_string_is(json, size, at, title, &end)))
                named = 1;
            fh2_json_string_is(json, size, at, NULL, &end);
            at = end;
        }
        if (listed && !named && id && id[0]) allowed = 0;
    }
    HeapFree(GetProcessHeap(), 0, path);
    return allowed;
}

// The game Azimuth runs beside this mods folder: <mods>\..\data\azimuth.ini
// [launcher] game (its title id). id: "fh2", "fh2ff" or "" (not known);
// title: the 8 hex digits in capitals, or "".
static FH2_NOINLINE void fh2_running_game(const wchar_t *mods, char id[8], char title[12]) {
    wchar_t *ini, value[16];
    size_t length = 0, cut;
    id[0] = title[0] = 0;
    if (!mods) return;
    ini = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, FH2_LANG_PATH * sizeof(wchar_t));
    if (!ini) return;
    while (mods[length]) ++length;
    while (length && (mods[length - 1] == L'\\' || mods[length - 1] == L'/')) --length;
    cut = length;
    while (cut && mods[cut - 1] != L'\\' && mods[cut - 1] != L'/') --cut;
    if (cut > 1 && cut < FH2_LANG_PATH - 20) {
        fh2_copy_bytes(ini, mods, (cut - 1) * sizeof(wchar_t));
        ini[cut - 1] = 0;
        if (fh2_join_path(ini, FH2_LANG_PATH, ini, L"data\\azimuth.ini", NULL) && fh2_file_exists(ini) &&
            GetPrivateProfileStringW(L"launcher", L"game", L"", value, 16, ini) == 8) {
            for (int i = 0; i < 8; ++i) {
                wchar_t c = value[i];
                if (c >= L'a' && c <= L'f') c = (wchar_t)(c - L'a' + L'A');
                if (!((c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'F'))) {
                    title[0] = 0;
                    break;
                }
                title[i] = (char)c;
                title[i + 1] = 0;
            }
            if (title[0] && fh2_codes_equal(title, "1A96545B"))
                fh2_copy_bytes(id, "fh2", 4);
            else if (title[0] && fh2_codes_equal(title, "5A9771A4"))
                fh2_copy_bytes(id, "fh2ff", 6);
        }
    }
    HeapFree(GetProcessHeap(), 0, ini);
}

// The pack folder's language when it fills the game's slot (`game_code`), else 0.
// A pack whose mod.json "games" leaves the running game out (fh2_running_game) does not count.
static FH2_NOINLINE size_t fh2_pack_in_slot(const wchar_t *mods, const wchar_t *name, const char *game_code,
                                      char code[4]) {
    wchar_t folder[FH2_LANG_PATH], ini[FH2_LANG_PATH], slot[32];
    const char *slot_code;
    char game[8], title[12];
    code[0] = 0;
    if (!fh2_join_path(folder, FH2_LANG_PATH, mods, name, NULL) ||
        !fh2_join_path(ini, FH2_LANG_PATH, folder, L"language.ini", NULL) || !fh2_file_exists(ini))
        return 0;
    GetPrivateProfileStringW(L"Language", L"Slot", L"", slot, 32, ini);
    slot_code = fh2_slot_code(slot);
    if (!slot_code || !fh2_codes_equal(slot_code, game_code) || fh2_mod_disabled(mods, name)) return 0;
    fh2_running_game(mods, game, title);
    if (!fh2_mod_for_game(folder, game, title)) return 0;
    return fh2_pack_id(folder, code);
}

static inline size_t fh2_pack_language_length(const char code[4]) {
    size_t length = 0;
    while (length < 3 && code[length]) ++length;
    return length;
}

static FH2_NOINLINE size_t fh2_pack_search_body(const wchar_t *mods, char code[4], size_t length, wchar_t *settings,
                                                 wchar_t *pattern, wchar_t *ini, wchar_t *wanted, wchar_t *found,
                                                 char pack[4]);

// fh2_text_language for a game code and a mods folder (see above).
static FH2_NOINLINE size_t fh2_pack_language(const wchar_t *mods, const char *game_code, char code[4]) {
    wchar_t wanted[260], found[260], *settings, *pattern, *ini;
    size_t length = 0;
    code[0] = 0;
    while (game_code && game_code[length] && length < 3) {
        code[length] = game_code[length];
        ++length;
    }
    code[length] = 0;
    if (!length || !mods || !mods[0]) return length;
    char pack[4];
    // The three long paths on the heap: settings, pattern, ini.
    settings = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, 3 * FH2_LANG_PATH * sizeof(wchar_t));
    if (!settings) return length;
    pattern = settings + FH2_LANG_PATH;
    ini = pattern + FH2_LANG_PATH;
    size_t result = fh2_pack_search_body(mods, code, length, settings, pattern, ini, wanted, found, pack);
    HeapFree(GetProcessHeap(), 0, settings);
    return result;
}

// fh2_pack_language's search, with its buffers.
static FH2_NOINLINE size_t fh2_pack_search_body(const wchar_t *mods, char code[4], size_t length, wchar_t *settings,
                                                 wchar_t *pattern, wchar_t *ini, wchar_t *wanted, wchar_t *found,
                                                 char pack[4]) {
    WIN32_FIND_DATAW entry;
    HANDLE search;
    unsigned matching = 0;
    char running[8] = {0}, title[12] = {0};
    int known = 0;
    if (fh2_join_path(settings, FH2_LANG_PATH, mods, L"Language Support\\plugins", L"LangSupport.ini") &&
        fh2_file_exists(settings)) {
        GetPrivateProfileStringW(L"LangSupport", L"Pack", L"", wanted, 260, settings);
        size_t end = 0;
        while (wanted[end]) ++end;
        while (end && (wanted[end - 1] == L' ' || wanted[end - 1] == L'\t')) wanted[--end] = 0;
        if (wanted[0]) {
            // As Language Support: a folder name, nothing else.
            for (size_t i = 0; wanted[i]; ++i)
                if (wanted[i] == L'\\' || wanted[i] == L'/' || wanted[i] == L':') return length;
            if ((wanted[0] == L'.' && !wanted[1]) || (wanted[0] == L'.' && wanted[1] == L'.' && !wanted[2]))
                return length;
            if (!fh2_pack_in_slot(mods, wanted, code, pack)) return length;
            fh2_copy_bytes(code, pack, 4);
            return fh2_pack_language_length(code);
        }
    }
    if (!fh2_join_path(pattern, FH2_LANG_PATH, mods, L"*", NULL)) return length;
    search = FindFirstFileW(pattern, &entry);
    if (search == INVALID_HANDLE_VALUE) return length;
    found[0] = 0;
    do {
        if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || entry.cFileName[0] == L'.')
            continue;  // as Extra Options: no dot folders, no directory links
        if (!fh2_join_path(ini, FH2_LANG_PATH, mods, entry.cFileName, L"language.ini") || !fh2_file_exists(ini))
            continue;
        wchar_t slot[32];
        const char *slot_code;
        GetPrivateProfileStringW(L"Language", L"Slot", L"", slot, 32, ini);
        slot_code = fh2_slot_code(slot);
        if (!slot_code || !fh2_codes_equal(slot_code, code)) continue;
        // A pack for the other game is not counted.
        if (!known) {
            fh2_running_game(mods, running, title);
            known = 1;
        }
        if (!fh2_join_path(ini, FH2_LANG_PATH, mods, entry.cFileName, NULL) || !fh2_mod_for_game(ini, running, title))
            continue;
        if (!matching++)
            fh2_join_path(found, 260, entry.cFileName, NULL, NULL);
    } while (FindNextFileW(search, &entry));
    FindClose(search);
    // One pack (Language Support's rule), or several with one in this slot.
    if (matching != 1 || !found[0] || !fh2_pack_in_slot(mods, found, code, pack)) return length;
    fh2_copy_bytes(code, pack, 4);
    return fh2_pack_language_length(code);
}

// The mods folder of the module this code is in (<mods>\<Mod>\plugins\X.dll).
static FH2_NOINLINE int fh2_own_mods_folder(wchar_t *mods, size_t capacity) {
    HMODULE module = NULL;
    DWORD length;
    int cut = 0;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)(const void *)&fh2_own_mods_folder, &module))
        return 0;
    length = GetModuleFileNameW(module, mods, (DWORD)capacity);
    if (!length || length >= capacity) return 0;
    while (length && cut < 3) {
        if (mods[--length] == L'\\' || mods[length] == L'/') {
            mods[length] = 0;
            ++cut;
        }
    }
    return cut == 3 && length;
}

// The language of the game's text for the plugins' captions (see above):
// "UK" for a pack with id "uk" in the game's slot, else the game's code. Its
// length, or 0 (code empty) when the game's code cannot be read. The pack is
// looked up once per game code.
static inline size_t fh2_text_language(const unsigned char *game, char code[4]) {
    static char cached_game[4], cached_code[4];
    static volatile LONG cached;
    char game_code[4];
    wchar_t mods[FH2_LANG_PATH];
    if (!fh2_game_language(game, game_code)) {
        code[0] = 0;
        return 0;
    }
    if (cached && fh2_codes_equal(cached_game, game_code)) {
        fh2_copy_bytes(code, cached_code, 4);
        return fh2_pack_language_length(code);
    }
    if (!fh2_own_mods_folder(mods, FH2_LANG_PATH)) mods[0] = 0;
    fh2_pack_language(mods, game_code, code);
    InterlockedExchange(&cached, 0);
    fh2_copy_bytes(cached_game, game_code, 4);
    fh2_copy_bytes(cached_code, code, 4);
    InterlockedExchange(&cached, 1);
    return fh2_pack_language_length(code);
}

// The plugin's INI: its DLL path with the extension replaced by ".ini"
// (".ini" appended when the file name has none). 0 when it does not fit.
static inline int fh2_plugin_ini_path(const wchar_t *plugin_path, wchar_t *output, size_t capacity) {
    size_t length = 0, end;
    if (!plugin_path || !output || !capacity) return 0;
    while (plugin_path[length]) ++length;
    end = length;
    for (size_t i = length; i > 0; --i) {
        if (plugin_path[i - 1] == L'\\' || plugin_path[i - 1] == L'/') break;
        if (plugin_path[i - 1] == L'.') {
            end = i - 1;
            break;
        }
    }
    if (end + 5 > capacity) return 0;
    fh2_copy_bytes(output, plugin_path, end * sizeof(wchar_t));
    fh2_copy_bytes(output + end, L".ini", 5 * sizeof(wchar_t));
    return 1;
}

#endif
