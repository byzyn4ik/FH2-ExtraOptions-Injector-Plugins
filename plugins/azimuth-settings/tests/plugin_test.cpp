// Azimuth Settings (Extra Options / forza_injector plugin).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Offline test of the built plugin DLL as the mod loader drives it (Extra
// Options' FH2PluginInitialize, which forza_injector calls the same way).
// Never runs a game and never touches the games' files: the real executables
// are only read, into private memory.
//
//   plugin_test <mode> <plugin dll> <work folder> [game exe] [azimuth exe] [video xaml] [audio xaml]
//
// The plugin's INI (beside the DLL) and the azimuth.ini it names are written by
// tests\run_tests.py.
//   nohost    the shipping DLL in this process: not Azimuth, nothing installed
//   disabled  [Video] and [Audio] Enabled=0
//   other     AzimuthGraphicsOptions.dll (a stand-in) is loaded: refused
// With the test build (AzimuthSettingsTest.dll), a copy of Azimuth's
// azimuth.exe (1.0.4 or 1.0.2) as the host and a copy of the game's image
// (Forza Horizon 2 or Fast & Furious, told apart by its TimeDateStamp):
//   badhost   the host's launcher frame rate code changed: an unknown build
//   badgame   the game's DeleteObject changed: the native guards refuse
//   unknown   a game image of another size (Forza Motorsport): no pages
//   layout    azimuth.ini without the game's [title-...] section
//   native    the resource call still calls the game's reader
//   eo        (FH2) the call goes to Extra Options' thunk (as 0.3.2 and 0.4.4
//             patch it) and Realtime shadows = On is patched in; the pages are
//             Extra Options' extended pages
//   polish    native, with the game's text language PL
//   ukrainian native, with the game's text language RU and [Captions] Language=uk
// In the flows the game's native functions and Azimuth's volume setter are
// replaced by fakes after FH2PluginInitialize (whose guards saw the real
// bytes), then the Video and Audio pages are read through the call site and
// their properties are driven like the UI.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "fh2_plugin.h"

namespace {

// The games' sites, written down here independently of the plugin's table.
struct GameSites {
    const char *name;
    uint32_t stamp, image_size;
    const wchar_t *title; // [title-...]
    uint32_t call_site, get_resource, thunk; // thunk 0: no Extra Options build
    uint32_t video_registry, audio_registry, text_tables;
    uint32_t cascades, renderer_pointer, renderer_vtable, shadow_read;
    uint32_t delete_object, allocate, free_memory, object_init, object_cleanup, collection_type, allocate_typed,
        collection_init, make_string, insert_item, video_type, audio_type, int_property, float_property,
        collection_property, box_int, box_float, property_changed;
};
const GameSites kGames[] = {
    {"Forza Horizon 2", 0x565501edu, 0x3a18000u, L"title-1a96545b", 0x16ebb37u, 0xaabec0u, 0x13fe354u, 0x35470d0u,
     0x35107b0u, 0x3662990u, 0x2207288u, 0x36609a0u, 0x2e82b88u, 0x1131c02u, 0x257730u, 0x3229c0u, 0x31c7b0u,
     0x143eb80u, 0x1442050u, 0x1476110u, 0x14424e0u, 0xaefd60u, 0x13fe8e0u, 0xb52170u, 0xeca710u, 0xc803c0u,
     0xed81f0u, 0xed6e90u, 0xed77d0u, 0x4702d0u, 0x3f5990u, 0xcf9c60u},
    {"Fast & Furious", 0x55b62fdeu, 0x3a89000u, L"title-5a9771a4", 0x1712d47u, 0xc795c0u, 0, 0x35adf70u, 0x357a460u,
     0x36cc1b0u, 0x22332d8u, 0x36ca1b0u, 0x2ecaea8u, 0x1157a12u, 0x25ed30u, 0x3286d0u, 0x322a70u, 0x1465c90u,
     0x1469160u, 0x1499af0u, 0x14695f0u, 0xb2a7d0u, 0x1425a40u, 0xb8afc0u, 0xee4ca0u, 0xca5f70u, 0xef8ac0u,
     0xef6b00u, 0xef7aa0u, 0x479fa0u, 0x402860u, 0xd51ae0u},
};
const GameSites *game_sites;

// Azimuth's sites, written down here independently of the plugin's table.
struct HostSites {
    uint32_t timestamp, target_rate, volume_setter, volume, rate_list;
    const char *name;
};
const HostSites kHosts[] = {{0x6ae393ebu, 0x7d8554u, 0xc37f0u, 0x7da2e0u, 0x10528u, "1.0.4"},
                            {0x6ae05dd1u, 0x8e7540u, 0xbaea0u, 0x8e7be8u, 0x105f8u, "1.0.2"}};
const HostSites *sites;

int failures;
std::wstring work;
std::string mode;

void check(bool ok, const std::string &what) {
    printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

std::string read_text(const std::wstring &path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return std::string();
    std::string text;
    char buffer[65536];
    DWORD got;
    while (ReadFile(file, buffer, sizeof(buffer), &got, nullptr) && got) text.append(buffer, got);
    CloseHandle(file);
    return text;
}

void write_file(const std::wstring &path, const char *data, size_t size) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD written;
    if (file != INVALID_HANDLE_VALUE) {
        WriteFile(file, data, (DWORD)size, &written, nullptr);
        CloseHandle(file);
    }
}

void host_log(const char *message) {
    HANDLE file = CreateFileW((work + L"\\host.log").c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written;
    WriteFile(file, message, (DWORD)strlen(message), &written, nullptr);
    CloseHandle(file);
}

bool contains(const std::string &text, const char *what) { return text.find(what) != std::string::npos; }

std::wstring ini_value(const wchar_t *section, const wchar_t *key) {
    wchar_t value[256];
    GetPrivateProfileStringW(section, key, L"", value, 256, (work + L"\\azimuth.ini").c_str());
    return value;
}
std::wstring title_value(const wchar_t *key) { return ini_value(game_sites->title, key); }

// A PE file laid out as the loader maps it (no relocations, no imports), in
// private read-write-execute memory.
unsigned char *map_image(const std::wstring &path, uint32_t *size, uint32_t *timestamp) {
    std::string file = read_text(path);
    if (file.size() < 0x400) return nullptr;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)file.data();
    const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)(file.data() + dos->e_lfanew);
    *size = nt->OptionalHeader.SizeOfImage;
    if (timestamp) *timestamp = nt->FileHeader.TimeDateStamp;
    unsigned char *image =
        (unsigned char *)VirtualAlloc(nullptr, *size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!image) return nullptr;
    memcpy(image, file.data(), nt->OptionalHeader.SizeOfHeaders);
    const IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        size_t raw = std::min<size_t>(section->SizeOfRawData, section->Misc.VirtualSize ? section->Misc.VirtualSize
                                                                                         : section->SizeOfRawData);
        if (section->PointerToRawData + raw <= file.size() && section->VirtualAddress + raw <= *size)
            memcpy(image + section->VirtualAddress, file.data() + section->PointerToRawData, raw);
    }
    return image;
}

// ---------------------------------------------------------------- fakes

unsigned char *image;
uint32_t image_size;

struct Fake {
    void **vtable;
    unsigned char pad[8];
    volatile LONG references;
    unsigned char rest[0x60];
};

int deleted_originals, deleted_strings, cleanups, video_registrations, audio_registrations;
std::vector<std::string> changed;
std::vector<std::pair<void *, std::wstring>> inserted;
int last_boxed = -12345;
float last_boxed_float = -1.0f;
std::vector<int> volumes_set;

void *delete_original(Fake *object, unsigned) {
    ++deleted_originals;
    return object;
}
void *delete_string(Fake *object, unsigned) {
    ++deleted_strings;
    return object;
}
void *original_vtable[8] = {nullptr, (void *)(uintptr_t)delete_original};
void *string_vtable[8] = {nullptr, (void *)(uintptr_t)delete_string};

void *fake_allocate(size_t size) { return calloc(1, size); }
void fake_free(void *p) { free(p); }
void fake_object_init(void *object) { ((Fake *)object)->references = 1; }
void fake_object_cleanup(void *) { ++cleanups; }
void fake_delete_object(void *object) {
    void **vtable = ((Fake *)object)->vtable;
    ((void *(*)(void *, unsigned))vtable[0])(object, 1);
}
void *fake_collection_type(void *) { return (void *)(uintptr_t)0x7777; }
void *fake_allocate_typed(size_t size, void *) { return calloc(1, size < sizeof(Fake) ? sizeof(Fake) : size); }
void fake_collection_init(void *object) { ((Fake *)object)->references = 1; }
void **fake_make_string(void **out, const wchar_t *text) {
    Fake *object = (Fake *)calloc(1, sizeof(Fake));
    object->vtable = string_vtable + 1;
    object->references = 1;
    std::wstring *copy = new std::wstring(text ? text : L"");
    memcpy(object->rest, &copy, sizeof(copy));
    *out = object;
    return out;
}
void fake_insert_item(void *items, int, void *label) {
    InterlockedIncrement(&((Fake *)label)->references);
    std::wstring *text;
    memcpy(&text, ((Fake *)label)->rest, sizeof(text));
    inserted.push_back({items, *text});
}
void fake_register_video_type() { ++video_registrations; }
void fake_register_audio_type() { ++audio_registrations; }
unsigned char int_descriptor[0x58], float_descriptor[0x58], list_descriptor[0x58];
void *fake_int_property() { return int_descriptor; }
void *fake_float_property() { return float_descriptor; }
void *fake_collection_property() { return list_descriptor; }
void **fake_box_int(void **out, const int *value) {
    unsigned char *box = (unsigned char *)calloc(1, 0x40);
    memcpy(box + 0x28, value, 4);
    last_boxed = *value;
    *out = box;
    return out;
}
void **fake_box_float(void **out, const float *value) {
    unsigned char *box = (unsigned char *)calloc(1, 0x40);
    memcpy(box + 0x28, value, 4);
    last_boxed_float = *value;
    *out = box;
    return out;
}
void fake_property_changed(void *, void *property) {
    const char *name;
    memcpy(&name, (unsigned char *)property + 0x40, sizeof(name));
    changed.push_back(name ? name : "?");
}
void fake_set_volume(int volume) { volumes_set.push_back(volume); }

std::string next_resource;
Fake *last_original;

// The game's reader (native mode) or Extra Options' hook (eo mode).
void **fake_reader(void *, void **output) {
    if (next_resource.empty()) {
        *output = nullptr;
        return output;
    }
    unsigned char *object = (unsigned char *)calloc(1, 0x40 + next_resource.size() + 1);
    Fake *resource = (Fake *)object;
    resource->vtable = original_vtable + 1;
    resource->references = 1;
    char *bytes = (char *)(object + 0x40);
    memcpy(bytes, next_resource.data(), next_resource.size());
    memcpy(object + 0x28, &bytes, sizeof(bytes));
    int length = (int)next_resource.size();
    memcpy(object + 0x30, &length, 4);
    memcpy(object + 0x34, &length, 4);
    last_original = resource;
    *output = resource;
    return output;
}

void jump_stub(unsigned char *base, uint32_t rva, const void *target) {
    unsigned char code[12] = {0x48, 0xb8};
    uintptr_t address = (uintptr_t)target;
    memcpy(code + 2, &address, 8);
    code[10] = 0xff;
    code[11] = 0xe0;
    memcpy(base + rva, code, sizeof(code));
    FlushInstructionCache(GetCurrentProcess(), base + rva, sizeof(code));
}

void install_fakes(unsigned char *host) {
    const GameSites &g = *game_sites;
    const struct {
        uint32_t rva;
        const void *fake;
    } fakes[] = {
        {g.allocate, (void *)(uintptr_t)fake_allocate},
        {g.free_memory, (void *)(uintptr_t)fake_free},
        {g.object_init, (void *)(uintptr_t)fake_object_init},
        {g.object_cleanup, (void *)(uintptr_t)fake_object_cleanup},
        {g.delete_object, (void *)(uintptr_t)fake_delete_object},
        {g.collection_type, (void *)(uintptr_t)fake_collection_type},
        {g.allocate_typed, (void *)(uintptr_t)fake_allocate_typed},
        {g.collection_init, (void *)(uintptr_t)fake_collection_init},
        {g.make_string, (void *)(uintptr_t)fake_make_string},
        {g.insert_item, (void *)(uintptr_t)fake_insert_item},
        {g.video_type, (void *)(uintptr_t)fake_register_video_type},
        {g.audio_type, (void *)(uintptr_t)fake_register_audio_type},
        {g.int_property, (void *)(uintptr_t)fake_int_property},
        {g.float_property, (void *)(uintptr_t)fake_float_property},
        {g.collection_property, (void *)(uintptr_t)fake_collection_property},
        {g.box_int, (void *)(uintptr_t)fake_box_int},
        {g.box_float, (void *)(uintptr_t)fake_box_float},
        {g.property_changed, (void *)(uintptr_t)fake_property_changed},
    };
    for (const auto &f : fakes) jump_stub(image, f.rva, f.fake);
    // RegisterProperty keeps the game's own code: it only links the
    // descriptor into the registry's list.
    void *vtable = (void *)(uintptr_t)0x1234;
    memcpy(int_descriptor, &vtable, 8);
    memcpy(float_descriptor, &vtable, 8);
    memcpy(list_descriptor, &vtable, 8);
    memset(int_descriptor + 0x28, 0x11, 0x30);
    memset(float_descriptor + 0x28, 0x33, 0x30);
    memset(list_descriptor + 0x28, 0x22, 0x30);
    // Azimuth's volume setter takes its audio lock: a fake records the calls.
    jump_stub(host, sites->volume_setter, (void *)(uintptr_t)fake_set_volume);
}

void *call_target() {
    int32_t displacement;
    memcpy(&displacement, image + game_sites->call_site + 1, 4);
    return image + game_sites->call_site + 5 + displacement;
}

void **read_page(const std::string &xaml) {
    static void *output;
    next_resource = xaml;
    output = nullptr;
    typedef void **(*Reader)(void *, void **);
    ((Reader)call_target())(nullptr, &output);
    return &output;
}

std::string page_text(void **out) {
    std::string text;
    if (*out && *out != last_original) {
        char *bytes;
        int length;
        memcpy(&bytes, (unsigned char *)*out + 0x28, 8);
        memcpy(&length, (unsigned char *)*out + 0x30, 4);
        text.assign(bytes, (size_t)length);
    }
    return text;
}

struct Property {
    unsigned char *descriptor;
    std::string name;
};

std::vector<Property> registered(uint32_t registry) {
    std::vector<Property> list;
    unsigned char *link;
    memcpy(&link, image + registry + 0x40, sizeof(link));
    for (int guard = 0; link && guard < 1000; ++guard) {
        unsigned char *descriptor;
        memcpy(&descriptor, link, 8);
        const char *name;
        memcpy(&name, descriptor + 0x40, 8);
        list.push_back({descriptor, name ? name : ""});
        memcpy(&link, link + 8, 8);
    }
    return list;
}

unsigned char *property(const char *name) {
    for (uint32_t registry : {game_sites->video_registry, game_sites->audio_registry})
        for (const auto &p : registered(registry))
            if (p.name == name) return p.descriptor;
    return nullptr;
}

int count_prefixed(uint32_t registry, const char *prefix) {
    int n = 0;
    for (const auto &p : registered(registry)) n += p.name.rfind(prefix, 0) == 0;
    return n;
}

void *call_getter(const char *name) {
    unsigned char *descriptor = property(name);
    if (!descriptor) return nullptr;
    typedef void **(*Getter)(void **, void *, void *);
    Getter getter;
    memcpy(&getter, descriptor + 0x28, 8);
    void *out = nullptr;
    getter(&out, descriptor, nullptr);
    return out;
}

int get_value(const char *name) {
    last_boxed = -12345;
    call_getter(name);
    return last_boxed;
}

float get_float(const char *name) {
    last_boxed_float = -1.0f;
    call_getter(name);
    return last_boxed_float;
}

void *get_object(const char *name) { return call_getter(name); }

bool set_raw(const char *name, const void *value) {
    unsigned char *descriptor = property(name);
    if (!descriptor) return false;
    typedef void (*Setter)(void *, void *, void *);
    Setter setter;
    memcpy(&setter, descriptor + 0x30, 8);
    if (!setter) return false;
    unsigned char box[0x40] = {};
    memcpy(box + 0x28, value, 4);
    changed.clear();
    setter(descriptor, (void *)(uintptr_t)0x5150, box);
    return true;
}
bool set_value(const char *name, int value) { return set_raw(name, &value); }
bool set_float(const char *name, float value) { return set_raw(name, &value); }

std::vector<std::wstring> labels(void *collection) {
    std::vector<std::wstring> out;
    for (const auto &item : inserted)
        if (item.first == collection) out.push_back(item.second);
    return out;
}

bool cascades_are(const float *expected) {
    float now[8];
    memcpy(now, image + game_sites->cascades, sizeof(now));
    for (int i = 0; i < 8; ++i)
        if (i != 3 && i != 7 && now[i] != expected[i]) return false;
    return true;
}

// -------------------------------------------------------------- the plugin

HMODULE plugin;
FH2PluginContext context;
std::wstring plugin_path, plugin_folder;

BOOL initialize(unsigned char *host) {
    plugin = LoadLibraryExW(plugin_path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    check(plugin != nullptr, "the plugin DLL loads");
    if (!plugin) return FALSE;
    auto init = (FH2PluginInitializeFn)(uintptr_t)GetProcAddress(plugin, "FH2PluginInitialize");
    check(init != nullptr, "FH2PluginInitialize is exported");
    if (!init) return FALSE;
    if (host) {
        auto use = (void (*)(unsigned char *))(uintptr_t)GetProcAddress(plugin, "AzsTestUseHost");
        check(use != nullptr, "the test build takes a host image");
        if (use) use(host);
    }
    context.size = sizeof(context);
    context.api_version = FH2_PLUGIN_API_VERSION;
    context.adapter = FH2_PLUGIN_AZIMUTH;
    context.game_image_size = image_size;
    context.game_image = image;
    context.plugin_module = plugin;
    context.plugins_directory = plugin_folder.c_str();
    context.plugin_path = plugin_path.c_str();
    context.extra_options_version = "0.4.4";
    context.log_utf8 = host_log;
    return init(&context);
}

std::wstring widen(const char *text) {
    int n = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    std::wstring out((size_t)(n > 0 ? n - 1 : 0), L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, text, -1, &out[0], n);
    return out;
}

const float kLow[8] = {5.0f, 15.0f, 40.0f, 0, 1.6f, 15.0f, 35.0f, 0};
const float kHigh[8] = {5.0f, 15.0f, 124.0f, 0, 1.6f, 15.0f, 120.0f, 0};
const float kUltra[8] = {17.0f, 130.0f, 360.0f, 0, 4.0f, 60.0f, 270.0f, 0};

void set_language(const char *code) {
    static unsigned char manager[0x60];
    unsigned char *text = manager + 0x30;
    memset(text, 0, 16);
    memcpy(text, code, strlen(code));
    uint64_t length = strlen(code), capacity = 15;
    memcpy(text + 16, &length, 8);
    memcpy(text + 24, &capacity, 8);
    void *pointer = manager;
    memcpy(image + game_sites->text_tables, &pointer, 8);
}

const wchar_t *const kDetailKeys[9] = {L"shadow_detail", L"shadow_distance", L"shadow_redraw",
                                       L"reflections",   L"scenery_detail",  L"car_detail",
                                       L"particles",     L"motion_blur",     L"texture_filtering"};
const char *const kDetailRows[9] = {"AzsShadowDetail", "AzsShadowDistance", "AzsShadowRedraw",
                                    "AzsReflections",  "AzsSceneryDetail",  "AzsCarDetail",
                                    "AzsParticles",    "AzsMotionBlur",     "AzsTextureFiltering"};

bool details_are(const wchar_t *const expected[9]) {
    for (int i = 0; i < 9; ++i)
        if (title_value(kDetailKeys[i]) != expected[i]) return false;
    return true;
}

LONG renderer_updates(unsigned char *renderer) {
    LONG updates;
    memcpy(&updates, renderer + 0x21bc, 4);
    return updates;
}

void video_flow(unsigned char *host, const std::string &video, bool eo, bool polish, bool ukrainian) {
    const GameSites &g = *game_sites;
    // A page that is not a settings page passes through untouched.
    std::string hud = "<Page x:Class=\"ForzaUI.A_HudOptions\"/>";
    void **out = read_page(hud);
    check(*out == last_original && deleted_originals == 0, "another page passes through unchanged");
    std::string standalone = "<!-- EO:StandaloneOptionsPage -->" + video;
    out = read_page(standalone);
    check(*out == last_original && deleted_originals == 0, "a plugin's standalone page on the Video class is left alone");

    out = read_page(video);
    check(*out && *out != last_original, "the Video page comes back as a new resource");
    check(deleted_originals == 1, "the original resource is released once");
    std::string extended = page_text(out);
    if (!extended.empty()) {
        void **vtable = ((Fake *)*out)->vtable;
        check(vtable[0] != original_vtable[1], "the new resource deletes itself through the plugin");
    }
    write_file(work + L"\\video_out.xaml", extended.data(), extended.size());
    check(contains(extended, "SelectedIndex=\"{Binding AzsFrameRate}\"") &&
              contains(extended, "SelectedIndex=\"{Binding AzsMotionBlur}\""),
          "the Video page has the rows (Frame rate ... Motion blur)");
    if (eo) check(contains(extended, "EO_RealtimeShadows"), "Extra Options' own Video rows are kept");
    check(contains(extended, ukrainian ? "Частота кадрів" : polish ? "Liczba klatek" : "Label=\"Frame rate\""),
          ukrainian ? "Ukrainian captions" : polish ? "Polish captions" : "English captions");
    check(video_registrations == 1, "the Video page type is set up before its properties");
    check(audio_registrations == 0, "the Audio page type is not touched by the Video page");

    int ours = count_prefixed(g.video_registry, "Azs");
    check(ours == 34, "34 Video properties registered (17 rows and their choices): " + std::to_string(ours));

    std::vector<std::wstring> fps = labels(get_object("AzsFrameRateChoices"));
    check(fps.size() == 7 && fps[0] == L"30 FPS" && fps[4] == L"144 FPS" && fps[6] == L"240 FPS",
          "Frame rate choices are Azimuth's list (30 ... 240 FPS)");
    std::vector<std::wstring> resolutions = labels(get_object("AzsResolutionChoices"));
    check(resolutions.size() == 10 && resolutions[9] == L"3840 x 2160 (4K)", "Resolution choices");
    std::vector<std::wstring> hdr = labels(get_object("AzsHdrChoices"));
    check(hdr.size() == 2 && hdr[0] == (ukrainian ? widen("Вимк") : polish ? widen("Wył.") : L"Off"),
          "HDR choices in the page's language");
    std::vector<std::wstring> blur = labels(get_object("AzsMotionBlurChoices"));
    check(blur.size() == 4 && blur[0] == (ukrainian ? widen("Вимк") : polish ? widen("Wył.") : L"Off"),
          "Motion blur choices: Off, Default, High, Ultra");
    std::vector<std::wstring> filtering = labels(get_object("AzsTextureFilteringChoices"));
    check(filtering.size() == 2 && filtering[1] == L"16x", "Texture filtering choices: Default, 16x");
    std::vector<std::wstring> distance = labels(get_object("AzsShadowDistanceChoices"));
    check(distance.size() == 3, "Shadow distance choices: Default, High, Ultra");
    std::vector<std::wstring> aa = labels(get_object("AzsAntiAliasingChoices"));
    check(aa.size() == 4 && aa[2] == L"4x MSAA", "Anti-aliasing choices: Off, 2x, 4x MSAA, Console");

    static const int rates[] = {30, 60, 90, 120, 144, 165, 240};
    check(get_value("AzsFrameRate") == 3, "Frame rate shows 120 FPS from azimuth.ini");
    LONG *target = (LONG *)(host + sites->target_rate);
    for (int index = 0; index < 7; ++index) {
        int rate = rates[index];
        if (index == 3) continue; // already 120
        set_value("AzsFrameRate", index);
        check(title_value(L"frame_rate") == std::to_wstring(rate) && *target == rate &&
                  get_value("AzsFrameRate") == index,
              "Frame rate " + std::to_string(rate) + ": saved and applied at once (Azimuth's target " +
                  std::to_string(*target) + ")");
    }
    set_value("AzsFrameRate", 3);
    check(changed.size() == 1 && changed[0] == "AzsFrameRate", "a change notifies its row");
    changed.clear();
    set_value("AzsFrameRate", 3);
    check(changed.empty(), "choosing the current value writes nothing");

    // A shadow renderer, as the game makes it.
    unsigned char *renderer = (unsigned char *)calloc(1, 0x2400);
    void *vtable = image + g.renderer_vtable;
    memcpy(renderer, &vtable, 8);
    LONG one = 1;
    memcpy(renderer + 0x21bc, &one, 4);
    memcpy(image + g.renderer_pointer, &renderer, 8);

    static const wchar_t *const ultra[9] = {L"ultra", L"ultra", L"ultra", L"ultra", L"ultra",
                                            L"ultra", L"default", L"ultra", L"16x"};
    static const wchar_t *const high[9] = {L"high", L"high", L"high", L"high", L"high",
                                           L"high", L"default", L"high", L"16x"};
    static const wchar_t *const low[9] = {L"low", L"default", L"default", L"low", L"low",
                                          L"low", L"low", L"off", L"default"};
    set_value("AzsPreset", 0);
    check(title_value(L"graphics") == L"low" && details_are(low),
          "Graphics Low writes every detail as Azimuth's preset (distance and redraw Default, motion blur Off)");
    check(changed.size() == 10 && changed[0] == "AzsPreset" && changed[9] == "AzsTextureFiltering",
          "Graphics notifies its row and the nine details");
    check(cascades_are(kLow) && renderer_updates(renderer) == 1, "Graphics Low: the console's cascades, 1 map per frame");
    set_value("AzsPreset", 3);
    check(title_value(L"graphics") == L"ultra" && details_are(ultra),
          "Graphics Ultra writes every detail (particles Default, texture filtering 16x)");
    check(cascades_are(kUltra), "Graphics Ultra: Ultra's cascades at once");
    check(eo ? renderer_updates(renderer) == 1 : renderer_updates(renderer) == 4,
          eo ? "Realtime shadows on: the renderer's update rate is left alone" : "Graphics Ultra: 4 maps per frame");
    bool levels = true;
    for (int i = 0; i < 9; ++i) {
        int want = i == 1 || i == 2 ? 2 : i == 6 || i == 8 ? 1 : 3;
        levels = levels && get_value(kDetailRows[i]) == want;
    }
    check(levels && get_value("AzsPreset") == 3, "Graphics shows Ultra and each detail the preset's level");
    set_value("AzsShadowDistance", 1);
    check(title_value(L"shadow_distance") == L"high" && title_value(L"graphics") == L"custom" &&
              get_value("AzsPreset") == 4,
          "Shadow distance High: shadow_distance=high, Graphics Custom");
    check(changed.size() == 2 && changed[1] == "AzsPreset", "a detail notifies its row and Graphics");
    check(cascades_are(kHigh), "Shadow distance High: High's cascades at once");
    set_value("AzsShadowRedraw", 0);
    check(title_value(L"shadow_redraw") == L"default" && (eo || renderer_updates(renderer) == 1),
          "Shadow redraw Default: shadow_redraw=default, 1 map per frame at once");
    set_value("AzsPreset", 2);
    check(title_value(L"graphics") == L"high" && details_are(high) && cascades_are(kHigh) &&
              (eo || renderer_updates(renderer) == 4),
          "Graphics High: every detail High (particles Default, texture filtering 16x), High's cascades");
    set_value("AzsShadowDetail", 3);
    set_value("AzsShadowDetail", 2);
    check(title_value(L"shadow_detail") == L"high" && title_value(L"graphics") == L"high" && get_value("AzsPreset") == 2,
          "details back on High's levels: Graphics shows High again");
    set_value("AzsMotionBlur", 0);
    set_value("AzsTextureFiltering", 0);
    set_value("AzsParticles", 0);
    check(title_value(L"motion_blur") == L"off" && title_value(L"texture_filtering") == L"default" &&
              title_value(L"particles") == L"low",
          "Motion blur Off, Texture filtering Default, Particles Low write Azimuth's values");
    set_value("AzsDisplay", 0);
    check(ini_value(L"launcher", L"display") == L"windowed", "Display mode writes [launcher] display");
    set_value("AzsAspect", 1);
    set_value("AzsAntiAliasing", 3);
    set_value("AzsVSync", 0);
    set_value("AzsHdr", 1);
    set_value("AzsResolution", 5);
    check(title_value(L"aspect") == L"display" && title_value(L"antialiasing") == L"coverage" &&
              title_value(L"vsync") == L"off" && title_value(L"hdr") == L"on" && title_value(L"resolution") == L"1440",
          "Aspect ratio, Anti-aliasing, V-sync, HDR and Resolution write their keys");
    set_value("AzsHdr", 7);
    check(title_value(L"hdr") == L"on", "an out-of-range value is ignored");

    size_t before = registered(g.video_registry).size();
    out = read_page(video);
    check(*out && *out != last_original && registered(g.video_registry).size() == before,
          "a second visit extends the page again without registering twice");
}

void audio_flow(const std::string &audio, bool eo, bool polish, bool ukrainian) {
    const GameSites &g = *game_sites;
    int deleted = deleted_originals;
    std::string standalone = "<!-- EO:StandaloneOptionsPage -->" + audio;
    void **out = read_page(standalone);
    check(*out == last_original && deleted_originals == deleted, "a plugin's standalone page on the Audio class is left alone");
    out = read_page(audio);
    check(*out && *out != last_original && deleted_originals == deleted + 1, "the Audio page comes back as a new resource");
    std::string extended = page_text(out);
    write_file(work + L"\\audio_out.xaml", extended.data(), extended.size());
    check(contains(extended, "SelectedIndex=\"{Binding AzsAudioOutput}\"") &&
              contains(extended, "Value=\"{Binding AzsVolume}\"") &&
              contains(extended, "SelectedIndex=\"{Binding AzsSpeech}\"") &&
              contains(extended, "SelectedIndex=\"{Binding AzsLanguage}\""),
          "the Audio page has the audio output, volume, speech and text language rows");
    if (eo) check(contains(extended, "{Binding MasterVolume}") && contains(extended, "{Binding CheckpointSound}"),
                  "Extra Options' own Audio rows are kept");
    check(contains(extended, ukrainian ? "Аудіовихід (Azimuth)" : polish ? "Wyjście audio (Azimuth)" : "Audio output (Azimuth)"),
          "Audio captions in the page's language");
    check(audio_registrations == 1, "the Audio page type is set up before its properties");
    int ours = count_prefixed(g.audio_registry, "Azs");
    check(ours == 7, "7 Audio properties registered (output, speech, language, their choices; volume): " +
                         std::to_string(ours));
    check(count_prefixed(g.audio_registry, "AzsFrameRate") == 0 && count_prefixed(g.video_registry, "AzsVolume") == 0,
          "each page's properties are in its own type");
    std::vector<std::wstring> outputs = labels(get_object("AzsAudioOutputChoices"));
    check(outputs.size() == 3 && outputs[0] == (ukrainian ? widen("Стерео") : L"Stereo") &&
              outputs[2] == (ukrainian ? widen("Багатоканальний") : polish ? widen("Wielokanałowe") : L"Multichannel"),
          "Audio output choices: Stereo, System, Multichannel");
    check(get_value("AzsAudioOutput") == 0, "Audio output shows stereo from azimuth.ini");
    set_value("AzsAudioOutput", 2);
    check(title_value(L"audio") == L"multichannel" && volumes_set.empty() && changed.size() == 1 &&
              changed[0] == "AzsAudioOutput",
          "Audio output Multichannel: audio=multichannel (after a restart), only its row notified");
    set_value("AzsAudioOutput", 1);
    check(title_value(L"audio") == L"system", "Audio output System: audio=system");
    set_value("AzsAudioOutput", 3);
    check(title_value(L"audio") == L"system", "an out-of-range output is ignored");

    float level = get_float("AzsVolume");
    check(std::fabs(level - 0.8f) < 0.001f, "Volume shows 80 % from azimuth.ini as 0.80: " + std::to_string(level));
    set_float("AzsVolume", 0.55f);
    check(title_value(L"volume") == L"55" && volumes_set.size() == 1 && volumes_set[0] == 55,
          "Volume 0.55: volume=55, Azimuth's volume setter called with 55 at once");
    set_float("AzsVolume", 0.0f);
    check(title_value(L"volume") == L"0" && volumes_set.size() == 2 && volumes_set[1] == 0, "Volume 0: volume=0, applied");
    set_float("AzsVolume", 1.0f);
    check(title_value(L"volume") == L"100" && volumes_set.back() == 100, "Volume 1.0: volume=100");
    size_t calls = volumes_set.size();
    set_float("AzsVolume", 1.0f);
    check(volumes_set.size() == calls, "the same volume again writes and applies nothing");
    set_float("AzsVolume", 3.0f);
    check(title_value(L"volume") == L"100" && volumes_set.size() == calls, "an out-of-range volume is ignored");

    std::vector<std::wstring> speech = labels(get_object("AzsSpeechChoices"));
    std::vector<std::wstring> text = labels(get_object("AzsLanguageChoices"));
    check(speech.size() >= 13 && text.size() >= 13 &&
              speech[0] == (ukrainian ? widen("Як текст") : polish ? widen("Jak tekst") : L"Same as text") &&
              speech[8] == (ukrainian ? widen("Польська") : polish ? widen("Polski") : L"Polish"),
          "Speech and text language choices: absent first, then the languages");
    set_value("AzsSpeech", 8);
    check(title_value(L"speech") == L"pl-PL" && get_value("AzsSpeech") == 8, "Speech Polish: speech=pl-PL");
    set_value("AzsSpeech", 0);
    check(title_value(L"speech").empty() && get_value("AzsSpeech") == 0, "Speech Same as text: no speech key");
    set_value("AzsLanguage", 1);
    check(title_value(L"language") == L"en-US", "Text language English (US): language=en-US");
    size_t before = registered(g.audio_registry).size();
    out = read_page(audio);
    check(*out && *out != last_original && registered(g.audio_registry).size() == before,
          "a second visit extends the Audio page again without registering twice");
}

int run_flow(unsigned char *host, const std::string &video, const std::string &audio) {
    const GameSites &g = *game_sites;
    bool eo = mode == "eo", polish = mode == "polish", ukrainian = mode == "ukrainian";
    if (eo) {
        // As Extra Options 0.3.2 and 0.4.4: its thunk in padding, the call
        // pointed at it, and Realtime shadows = On (all cascades every frame).
        jump_stub(image, g.thunk, (void *)(uintptr_t)fake_reader);
        int32_t displacement = (int32_t)(g.thunk - (g.call_site + 5));
        memcpy(image + g.call_site + 1, &displacement, 4);
        static const unsigned char all_cascades[] = {0xb8, 0x04, 0x00, 0x00, 0x00, 0x90, 0x90};
        memcpy(image + g.shadow_read, all_cascades, sizeof(all_cascades));
    }
    if (polish) set_language("PL");
    if (ukrainian) set_language("RU");
    void *before = call_target();
    BOOL ok = initialize(host);
    check(ok == TRUE, "FH2PluginInitialize returns TRUE");
    auto set_rates = (void (*)(const int *, int))(uintptr_t)GetProcAddress(plugin, "AzsTestSetFrameRates");
    static const int rates[] = {30, 60, 90, 120, 144, 165, 240}; // Azimuth's list for a 240 Hz display
    if (set_rates) set_rates(rates, 7);
    void *after = call_target();
    check(after != before, "the resource call site now calls the plugin's jump");
    unsigned char jump[6];
    memcpy(jump, after, 6);
    check(jump[0] == 0xff && jump[1] == 0x25, "the jump is jmp [rip+0] (within 2 GB of the site)");
    std::string log = read_text(work + L"\\host.log");
    check(contains(log, (std::string("host: Azimuth ") + sites->name).c_str()), std::string("the log names Azimuth ") + sites->name);
    check(contains(log, g.thunk ? "game: Forza Horizon 2 1.0.0.64" : "game: Forza Horizon 2 Presents Fast & Furious 1.0.0.6"),
          "the log names the game");
    char narrow_title[32] = {};
    WideCharToMultiByte(CP_UTF8, 0, g.title, -1, narrow_title, sizeof(narrow_title) - 1, nullptr, nullptr);
    std::string section = std::string("settings in [") + narrow_title + "]; the title's own frame rate 30 FPS (Azimuth's)";
    check(contains(log, section.c_str()), "the rows write the game's own section; its own rate (30) read from Azimuth");
    check(contains(log, eo ? "chained to Extra Options' resource hook (thunk 0x13fe354" : "chained to the game's resource reader "),
          "the log names what the call was chained to");
    check(contains(log, "frame rate applies at once"), "frame rate applies at once");
    check(contains(log, "display mode applies at once"), "display mode applies at once");
    check(contains(log, "volume applies at once"), "volume applies at once");
    check(contains(log, "shadow distance applies at once"), "shadow distance applies at once");
    check(contains(log, eo ? "updates every cascade each frame; shadow redraw left to it" : "shadow redraw applies at once"),
          eo ? "Extra Options' Realtime shadows patch is left alone" : "shadow redraw applies at once");
    if (ukrainian) check(contains(log, "captions: Ukrainian"), "[Captions] Language=uk is logged");

    install_fakes(host);
    if (!eo) jump_stub(image, g.get_resource, (void *)(uintptr_t)fake_reader);
    video_flow(host, video, eo, polish, ukrainian);
    if (!audio.empty()) audio_flow(audio, eo, polish, ukrainian);
    return 0;
}

int finish() {
    printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: plugin_test <mode> <plugin dll> <work> [game exe] [azimuth exe] [video xaml] [audio xaml]\n");
        return 2;
    }
    char narrow_mode[32] = {};
    WideCharToMultiByte(CP_UTF8, 0, argv[1], -1, narrow_mode, sizeof(narrow_mode) - 1, nullptr, nullptr);
    mode = narrow_mode;
    plugin_path = argv[2];
    plugin_folder = plugin_path.substr(0, plugin_path.find_last_of(L"\\/"));
    work = argv[3];
    printf("plugin_test %s\n", mode.c_str());

    if (mode == "nohost" || mode == "disabled" || mode == "other") {
        image_size = 0x3a18000u;
        image = (unsigned char *)VirtualAlloc(nullptr, image_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (mode == "other") {
            // A stand-in module named as Azimuth Graphics Options' DLL.
            HMODULE other = LoadLibraryW((plugin_folder + L"\\AzimuthGraphicsOptions.dll").c_str());
            check(other != nullptr, "the stand-in AzimuthGraphicsOptions.dll is loaded");
        }
        BOOL ok = initialize(nullptr);
        std::string log = read_text(work + L"\\host.log");
        check(ok == TRUE, "FH2PluginInitialize returns TRUE");
        if (mode == "nohost")
            check(contains(log, "the host is not Azimuth 1.0.4 or 1.0.2") && contains(log, "nothing is installed"),
                  "not on Azimuth 1.0.4 or 1.0.2: nothing is installed, and the log says why");
        else if (mode == "disabled")
            check(contains(log, "[Video] and [Audio] Enabled=0; nothing is installed"), "Enabled=0: nothing is installed");
        else
            check(contains(log, "AzimuthGraphicsOptions.dll (Azimuth Graphics Options) is loaded") &&
                      contains(log, "Nothing is installed"),
                  "with Azimuth Graphics Options loaded: refused, and the log says why");
        unsigned char zero[16] = {};
        check(!memcmp(image + 0x16ebb37u, zero, sizeof(zero)), "the game image is left as it is");
        return finish();
    }
    if (argc < 6) {
        fprintf(stderr, "%s needs the game and azimuth executables\n", mode.c_str());
        return 2;
    }
    uint32_t host_size = 0, timestamp = 0, game_stamp = 0;
    unsigned char *host = map_image(argv[5], &host_size, &timestamp);
    for (const HostSites &h : kHosts)
        if (h.timestamp == timestamp) sites = &h;
    check(host != nullptr && sites, std::string("Azimuth's azimuth.exe is mapped (") + (sites ? sites->name : "?") + ")");
    image = map_image(argv[4], &image_size, &game_stamp);
    for (const GameSites &g : kGames)
        if (g.stamp == game_stamp && g.image_size == image_size) game_sites = &g;
    check(image != nullptr && game_sites, std::string("the game's image is mapped (") +
                                              (game_sites ? game_sites->name : "?") + ")");
    if (!host || !sites || !image || !game_sites) return 1;
    const GameSites &g = *game_sites;
    unsigned char call[16];
    memcpy(call, image + g.call_site, sizeof(call));
    if (mode == "badhost") host[sites->rate_list + 4] = 0x3d; // the launcher's 60 becomes 61
    if (mode == "badgame") image[g.delete_object + 1] ^= 0xff;
    if (mode == "unknown") image_size = 0x2000000u; // another game (Forza Motorsport 5 or 6): another image size
    if (mode == "badhost" || mode == "badgame" || mode == "layout" || mode == "unknown") {
        BOOL ok = initialize(host);
        std::string log = read_text(work + L"\\host.log");
        check(ok == TRUE, "FH2PluginInitialize returns TRUE");
        char rva[16];
        snprintf(rva, sizeof(rva), "0x%x", g.delete_object);
        std::string expected = mode == "badhost"   ? std::string("is not Azimuth ") + sites->name +
                                                         "'s; an unknown build; nothing is installed"
                               : mode == "badgame" ? std::string("DeleteObject at ") + rva +
                                                         " is not as expected; nothing is installed"
                               : mode == "unknown" ? "no settings pages here; nothing is installed"
                                                   : "] section (Azimuth 1.x keeps the game's settings there)";
        check(contains(log, expected.c_str()), mode + ": nothing is installed, and the log says why");
        check(!memcmp(call, image + g.call_site, sizeof(call)), "the resource call site is left as it is");
        return finish();
    }
    std::string video = argc > 6 ? read_text(argv[6]) : std::string();
    std::string audio = argc > 7 ? read_text(argv[7]) : std::string();
    check(!video.empty(), "the Video page XAML is read");
    if (argc > 7) check(!audio.empty(), "the Audio page XAML is read");
    if (mode == "eo" && !g.thunk) {
        check(false, "eo mode needs Forza Horizon 2 (Extra Options' thunk)");
        return finish();
    }
    run_flow(host, video, audio);
    return finish();
}
