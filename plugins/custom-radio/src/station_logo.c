// Custom Radio for Forza Horizon 2 Extra Options: the station's logo on the HUD.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "station_logo.h"

#include "hooks.h"
#include "mp3.h"  // TEXT_CAPACITY
#include "radio.h"
#include "texture.h"

/* The HUD's radio display (Controls/HUDRadioControl.xaml, a UserControl)
   shows a station's logo through the style A_Style_FZMaterial_StationIcon: a
   196x96 FZMaterial whose ImageSource is set by one DataTrigger per stock
   station name ("Horizon XS" -> /ForzaUI;component/Textures/Anthem/HUD/
   HUD_RadioIcon_XS.png, read as media\UI\Textures\Anthem\HUD\
   HUD_RadioIcon_XS.swatchbin). A station whose name has no trigger gets no
   image and the material is drawn plain white: the white box.

   The control's XAML is read through UIRenderer's XAML load (vtable slot
   0x1d8), the same call (0x16ebb37) the pause and options pages go through,
   which Extra Options 0.4.4 and other plugins chain. This plugin chains it too
   and, for HUDRadioControl only, adds a DataTrigger per name the custom
   station can have, pointing at the plugin's logo in its media folder (which
   Extra Options mounts like the mod's own media). The text comes back in a new
   resource object, as Extra Options does for its own edits. Nothing else in
   the file changes; the stock stations keep their triggers. */

enum {
    MAX_XAML = 1 << 20,
    CLASS_WINDOW = 4096,
    MAX_ENTRIES = RADIO_NAME_LANGUAGES,
    TEXTURE_NAME_CAPACITY = 64
};

typedef struct {
    char name[TEXT_CAPACITY * 2];  // as the trigger's Value (XAML-escaped)
    char texture[TEXTURE_NAME_CAPACITY];  // empty: the icon is hidden (no texture to show)
} Entry;

static Entry entries[MAX_ENTRIES];
static unsigned entry_count;
static wchar_t media[PATH_CAPACITY];
static int media_ready;
static int stock_names;

/* Fast & Furious draws a texture whose name is not in its Anthem.zip as a
   white box, even with the file served (seen in game: its own XS logo copied
   to HUD_RadioIcon_CustomRadio_UK was white, our logo copied to
   HUD_RadioIcon_Off was drawn). Its HUD shows no stock station icon but Off,
   so there the logos take the names of FH2's station icons it ships unused. */
static const char *served_texture(const char *texture) {
    static const struct {
        const char *ours, *stock;
    } names[] = {{"HUD_RadioIcon_CustomRadio", "HUD_RadioIcon_Bass"},
                 {"HUD_RadioIcon_CustomRadio_PL", "HUD_RadioIcon_Hospital"},
                 {"HUD_RadioIcon_CustomRadio_UK", "HUD_RadioIcon_Innovative_Leisure"},
                 {"HUD_RadioIcon_CustomRadio_Badge", "HUD_RadioIcon_Levante"},
                 {"HUD_RadioIcon_CustomRadio_User", "HUD_RadioIcon_Ninja_Tune"}};
    for (unsigned i = 0; stock_names && i < ARRAY_COUNT(names); ++i) {
        if (!lstrcmpA(texture, names[i].ours)) {
            return names[i].stock;
        }
    }
    return texture;
}

// --- text -------------------------------------------------------------------------

static const char *find(const char *text, size_t length, const char *needle) {
    size_t count = text_length(needle);
    for (size_t at = 0; count && at + count <= length; ++at) {
        if (text[at] == needle[0] && memory_equal(text + at, needle, count)) {
            return text + at;
        }
    }
    return NULL;
}

typedef struct {
    char *data;
    size_t length, capacity;
    int failed;
} Text;

static void put(Text *out, const char *text, size_t length) {
    if (out->failed || out->length + length + 1 > out->capacity) {
        out->failed = 1;
        return;
    }
    memory_copy(out->data + out->length, text, length);
    out->length += length;
    out->data[out->length] = 0;
}

static void put_text(Text *out, const char *text) {
    put(out, text, text_length(text));
}

// A station name as a XAML attribute value: '&' as an entity (XAML is XML),
// and "{}" first when it starts with '{' (else it would be a markup extension).
static void xaml_value(const char *name, char *output, size_t capacity) {
    Text out = {output, 0, capacity, 0};
    output[0] = 0;
    if (name[0] == '{') {
        put_text(&out, "{}");
    }
    for (; *name; ++name) {
        if (*name == '&') {
            put_text(&out, "&amp;");
        } else {
            put(&out, name, 1);
        }
    }
    if (out.failed) {
        output[0] = 0;
    }
}

static void put_triggers(Text *out, const char *indent) {
    put_text(out, indent);
    put_text(out, LOGO_MARK "\r\n");
    for (unsigned i = 0; i < entry_count; ++i) {
        put_text(out, indent);
        put_text(out, "<DataTrigger Binding=\"{Binding StationName}\" Value=\"");
        put_text(out, entries[i].name);
        put_text(out, "\">\r\n");
        put_text(out, indent);
        if (!entries[i].texture[0]) {
            // As Fast & Furious hides the icon of its own stations: no white box.
            put_text(out, "    <Setter Property=\"Visibility\" Value=\"Hidden\" />\r\n");
        } else {
            put_text(out, "    <Setter Property=\"ImageSource\" Value=\"" LOGO_TEXTURE_URI);
            put_text(out, served_texture(entries[i].texture));
            put_text(out, ".png\" />\r\n");
        }
        put_text(out, indent);
        put_text(out, "</DataTrigger>\r\n");
    }
}

// The entry for the name the station was given in RadioInfo (the first one
// before that, or with a name no entry has); NULL when its icon is hidden.
static const Entry *shown_entry(void) {
    char name[TEXT_CAPACITY * 2];
    const Entry *shown = entry_count ? &entries[0] : NULL;
    const char *given = radio_given_name();
    if (given && given[0]) {
        xaml_value(given, name, sizeof(name));
        for (unsigned i = 0; i < entry_count; ++i) {
            if (!lstrcmpA(entries[i].name, name)) {
                shown = &entries[i];
            }
        }
    }
    return shown && shown->texture[0] ? shown : NULL;
}

size_t station_logo_edit(const char *xaml, size_t length, char *output, size_t capacity, const char **why) {
    static const char style_key[] = "x:Key=\"A_Style_FZMaterial_StationIcon\"";
    *why = NULL;
    if (!find(xaml, length < CLASS_WINDOW ? length : CLASS_WINDOW, "x:Class=\"ForzaUI.HUDRadioControl\"")) {
        return 0;
    }
    if (!entry_count) {
        *why = "no station logo is ready";
        return 0;
    }
    if (find(xaml, length, LOGO_MARK)) {
        *why = "it already has the logo";
        return 0;
    }
    const char *style = find(xaml, length, style_key);
    size_t rest = style ? length - (size_t)(style - xaml) : 0;
    if (!style || find(style + 1, rest - 1, style_key)) {
        *why = "the station icon style is not there once";
        return 0;
    }
    const char *end = find(style, rest, "</Style>");
    const char *triggers = find(style, end ? (size_t)(end - style) : 0, "<Style.Triggers>");
    if (!end || !triggers) {
        *why = "the station icon style has no triggers";
        return 0;
    }
    // After "<Style.Triggers>" and its line end; the new lines take the
    // indent of the line that follows (the first stock trigger's).
    const char *at = triggers + 16;
    if (at < end && *at == '\r') {
        ++at;
    }
    if (at < end && *at == '\n') {
        ++at;
    }
    char indent[64];
    size_t spaces = 0;
    while (at + spaces < end && (at[spaces] == ' ' || at[spaces] == '\t') && spaces + 1 < sizeof(indent)) {
        indent[spaces] = at[spaces];
        ++spaces;
    }
    indent[spaces] = 0;
    // The style's own image, on a line of its own before "<Style.Triggers>":
    // the game loads an icon's texture when the HUD is made, and one first
    // asked for later by a trigger stays a white box (seen in game on Fast &
    // Furious). So the logo of the name the station has is the default image;
    // every stock station keeps its own trigger.
    const char *line = triggers;
    while (line > style && (line[-1] == ' ' || line[-1] == '\t')) {
        --line;
    }
    const Entry *shown = shown_entry();
    Text out = {output, 0, capacity, 0};
    put(&out, xaml, (size_t)(triggers - xaml));
    if (shown) {
        put_text(&out, "<Setter Property=\"ImageSource\" Value=\"" LOGO_TEXTURE_URI);
        put_text(&out, served_texture(shown->texture));
        put_text(&out, ".png\" />\r\n");
        put(&out, line, (size_t)(triggers - line));
    }
    put(&out, triggers, (size_t)(at - triggers));
    put_triggers(&out, indent);
    put(&out, at, length - (size_t)(at - xaml));
    if (out.failed) {
        *why = "the edited text does not fit";
        return 0;
    }
    return out.length;
}

// --- the logos ----------------------------------------------------------------------

static int path_join(wchar_t *output, const wchar_t *a, const wchar_t *b) {
    output[0] = 0;
    return wide_append(output, PATH_CAPACITY, a) && wide_append(output, PATH_CAPACITY, L"\\") &&
           wide_append(output, PATH_CAPACITY, b);
}

static int texture_file(wchar_t *output, const char *texture) {
    static wchar_t folder[PATH_CAPACITY];
    wchar_t name[TEXTURE_NAME_CAPACITY + 16];
    if (!MultiByteToWideChar(CP_UTF8, 0, texture, -1, name, TEXTURE_NAME_CAPACITY)) {
        return 0;
    }
    return path_join(folder, media, LOGO_TEXTURE_FOLDER) && wide_append(name, ARRAY_COUNT(name), L".swatchbin") &&
           path_join(output, folder, name);
}

static int texture_path(wchar_t *output, const char *texture) {
    return texture_file(output, served_texture(texture));
}

static int is_file(const wchar_t *path) {
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

// A logo the package has under its own name and no PNG made under the stock
// name: copied there, so Fast & Furious finds it.
static void serve_packaged(const char *texture) {
    static wchar_t packaged[PATH_CAPACITY], served[PATH_CAPACITY];
    if (served_texture(texture) != texture && texture_file(packaged, texture) && texture_path(served, texture) &&
        !is_file(served) && is_file(packaged)) {
        CopyFileW(packaged, served, TRUE);
    }
}

// Writes `data` unless the file already holds exactly it (it may be in use by a running game later).
static int write_if_changed(const wchar_t *path, const unsigned char *data, DWORD size) {
    static unsigned char current[LOGO_SWATCH_SIZE];
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD got = 0, extra = 0;
        unsigned char more;
        int same = size <= sizeof(current) && ReadFile(file, current, size, &got, NULL) && got == size &&
                   ReadFile(file, &more, 1, &extra, NULL) && !extra && memory_equal(current, data, size);
        CloseHandle(file);
        if (same) {
            return 1;
        }
    }
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }
    DWORD written = 0;
    int ok = WriteFile(file, data, size, &written, NULL) && written == size;
    CloseHandle(file);
    if (!ok) {
        DeleteFileW(path);
    }
    return ok;
}

static void to_utf8(const wchar_t *text, char *output, int capacity) {
    if (WideCharToMultiByte(CP_UTF8, 0, text, -1, output, capacity, NULL, NULL) <= 0) {
        output[0] = 0;
    }
}

static void put_decimal(Text *out, unsigned value) {
    char digits[12];
    char *at = digits + sizeof(digits);
    do {
        *--at = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    put(out, at, (size_t)(digits + sizeof(digits) - at));
}

// Logo=: the PNG made into the user texture. 1 when it is ready.
static int prepare_user_logo(const wchar_t *plugins_directory, const wchar_t *setting) {
    static unsigned char texture[LOGO_SWATCH_SIZE];
    static wchar_t joined[PATH_CAPACITY], path[PATH_CAPACITY], target[PATH_CAPACITY];
    static char value[PATH_CAPACITY * 3], line[PATH_CAPACITY * 3 + 64];
    unsigned width = 0, height = 0;
    const char *why = NULL;
    to_utf8(setting, value, sizeof(value));
    int absolute = (setting[0] && setting[1] == L':') || setting[0] == L'\\' || setting[0] == L'/';
    if (absolute) {
        joined[0] = 0;
        wide_append(joined, PATH_CAPACITY, setting);
    } else if (!path_join(joined, plugins_directory, setting)) {
        joined[0] = 0;
    }
    DWORD length = joined[0] ? GetFullPathNameW(joined, PATH_CAPACITY, path, NULL) : 0;
    if (!length || length >= PATH_CAPACITY) {
        why = "the path is not valid";
    } else if (!texture_path(target, "HUD_RadioIcon_CustomRadio_User")) {
        why = "the media folder path is too long";
    } else {
        why = texture_from_png(path, texture, &width, &height);
        if (!why && !write_if_changed(target, texture, LOGO_SWATCH_SIZE)) {
            why = "its texture could not be written into the plugin's media folder";
        }
    }
    Text out = {line, 0, sizeof(line), 0};
    put_text(&out, "CUSTOM RADIO: Logo=");
    put_text(&out, value);
    if (why) {
        put_text(&out, " is not used: ");
        put_text(&out, why);
        put_text(&out, "; the built-in logo is shown.\r\n");
    } else {
        put_text(&out, ": the station shows this logo (");
        put_decimal(&out, width);
        put_text(&out, "x");
        put_decimal(&out, height);
        put_text(&out, width == LOGO_WIDTH && height == LOGO_HEIGHT
                           ? " pixels, as it is, with the game's drop shadow).\r\n"
                           : " pixels, fitted into the art area 188x87, with the game's drop shadow).\r\n");
    }
    log_append(out.failed ? (why ? "CUSTOM RADIO: Logo= is not used; the built-in logo is shown.\r\n"
                                 : "CUSTOM RADIO: Logo= is used.\r\n")
                          : line);
    return !why;
}

// `texture` NULL: the icon is hidden for the name.
static void add_entry(const char *name, const char *texture) {
    static wchar_t path[PATH_CAPACITY];
    Entry *entry = &entries[entry_count];
    if (entry_count >= MAX_ENTRIES || !name[0]) {
        return;
    }
    if (texture && (!media_ready || !texture_path(path, texture) || !is_file(path))) {
        log_append_parts("CUSTOM RADIO: the logo ", texture,
                         ".swatchbin is missing from the media folder; the station's icon is hidden (no white box).",
                         NULL);
        texture = NULL;
    }
    xaml_value(name, entry->name, sizeof(entry->name));
    if (texture) {
        memory_copy(entry->texture, texture, text_length(texture) + 1);
    } else {
        entry->texture[0] = 0;
    }
    if (entry->name[0]) {
        ++entry_count;
    }
}

// The built-in logos come from PNG files beside the plugin (Logo.png,
// Logo_PL.png, Logo_UK.png, Logo_Badge.png): each is made into its texture in
// the media folder (written only when it changed), so a replaced PNG is the
// new built-in logo. A PNG that is missing or refused leaves the texture the
// package has.
static void prepare_default_logos(const wchar_t *plugins_directory) {
    static const struct {
        const wchar_t *png;
        const char *texture;
    } logos[] = {{L"Logo.png", "HUD_RadioIcon_CustomRadio"},
                 {L"Logo_PL.png", "HUD_RadioIcon_CustomRadio_PL"},
                 {L"Logo_UK.png", "HUD_RadioIcon_CustomRadio_UK"},
                 {L"Logo_Badge.png", "HUD_RadioIcon_CustomRadio_Badge"}};
    static unsigned char texture[LOGO_SWATCH_SIZE];
    static wchar_t png[PATH_CAPACITY], target[PATH_CAPACITY];
    char name[64];
    for (unsigned i = 0; i < ARRAY_COUNT(logos); ++i) {
        unsigned width = 0, height = 0;
        if (!plugins_directory || !path_join(png, plugins_directory, logos[i].png) || !is_file(png) ||
            !texture_path(target, logos[i].texture)) {
            continue;
        }
        to_utf8(logos[i].png, name, sizeof(name));
        const char *why = texture_from_png(png, texture, &width, &height);
        if (!why && !write_if_changed(target, texture, LOGO_SWATCH_SIZE)) {
            why = "its texture could not be written into the media folder";
        }
        if (why) {
            log_append_parts("CUSTOM RADIO: ", name, " is not used: ", why);
        } else {
            log_append_parts("CUSTOM RADIO: ", name, " -> ", logos[i].texture);
        }
    }
}

void station_logo_prepare(const wchar_t *plugins_directory, const wchar_t *media_folder, const RadioConfig *config) {
    static wchar_t user[PATH_CAPACITY];
    char name[TEXT_CAPACITY];
    entry_count = 0;
    const RadioGame *game = hooks_game();
    stock_names = game && game->fast_furious;
    media_ready = 0;
    DWORD attributes = INVALID_FILE_ATTRIBUTES;
    media[0] = 0;
    if (media_folder && wide_append(media, PATH_CAPACITY, media_folder)) {
        attributes = GetFileAttributesW(media);
    }
    static const char *const default_logos[RADIO_NAME_LANGUAGES] = {
        "HUD_RadioIcon_CustomRadio", "HUD_RadioIcon_CustomRadio_PL", "HUD_RadioIcon_CustomRadio_UK"};
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        if (media_folder) {
            log_append_wide("CUSTOM RADIO: the plugin's media folder is missing (the station's icon is hidden): ",
                            media);
        } else {
            log_append("CUSTOM RADIO: no media folder is served (the station's icon is hidden).\r\n");
        }
        media[0] = 0;
    } else {
        media_ready = 1;
        prepare_default_logos(plugins_directory);
        for (unsigned i = 0; i < RADIO_NAME_LANGUAGES; ++i) {
            serve_packaged(default_logos[i]);
        }
        serve_packaged("HUD_RadioIcon_CustomRadio_Badge");
    }
    int custom = media_ready && config->logo[0] && prepare_user_logo(plugins_directory, config->logo);
    if (media_ready && !custom && texture_path(user, "HUD_RadioIcon_CustomRadio_User")) {
        DeleteFileW(user);  // a logo from an earlier Logo=, no longer used
    }
    // The names the station can have: Name=, else the three defaults (the
    // game's text language picks one when RadioInfo is read).
    if (config->name[0]) {
        const char *logo = "HUD_RadioIcon_CustomRadio_Badge";
        char known[TEXT_CAPACITY];
        radio_station_name(config->name, 0, name, sizeof(name));
        for (int language = 0; language < RADIO_NAME_LANGUAGES; ++language) {
            radio_station_name(NULL, language, known, sizeof(known));
            if (!lstrcmpA(name, known)) {
                logo = default_logos[language];
            }
        }
        add_entry(name, custom ? "HUD_RadioIcon_CustomRadio_User" : logo);
    } else {
        for (int language = 0; language < RADIO_NAME_LANGUAGES; ++language) {
            radio_station_name(NULL, language, name, sizeof(name));
            add_entry(name, custom ? "HUD_RadioIcon_CustomRadio_User" : default_logos[language]);
        }
    }
    for (unsigned i = 0; i < entry_count; ++i) {
        const char *served = served_texture(entries[i].texture);
        log_append_parts("CUSTOM RADIO: station logo ", served, served == entries[i].texture ? " for the name "
                         : " (a stock icon name Fast & Furious leaves unused) for the name ", entries[i].name);
    }
}

const wchar_t *station_logo_media_directory(void) {
    return media_ready ? media : NULL;
}

// --- the XAML resource hook -----------------------------------------------------------

typedef struct {
    void **vtable;
    unsigned char header[0x20];
    char *bytes;
    int length;
    int capacity;
} NativeResource;
_Static_assert(offsetof(NativeResource, bytes) == 0x28, "native resource buffer");

typedef void **(*ReadResourceFn)(void *reader, void **output);
typedef struct {
    void *(*allocate)(size_t);
    void (*free_memory)(void *);
    void (*object_init)(void *);
    void (*object_cleanup)(void *);
    void (*delete_object)(void *);
} Natives;

static Natives natives;
static ReadResourceFn previous_reader;
static void *resource_vtable[7];
static volatile LONG told_added, told_layout, told_memory;
#ifdef CUSTOM_RADIO_TEST
static Natives test_natives;
static int use_test_natives;
#endif

static void tell_once(volatile LONG *flag, const char *line) {
    if (!InterlockedExchange(flag, 1)) {
        log_append(line);
    }
}

static void release_object(void *object) {
    if (object && InterlockedDecrement((volatile LONG *)((unsigned char *)object + 0x10)) == 0) {
        natives.delete_object(object);
    }
}

static void *delete_resource(NativeResource *resource, unsigned flags) {
    natives.object_cleanup(resource);
    if (flags & 1) {
        natives.free_memory(resource);
    }
    return resource;
}

static void **read_resource(void *reader, void **output) {
    previous_reader(reader, output);
    NativeResource *original = output ? (NativeResource *)*output : NULL;
    if (!original || !original->bytes || original->length <= 0 || original->length > MAX_XAML ||
        !find(original->bytes, original->length < CLASS_WINDOW ? (size_t)original->length : CLASS_WINDOW,
              "x:Class=\"ForzaUI.HUDRadioControl\"")) {
        return output;
    }
    size_t capacity = (size_t)original->length + 1024 + entry_count * (sizeof(entries[0]) + 256);
    NativeResource *replacement = natives.allocate(sizeof(NativeResource) + capacity);
    if (!replacement) {
        tell_once(&told_memory, "CUSTOM RADIO: no memory for the HUD radio display with the station's logo.\r\n");
        return output;
    }
    char *bytes = (char *)(replacement + 1);
    const char *why = NULL;
    size_t length = station_logo_edit(original->bytes, (size_t)original->length, bytes, capacity, &why);
    if (!length) {
        natives.free_memory(replacement);
        // A control that already has the logo (read through this hook twice) keeps it.
        if (why && lstrcmpA(why, "it already has the logo") && !InterlockedExchange(&told_layout, 1)) {
            log_append_parts("CUSTOM RADIO: the HUD radio display is not as expected (", why,
                             "); the station's logo is not added.", NULL);
        }
        return output;
    }
    // As Extra Options does: the game's resource object with this buffer,
    // owned by the parser through the native reference count.
    natives.object_init(replacement);
    memory_copy(resource_vtable, original->vtable - 1, sizeof(resource_vtable));
    resource_vtable[1] = (void *)(uintptr_t)delete_resource;  // vtable slot 0, after the type information
    replacement->vtable = resource_vtable + 1;
    replacement->bytes = bytes;
    replacement->length = (int)length;
    replacement->capacity = (int)capacity;
    *output = replacement;
    release_object(original);
    tell_once(&told_added, "CUSTOM RADIO: the station's logo is added to the HUD radio display.\r\n");
    return output;
}

static void hex_address(const void *address, char *output) {
    static const char digits[] = "0123456789abcdef";
    uintptr_t value = (uintptr_t)address;
    int shift = 60;
    while (shift > 0 && !((value >> shift) & 15)) {
        shift -= 4;
    }
    *output++ = '0';
    *output++ = 'x';
    for (; shift >= 0; shift -= 4) {
        *output++ = digits[(value >> shift) & 15];
    }
    *output = 0;
}

int station_logo_install(void) {
    static const unsigned used[] = {FN_GAME_ALLOC, FN_GAME_FREE, FN_OBJECT_INIT, FN_OBJECT_CLEANUP,
                                    FN_DELETE_OBJECT};
    Hook hook;
    Hook *group[1] = {&hook};
    if (!entry_count) {
        return 0;
    }
    for (unsigned i = 0; i < ARRAY_COUNT(used); ++i) {
        if (!hooks_function_ok(used[i])) {
            log_append_parts("CUSTOM RADIO: ", hooks_function_name(used[i]),
                             ": the game function's first bytes are not as expected; no station logo.", NULL);
            return 0;
        }
    }
    if (!hooks_prepare_call(&hook, CALL_XAML_RESOURCE)) {
        log_append("CUSTOM RADIO: the XAML resource call did not match; no station logo on the HUD.\r\n");
        return 0;
    }
    natives.allocate = (void *(*)(size_t))hooks_function(FN_GAME_ALLOC);
    natives.free_memory = (void (*)(void *))hooks_function(FN_GAME_FREE);
    natives.object_init = (void (*)(void *))hooks_function(FN_OBJECT_INIT);
    natives.object_cleanup = (void (*)(void *))hooks_function(FN_OBJECT_CLEANUP);
    natives.delete_object = (void (*)(void *))hooks_function(FN_DELETE_OBJECT);
#ifdef CUSTOM_RADIO_TEST
    if (use_test_natives) {
        natives = test_natives;
    }
#endif
    unsigned char *image = hooks_image();
    char onto[96];
    int ff = hooks_game()->fast_furious;
    if (hook.current == hooks_function(FN_GET_RESOURCE)) {
        if (ff) {
            memory_copy(onto, "the game's resource reader 0xc795c0", 36);
        } else {
            memory_copy(onto, "the game's resource reader 0xaabec0", 36);
        }
    } else if (hooks_game()->extra_options_thunk && hook.current == image + hooks_game()->extra_options_thunk) {
        memory_copy(onto, "Extra Options' resource thunk 0x13fe354", 40);
    } else {
        memory_copy(onto, "another module's hook at ", 26);
        hex_address(hook.current, onto + 25);
    }
    previous_reader = (ReadResourceFn)hook.current;
    hook.replacement = (void *)(uintptr_t)read_resource;
    if (!hooks_commit(group, 1)) {
        log_append("CUSTOM RADIO: the XAML resource call could not be hooked; no station logo on the HUD.\r\n");
        return 0;
    }
    log_append_parts(ff ? "CUSTOM RADIO: XAML resource call 0x1712d47 chained onto "
                        : "CUSTOM RADIO: XAML resource call 0x16ebb37 chained onto ",
                     onto, NULL, NULL);
    return 1;
}

#ifdef CUSTOM_RADIO_TEST
// Test build only: stand-ins for the game's functions (a fake image's code cannot run).
__declspec(dllexport) void CustomRadioTestLogoNatives(void *allocate, void *free_memory, void *object_init,
                                                     void *object_cleanup, void *delete_object) {
    test_natives.allocate = (void *(*)(size_t))allocate;
    test_natives.free_memory = (void (*)(void *))free_memory;
    test_natives.object_init = (void (*)(void *))object_init;
    test_natives.object_cleanup = (void (*)(void *))object_cleanup;
    test_natives.delete_object = (void (*)(void *))delete_object;
    use_test_natives = 1;
    natives = test_natives;
}
// The XAML edit with the entries prepared by FH2PluginInitialize; its length, 0 with `why`.
__declspec(dllexport) unsigned CustomRadioTestLogoEdit(const char *xaml, unsigned length, char *output,
                                                      unsigned capacity, const char **why) {
    return (unsigned)station_logo_edit(xaml, length, output, capacity, why);
}
// A PNG to the logo texture (LOGO_SWATCH_SIZE bytes); NULL or why it is refused.
__declspec(dllexport) const char *CustomRadioTestLogoFromPng(const wchar_t *path, unsigned char *output,
                                                            unsigned *width, unsigned *height) {
    return texture_from_png(path, output, width, height);
}
__declspec(dllexport) void CustomRadioTestLogoEncode(const unsigned char *pixels, unsigned char *output) {
    texture_encode(pixels, output);
}
__declspec(dllexport) const wchar_t *CustomRadioTestLogoMedia(void) {
    return station_logo_media_directory();
}
#endif
