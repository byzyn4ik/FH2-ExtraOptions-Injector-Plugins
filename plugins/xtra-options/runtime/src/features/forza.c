// forza_injector: plugin API version 2 in the core: the version-2 context,
// event subscriptions and the early plugin phase (sdk/include/forza_plugin.h).
#include "forza.h"
#include "forza_profile.h"
#include "addons.h"
#include "logging.h"
#include "support.h"

static const char *injector_version = "0.1.0";
void forza_set_injector_version(const char *version) {
    if (version) injector_version = version;
}

#define MAX_SUBSCRIPTIONS 64

typedef struct {
    uint32_t event;
    ForzaEventFn callback;
    void *user;
} Subscription;

static Subscription subscriptions[MAX_SUBSCRIPTIONS];
static volatile LONG subscription_count;
static volatile LONG vehicle_subscribers;
static INIT_ONCE early_once = INIT_ONCE_STATIC_INIT;

static BOOL subscribe(uint32_t event, ForzaEventFn callback, void *user) {
    if (!callback || event < FORZA_EVENT_FILES_READY || event > FORZA_EVENT_VEHICLE) return FALSE;
    LONG index = InterlockedIncrement(&subscription_count) - 1;
    if (index >= MAX_SUBSCRIPTIONS) {
        InterlockedDecrement(&subscription_count);
        log_append("PLUGINS: too many event subscriptions; one refused.\r\n");
        return FALSE;
    }
    subscriptions[index].callback = callback;
    subscriptions[index].user = user;
    InterlockedExchange((volatile LONG *)&subscriptions[index].event, (LONG)event);  // published last
    if (event == FORZA_EVENT_VEHICLE) InterlockedIncrement(&vehicle_subscribers);
    return TRUE;
}

void forza_dispatch(uint32_t event, const void *frame) {
    LONG count = InterlockedCompareExchange(&subscription_count, 0, 0);
    if (count > MAX_SUBSCRIPTIONS) count = MAX_SUBSCRIPTIONS;
    for (LONG i = 0; i < count; ++i) {
        if ((uint32_t)InterlockedCompareExchange((volatile LONG *)&subscriptions[i].event, 0, 0) == event)
            subscriptions[i].callback(event, frame, subscriptions[i].user);
    }
}

LONG forza_vehicle_subscribers(void) { return InterlockedCompareExchange(&vehicle_subscribers, 0, 0); }

void forza_context_fill(ForzaContext *context, const FH2PluginContext *v1) {
    const ForzaGameInfo *game = forza_game_by_image(v1->game_image);
    unsigned char *host = (unsigned char *)GetModuleHandleW(NULL);
    uint32_t host_timestamp = 0, host_size = 0;
    forza_image_identity(host, &host_timestamp, &host_size);
    uint32_t game_timestamp = 0, game_size = 0;
    forza_image_identity(v1->game_image, &game_timestamp, &game_size);
    context->size = sizeof(*context);
    context->api_version = FORZA_PLUGIN_API_VERSION;
    context->v1 = v1;
    context->game = game ? game->game : FORZA_GAME_UNKNOWN;
    context->game_id = game ? game->id : "";
    context->title_id = game ? game->title_id : "";
    context->game_timestamp = game_timestamp;
    context->host_image = host;
    context->host_timestamp = host_timestamp;
    context->host_image_size = host_size;
    context->injector_version = injector_version;
    context->subscribe = subscribe;
}

// --------------------------------------------------------------- export check

static int read_at(const unsigned char *data, LONGLONG size, LONGLONG offset, void *out, DWORD length) {
    if (offset < 0 || offset > size || (LONGLONG)length > size - offset) return 0;
    memory_copy(out, data + offset, length);
    return 1;
}

BOOL forza_file_exports(const wchar_t *path, const char *name) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    LARGE_INTEGER size;
    BOOL found = FALSE;
    unsigned char *data = NULL;
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0x200 || size.QuadPart > 64 * 1024 * 1024) goto done;
    data = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)size.QuadPart);
    DWORD got = 0;
    if (!data || !ReadFile(file, data, (DWORD)size.QuadPart, &got, NULL) || got != (DWORD)size.QuadPart) goto done;
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS64 nt;
    if (!read_at(data, size.QuadPart, 0, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        !read_at(data, size.QuadPart, dos.e_lfanew, &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        goto done;
    IMAGE_DATA_DIRECTORY directory = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!directory.VirtualAddress) goto done;
    LONGLONG sections = dos.e_lfanew + 4 + sizeof(IMAGE_FILE_HEADER) + nt.FileHeader.SizeOfOptionalHeader;
    // RVA -> file offset through the section table.
#define FILE_OFFSET(rva, out)                                                                                  \
    do {                                                                                                       \
        (out) = -1;                                                                                            \
        for (WORD s_ = 0; s_ < nt.FileHeader.NumberOfSections; ++s_) {                                         \
            IMAGE_SECTION_HEADER h_;                                                                           \
            if (!read_at(data, size.QuadPart, sections + s_ * (LONGLONG)sizeof(h_), &h_, sizeof(h_))) break;   \
            if ((rva) >= h_.VirtualAddress && (rva) < h_.VirtualAddress + h_.SizeOfRawData) {                  \
                (out) = (LONGLONG)h_.PointerToRawData + (rva) - h_.VirtualAddress;                             \
                break;                                                                                         \
            }                                                                                                  \
        }                                                                                                      \
    } while (0)
    LONGLONG at;
    FILE_OFFSET(directory.VirtualAddress, at);
    IMAGE_EXPORT_DIRECTORY exports;
    if (at < 0 || !read_at(data, size.QuadPart, at, &exports, sizeof(exports))) goto done;
    LONGLONG names;
    FILE_OFFSET(exports.AddressOfNames, names);
    for (DWORD i = 0; names >= 0 && i < exports.NumberOfNames && i < 65536; ++i) {
        DWORD rva;
        LONGLONG text;
        if (!read_at(data, size.QuadPart, names + 4 * (LONGLONG)i, &rva, 4)) break;
        FILE_OFFSET(rva, text);
        if (text < 0) continue;
        LONGLONG k = 0;
        while (name[k] && text + k < size.QuadPart && data[text + k] == (unsigned char)name[k]) ++k;
        if (!name[k] && text + k < size.QuadPart && !data[text + k]) {
            found = TRUE;
            break;
        }
    }
#undef FILE_OFFSET
done:
    if (data) HeapFree(GetProcessHeap(), 0, data);
    CloseHandle(file);
    return found;
}

// ---------------------------------------------------------------- early phase

static void log_early(const wchar_t *path, const char *result) {
    int size = WideCharToMultiByte(CP_UTF8, 0, path, -1, NULL, 0, NULL, NULL);
    char *text = size ? HeapAlloc(GetProcessHeap(), 0, (size_t)size) : NULL;
    if (text) WideCharToMultiByte(CP_UTF8, 0, path, -1, text, size, NULL, NULL);
    log_append("PLUGINS (early): ");
    log_append(text ? text : "[path conversion failed]");
    log_append(" | ");
    log_append(result);
    log_append("\r\n");
    if (text) HeapFree(GetProcessHeap(), 0, text);
}

typedef struct {
    FH2PluginContext v1;
    ForzaContext v2;
    wchar_t *path, *folder;
} EarlyPlugin;

static unsigned char *early_game;

static void early_directory(const wchar_t *folder) {
    wchar_t *pattern = HeapAlloc(GetProcessHeap(), 0, PATH_CAPACITY * sizeof(wchar_t));
    // The same order as the loader: alphabetical, case-insensitive, top level only.
    wchar_t (*names)[MAX_PATH] = HeapAlloc(GetProcessHeap(), 0, 64 * sizeof(*names));
    unsigned count = 0;
    if (!pattern || !names || !path_join(pattern, folder, L"*.dll")) goto done;
    WIN32_FIND_DATAW data;
    HANDLE search = FindFirstFileW(pattern, &data);
    if (search == INVALID_HANDLE_VALUE) goto done;
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY || count == 64) continue;
        unsigned at = count++;
        while (at && lstrcmpiW(names[at - 1], data.cFileName) > 0) {
            lstrcpyW(names[at], names[at - 1]);
            --at;
        }
        lstrcpyW(names[at], data.cFileName);
    } while (FindNextFileW(search, &data));
    FindClose(search);
    for (unsigned i = 0; i < count; ++i) {
        if (!path_join(pattern, folder, names[i]) || !forza_file_exports(pattern, "ForzaPluginEarlyInitialize"))
            continue;
        EarlyPlugin *plugin = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*plugin));
        size_t path_length = (size_t)lstrlenW(pattern) + 1, folder_length = (size_t)lstrlenW(folder) + 1;
        if (plugin) {
            plugin->path = HeapAlloc(GetProcessHeap(), 0, path_length * sizeof(wchar_t));
            plugin->folder = HeapAlloc(GetProcessHeap(), 0, folder_length * sizeof(wchar_t));
        }
        if (!plugin || !plugin->path || !plugin->folder) {
            log_early(pattern, "out of memory");
            break;  // kept: plugins are never unloaded, nor their contexts freed
        }
        memory_copy(plugin->path, pattern, path_length * sizeof(wchar_t));
        memory_copy(plugin->folder, folder, folder_length * sizeof(wchar_t));
        HMODULE module = LoadLibraryExW(plugin->path, NULL,
                                        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!module) {
            log_early(plugin->path, "load failed");
            continue;
        }
        ForzaPluginEarlyInitializeFn early =
            (ForzaPluginEarlyInitializeFn)(uintptr_t)GetProcAddress(module, "ForzaPluginEarlyInitialize");
        uint32_t timestamp = 0, image_size = 0;
        forza_image_identity(early_game, &timestamp, &image_size);
        plugin->v1.size = sizeof(plugin->v1);
        plugin->v1.api_version = FH2_PLUGIN_API_VERSION;
        plugin->v1.adapter = FH2_PLUGIN_AZIMUTH;
        plugin->v1.game_image_size = image_size;
        plugin->v1.game_image = early_game;
        plugin->v1.plugin_module = module;
        plugin->v1.plugins_directory = plugin->folder;
        plugin->v1.plugin_path = plugin->path;
        plugin->v1.extra_options_version = MOD_VERSION;
        plugin->v1.log_utf8 = log_append;
        forza_context_fill(&plugin->v2, &plugin->v1);
        log_early(plugin->path, early && early(&plugin->v2) ? "early initialization complete"
                                                            : "early initializer returned FALSE");
    }
done:
    if (names) HeapFree(GetProcessHeap(), 0, names);
    if (pattern) HeapFree(GetProcessHeap(), 0, pattern);
}

static BOOL CALLBACK early_once_body(PINIT_ONCE once, PVOID parameter, PVOID *context) {
    (void)once, (void)parameter, (void)context;
    wchar_t *folder = HeapAlloc(GetProcessHeap(), 0, PATH_CAPACITY * sizeof(wchar_t));
    if (!folder) return TRUE;
    unsigned count = addons_count();
    for (unsigned i = 0; i < count; ++i) {
        const wchar_t *root = addons_directory(i);
        if (root && addons_media_active(i) && path_join(folder, root, L"plugins")) early_directory(folder);
    }
    HeapFree(GetProcessHeap(), 0, folder);
    return TRUE;
}

void forza_plugins_early(unsigned char *game_image) {
    early_game = game_image;
    InitOnceExecuteOnce(&early_once, early_once_body, NULL, NULL);
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) BOOL ForzaTestFileExports(const wchar_t *path, const char *name) {
    return forza_file_exports(path, name);
}
__declspec(dllexport) void ForzaTestPluginsEarly(unsigned char *game_image) { forza_plugins_early(game_image); }
#endif
