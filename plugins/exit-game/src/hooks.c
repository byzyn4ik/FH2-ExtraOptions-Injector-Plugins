// Exit Game for Forza Horizon 2 (Extra Options plugin).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Two hooks, both chained onto whatever is there when the plugin starts
// (Extra Options 0.4.4 has its own hooks on both sites; other plugins may
// chain them too). Each handler calls the previous target first.
//
// 1. The pause page's XAML. The game reads every XAML resource through one
//    call on its UIRenderer LoadComponent path, "call 0xaabec0" at
//    game+0x16ebb37. Extra Options points that call at its thunk in padding
//    (game+0x13fe354: mov rax, <its reader>; jmp rax), whose reader edits the
//    pause page (CarsNextToHome) and others. The call's 4-byte displacement is
//    swapped, atomically, for a jump to read_resource (below), which calls
//    the previous target and then gives the pause page (A_PauseMenu) its
//    Exit Game tile (pause_xaml.c) in a new resource buffer, as Extra Options
//    does for its own edits.
//
// The addresses below are Forza Horizon 2 1.0.0.64's; Fast & Furious 1.0.0.6
// has the same code at its own addresses (the `builds` table).
//
// 2. The pause page's init. PauseMenuTiled's vtable slot game+0x2e14310 holds
//    its page init (game 0xdc6e30; Extra Options' Return to Garage and the
//    Weather Control plugin chain it). The init binds each tile:
//    `lea rax,<handler>` then 0xd9f9f0(page, "PART_Button_<tile>",
//    &{handler, 0}), as for Car Packs at 0xdc8155. The slot is swapped
//    (compare-exchange) for pause_init, which runs the previous init and then
//    binds PART_Button_EOExitGame the same way to choose_exit_game().
//
// Every native function and site is checked byte for byte first; on any
// difference nothing is changed.
#include "exit_game.h"

#include <stdio.h>
#include <string.h>

#include "fh2_game.h"  // FH2_TEXT_TABLES
#include "pause_xaml.h"

enum {
    MAX_XAML = 1 << 20,
    EXTRA_ROOM = 0x4000,
};

typedef struct {
    uint32_t rva;
    const char *name;
    unsigned length;
    const unsigned char *bytes;
} Code;

#define CODE(rva, name, ...) {rva, name, sizeof((const unsigned char[]){__VA_ARGS__}), (const unsigned char[]){__VA_ARGS__}}

// One inspected game build: its sites, and the code expected at each.
typedef struct {
    const char *name;
    uint32_t image_size, stamp;    // SizeOfImage, PE TimeDateStamp
    uint32_t pause_init_slot;      // PauseMenuTiled vtable: page init
    uint32_t pause_update_slot;    // ... and page update (identifies the vtable)
    uint32_t pause_init, pause_update;
    uint32_t binding;              // lea rax,<a tile's handler> ... call bind_button
    uint32_t binding_name;         // offset of its `lea rdx,<"PART_Button_...">`
    const char *binding_tile;      // that name
    uint32_t bind_button, resource_call, get_resource;
    uint32_t extra_options_thunk;  // 0: no Extra Options build for this game
    uint32_t allocate, free_memory, object_init, object_cleanup, delete_object;
    uint32_t text_tables;          // the text table manager (common/include/fh2_game.h FH2_TEXT_TABLES)
    const Code *code;
    unsigned code_count;
} Build;

// Forza Horizon 2 1.0.0.64.
static const Code fh2_code[] = {
    CODE(0xdc6e30, "pause page init",
         0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8d, 0xac),
    CODE(0xdce660, "pause page update",
         0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x30, 0x48, 0x8b, 0xf9, 0xe8, 0x8e, 0xe2),
    CODE(0xd9f9f0, "tile binder",
         0x48, 0x8b, 0xc4, 0x57, 0x48, 0x81, 0xec, 0x90, 0x00, 0x00, 0x00, 0x48, 0xc7, 0x40, 0xa0, 0xfe),
    // lea rax,[handler]; mov [rsp+20],rax; mov [rsp+28],r12d; vmovaps xmm0,[rsp+20];
    // vmovdqa [rbp+4e0],xmm0; lea r8,[rbp+4e0]; lea rdx,["PART_Button_CarPacks"];
    // mov rcx,rdi; call 0xd9f9f0
    CODE(0xdc8155, "Car Packs tile binding",
         0x48, 0x8d, 0x05, 0xc4, 0x55, 0xff, 0xff, 0x48, 0x89, 0x44, 0x24, 0x20, 0x44, 0x89, 0x64, 0x24,
         0x28, 0xc5, 0xf8, 0x28, 0x44, 0x24, 0x20, 0xc5, 0xf9, 0x7f, 0x85, 0xe0, 0x04, 0x00, 0x00, 0x4c,
         0x8d, 0x85, 0xe0, 0x04, 0x00, 0x00, 0x48, 0x8d, 0x15, 0x86, 0x7c, 0x19, 0x01, 0x48, 0x8b, 0xcf,
         0xe8, 0x66, 0x78, 0xfd, 0xff),
    // ... mov [rdi],r15; mov rcx,[rax]; mov [rdi+8],rcx after the resource call.
    CODE(0x16ebb37 + 5, "code after the XAML resource call",
         0x4c, 0x89, 0x3f, 0x48, 0x8b, 0x08, 0x48, 0x89, 0x4f, 0x08),
    CODE(0x3229c0, "allocate",
         0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x8b, 0x15),
    CODE(0x31c7b0, "free",
         0x48, 0x85, 0xc9, 0x74, 0x1d, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b),
    CODE(0x143eb80, "object init",
         0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8d),
    CODE(0x1442050, "object cleanup",
         0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8d, 0x05, 0xb3, 0xd0, 0x9f),
    CODE(0x257730, "delete object",
         0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0xe8, 0xb2, 0xa2),
};

// Forza Horizon 2 Presents Fast & Furious 1.0.0.6: the same pause page (its
// Home tab has Online Free Roam where FH2 has Online Road Trip) and the same
// functions, found again by the tile names its init binds and by the code
// around each site (no Car Packs: the My Profile binding is the model).
static const Code ff_code[] = {
    CODE(0xde1d00, "pause page init",
         0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8d, 0xac, 0x24, 0x58, 0xfd, 0xff),
    CODE(0xde68b0, "pause page update",
         0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xf9, 0xe8, 0x0e, 0xc2),
    CODE(0xdc22f0, "tile binder",
         0x48, 0x8b, 0xc4, 0x57, 0x48, 0x81, 0xec, 0x90, 0x00, 0x00, 0x00, 0x48, 0xc7, 0x40, 0xa0, 0xfe),
    // lea rax,[handler]; mov [rsp+30],rax; mov [rsp+38],r15d; vmovaps xmm0,[rsp+30];
    // vmovdqa [rbp+50],xmm0; lea r8,[rbp+50]; lea rdx,["PART_Button_MyProfile"];
    // mov rcx,rsi; call 0xdc22f0
    CODE(0xde2170, "My Profile tile binding",
         0x48, 0x8d, 0x05, 0x49, 0x7d, 0xff, 0xff, 0x48, 0x89, 0x44, 0x24, 0x30, 0x44, 0x89, 0x7c, 0x24,
         0x38, 0xc5, 0xf8, 0x28, 0x44, 0x24, 0x30, 0xc5, 0xf9, 0x7f, 0x45, 0x50, 0x4c, 0x8d, 0x45, 0x50,
         0x48, 0x8d, 0x15, 0x69, 0x3d, 0x1a, 0x01, 0x48, 0x8b, 0xce, 0xe8, 0x51, 0x01, 0xfe, 0xff),
    CODE(0x1712d47 + 5, "code after the XAML resource call",
         0x4c, 0x89, 0x3f, 0x48, 0x8b, 0x08, 0x48, 0x89, 0x4f, 0x08),
    CODE(0x3286d0, "allocate",
         0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x8b, 0x15),
    CODE(0x322a70, "free",
         0x48, 0x85, 0xc9, 0x74, 0x1d, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b),
    CODE(0x1465c90, "object init",
         0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8d),
    CODE(0x1469160, "object cleanup",
         0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8d, 0x05, 0xb3, 0x96, 0xa1),
    CODE(0x25ed30, "delete object",
         0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0xe8, 0xf2, 0x9c),
};

static const Build builds[] = {
    {"Forza Horizon 2 1.0.0.64", 0x3a18000, 0x565501ed, 0x2e14310, 0x2e14350, 0xdc6e30, 0xdce660, 0xdc8155, 0x26,
     "PART_Button_CarPacks", 0xd9f9f0, 0x16ebb37, 0xaabec0, 0x13fe354, 0x3229c0, 0x31c7b0, 0x143eb80, 0x1442050,
     0x257730, 0x3662990, fh2_code, sizeof(fh2_code) / sizeof(fh2_code[0])},
    {"Forza Horizon 2 Presents Fast & Furious 1.0.0.6", 0x3a89000, 0x55b62fde, 0x2e5b960, 0x2e5b9a0, 0xde1d00,
     0xde68b0, 0xde2170, 0x20, "PART_Button_MyProfile", 0xdc22f0, 0x1712d47, 0xc795c0, 0, 0x3286d0, 0x322a70,
     0x1465c90, 0x1469160, 0x25ed30, 0x36cc1b0, ff_code, sizeof(ff_code) / sizeof(ff_code[0])},
};
static const unsigned char reader_prologue[] = {0x48, 0x83, 0xec, 0x18, 0x48, 0x8b, 0x41, 0x38, 0xc7, 0x04, 0x24, 0x00};
static const Build *build;  // the running game's, once hooks_check found it

// The mapped image's PE TimeDateStamp, or 0.
static uint32_t fh2_image_stamp(const unsigned char *image) {
    int32_t header = 0;
    uint32_t stamp = 0;
    if (!fh2_safe_read(image + 0x3c, &header, sizeof(header)) || header <= 0 || header > 0x1000 ||
        !fh2_safe_read(image + header + 8, &stamp, sizeof(stamp)))
        return 0;
    return stamp;
}

typedef struct {
    void **vtable;
    unsigned char header[0x20];
    char *bytes;
    int length;
    int capacity;
} NativeResource;
_Static_assert(offsetof(NativeResource, bytes) == 0x28, "native resource buffer");

typedef struct {
    void *method;
    int32_t adjust, unused;
} MemberHandler;

typedef void (*PageFunction)(void *page);
typedef void **(*ReadResource)(void *reader, void **output);
typedef struct {
    void *(*allocate)(size_t);
    void (*free_memory)(void *);
    void (*object_init)(void *);
    void (*object_cleanup)(void *);
    void (*delete_object)(void *);
    void (*bind_button)(void *page, const char *name, const MemberHandler *handler);
} Natives;

static unsigned char *image;
static Natives natives;
static PageFunction previous_init;
static ReadResource previous_reader;
static void *resource_vtable[7];
static volatile LONG tile_on_page, told_added, told_layout, told_memory;
#ifdef EXIT_GAME_TEST
static Natives test_natives;
static int use_test_natives;
#endif

const unsigned char *hooks_text_base(const unsigned char *game) {
    // Only for the image the hooks went into (the build is that game's).
    return build && game && game == image ? game + build->text_tables - FH2_TEXT_TABLES : game;
}

static int in_game(const unsigned char *game, const void *address) {
    return (const unsigned char *)address >= game && (const unsigned char *)address < game + build->image_size;
}

// "winmm.dll+0x1234" for code in a module, else the address.
static void describe(const void *address, char *out, size_t size) {
    HMODULE module = NULL;
    wchar_t path[MAX_PATH];
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)address, &module) &&
        GetModuleFileNameW(module, path, MAX_PATH)) {
        const wchar_t *name = wcsrchr(path, L'\\');
        snprintf(out, size, "%ls+0x%llx", name ? name + 1 : path,
                 (unsigned long long)((const unsigned char *)address - (const unsigned char *)module));
    } else {
        snprintf(out, size, "0x%llx", (unsigned long long)(uintptr_t)address);
    }
}

// A slot holds the game's function or another module's hook (executable code
// outside the game image).
static int slot_ok(const unsigned char *game, uint32_t slot, uint32_t native, void **target) {
    void *value = NULL;
    if (!fh2_safe_read(game + slot, &value, sizeof(value))) return 0;
    *target = value;
    return value == game + native || (!in_game(game, value) && fh2_is_code(value));
}

const char *hooks_check(const unsigned char *game, uint32_t size, const char **init_target,
                        const char **resource_target) {
    static char problem[200], init_text[160], resource_text[200];
    char where[120];
    if (init_target) *init_target = NULL;
    if (resource_target) *resource_target = NULL;
    build = NULL;
    for (unsigned i = 0; game && i < sizeof(builds) / sizeof(builds[0]); ++i)
        if (size == builds[i].image_size && fh2_image_stamp(game) == builds[i].stamp) build = &builds[i];
    if (!build)
        return "the game image is not a supported build (Forza Horizon 2 1.0.0.64 or Fast & Furious 1.0.0.6)";
    for (unsigned i = 0; i < build->code_count; ++i) {
        const Code *code = &build->code[i];
        if (!fh2_bytes_equal(game + code->rva, code->bytes, code->length)) {
            snprintf(problem, sizeof(problem), "the game's %s at 0x%x is not as expected", code->name, code->rva);
            return problem;
        }
    }
    // The model binding's name operand: lea rdx at +binding_name, disp32 3 bytes on.
    int32_t displacement;
    memcpy(&displacement, game + build->binding + build->binding_name + 3, 4);
    if (!fh2_bytes_equal(game + build->binding + build->binding_name + 7 + displacement, build->binding_tile,
                         strlen(build->binding_tile) + 1)) {
        snprintf(problem, sizeof(problem), "the tile binding at 0x%x does not name %s", build->binding,
                 build->binding_tile);
        return problem;
    }

    void *init = NULL, *update = NULL;
    if (!slot_ok(game, build->pause_update_slot, build->pause_update, &update)) {
        snprintf(problem, sizeof(problem), "the pause page vtable (update slot 0x%x) is not as expected",
                 build->pause_update_slot);
        return problem;
    }
    if (!slot_ok(game, build->pause_init_slot, build->pause_init, &init)) {
        snprintf(problem, sizeof(problem), "the pause page init slot 0x%x holds neither the game's init nor executable code",
                 build->pause_init_slot);
        return problem;
    }
    if (init == game + build->pause_init) {
        snprintf(init_text, sizeof(init_text), "the game's pause page init 0x%x", build->pause_init);
    } else {
        describe(init, where, sizeof(where));
        snprintf(init_text, sizeof(init_text), "a hook at %s (Extra Options' or another plugin's)", where);
    }

    const unsigned char *called = fh2_call_target(game + build->resource_call);
    if (!called) return "the XAML resource site is not a call";
    if (called == game + build->get_resource) {
        if (!fh2_bytes_equal(called, reader_prologue, sizeof(reader_prologue)))
            return "the game's resource reader is not as expected";
        snprintf(resource_text, sizeof(resource_text), "the game's resource reader 0x%x", build->get_resource);
    } else if (build->extra_options_thunk && called == game + build->extra_options_thunk) {
        unsigned char thunk[12];
        void *hook;
        if (!fh2_safe_read(called, thunk, sizeof(thunk)) || thunk[0] != 0x48 || thunk[1] != 0xb8 || thunk[10] != 0xff ||
            thunk[11] != 0xe0)
        {
            snprintf(problem, sizeof(problem), "the XAML resource call goes to 0x%x, which is not Extra Options' thunk",
                     build->extra_options_thunk);
            return problem;
        }
        memcpy(&hook, thunk + 2, sizeof(hook));
        if (in_game(game, hook) || !fh2_is_code(hook)) return "Extra Options' resource thunk does not lead to its code";
        describe(hook, where, sizeof(where));
        snprintf(resource_text, sizeof(resource_text), "Extra Options' resource thunk 0x%x -> %s", build->extra_options_thunk,
                 where);
    } else if (!in_game(game, called) && fh2_is_code(called)) {
        describe(called, where, sizeof(where));
        snprintf(resource_text, sizeof(resource_text), "another module's hook at %s", where);
    } else {
        return "the XAML resource call goes somewhere unexpected";
    }
    if (((uintptr_t)(game + build->resource_call + 1) & 3) != 0) return "the XAML resource call is not 4-byte aligned";
    if (init_target) *init_target = init_text;
    if (resource_target) *resource_target = resource_text;
    return NULL;
}

// ------------------------------------------------------------- chaining

// The shared chaining (common/include/fh2_guard.h, also used by Azimuth
// Graphics Options on the same resource call), with this plugin's wording.
static const char *chain_problem(Fh2ChainResult result) {
    switch (result) {
    case FH2_CHAINED: return NULL;
    case FH2_NOT_CALL:
    case FH2_TARGET_NOT_CODE: return "its target is not executable code";
    case FH2_UNALIGNED: return "it is not 8-byte aligned";  // only the slot can be (hooks_check)
    case FH2_NO_NEAR_MEMORY: return "no free memory within 2 GB of it";
    case FH2_JUMP_FAILED: return "the jump could not be made executable";
    case FH2_WRITE_FAILED: return "it could not be written";
    case FH2_RACED: return "another module changed it at the same time";
    }
    return "?";
}

// --------------------------------------------------------------- handlers

static void release_object(void *object) {
    if (object && InterlockedDecrement((volatile LONG *)((unsigned char *)object + 0x10)) == 0)
        natives.delete_object(object);
}

static void *delete_resource(NativeResource *resource, unsigned flags) {
    natives.object_cleanup(resource);
    if (flags & 1) natives.free_memory(resource);
    return resource;
}

static void tell_once(volatile LONG *flag, const char *text) {
    if (!InterlockedExchange(flag, 1)) say(text);
}

static void **read_resource(void *reader, void **output) {
    previous_reader(reader, output);
    NativeResource *original = output ? (NativeResource *)*output : NULL;
    if (!original || !original->bytes || original->length <= 0 || original->length > MAX_XAML ||
        !pause_xaml_is_page(original->bytes, (size_t)original->length))
        return output;
    size_t capacity = (size_t)original->length + EXTRA_ROOM;
    NativeResource *replacement = natives.allocate(sizeof(NativeResource) + capacity);
    if (!replacement) {
        InterlockedExchange(&tile_on_page, 0);
        tell_once(&told_memory, "no memory for the pause menu with the tile; the menu is left as it is");
        return output;
    }
    char *bytes = (char *)(replacement + 1);
    const char *why = NULL;
    size_t length = pause_xaml_extend(original->bytes, (size_t)original->length, bytes, capacity,
                                      exit_caption(image), &why);
    if (!length) {
        natives.free_memory(replacement);
        // A page that already has the tile (loaded through this hook twice) keeps it.
        if (!why || strcmp(why, "it already has the Exit Game tile")) {
            InterlockedExchange(&tile_on_page, 0);
            if (!InterlockedExchange(&told_layout, 1)) {
                char line[200];
                snprintf(line, sizeof(line), "the pause menu is not as expected (%s); no Exit Game tile",
                         why ? why : "?");
                say(line);
            }
        }
        return output;
    }
    // As Extra Options does: the game's resource object with this buffer,
    // owned by the parser through the native reference count.
    natives.object_init(replacement);
    memcpy(resource_vtable, original->vtable - 1, sizeof(resource_vtable));
    resource_vtable[1] = (void *)delete_resource;  // vtable slot 0, after the type information
    replacement->vtable = resource_vtable + 1;
    replacement->bytes = bytes;
    replacement->length = (int)length;
    replacement->capacity = (int)capacity;
    *output = replacement;
    release_object(original);
    InterlockedExchange(&tile_on_page, 1);
    tell_once(&told_added, "Exit Game tile added to the pause menu's Home tab");
    return output;
}

static void pause_init(void *page) {
    previous_init(page);
    if (page && tile_on_page) {
        MemberHandler handler = {(void *)(uintptr_t)choose_exit_game, 0, 0};
        natives.bind_button(page, EXIT_GAME_TILE_NAME, &handler);
    }
}

int hooks_install(unsigned char *game) {
    image = game;
    natives.allocate = (void *(*)(size_t))(void *)(game + build->allocate);
    natives.free_memory = (void (*)(void *))(void *)(game + build->free_memory);
    natives.object_init = (void (*)(void *))(void *)(game + build->object_init);
    natives.object_cleanup = (void (*)(void *))(void *)(game + build->object_cleanup);
    natives.delete_object = (void (*)(void *))(void *)(game + build->delete_object);
    natives.bind_button = (void (*)(void *, const char *, const MemberHandler *))(void *)(game + build->bind_button);
#ifdef EXIT_GAME_TEST
    if (use_test_natives) natives = test_natives;
#endif
    char line[200];
    // The init first: alone it binds nothing until the page has the tile.
    void **slot = (void **)(game + build->pause_init_slot);
    void *init = *slot;
    previous_init = (PageFunction)init;
    const char *problem = chain_problem(fh2_chain_slot(slot, init, (void *)(uintptr_t)pause_init));
    if (problem) {
        snprintf(line, sizeof(line), "the pause page init slot 0x%x could not be chained (%s); nothing is installed",
                 build->pause_init_slot, problem);
        say(line);
        return 0;
    }
    // previous_reader is set inside chain_call before the call is swapped.
    problem = chain_problem(
        fh2_chain_call(game + build->resource_call, (void *)(uintptr_t)read_resource, (void **)&previous_reader));
    if (problem) {
        // The init hook stays (another module may have chained it since); it binds nothing.
        snprintf(line, sizeof(line), "the XAML resource call 0x%x could not be chained (%s); no Exit Game tile",
                 build->resource_call, problem);
        say(line);
        return 0;
    }
    return 1;
}

#ifdef EXIT_GAME_TEST
// Test build only: stand-ins for the game's functions (the tests run on a
// mapped copy of the game executable, whose code cannot be run).
__declspec(dllexport) void ExitGameTestNatives(void *allocate, void *free_memory, void *object_init,
                                               void *object_cleanup, void *delete_object, void *bind_button) {
    test_natives.allocate = (void *(*)(size_t))allocate;
    test_natives.free_memory = (void (*)(void *))free_memory;
    test_natives.object_init = (void (*)(void *))object_init;
    test_natives.object_cleanup = (void (*)(void *))object_cleanup;
    test_natives.delete_object = (void (*)(void *))delete_object;
    test_natives.bind_button = (void (*)(void *, const char *, const MemberHandler *))bind_button;
    use_test_natives = 1;
}

__declspec(dllexport) const char *ExitGameTestCheck(const unsigned char *game, uint32_t size, int which) {
    const char *init = NULL, *resource = NULL;
    const char *problem = hooks_check(game, size, &init, &resource);
    return problem ? problem : which ? resource : init;
}

__declspec(dllexport) void *ExitGameTestHandlers(int which) {
    return which == 0 ? (void *)(uintptr_t)pause_init
         : which == 1 ? (void *)(uintptr_t)read_resource
                      : (void *)(uintptr_t)choose_exit_game;
}
#endif
