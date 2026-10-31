// Custom Radio: FLAC, WAV, M4A (AAC/ALAC) and WMA files through Windows' Media Foundation.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Windows decodes them (Media Foundation's source reader: FLAC since Windows
// 10 1709, ALAC since 10 1809, AAC, WMA and WAV always), to 16-bit PCM that
// FMOD plays from memory as a WAV, like MP3s with PcmDecode=1. mfplat,
// mfreadwrite and ole32 are loaded when the first such file is met, so the DLL
// still imports only kernel32, user32 and msvcrt. The interfaces come from the
// MinGW headers (COBJMACROS); the GUIDs are defined here (no mfuuid.lib).
// Frames stay small (no C runtime, so no __chkstk).
#define COBJMACROS
#include "media.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propsys.h>

enum {
    MF_VERSION_VALUE = 0x00020070,     // MF_SDK_VERSION 2 << 16 | MF_API_VERSION 0x70
    MF_STARTUP_LITE = 1,               // MFSTARTUP_NOSOCKET
    FIRST_AUDIO = 0xfffffffd,          // MF_SOURCE_READER_FIRST_AUDIO_STREAM
    ALL_STREAMS = 0xfffffffe,          // MF_SOURCE_READER_ALL_STREAMS
    MEDIA_SOURCE = 0xffffffff,         // MF_SOURCE_READER_MEDIASOURCE
    END_OF_STREAM = 2,                 // MF_SOURCE_READERF_ENDOFSTREAM
    MAX_RATE = 48000,
    MAX_PCM = 0x7fff0000,
    MAX_COMMENTS = 1 << 20
};

static const GUID major_type = {0x48eba18e, 0xf8c9, 0x4687, {0xbf, 0x11, 0x0a, 0x74, 0xc9, 0xf9, 0x6a, 0x8f}};
static const GUID subtype = {0xf7e34c9a, 0x42e8, 0x4714, {0xb7, 0x4b, 0xcb, 0x29, 0xd7, 0x2c, 0x35, 0xe5}};
static const GUID audio_major = {0x73647561, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
static const GUID audio_pcm = {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
static const GUID audio_channels = {0x37e48bf5, 0x645e, 0x4c5b, {0x89, 0xde, 0xad, 0xa9, 0xe2, 0x9b, 0x69, 0x6a}};
static const GUID audio_rate = {0x5faeeae7, 0x0290, 0x4c31, {0x9e, 0x8a, 0xc5, 0x34, 0xf6, 0x8d, 0x9d, 0xba}};
static const GUID audio_bits = {0xf2deb57f, 0x40fa, 0x4764, {0xaa, 0x33, 0xed, 0x4f, 0x2d, 0x1f, 0xf6, 0x69}};
static const GUID audio_align = {0x322de230, 0x9eeb, 0x43bd, {0xab, 0x7a, 0xff, 0x41, 0x22, 0x51, 0x54, 0x1d}};
static const GUID audio_bytes = {0x1aab75c8, 0xcfef, 0x451c, {0xab, 0x95, 0xac, 0x03, 0x4b, 0x8e, 0x17, 0x31}};
static const GUID independent = {0xc9173739, 0x5e56, 0x461c, {0xb7, 0x13, 0x46, 0xfb, 0x99, 0x5c, 0xb9, 0x5f}};
static const GUID duration_key = {0x6c990d33, 0xbb8e, 0x477a, {0x85, 0x98, 0x0d, 0x5d, 0x96, 0xfc, 0xd8, 0x8a}};
static const GUID property_service = {0xa3face02, 0x32b8, 0x41dd, {0x90, 0xe7, 0x5f, 0xef, 0x7c, 0x89, 0x91, 0xb5}};
static const GUID property_store = {0x886d8eeb, 0x8cf2, 0x4446, {0x8d, 0x02, 0xcd, 0xba, 0x1d, 0xbd, 0xcf, 0x99}};
static const PROPERTYKEY title_key = {{0xf29f85e0, 0x4ff9, 0x1068, {0xab, 0x91, 0x08, 0x00, 0x2b, 0x27, 0xb3, 0xd9}}, 2};
static const PROPERTYKEY artist_key = {{0x56a3372e, 0xce9c, 0x11d2, {0x9f, 0x0e, 0x00, 0x60, 0x97, 0xc6, 0x86, 0xf6}}, 2};

typedef HRESULT(WINAPI *StartupFn)(ULONG, DWORD);
typedef HRESULT(WINAPI *CreateTypeFn)(IMFMediaType **);
typedef HRESULT(WINAPI *CreateReaderFn)(const WCHAR *, IMFAttributes *, IMFSourceReader **);
typedef HRESULT(WINAPI *CoInitializeFn)(void *, DWORD);
typedef void(WINAPI *CoUninitializeFn)(void);
typedef HRESULT(WINAPI *ClearFn)(PROPVARIANT *);

static CreateTypeFn create_type;
static CreateReaderFn create_reader;
static CoInitializeFn co_initialize;
static CoUninitializeFn co_uninitialize;
static ClearFn clear_variant;
static INIT_ONCE once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK load(INIT_ONCE *init, void *parameter, void **context) {
    (void)init;
    (void)parameter;
    (void)context;
    HMODULE platform = LoadLibraryExW(L"mfplat.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    HMODULE readwrite = LoadLibraryExW(L"mfreadwrite.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    HMODULE ole = LoadLibraryExW(L"ole32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    StartupFn startup = platform ? (StartupFn)(uintptr_t)GetProcAddress(platform, "MFStartup") : NULL;
    create_type = platform ? (CreateTypeFn)(uintptr_t)GetProcAddress(platform, "MFCreateMediaType") : NULL;
    create_reader = readwrite ? (CreateReaderFn)(uintptr_t)GetProcAddress(readwrite, "MFCreateSourceReaderFromURL")
                              : NULL;
    co_initialize = ole ? (CoInitializeFn)(uintptr_t)GetProcAddress(ole, "CoInitializeEx") : NULL;
    co_uninitialize = ole ? (CoUninitializeFn)(uintptr_t)GetProcAddress(ole, "CoUninitialize") : NULL;
    clear_variant = ole ? (ClearFn)(uintptr_t)GetProcAddress(ole, "PropVariantClear") : NULL;
    if (!startup || !create_type || !create_reader || !co_initialize || !co_uninitialize || !clear_variant ||
        FAILED(startup(MF_VERSION_VALUE, MF_STARTUP_LITE))) {
        create_reader = NULL;
        log_append("CUSTOM RADIO: Windows Media Foundation is not available; FLAC, WAV, M4A and WMA files are "
                   "skipped.\r\n");
    }
    return TRUE;
}

int media_available(void) {
    InitOnceExecuteOnce(&once, load, NULL, NULL);
    return create_reader != NULL;
}

static int ends_with(const wchar_t *name, const wchar_t *suffix) {
    size_t a = wide_length(name), b = wide_length(suffix);
    return a > b && wide_equal_ignore_case(name + a - b, suffix);
}

int media_kind(const wchar_t *name) {
    if (ends_with(name, L".mp3")) {
        return MEDIA_MP3;
    }
    if (ends_with(name, L".flac") || ends_with(name, L".wav") || ends_with(name, L".m4a") ||
        ends_with(name, L".wma")) {
        return MEDIA_FOUNDATION;
    }
    return MEDIA_NONE;
}

// --- COM on this thread ------------------------------------------------------------

// 1: CoUninitialize when done (this call initialised COM on the thread).
static int com_enter(void) {
    HRESULT result = co_initialize(NULL, COINIT_MULTITHREADED);
    return result == S_OK || result == S_FALSE;
}

static void com_leave(int entered) {
    if (entered) {
        co_uninitialize();
    }
}

// --- text -------------------------------------------------------------------------

// A UTF-8 value into a Track field (TEXT_CAPACITY), cut at a character boundary,
// trailing spaces dropped.
static void put_utf8(char *output, const char *text, size_t length) {
    if (length >= TEXT_CAPACITY) {
        length = TEXT_CAPACITY - 1;
        while (length && ((unsigned char)text[length] & 0xc0) == 0x80) {
            --length;
        }
    }
    memory_copy(output, text, length);
    while (length && (output[length - 1] == ' ' || output[length - 1] == '\t')) {
        --length;
    }
    output[length] = 0;
}

static void put_wide(char *output, const wchar_t *text) {
    char utf8[TEXT_CAPACITY * 2];
    int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof(utf8), NULL, NULL);
    if (length > 1) {
        put_utf8(output, utf8, (size_t)length - 1);
    }
}

static void title_from_name(Track *track) {
    if (track->title[0]) {
        return;
    }
    WideCharToMultiByte(CP_UTF8, 0, track->name, -1, track->title, TEXT_CAPACITY, NULL, NULL);
    size_t length = text_length(track->title);
    while (length && track->title[length - 1] != '.') {
        --length;
    }
    if (length > 1) {
        track->title[length - 1] = 0;
    }
}

// --- FLAC metadata -----------------------------------------------------------------

static uint32_t little32(const unsigned char *bytes) {
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static int key_is(const char *entry, uint32_t length, const char *key) {
    size_t count = text_length(key);
    if (length <= count || entry[count] != '=') {
        return 0;
    }
    for (size_t i = 0; i < count; ++i) {
        char character = entry[i] >= 'a' && entry[i] <= 'z' ? (char)(entry[i] - 'a' + 'A') : entry[i];
        if (character != key[i]) {
            return 0;
        }
    }
    return 1;
}

// VORBIS_COMMENT: vendor, then "KEY=value" entries (little-endian lengths).
static void read_comments(const unsigned char *data, uint32_t size, Track *track) {
    if (size < 8) {
        return;
    }
    uint32_t at = 4 + little32(data);
    if (at < 4 || at + 4 > size) {
        return;
    }
    uint32_t count = little32(data + at);
    at += 4;
    for (uint32_t i = 0; i < count && at + 4 <= size; ++i) {
        uint32_t length = little32(data + at);
        at += 4;
        if (length > size - at) {
            break;
        }
        const char *entry = (const char *)data + at;
        if (!track->title[0] && key_is(entry, length, "TITLE")) {
            put_utf8(track->title, entry + 6, length - 6);
        } else if (!track->artist[0] && key_is(entry, length, "ARTIST")) {
            put_utf8(track->artist, entry + 7, length - 7);
        }
        at += length;
    }
}

static int seek(HANDLE file, int64_t offset, DWORD from) {
    LARGE_INTEGER position;
    position.QuadPart = offset;
    return SetFilePointerEx(file, position, NULL, from);
}

static int read_exact(HANDLE file, void *output, DWORD size) {
    DWORD read = 0;
    return ReadFile(file, output, size, &read, NULL) && read == size;
}

// 1 when the file is FLAC (its STREAMINFO read): length and tags filled.
static int describe_flac(const wchar_t *path, Track *track) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    unsigned char head[10];
    int flac = 0;
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }
    if (read_exact(file, head, 10) && head[0] == 'I' && head[1] == 'D' && head[2] == '3') {
        // An ID3v2 tag in front (some taggers write one): skipped.
        int64_t skip = (int64_t)((head[6] & 0x7f) << 21 | (head[7] & 0x7f) << 14 | (head[8] & 0x7f) << 7 |
                                 (head[9] & 0x7f)) + 10 + (head[5] & 0x10 ? 10 : 0);
        seek(file, skip, FILE_BEGIN);
    } else {
        seek(file, 0, FILE_BEGIN);
    }
    if (read_exact(file, head, 4) && head[0] == 'f' && head[1] == 'L' && head[2] == 'a' && head[3] == 'C') {
        for (int last = 0, blocks = 0; !last && blocks < 256; ++blocks) {
            unsigned char block[4];
            if (!read_exact(file, block, 4)) {
                break;
            }
            last = block[0] & 0x80;
            unsigned type = block[0] & 0x7f;
            uint32_t length = (uint32_t)block[1] << 16 | (uint32_t)block[2] << 8 | block[3];
            if (type == 0 && length >= 18) {
                unsigned char info[18];
                if (!read_exact(file, info, 18) || !seek(file, (int64_t)length - 18, FILE_CURRENT)) {
                    break;
                }
                uint64_t packed = 0;
                for (unsigned i = 10; i < 18; ++i) {
                    packed = packed << 8 | info[i];
                }
                uint64_t rate = packed >> 44, samples = packed & 0xfffffffffull;
                flac = rate != 0;
                if (rate && samples && samples / rate < 24 * 3600) {
                    track->duration = (unsigned)(samples * 1000 / rate);
                }
            } else if (type == 4 && length <= MAX_COMMENTS) {
                unsigned char *data = HeapAlloc(GetProcessHeap(), 0, length ? length : 1);
                if (!data || !read_exact(file, data, length)) {
                    if (data) {
                        HeapFree(GetProcessHeap(), 0, data);
                    }
                    break;
                }
                read_comments(data, length, track);
                HeapFree(GetProcessHeap(), 0, data);
            } else if (!seek(file, length, FILE_CURRENT)) {
                break;
            }
        }
    }
    CloseHandle(file);
    return flac;
}

// --- Media Foundation --------------------------------------------------------------

static void read_tag(IPropertyStore *store, const PROPERTYKEY *key, char *output) {
    PROPVARIANT value;
    memory_zero(&value, sizeof(value));
    if (output[0] || FAILED(IPropertyStore_GetValue(store, key, &value))) {
        return;
    }
    if (value.vt == VT_LPWSTR && value.pwszVal) {
        put_wide(output, value.pwszVal);
    } else if (value.vt == (VT_VECTOR | VT_LPWSTR) && value.calpwstr.cElems && value.calpwstr.pElems[0]) {
        put_wide(output, value.calpwstr.pElems[0]);
    }
    clear_variant(&value);
}

static unsigned reader_duration(IMFSourceReader *reader) {
    PROPVARIANT value;
    unsigned duration = 0;
    memory_zero(&value, sizeof(value));
    if (SUCCEEDED(IMFSourceReader_GetPresentationAttribute(reader, MEDIA_SOURCE, &duration_key, &value))) {
        if (value.vt == VT_UI8 && value.uhVal.QuadPart / 10000 < 24ull * 3600 * 1000) {
            duration = (unsigned)(value.uhVal.QuadPart / 10000);
        }
        clear_variant(&value);
    }
    return duration;
}

void media_describe(const wchar_t *path, Track *track) {
    track->title[0] = track->artist[0] = 0;
    track->duration = 0;
    int flac = ends_with(path, L".flac") && describe_flac(path, track);
    if (!flac && media_available()) {
        int entered = com_enter();
        IMFSourceReader *reader = NULL;
        if (SUCCEEDED(create_reader(path, NULL, &reader))) {
            IPropertyStore *store = NULL;
            track->duration = reader_duration(reader);
            if (SUCCEEDED(IMFSourceReader_GetServiceForStream(reader, MEDIA_SOURCE, &property_service,
                                                              &property_store, (void **)&store)) && store) {
                read_tag(store, &title_key, track->title);
                read_tag(store, &artist_key, track->artist);
                IPropertyStore_Release(store);
            }
            IMFSourceReader_Release(reader);
        }
        com_leave(entered);
    }
    title_from_name(track);
}

static void wav_header(unsigned char *out, unsigned rate, unsigned channels, uint32_t data) {
    static const unsigned char header[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ',
                                             16, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16, 0,
                                             'd', 'a', 't', 'a', 0, 0, 0, 0};
    uint32_t riff = data + 36, bytes = rate * channels * 2;
    uint16_t count = (uint16_t)channels, align = (uint16_t)(channels * 2);
    memory_copy(out, header, sizeof(header));
    memory_copy(out + 4, &riff, 4);
    memory_copy(out + 22, &count, 2);
    memory_copy(out + 24, &rate, 4);
    memory_copy(out + 28, &bytes, 4);
    memory_copy(out + 32, &align, 2);
    memory_copy(out + 40, &data, 4);
}

// The PCM output: 16-bit stereo at `rate`; else (a reader that cannot convert)
// 16-bit at the file's own rate and channels.
static int set_output(IMFSourceReader *reader, unsigned *rate, unsigned *channels) {
    IMFMediaType *native = NULL, *type = NULL, *current = NULL;
    UINT32 native_rate = 0, native_channels = 0;
    int ok = 0;
    IMFSourceReader_SetStreamSelection(reader, ALL_STREAMS, FALSE);
    IMFSourceReader_SetStreamSelection(reader, FIRST_AUDIO, TRUE);
    if (SUCCEEDED(IMFSourceReader_GetNativeMediaType(reader, FIRST_AUDIO, 0, &native))) {
        IMFMediaType_GetUINT32(native, &audio_rate, &native_rate);
        IMFMediaType_GetUINT32(native, &audio_channels, &native_channels);
        IMFMediaType_Release(native);
    }
    unsigned want = native_rate > MAX_RATE ? (native_rate % 44100 == 0 ? 44100 : MAX_RATE)
                                           : native_rate ? native_rate : 44100;
    if (SUCCEEDED(create_type(&type))) {
        IMFMediaType_SetGUID(type, &major_type, &audio_major);
        IMFMediaType_SetGUID(type, &subtype, &audio_pcm);
        IMFMediaType_SetUINT32(type, &audio_bits, 16);
        IMFMediaType_SetUINT32(type, &audio_channels, 2);
        IMFMediaType_SetUINT32(type, &audio_rate, want);
        IMFMediaType_SetUINT32(type, &audio_align, 4);
        IMFMediaType_SetUINT32(type, &audio_bytes, want * 4);
        IMFMediaType_SetUINT32(type, &independent, TRUE);
        ok = SUCCEEDED(IMFSourceReader_SetCurrentMediaType(reader, FIRST_AUDIO, NULL, type));
        IMFMediaType_Release(type);
        type = NULL;
    }
    if (!ok && SUCCEEDED(create_type(&type))) {
        IMFMediaType_SetGUID(type, &major_type, &audio_major);
        IMFMediaType_SetGUID(type, &subtype, &audio_pcm);
        IMFMediaType_SetUINT32(type, &audio_bits, 16);
        ok = SUCCEEDED(IMFSourceReader_SetCurrentMediaType(reader, FIRST_AUDIO, NULL, type));
        IMFMediaType_Release(type);
    }
    UINT32 bits = 0, got_rate = 0, got_channels = 0;
    if (ok && SUCCEEDED(IMFSourceReader_GetCurrentMediaType(reader, FIRST_AUDIO, &current))) {
        IMFMediaType_GetUINT32(current, &audio_bits, &bits);
        IMFMediaType_GetUINT32(current, &audio_rate, &got_rate);
        IMFMediaType_GetUINT32(current, &audio_channels, &got_channels);
        IMFMediaType_Release(current);
    }
    *rate = got_rate;
    *channels = got_channels;
    return ok && bits == 16 && got_rate >= 8000 && got_rate <= 192000 && got_channels >= 1 && got_channels <= 8;
}

static unsigned char *grow(unsigned char *data, size_t used, size_t *capacity, size_t need) {
    if (need <= *capacity) {
        return data;
    }
    size_t bigger = *capacity + *capacity / 2 > need ? *capacity + *capacity / 2 : need + (1 << 20);
    unsigned char *next = HeapAlloc(GetProcessHeap(), 0, bigger);
    if (next) {
        memory_copy(next, data, used);
        *capacity = bigger;
    }
    HeapFree(GetProcessHeap(), 0, data);
    return next;
}

static unsigned char *read_all(IMFSourceReader *reader, size_t capacity, size_t *used) {
    unsigned char *out = HeapAlloc(GetProcessHeap(), 0, capacity);
    *used = 44;
    while (out) {
        DWORD flags = 0;
        IMFSample *sample = NULL;
        if (FAILED(IMFSourceReader_ReadSample(reader, FIRST_AUDIO, 0, NULL, &flags, NULL, &sample))) {
            HeapFree(GetProcessHeap(), 0, out);
            return NULL;
        }
        if (sample) {
            IMFMediaBuffer *buffer = NULL;
            BYTE *bytes = NULL;
            DWORD length = 0;
            if (SUCCEEDED(IMFSample_ConvertToContiguousBuffer(sample, &buffer))) {
                if (SUCCEEDED(IMFMediaBuffer_Lock(buffer, &bytes, NULL, &length))) {
                    if (*used + length > MAX_PCM) {
                        length = 0;
                        flags |= END_OF_STREAM;  // longer than a WAV holds: the rest is cut
                    }
                    out = grow(out, *used, &capacity, *used + length);
                    if (out) {
                        memory_copy(out + *used, bytes, length);
                        *used += length;
                    }
                    IMFMediaBuffer_Unlock(buffer);
                }
                IMFMediaBuffer_Release(buffer);
            }
            IMFSample_Release(sample);
        }
        if (flags & END_OF_STREAM) {
            break;
        }
    }
    return out;
}

void *media_decode(const wchar_t *path, DWORD *output_size) {
    unsigned char *out = NULL;
    size_t used = 0;
    *output_size = 0;
    if (!media_available()) {
        return NULL;
    }
    int entered = com_enter();
    IMFSourceReader *reader = NULL;
    unsigned rate = 0, channels = 0;
    if (SUCCEEDED(create_reader(path, NULL, &reader))) {
        if (set_output(reader, &rate, &channels)) {
            uint64_t expected = (uint64_t)reader_duration(reader) * rate * channels * 2 / 1000 + (1 << 16) + 44;
            out = read_all(reader, expected < MAX_PCM ? (size_t)expected : MAX_PCM, &used);
        }
        IMFSourceReader_Release(reader);
    }
    com_leave(entered);
    if (out && used > 44) {
        wav_header(out, rate, channels, (uint32_t)(used - 44));
        *output_size = (DWORD)used;
        return out;
    }
    if (out) {
        HeapFree(GetProcessHeap(), 0, out);
    }
    log_append_wide("CUSTOM RADIO: Windows could not decode ", path);
    return NULL;
}
