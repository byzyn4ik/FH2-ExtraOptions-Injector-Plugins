// Language Support: mod media on Forza Horizon 2 Presents Fast & Furious.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// On Forza Horizon 2 the Xtra Options runtime (Extra Options' former core)
// serves every mod's media\ folder: it hooks the game's file resolver
// (CFileSys, vtable slot +0x210) and, on the game's first file lookup, mounts
// one native folder volume (IOSys::CFileVolume) per mod at game:\media\,
// whose file methods it replaces so they read the mod's folder
// (plugins/xtra-options/runtime/src/features/media_overrides.c). The game
// resolves a member of one of its archives (media\stripped\stringtables\RU.zip
// member Main.str, media\UI.zip member Fonts\verdana.vfont) through the same
// volumes, so a loose media\stripped\stringtables\RU\Main.str in a mod
// replaces it.
//
// Fast & Furious runs on forza_injector without that runtime, and nothing
// else serves media there: neither the game nor Azimuth reads
// <game>\Mount\mediaoverride (the game has no such path; Extra Options mounted
// it on Forza Horizon 2). This file ports the runtime's mount to Fast &
// Furious' executable (tools/port_ff_sites.py: every function and vtable is
// the Forza Horizon 2 one, found again and compared instruction for
// instruction), for every mod folder forza_injector loads in Fast & Furious
// (mods\loadorder.txt order, mod.json "enabled" and "games") that has a
// media\ folder. Later mods win, as on Forza Horizon 2; mediaoverride and
// mediapc are not mounted (they never were on Fast & Furious).
//
// Installed from ForzaPluginEarlyInitialize, before the game's code runs: the
// resolver slot is swapped (compare-exchange) once every site is as inspected.
// Files the game opens from a mod are named in forza-injector.log
// ("LANG SUPPORT: media: ...").
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "fh2_game.h"
#include "lang_support.h"

// Fast & Furious (forza_durango_release_final.exe, TimeDateStamp 0x55b62fde,
// SizeOfImage 0x3a89000); Forza Horizon 2's RVA in each comment.
#define FF_RESOLVE_SLOT 0x2eb7208u       // CFileSys vtable 0x2eb6ff8 + 0x210 (FH2 0x2e6ef28)
#define FF_FILESYS_VTABLE 0x2eb6ff8u     // .?AVCFileSys@@ (FH2 0x2e6ed18)
#define FF_RESOLVE 0x121a7a0u            // CFileSys::Resolve (FH2 0x11f49b0)
#define FF_ALLOCATE 0x3286d0u            // the game's operator new (FH2 0x3229c0)
#define FF_CONSTRUCT_VOLUME 0x1206420u   // CFileVolume::CFileVolume (FH2 0x11e05a0)
#define FF_FILE_MODE 0x1207110u          // open flags -> CreateFile mode (FH2 0x11e1290)
#define FF_CONSTRUCT_FILE 0x1206320u     // CFileStream::CFileStream (FH2 0x11e04a0)
#define FF_VOLUME_VTABLE 0x2ef3c38u      // .?AVCFileVolume@IOSys@@ (FH2 0x2eab918)
#define FF_STREAM_VTABLE 0x2ef3660u      // .?AVCFileStream@IOSys@@ (FH2 0x2eab340)
#define FF_CREATEFILE2_IAT 0x1df8150u    // kernelx.dll!CreateFile2 (FH2 0x1dd1148)
#define VOLUME_SLOTS (1 + 0x1f0 / 8)     // the RTTI locator, then the methods up to +0x1e8
#define MAX_MOUNTS 64
#define SERVED_LOG_LIMIT 400

typedef struct {
    const char *data;
    uintptr_t reserved;
    size_t length, capacity;
} GameString;

typedef unsigned char (*ResolveFile)(void *, void **, void *, const void *, unsigned char, unsigned char,
                                     unsigned char, void **, unsigned char);
typedef void *(*Allocate)(size_t);
typedef void *(*ConstructVolume)(void *, const void *, unsigned char);
typedef void *(*ConstructFile)(void *, HANDLE, void **, unsigned);
typedef void *(*FileMode)(void *, unsigned);
typedef void (*Reference)(void *);
typedef unsigned char (*MountVolume)(void *, const void *, void **, unsigned, unsigned char, unsigned char,
                                     const void *);
typedef void (*SortVolumes)(void *);
typedef unsigned char (*FileExists)(void *, const GameString *);
typedef DWORD (*FileAttributes)(void *, const GameString *);
typedef unsigned (*FileHash)(void *, const GameString *);
typedef void (*SetFilename)(void *, const GameString *);
typedef void **(*OpenNativeFile)(void *, void **, const GameString *, unsigned, unsigned char, unsigned char,
                                 void *);
typedef void **(*OpenAsyncFile)(void *, void **, const GameString *, unsigned, void *, unsigned, unsigned,
                                unsigned char);
typedef HANDLE(WINAPI *CreateFile2Fn)(LPCWSTR, DWORD, DWORD, DWORD, void *);

// ------------------------------------------------------------------ guards

// The first bytes of every function the mount calls or replaces (none of them
// holds a relocated field), as in Fast & Furious and, at its own RVA, in Forza
// Horizon 2.
static const struct {
    uint32_t rva;
    unsigned char size, bytes[16];
    const char *name;
} guards[] = {
    {FF_RESOLVE, 16, {0x48, 0x8b, 0xc4, 0x4c, 0x89, 0x48, 0x20, 0x48, 0x89, 0x50, 0x10, 0x48, 0x89, 0x48, 0x08, 0x55},
     "CFileSys::Resolve 0x121a7a0"},
    {FF_ALLOCATE, 15, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x8b, 0x15, 0x10, 0xfd, 0x28},
     "the game's allocator 0x3286d0"},
    {FF_CONSTRUCT_VOLUME, 15, {0x48, 0x89, 0x4c, 0x24, 0x08, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xec, 0x30, 0x48, 0xc7},
     "CFileVolume's constructor 0x1206420"},
    {FF_FILE_MODE, 16, {0x8b, 0xc2, 0x4c, 0x8b, 0xc1, 0xc6, 0x41, 0x10, 0x01, 0x83, 0xe0, 0x07, 0x32, 0xc9, 0x45, 0x33},
     "the open mode function 0x1207110"},
    {FF_CONSTRUCT_FILE, 10, {0x4c, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89, 0x4c, 0x24, 0x08},
     "CFileStream's constructor 0x1206320"},
};

// Run-time type of the object a vtable at `rva` belongs to (its locator at -8).
static int vtable_type(const unsigned char *game, uint32_t rva, const char *name) {
    uintptr_t locator = 0;
    uint32_t type = 0;
    char text[40];
    size_t length = strlen(name);
    if (length + 1 > sizeof(text) || !fh2_safe_read(game + rva - 8, &locator, 8) || !locator ||
        !fh2_safe_read((const void *)(locator + 12), &type, 4) || !type ||
        !fh2_safe_read(game + type + 0x10, text, length + 1))
        return 0;
    return !memcmp(text, name, length + 1);
}

static uintptr_t slot_value(const unsigned char *game, uint32_t rva) {
    uintptr_t value = 0;
    return fh2_safe_read(game + rva, &value, sizeof(value)) ? value : 0;
}

static uint32_t pe_value(const unsigned char *game, unsigned offset) {
    uint32_t value = 0, header = 0;
    if (!fh2_safe_read(game + 0x3c, &header, 4) || header > 0x1000 || !fh2_safe_read(game + header + offset, &value, 4))
        return 0;
    return value;
}

int ff_game_is(const unsigned char *game, uint32_t size) {
    return game && size == FF_IMAGE_SIZE && pe_value(game, 8) == FF_TIMESTAMP;
}

const char *ff_media_check(const unsigned char *game, uint32_t size) {
    static char why[200];
    if (!game) return "no game image";
    if (size != FF_IMAGE_SIZE) return "the game image is not Fast & Furious' forza_durango_release_final.exe (image size)";
    if (pe_value(game, 8) != FF_TIMESTAMP)
        return "the game image is not Fast & Furious' forza_durango_release_final.exe (TimeDateStamp)";
    for (unsigned i = 0; i < ARRAY_COUNT(guards); ++i) {
        if (!fh2_bytes_equal(game + guards[i].rva, guards[i].bytes, guards[i].size)) {
            snprintf(why, sizeof(why), "%s is not as inspected", guards[i].name);
            return why;
        }
    }
    if (!vtable_type(game, FF_FILESYS_VTABLE, ".?AVCFileSys@@")) return "CFileSys' vtable 0x2eb6ff8 is not as inspected";
    if (!vtable_type(game, FF_VOLUME_VTABLE, ".?AVCFileVolume@IOSys@@"))
        return "CFileVolume's vtable 0x2ef3c38 is not as inspected";
    if (!vtable_type(game, FF_STREAM_VTABLE, ".?AVCFileStream@IOSys@@"))
        return "CFileStream's vtable 0x2ef3660 is not as inspected";
    uintptr_t resolve = slot_value(game, FF_RESOLVE_SLOT);
    if (resolve != (uintptr_t)(game + FF_RESOLVE))
        return resolve ? "the file resolver's vtable slot 0x2eb7208 holds another function (another module hooks it)"
                       : "the file resolver's vtable slot 0x2eb7208 cannot be read";
    if (slot_value(game, FF_FILESYS_VTABLE + 0x1e8) != (uintptr_t)(game + 0x121e030u) ||
        slot_value(game, FF_FILESYS_VTABLE + 0x1f8) != (uintptr_t)(game + 0x12297d0u))
        return "CFileSys' mount and sort methods (vtable +0x1e8, +0x1f8) are not as inspected";
    return NULL;
}

// --------------------------------------------------------------- the mods

typedef struct {
    wchar_t name[260];
    wchar_t media[LANG_PATH];  // <mods>\<name>\media
    void *volume;
} MediaMod;

static MediaMod mounts[MAX_MOUNTS];
static unsigned mount_count;
static int mods_scanned;

static int is_directory(const wchar_t *path) {
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static int safe_folder(const wchar_t *name) {
    if (!name[0] || name[0] == L'.') return 0;
    for (const wchar_t *p = name; *p; ++p)
        if (*p < 32 || *p == L'/' || *p == L'\\' || *p == L':' || *p == L'*' || *p == L'?') return 0;
    return 1;
}

typedef struct {
    wchar_t name[260];
    int listed, disabled;
} Folder;

// forza_injector's order: mods\loadorder.txt's folders as listed ("# name":
// off), then the others alphabetically.
static unsigned mod_folders(const wchar_t *mods, Folder *folders, unsigned capacity) {
    wchar_t pattern[LANG_PATH];
    unsigned count = 0;
    WIN32_FIND_DATAW entry;
    if (_snwprintf(pattern, ARRAY_COUNT(pattern), L"%ls\\*", mods) < 0) return 0;
    pattern[ARRAY_COUNT(pattern) - 1] = 0;
    HANDLE search = FindFirstFileW(pattern, &entry);
    if (search == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || !safe_folder(entry.cFileName) ||
            count >= capacity || wcslen(entry.cFileName) >= 260)
            continue;
        unsigned at = count++;
        while (at && lstrcmpiW(folders[at - 1].name, entry.cFileName) > 0) {
            folders[at] = folders[at - 1];
            --at;
        }
        wcscpy(folders[at].name, entry.cFileName);
        folders[at].listed = folders[at].disabled = 0;
    } while (FindNextFileW(search, &entry));
    FindClose(search);

    wchar_t path[LANG_PATH];
    char *text = (char *)HeapAlloc(GetProcessHeap(), 0, 65537);
    long size = text && _snwprintf(path, ARRAY_COUNT(path), L"%ls\\loadorder.txt", mods) > 0
                    ? fh2_read_small_file(path, text, 65537)
                    : -1;
    if (size > 0) {
        Folder *sorted = (Folder *)HeapAlloc(GetProcessHeap(), 0, sizeof(Folder) * capacity);
        unsigned placed = 0;
        char *cursor = text;
        if ((unsigned char)cursor[0] == 0xef && (unsigned char)cursor[1] == 0xbb && (unsigned char)cursor[2] == 0xbf)
            cursor += 3;
        while (sorted && *cursor) {
            char *start = cursor;
            while (*cursor && *cursor != '\r' && *cursor != '\n') ++cursor;
            if (*cursor) *cursor++ = 0;
            while (*start == ' ' || *start == '\t') ++start;
            size_t n = strlen(start);
            while (n && (start[n - 1] == ' ' || start[n - 1] == '\t')) start[--n] = 0;
            int disabled = *start == '#';
            if (disabled) {
                ++start;
                while (*start == ' ' || *start == '\t') ++start;
            }
            wchar_t name[260];
            if (!*start || !MultiByteToWideChar(CP_UTF8, 0, start, -1, name, 260)) continue;
            for (unsigned i = 0; i < count; ++i) {
                if (!folders[i].listed && !lstrcmpiW(folders[i].name, name)) {
                    folders[i].listed = 1;
                    folders[i].disabled = disabled;
                    sorted[placed++] = folders[i];
                    break;
                }
            }
        }
        if (sorted) {
            for (unsigned i = 0; i < count; ++i)
                if (!folders[i].listed) sorted[placed++] = folders[i];
            memcpy(folders, sorted, sizeof(Folder) * count);
            HeapFree(GetProcessHeap(), 0, sorted);
        }
    }
    if (text) HeapFree(GetProcessHeap(), 0, text);
    return count;
}

unsigned ff_media_scan(const wchar_t *mods) {
    Folder *folders = (Folder *)HeapAlloc(GetProcessHeap(), 0, sizeof(Folder) * 256);
    mount_count = 0;
    mods_scanned = 1;
    if (!folders || !mods) {
        if (folders) HeapFree(GetProcessHeap(), 0, folders);
        return 0;
    }
    unsigned count = mod_folders(mods, folders, 256);
    for (unsigned i = 0; i < count && mount_count < MAX_MOUNTS; ++i) {
        wchar_t folder[LANG_PATH];
        if (folders[i].disabled || _snwprintf(folder, ARRAY_COUNT(folder), L"%ls\\%ls", mods, folders[i].name) < 0)
            continue;
        folder[ARRAY_COUNT(folder) - 1] = 0;
        if (!fh2_mod_for_game(folder, "fh2ff", "5A9771A4")) continue;
        MediaMod *mod = &mounts[mount_count];
        if (_snwprintf(mod->media, ARRAY_COUNT(mod->media), L"%ls\\media", folder) < 0) continue;
        mod->media[ARRAY_COUNT(mod->media) - 1] = 0;
        if (!is_directory(mod->media)) continue;
        wcscpy(mod->name, folders[i].name);
        mod->volume = NULL;
        ++mount_count;
    }
    HeapFree(GetProcessHeap(), 0, folders);
    return mount_count;
}

unsigned ff_media_count(void) { return mount_count; }

const wchar_t *ff_media_name(unsigned index) { return index < mount_count ? mounts[index].name : NULL; }

// ------------------------------------------------------------ the volumes

static unsigned char *image;
static ResolveFile original_resolve;
static Allocate allocate;
static ConstructVolume construct_volume;
static ConstructFile construct_file;
static FileMode file_mode;
static FileExists original_exists;
static FileAttributes original_attributes;
static FileHash original_hash;
static OpenNativeFile original_open;
static OpenAsyncFile original_async_open;
static void *volume_vtable[VOLUME_SLOTS];
static volatile LONG mount_state, mount_thread, served_lines;
static SRWLOCK mount_lock = SRWLOCK_INIT;
static void *volatile media_filesystem;
static void (*media_log)(const char *message);

static void media_say(const char *format, ...) {
    char text[900], line[960];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    snprintf(line, sizeof(line), "LANG SUPPORT: media: %s.\r\n", text);
    if (media_log) media_log(line);
}

static GameString borrowed(const char *text) { return (GameString){text, 0, strlen(text), 31}; }

static const char *string_text(const GameString *path) {
    return path->capacity < 16 ? (const char *)path : path->data;
}

static int mod_of(void *volume) {
    for (unsigned i = 0; volume && i < mount_count; ++i)
        if (mounts[i].volume == volume) return (int)i;
    return -1;
}

static int same_path(const char *text, size_t length, const char *expected) {
    for (size_t i = 0; i < length; ++i) {
        char a = text[i], b = expected[i];
        if (!b) return 0;
        if (a == '/') a = '\\';
        if (a >= 'A' && a <= 'Z') a = (char)(a + 'a' - 'A');
        if (b >= 'A' && b <= 'Z') b = (char)(b + 'a' - 'A');
        if (a != b) return 0;
    }
    return !expected[length];
}

// A relative path inside the mod's media\ (no "..", drive, wildcard or
// trailing dot); the game's own manifest files are never taken from a mod.
static int safe_relative(const char *text, size_t length) {
    if (!length || length > 32767) return 0;
    size_t component = 0;
    for (size_t i = 0; i <= length; ++i) {
        unsigned char ch = i < length ? (unsigned char)text[i] : 0;
        if (i == length || ch == '\\' || ch == '/') {
            if (i == component || (i - component == 1 && text[component] == '.') ||
                (i - component == 2 && text[component] == '.' && text[component + 1] == '.') ||
                text[i - 1] == '.' || text[i - 1] == ' ')
                return 0;
            component = i + 1;
        } else if (ch < 32 || ch == ':' || ch == '<' || ch == '>' || ch == '*' || ch == '?' || ch == '"' ||
                   ch == '|') {
            return 0;
        }
    }
    return !same_path(text, length, "stripped\\gamedbRC.slt") && !same_path(text, length, "zipmanifest.xml");
}

// <mod>\media\<path> when that file exists (heap; free it), else NULL.
static wchar_t *mod_file(int mod, const GameString *path) {
    const char *text = string_text(path);
    size_t length = path->length, prefix = wcslen(mounts[mod].media);
    if (!safe_relative(text, length)) return NULL;
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, (int)length, NULL, 0);
    if (!size || prefix + (unsigned)size + 2 > 32767) return NULL;
    wchar_t *physical = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, (prefix + (size_t)size + 2) * sizeof(wchar_t));
    if (!physical) return NULL;
    memcpy(physical, mounts[mod].media, prefix * sizeof(wchar_t));
    physical[prefix++] = L'\\';
    int converted = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, (int)length, physical + prefix, size);
    physical[prefix + (size_t)converted] = 0;
    for (size_t i = prefix; i < prefix + (size_t)converted; ++i)
        if (physical[i] == L'/') physical[i] = L'\\';
    DWORD attributes = converted ? GetFileAttributesW(physical) : INVALID_FILE_ATTRIBUTES;
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        HeapFree(GetProcessHeap(), 0, physical);
        return NULL;
    }
    return physical;
}

static void served(int mod, const GameString *path, const char *how) {
    if (InterlockedIncrement(&served_lines) > SERVED_LOG_LIMIT) return;
    char name[600];
    if (!WideCharToMultiByte(CP_UTF8, 0, mounts[mod].name, -1, name, sizeof(name), NULL, NULL)) name[0] = 0;
    size_t length = path->length < 400 ? path->length : 400;
    media_say("the game opens mods\\%s\\media\\%.*s (%s)%s", name, (int)length, string_text(path), how,
              served_lines == SERVED_LOG_LIMIT ? "; further files are not logged" : "");
}

// A full game:\media\ path asked of a mod's volume goes to the whole file system.
static void *whole_filesystem(void *volume, const GameString *path) {
    static const char prefix[] = "game:\\media\\";
    if (path->length <= sizeof(prefix) - 1 || !same_path(string_text(path), sizeof(prefix) - 1, prefix) ||
        mod_of(volume) < 0)
        return NULL;
    return InterlockedCompareExchangePointer(&media_filesystem, NULL, NULL);
}

static void **methods_of(void *object) { return *(void ***)object; }

static DWORD mod_file_attributes(void *volume, const GameString *path) {
    void *fs = whole_filesystem(volume, path);
    if (fs) return ((FileAttributes)methods_of(fs)[0xf8 / 8])(fs, path);
    int mod = mod_of(volume);
    if (mod < 0) return original_attributes(volume, path);
    wchar_t *physical = mod_file(mod, path);
    if (!physical) return INVALID_FILE_ATTRIBUTES;
    DWORD result = GetFileAttributesW(physical);
    HeapFree(GetProcessHeap(), 0, physical);
    return result;
}

static unsigned char mod_file_exists(void *volume, const GameString *path) {
    void *fs = whole_filesystem(volume, path);
    if (fs) return ((FileExists)methods_of(fs)[0xe8 / 8])(fs, path);
    const char *text = string_text(path);
    for (size_t i = 0; i < path->length; ++i) {  // archive selectors such as <*>0 are no file names
        unsigned char ch = (unsigned char)text[i];
        if (ch < 32 || ch == '<' || ch == '>' || ch == '*' || ch == '?' || ch == '"' || ch == '|') return 0;
    }
    int mod = mod_of(volume);
    if (mod < 0) return original_exists(volume, path);
    wchar_t *physical = mod_file(mod, path);
    if (!physical) return 0;
    HeapFree(GetProcessHeap(), 0, physical);
    return 1;
}

// The native loose-folder scan marks an existing file with hash 1 (a sound
// bank needs a hash that is not 0); other files keep the volume's own answer.
static unsigned mod_file_hash(void *volume, const GameString *path) {
    const char *text = string_text(path);
    if (path->length < 5 || !same_path(text + path->length - 5, 5, ".bank")) return original_hash(volume, path);
    void *fs = whole_filesystem(volume, path);
    if (fs) return ((FileHash)methods_of(fs)[0x120 / 8])(fs, path);
    if (mod_of(volume) < 0) return original_hash(volume, path);
    return mod_file_exists(volume, path) ? 1 : 0;
}

typedef struct {
    DWORD disposition, access, sharing, attributes;
    unsigned char valid;
    unsigned char spare[15];
} NativeFileMode;

static int read_mode(unsigned flags, NativeFileMode *mode) {
    memset(mode, 0, sizeof(*mode));
    file_mode(mode, flags);
    return mode->valid && mode->disposition == OPEN_EXISTING && mode->access == GENERIC_READ;
}

static void **open_mod_file(void *volume, void **output, const GameString *path, unsigned flags, unsigned char a,
                            unsigned char b, void *extra) {
    void *fs = whole_filesystem(volume, path);
    NativeFileMode mode;
    if (fs) {
        *output = NULL;
        if (read_mode(flags, &mode))
            return ((OpenNativeFile)methods_of(fs)[0x138 / 8])(fs, output, path, flags, a, b, extra);
        return output;
    }
    int mod = mod_of(volume);
    if (mod < 0) return original_open(volume, output, path, flags, a, b, extra);
    *output = NULL;
    if (!read_mode(flags, &mode)) return output;
    wchar_t *physical = mod_file(mod, path);
    if (!physical) return output;
    HANDLE handle = CreateFileW(physical, mode.access, mode.sharing, NULL, mode.disposition,
                                FILE_ATTRIBUTE_NORMAL | mode.attributes, NULL);
    HeapFree(GetProcessHeap(), 0, physical);
    if (handle == INVALID_HANDLE_VALUE) return output;
    void *storage = allocate(0x90);
    if (!storage) {
        CloseHandle(handle);
        return output;
    }
    ((Reference)methods_of(volume)[0x10 / 8])(volume);
    void *owner = volume;
    void *file = construct_file(storage, handle, &owner, flags);
    ((SetFilename)methods_of(file)[0x280 / 8])(file, path);
    ((Reference)methods_of(file)[0x10 / 8])(file);
    *output = file;
    served(mod, path, "read");
    return output;
}

// ---- asynchronous reads: the volume opens a registered alias, which the
// game's CreateFile2 import (kernelx.dll, slot FF_CREATEFILE2_IAT) maps to
// the mod's file.

typedef struct Alias {
    struct Alias *next;
    wchar_t *physical;
    char name[48];
} Alias;

static const char alias_prefix[] = "game:\\__lang_support_file__\\";
static SRWLOCK alias_lock = SRWLOCK_INIT;
static Alias *aliases;
static unsigned alias_count;
static CreateFile2Fn original_create_file2;
static volatile LONG create_file2_hooked;

static const char *alias_for(const wchar_t *physical) {
    const char *result = NULL;
    AcquireSRWLockExclusive(&alias_lock);
    for (Alias *p = aliases; p && !result; p = p->next)
        if (!lstrcmpiW(p->physical, physical)) result = p->name;
    if (!result && alias_count < 16384) {
        size_t bytes = (wcslen(physical) + 1) * sizeof(wchar_t);
        Alias *p = (Alias *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*p) + bytes);
        if (p) {
            p->physical = (wchar_t *)(p + 1);
            memcpy(p->physical, physical, bytes);
            snprintf(p->name, sizeof(p->name), "%s%08x.bin", alias_prefix, alias_count++);
            p->next = aliases;
            aliases = p;
            result = p->name;
        }
    }
    ReleaseSRWLockExclusive(&alias_lock);
    return result;
}

static HANDLE WINAPI create_file2(LPCWSTR name, DWORD access, DWORD sharing, DWORD disposition, void *options) {
    size_t prefix = sizeof(alias_prefix) - 1, i = 0;
    for (; name && i < prefix; ++i) {
        wchar_t ch = name[i];
        if (ch == L'/') ch = L'\\';
        if (ch >= L'A' && ch <= L'Z') ch = (wchar_t)(ch + L'a' - L'A');
        if (ch != (wchar_t)alias_prefix[i]) break;
    }
    if (i < prefix)
        return original_create_file2 ? original_create_file2(name, access, sharing, disposition, options)
                                     : INVALID_HANDLE_VALUE;
    if (access != GENERIC_READ || disposition != OPEN_EXISTING) {
        SetLastError(ERROR_ACCESS_DENIED);
        return INVALID_HANDLE_VALUE;
    }
    const wchar_t *physical = NULL;
    AcquireSRWLockShared(&alias_lock);
    for (Alias *p = aliases; p && !physical; p = p->next) {
        size_t length = strlen(p->name), k = 0;
        if (wcslen(name) != length) continue;
        for (; k < length; ++k) {
            wchar_t ch = name[k];
            if (ch == L'/') ch = L'\\';
            if (ch >= L'A' && ch <= L'Z') ch = (wchar_t)(ch + L'a' - L'A');
            if (ch != (wchar_t)p->name[k]) break;
        }
        if (k == length) physical = p->physical;
    }
    ReleaseSRWLockShared(&alias_lock);
    if (!physical) {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    // CREATEFILE2_EXTENDED_PARAMETERS: dwSize, dwFileAttributes, dwFileFlags, dwSecurityQosFlags,
    // lpSecurityAttributes, hTemplateFile.
    DWORD flags = FILE_ATTRIBUTE_NORMAL;
    if (options) {
        const DWORD *fields = (const DWORD *)options;
        flags = fields[1] | fields[2] | fields[3];
    }
    return CreateFileW(physical, access, sharing, NULL, disposition, flags, NULL);
}

// The game's import slot of kernelx.dll!CreateFile2, from its import table.
static void **create_file2_slot(unsigned char *game) {
    uint32_t imports = pe_value(game, 0x90);  // the import directory (optional header + 0x78)
    for (unsigned char *entry = imports ? game + imports : NULL; entry; entry += 20) {
        uint32_t names = *(uint32_t *)entry, dll = *(uint32_t *)(entry + 12), thunks = *(uint32_t *)(entry + 16);
        if (!dll) break;
        if (!names || _stricmp((const char *)game + dll, "kernelx.dll")) continue;  // names: the unbound thunks
        for (unsigned i = 0;; ++i) {
            uint64_t name = *(uint64_t *)(game + names + 8ull * i);
            if (!name) break;
            if (!(name >> 63) && !strcmp((const char *)game + (uint32_t)name + 2, "CreateFile2"))
                return (void **)(game + thunks + 8ull * i);
        }
    }
    return NULL;
}

static int hook_create_file2(void) {
    if (InterlockedCompareExchange(&create_file2_hooked, 0, 0)) return 1;
    AcquireSRWLockExclusive(&alias_lock);
    int ready = (int)create_file2_hooked;
    void **slot = ready ? NULL : create_file2_slot(image);
    DWORD protection, ignored;
    if (slot && (unsigned char *)slot == image + FF_CREATEFILE2_IAT &&
        VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &protection)) {
        original_create_file2 = (CreateFile2Fn)*slot;
        InterlockedExchangePointer(slot, (void *)create_file2);
        VirtualProtect(slot, sizeof(void *), protection, &ignored);
        InterlockedExchange(&create_file2_hooked, 1);
        ready = 1;
        media_say("the game's CreateFile2 import opens the mods' files for asynchronous reads");
    } else if (!ready) {
        media_say("the game's CreateFile2 import was not found; asynchronous reads keep the game's files");
    }
    ReleaseSRWLockExclusive(&alias_lock);
    return ready;
}

static void **open_mod_async(void *volume, void **output, const GameString *path, unsigned flags, void *device,
                             unsigned a, unsigned b, unsigned char c) {
    void *fs = whole_filesystem(volume, path);
    NativeFileMode mode;
    if (fs) {
        *output = NULL;
        if (read_mode(flags, &mode))
            return ((OpenAsyncFile)methods_of(fs)[0x148 / 8])(fs, output, path, flags, device, a, b, c);
        return output;
    }
    int mod = mod_of(volume);
    if (mod < 0) return original_async_open(volume, output, path, flags, device, a, b, c);
    *output = NULL;
    if (!read_mode(flags, &mode)) return output;
    wchar_t *physical = mod_file(mod, path);
    const char *alias = physical && hook_create_file2() ? alias_for(physical) : NULL;
    if (physical) HeapFree(GetProcessHeap(), 0, physical);
    if (!alias) return output;
    GameString name = borrowed(alias);
    original_async_open(volume, output, &name, flags, device, a, b, c);
    if (*output) {
        ((SetFilename)methods_of(*output)[0x280 / 8])(*output, path);
        served(mod, path, "asynchronous read");
    } else {
        media_say("the game's asynchronous open of a mod file failed");
    }
    return output;
}

// ------------------------------------------------------------- the mount

static int mount_folder(void *filesystem, unsigned index) {
    char source[] = "game:\\__lang_support_mods__\\000\\";
    size_t digits = sizeof(source) - 5;
    source[digits] = (char)('0' + index / 100);
    source[digits + 1] = (char)('0' + index / 10 % 10);
    source[digits + 2] = (char)('0' + index % 10);
    void *storage = allocate(0x50);
    if (!storage) return 0;
    GameString from = borrowed(source), to = borrowed("game:\\media\\");
    void *volume = construct_volume(storage, &from, 1);
    if (!volume) return 0;
    *(void ***)volume = volume_vtable + 1;
    ((Reference)methods_of(volume)[0x10 / 8])(volume);
    // The file system keeps its own reference; no hash manifest filtering.
    int mounted = ((MountVolume)methods_of(filesystem)[0x1e8 / 8])(filesystem, &to, &volume,
                                                                   0x70000001u + index * 2, 0, 0, 0);
    if (mounted) mounts[index].volume = volume;
    ((Reference)methods_of(volume)[0x18 / 8])(volume);
    return mounted;
}

static void ensure_mounted(void *filesystem) {
    void *root = NULL;
    if (!fh2_safe_read((unsigned char *)filesystem + 0x68, &root, sizeof(root)) || !root) return;
    InterlockedExchangePointer(&media_filesystem, filesystem);
    LONG thread = (LONG)GetCurrentThreadId();
    if (InterlockedCompareExchange(&mount_state, 0, 0) == 2 || InterlockedCompareExchange(&mount_thread, 0, 0) == thread)
        return;
    AcquireSRWLockExclusive(&mount_lock);
    if (InterlockedCompareExchange(&mount_state, 0, 0) == 2) {
        ReleaseSRWLockExclusive(&mount_lock);
        return;
    }
    InterlockedExchange(&mount_thread, thread);
    unsigned mounted = 0;
    for (unsigned i = 0; i < mount_count; ++i) {
        char name[600];
        if (!WideCharToMultiByte(CP_UTF8, 0, mounts[i].name, -1, name, sizeof(name), NULL, NULL)) name[0] = 0;
        if (mount_folder(filesystem, i)) {
            ++mounted;
            media_say("mods\\%s\\media mounted at game:\\media\\ (priority 0x%x)", name, 0x70000001u + i * 2);
        } else {
            media_say("mods\\%s\\media could not be mounted; the game keeps its own files", name);
        }
    }
    ((SortVolumes)methods_of(filesystem)[0x1f8 / 8])(filesystem);
    InterlockedExchange(&mount_thread, 0);
    InterlockedExchange(&mount_state, 2);
    ReleaseSRWLockExclusive(&mount_lock);
    media_say("%u of %u mod media folder(s) mounted on the game's first file lookup; a later mod's file wins", mounted,
              mount_count);
}

static unsigned char resolve_file(void *filesystem, void **volume, void *relative, const void *path,
                                  unsigned char flag1, unsigned char flag2, unsigned char flag3, void **mount,
                                  unsigned char flag4) {
    ensure_mounted(filesystem);
    return original_resolve(filesystem, volume, relative, path, flag1, flag2, flag3, mount, flag4);
}

static int installed;

const char *ff_media_install(unsigned char *game, uint32_t size, void (*log)(const char *message)) {
    media_log = log;
    if (installed) return "the media mount is already installed";
    if (!mods_scanned) return "the mods were not scanned";
    if (!mount_count) return "no mod for Fast & Furious has a media\\ folder";
    const char *problem = ff_media_check(game, size);
    if (problem) return problem;
    image = game;
    original_resolve = (ResolveFile)(game + FF_RESOLVE);
    allocate = (Allocate)(game + FF_ALLOCATE);
    construct_volume = (ConstructVolume)(game + FF_CONSTRUCT_VOLUME);
    construct_file = (ConstructFile)(game + FF_CONSTRUCT_FILE);
    file_mode = (FileMode)(game + FF_FILE_MODE);
    // CFileVolume's vtable with its RTTI locator, its file methods replaced.
    memcpy(volume_vtable, game + FF_VOLUME_VTABLE - 8, sizeof(volume_vtable));
    original_exists = (FileExists)volume_vtable[1 + 0xe8 / 8];
    volume_vtable[1 + 0xe8 / 8] = (void *)mod_file_exists;
    original_attributes = (FileAttributes)volume_vtable[1 + 0xf8 / 8];
    volume_vtable[1 + 0xf8 / 8] = (void *)mod_file_attributes;
    original_hash = (FileHash)volume_vtable[1 + 0x120 / 8];
    volume_vtable[1 + 0x120 / 8] = (void *)mod_file_hash;
    original_open = (OpenNativeFile)volume_vtable[1 + 0x138 / 8];
    volume_vtable[1 + 0x138 / 8] = (void *)open_mod_file;
    original_async_open = (OpenAsyncFile)volume_vtable[1 + 0x148 / 8];
    volume_vtable[1 + 0x148 / 8] = (void *)open_mod_async;

    void *volatile *slot = (void *volatile *)(game + FF_RESOLVE_SLOT);
    DWORD protection, ignored;
    if (!VirtualProtect((void *)slot, sizeof(void *), PAGE_READWRITE, &protection))
        return "the file resolver's vtable slot could not be made writable";
    void *before = InterlockedCompareExchangePointer(slot, (void *)resolve_file, (void *)original_resolve);
    VirtualProtect((void *)slot, sizeof(void *), protection, &ignored);
    if (before != (void *)original_resolve) return "another module changed the file resolver's vtable slot meanwhile";
    installed = 1;
    return NULL;
}

#ifdef LANG_SUPPORT_TEST
void ff_media_test_reset(void) {
    installed = 0;
    mods_scanned = 0;
    mount_count = 0;
    InterlockedExchange(&mount_state, 0);
    InterlockedExchange(&mount_thread, 0);
    InterlockedExchange(&served_lines, 0);
    InterlockedExchange(&create_file2_hooked, 0);
    InterlockedExchangePointer(&media_filesystem, NULL);
    original_create_file2 = NULL;
}

// The game functions the mount calls, replaced by the test's (NULL keeps one).
void ff_media_test_functions(void *allocator, void *volume_constructor, void *resolver, void *file_constructor,
                             void *async_open) {
    if (allocator) allocate = (Allocate)allocator;
    if (volume_constructor) construct_volume = (ConstructVolume)volume_constructor;
    if (resolver) original_resolve = (ResolveFile)resolver;
    if (file_constructor) construct_file = (ConstructFile)file_constructor;
    if (async_open) original_async_open = (OpenAsyncFile)async_open;
}

void *ff_media_test_volume(unsigned index) { return index < mount_count ? mounts[index].volume : NULL; }

void *ff_media_test_create_file2(void) { return (void *)create_file2; }
#endif
