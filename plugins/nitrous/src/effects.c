// Nitrous for Forza Horizon 2: the boost's camera and sound effects.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Forza Horizon 2 has none of Fast & Furious' nitrous presentation code (the
// camera's Nitrous inputs, NitrousSoundComponent). This file rebuilds them on
// the plugin's side from Fast & Furious' own data:
//   - the camera: Fast & Furious' follow-camera FOV layers (CameraPhysics.xml:
//     Nitrous +5 degrees, NitrousImpulse -5, their attack and decay) applied
//     through the driving cameras' FOV getter (vtable slot 0x70 of
//     CCamFollow/Low/High and CCamDriver/Hood/BumperHigh), chained after
//     whatever is there. Fast & Furious' camera shake and motion blur are not
//     rebuilt.
//   - the sound: Fast & Furious' NOS samples, decoded from the player's
//     install by tools/ff_sounds.py (XMA2 needs a decoder Windows lacks, so
//     not at run time), played through waveOut on a thread of our own: the
//     start, the sustain looped while boosting, the stop. Without them, our
//     synthesized burst and hiss. Silent while the game window has no focus
//     or the game takes no input (menus, pause).
// The gauge and the screen effect are drawn by the Performance plugin, from
// NitrousStatus (fh2_nitrous.h).
#include "effects.h"

#include <math.h>
#include <mmsystem.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// ---------------------------------------------------------------- settings

static float read_number(const wchar_t *ini, const wchar_t *key, float fallback, const char *name,
                         const char **warning) {
    wchar_t text[64];
    GetPrivateProfileStringW(L"Effects", key, L"", text, 64, ini);
    wchar_t *start = text;
    while (*start == L' ' || *start == L'\t') ++start;
    if (!*start) return fallback;
    wchar_t *end = NULL;
    float value = wcstof(start, &end);
    while (end && (*end == L' ' || *end == L'\t')) ++end;
    if (!end || *end || !(value == value)) {
        if (warning && !*warning) *warning = name;
        return fallback;
    }
    return value;
}

static float clamp(float value, float low, float high) { return value < low ? low : value > high ? high : value; }

float effects_number(const wchar_t *ini, const wchar_t *key, float fallback, float low, float high) {
    return clamp(read_number(ini, key, fallback, "", NULL), low, high);
}

void effects_read(const wchar_t *ini, NitrousEffects *effects, const char **warning) {
    if (warning) *warning = NULL;
    effects->fov = GetPrivateProfileIntW(L"Effects", L"Fov", 1, ini) != 0;
    effects->fov_degrees = clamp(read_number(ini, L"FovDegrees", 5.0f, "FovDegrees", warning), 0.0f, 30.0f);
    effects->impulse_degrees =
        clamp(read_number(ini, L"FovImpulseDegrees", 5.0f, "FovImpulseDegrees", warning), 0.0f, 30.0f);
    effects->cockpit_degrees =
        clamp(read_number(ini, L"FovCockpitDegrees", 0.0f, "FovCockpitDegrees", warning), 0.0f, 30.0f);
    effects->sound = GetPrivateProfileIntW(L"Effects", L"Sound", 1, ini) != 0;
    effects->flames = GetPrivateProfileIntW(L"Effects", L"ExhaustFlames", 1, ini) != 0;
    effects->camera_layers = GetPrivateProfileIntW(L"Effects", L"CameraLayers", 1, ini) != 0;
    effects->rumble = GetPrivateProfileIntW(L"Effects", L"Rumble", 1, ini) != 0;
    effects->sound_volume = clamp(read_number(ini, L"SoundVolume", 0.6f, "SoundVolume", warning), 0.0f, 1.0f);
    if (effects->fov_degrees <= 0.0f && effects->impulse_degrees <= 0.0f && effects->cockpit_degrees <= 0.0f)
        effects->fov = 0;
    if (effects->sound_volume <= 0.0f) effects->sound = 0;
}

// ---------------------------------------------------------------- camera

static float smooth(float x) {
    x = clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

void nitrous_camera_step(NitrousCamera *camera, int boosting, int started, float delta_seconds) {
    const float dt = delta_seconds > 0.0f && delta_seconds < 0.5f ? delta_seconds : 0.0f;
    // Both inputs rise with the same fast attack (so they cancel at first)
    // and fall at their decay rates.
    camera->pulse = started ? 1.0f : clamp(camera->pulse - 0.6f * dt, 0.0f, 1.0f);
    camera->impulse = camera->pulse > camera->impulse ? clamp(camera->impulse + dt / 0.05f, 0.0f, camera->pulse)
                                                      : camera->pulse;
    camera->nitrous = clamp(boosting ? camera->nitrous + dt / 0.05f : camera->nitrous - 0.5f * dt, 0.0f, 1.0f);
}

float nitrous_camera_degrees(const NitrousCamera *camera, const NitrousEffects *effects, int mounted) {
    if (!effects->fov) return 0.0f;
    if (mounted) return effects->cockpit_degrees * smooth(camera->nitrous);
    return effects->fov_degrees * smooth(camera->nitrous) - effects->impulse_degrees * smooth(camera->impulse);
}

float nitrous_fov_factor(float base_degrees, float kick_degrees) {
    if (!(base_degrees >= 10.0f && base_degrees <= 150.0f) || !(kick_degrees == kick_degrees) || kick_degrees == 0.0f)
        return 1.0f;
    float wanted = base_degrees + kick_degrees;
    if (wanted < 10.0f) wanted = 10.0f;
    return wanted / base_degrees;
}

typedef float (*GetFov)(void *camera);
static const unsigned fov_vtables[6] = {0x2e783a0u, 0x2e78610u, 0x2e78880u,   // CCamFollow, Low, High
                                        0x2e78120u, 0x2e77ea8u, 0x2e703b0u};  // CCamDriver, Hood, BumperHigh
static const unsigned char *fov_image;
static GetFov fov_previous[6];
static volatile float fov_chase_kick, fov_mounted_kick;

static float hooked_fov(void *camera) {
    void *vtable = *(void **)camera;
    unsigned index = 0;
    while (index < 5 && vtable != fov_image + fov_vtables[index]) ++index;
    GetFov previous = fov_previous[index];
    float result = previous ? previous(camera)
                            : ((GetFov)(fov_image + (index < 3 ? NITROUS_RVA_FOV_CHASE : NITROUS_RVA_FOV_MOUNTED)))(camera);
    const float kick = index < 3 ? fov_chase_kick : fov_mounted_kick;
    if (kick == 0.0f || !(result == result)) return result;
    float base = 0.0f;
    if (index < 3) {
        const unsigned char *settings = *(const unsigned char *const *)((const unsigned char *)camera + 0x180);
        if (settings) base = *(const float *)(settings + 0x1c);
    } else {
        base = *(const float *)((const unsigned char *)camera + 0x1d0) * 57.2957795f;
    }
    return result * nitrous_fov_factor(base, kick);
}

static int fov_install(const unsigned char *image, void (*say)(const char *format, ...)) {
    fov_image = image;
    int chained = 0;
    for (unsigned i = 0; i < 6; ++i) {
        void **slot = (void **)(image + fov_vtables[i] + NITROUS_FOV_SLOT);
        void *current = NULL;
        const unsigned char *native = image + (i < 3 ? NITROUS_RVA_FOV_CHASE : NITROUS_RVA_FOV_MOUNTED);
        if (!fh2_safe_read(slot, &current, sizeof(current))) continue;
        // The game's getter, or another module's hook (Extra Options' FOV option); never other game code.
        const int in_image = (const unsigned char *)current >= image &&
                             (const unsigned char *)current < image + NITROUS_FH2_IMAGE_SIZE;
        if (current == (void *)hooked_fov || (current != native && (in_image || !fh2_is_code(current)))) {
            if (say) say("camera FOV: vtable 0x%x slot 0x70 holds %p, not the game's getter or a hook; left alone",
                         fov_vtables[i], current);
            continue;
        }
        fov_previous[i] = (GetFov)current;
        if (fh2_chain_slot(slot, current, (void *)hooked_fov) == FH2_CHAINED) ++chained;
    }
    return chained;
}

// ---------------------------------------------------------------- Fast & Furious' samples

int nitrous_load_wav(const wchar_t *path, NitrousClip *clip) {
    clip->data = NULL;
    clip->frames = 0;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER size;
    unsigned char *bytes = NULL;
    DWORD got = 0;
    int ok = GetFileSizeEx(file, &size) && size.QuadPart > 44 && size.QuadPart < (32 << 20) &&
             (bytes = (unsigned char *)malloc((size_t)size.QuadPart)) != NULL &&
             ReadFile(file, bytes, (DWORD)size.QuadPart, &got, NULL) && got == size.QuadPart;
    CloseHandle(file);
    if (!ok || memcmp(bytes, "RIFF", 4) || memcmp(bytes + 8, "WAVE", 4)) {
        free(bytes);
        return 0;
    }
    unsigned channels = 0, rate = 0, bits = 0, format = 0;
    const unsigned char *data = NULL;
    size_t data_size = 0, at = 12, total = (size_t)size.QuadPart;
    while (at + 8 <= total) {
        uint32_t chunk;
        memcpy(&chunk, bytes + at + 4, 4);
        if (chunk > total - at - 8) chunk = (uint32_t)(total - at - 8);
        if (!memcmp(bytes + at, "fmt ", 4) && chunk >= 16) {
            format = bytes[at + 8] | bytes[at + 9] << 8;
            channels = bytes[at + 10] | bytes[at + 11] << 8;
            memcpy(&rate, bytes + at + 12, 4);
            bits = bytes[at + 22] | bytes[at + 23] << 8;
        } else if (!memcmp(bytes + at, "data", 4)) {
            data = bytes + at + 8;
            data_size = chunk;
        }
        at += 8 + chunk + (chunk & 1);
    }
    if (!data || (format != 1 && format != 0xfffe) || bits != 16 || (channels != 1 && channels != 2) ||
        rate != NITROUS_SOUND_RATE) {
        free(bytes);
        return 0;
    }
    const unsigned frames = (unsigned)(data_size / (2 * channels));
    clip->data = (short *)malloc((size_t)frames * 4 + 4);
    if (!clip->data || !frames) {
        free(clip->data);
        clip->data = NULL;
        free(bytes);
        return 0;
    }
    for (unsigned i = 0; i < frames; ++i) {
        short left, right;
        memcpy(&left, data + (size_t)i * 2 * channels, 2);
        if (channels == 2)
            memcpy(&right, data + (size_t)i * 4 + 2, 2);
        else
            right = left;
        clip->data[2 * i] = left;
        clip->data[2 * i + 1] = right;
    }
    clip->frames = frames;
    free(bytes);
    return 1;
}

#define CROSSFADE (NITROUS_SOUND_RATE / 20)  // the sustain's loop seam, 50 ms

void nitrous_player_render(NitrousPlayer *p, short *out, unsigned frames, int run, int started, float volume) {
    if (started) {
        p->start_pos = 0;
        p->stop_pos = -1;
        p->run = 1;
    }
    if (p->run && !run) p->stop_pos = 0;  // also a boost shorter than one buffer
    p->run = run;
    volume = clamp(volume, 0.0f, 1.0f);
    const float rise = 1.0f / (0.5f * NITROUS_SOUND_RATE), fall = 1.0f / (0.4f * NITROUS_SOUND_RATE);
    for (unsigned i = 0; i < frames; ++i) {
        float left = 0.0f, right = 0.0f;
        if (p->start_pos >= 0) {
            if ((unsigned)p->start_pos < p->start.frames) {
                left += p->start.data[2 * p->start_pos];
                right += p->start.data[2 * p->start_pos + 1];
                ++p->start_pos;
            } else {
                p->start_pos = -1;
            }
        }
        p->sustain_gain = clamp(p->sustain_gain + (run ? rise : -fall), 0.0f, 1.0f);
        if (p->sustain_gain > 0.0f && p->sustain.frames > 2 * CROSSFADE) {
            const unsigned n = p->sustain.frames, at = p->sustain_pos;
            float l = p->sustain.data[2 * at], r = p->sustain.data[2 * at + 1];
            if (at >= n - CROSSFADE) {  // fade into the loop's beginning
                const unsigned from = at - (n - CROSSFADE);
                const float w = (float)from / CROSSFADE;
                l = l * (1.0f - w) + p->sustain.data[2 * from] * w;
                r = r * (1.0f - w) + p->sustain.data[2 * from + 1] * w;
            }
            left += l * p->sustain_gain;
            right += r * p->sustain_gain;
            p->sustain_pos = at + 1 >= n ? CROSSFADE : at + 1;
        } else if (p->sustain_gain <= 0.0f) {
            p->sustain_pos = 0;
        }
        if (p->stop_pos >= 0) {
            if ((unsigned)p->stop_pos < p->stop.frames) {
                left += p->stop.data[2 * p->stop_pos];
                right += p->stop.data[2 * p->stop_pos + 1];
                ++p->stop_pos;
            } else {
                p->stop_pos = -1;
            }
        }
        p->gain += (volume - p->gain) * (1.0f / 1000.0f);
        left = clamp(left * p->gain, -32767.0f, 32767.0f);
        right = clamp(right * p->gain, -32767.0f, 32767.0f);
        out[2 * i] = (short)left;
        out[2 * i + 1] = (short)right;
    }
}

int nitrous_player_idle(const NitrousPlayer *p) {
    return !p->run && p->start_pos < 0 && p->stop_pos < 0 && p->sustain_gain <= 0.0f;
}

// ---------------------------------------------------------------- our synthesized sound

static float noise(NitrousSynth *synth) {
    unsigned x = synth->seed ? synth->seed : 0x9e3779b9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    synth->seed = x;
    return (float)(int)x * (1.0f / 2147483648.0f);
}

void nitrous_synth_reset(NitrousSynth *synth) {
    NitrousSynth empty = {0};
    *synth = empty;
    synth->seed = 0x1234567u;
}

void nitrous_synth_render(NitrousSynth *synth, short *out, unsigned count, float level, int burst, float volume) {
    const float step = 1.0f / NITROUS_SOUND_RATE;
    if (burst) {
        synth->burst = 1.0f;
        synth->burst_time = 0.0f;
    }
    level = clamp(level, 0.0f, 1.0f);
    volume = clamp(volume, 0.0f, 1.0f);
    for (unsigned i = 0; i < count; ++i) {
        const float n = noise(synth);
        float mix = 0.0f;
        if (synth->burst > 0.0f) {
            // A band of noise sweeping down from 4 kHz to 600 Hz, 12 ms attack, 0.35 s decay.
            const float t = synth->burst_time;
            const float envelope = t < 0.012f ? t / 0.012f : expf(-(t - 0.012f) / 0.35f);
            const float cutoff = 600.0f + 3400.0f * expf(-t / 0.25f);
            const float f = 2.0f * sinf(3.14159265f * cutoff / NITROUS_SOUND_RATE);
            synth->low += f * synth->band;
            const float high = n - synth->low - 0.7f * synth->band;
            synth->band += f * high;
            mix += envelope * synth->band * 0.9f;
            synth->burst_time += step;
            if (synth->burst_time > 2.0f) synth->burst = 0.0f;
        }
        synth->level += (level - synth->level) * 0.0004f;
        const float high_passed = n - synth->hiss_low;
        synth->hiss_low += 0.3f * (n - synth->hiss_low);
        synth->high_last += 0.6f * (high_passed - synth->high_last);
        mix += synth->level * synth->high_last * 0.3f;
        synth->gain += (volume - synth->gain) * 0.0005f;
        const float sample = clamp(mix * synth->gain, -1.0f, 1.0f);
        out[i] = (short)(sample * 30000.0f);
    }
}

// ---------------------------------------------------------------- the device

#define SOUND_BUFFERS 4
#define SOUND_FRAMES (NITROUS_SOUND_RATE / 50)  // 20 ms each
static HWAVEOUT wave;
static HANDLE wave_event;
static WAVEHDR headers[SOUND_BUFFERS];
static short buffers[SOUND_BUFFERS][SOUND_FRAMES * 2];
static short mono[SOUND_FRAMES];
static NitrousSynth synth;
static NitrousPlayer player;
static int use_samples;
static volatile float sound_level, sound_gain;
static volatile LONG sound_run, sound_started;

static DWORD WINAPI sound_thread(void *unused) {
    (void)unused;
    for (;;) {
        WaitForSingleObject(wave_event, 50);
        for (unsigned i = 0; i < SOUND_BUFFERS; ++i) {
            if (headers[i].dwFlags & WHDR_INQUEUE) continue;
            const int started = InterlockedExchange(&sound_started, 0) != 0;
            const int run = InterlockedCompareExchange(&sound_run, 0, 0) != 0;
            const float level = sound_level, gain = sound_gain;
            if (use_samples) {
                if (!started && !run && nitrous_player_idle(&player)) break;  // the device idles
                nitrous_player_render(&player, buffers[i], SOUND_FRAMES, run, started, gain);
            } else {
                if (!started && level <= 0.0f && synth.burst <= 0.0f && synth.level < 1.0e-4f) break;
                nitrous_synth_render(&synth, mono, SOUND_FRAMES, level, started, gain);
                for (unsigned k = 0; k < SOUND_FRAMES; ++k) buffers[i][2 * k] = buffers[i][2 * k + 1] = mono[k];
            }
            if (waveOutWrite(wave, &headers[i], sizeof(headers[i])) != MMSYSERR_NOERROR) return 0;
        }
    }
}

static int sound_open(void) {
    WAVEFORMATEX format = {0};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = NITROUS_SOUND_RATE;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = NITROUS_SOUND_RATE * 4;
    wave_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!wave_event) return 0;
    if (waveOutOpen(&wave, WAVE_MAPPER, &format, (DWORD_PTR)wave_event, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        CloseHandle(wave_event);
        wave_event = NULL;
        return 0;
    }
    nitrous_synth_reset(&synth);
    for (unsigned i = 0; i < SOUND_BUFFERS; ++i) {
        headers[i].lpData = (LPSTR)buffers[i];
        headers[i].dwBufferLength = sizeof(buffers[i]);
        if (waveOutPrepareHeader(wave, &headers[i], sizeof(headers[i])) != MMSYSERR_NOERROR) return 0;
    }
    HANDLE thread = CreateThread(NULL, 0, sound_thread, NULL, 0, NULL);
    if (!thread) return 0;
    CloseHandle(thread);
    return 1;
}

// The samples tools/ff_sounds.py writes into NitrousSounds\.
static int load_samples(const wchar_t *folder) {
    static const wchar_t *const names[3] = {L"nos_start.wav", L"nos_sustain.wav", L"nos_stop.wav"};
    NitrousClip *clips[3] = {&player.start, &player.sustain, &player.stop};
    wchar_t path[MAX_PATH + 64];
    for (unsigned i = 0; i < 3; ++i) {
        if (_snwprintf(path, MAX_PATH + 63, L"%ls\\NitrousSounds\\%ls", folder, names[i]) < 0 ||
            !nitrous_load_wav(path, clips[i])) {
            for (unsigned k = 0; k < 3; ++k) {
                free(clips[k]->data);
                clips[k]->data = NULL;
                clips[k]->frames = 0;
            }
            return 0;
        }
    }
    player.start_pos = player.stop_pos = -1;
    return 1;
}

// ---------------------------------------------------------------- plugin side

static NitrousEffects settings;
static NitrousCamera camera;
static int sound_ready;

float effects_fov_degrees(void) { return settings.fov && fov_image ? settings.fov_degrees : 0.0f; }

void effects_start(const wchar_t *ini, const wchar_t *folder, const unsigned char *image, int game_camera,
                   void (*say)(const char *format, ...)) {
    const char *warning = NULL;
    effects_read(ini, &settings, &warning);
    if (warning && say) say("[Effects] %s is not a number; its default is used", warning);
    NitrousCamera still = {0};
    camera = still;
    fov_chase_kick = fov_mounted_kick = 0.0f;
    if (game_camera) {
        if (say)
            say("camera: Fast & Furious' own nitrous layers (its CameraPhysics.xml in the mod's media: FOV, shake, "
                "offsets and motion blur in every driving camera); the plugin's FOV kick is off");
    } else if (settings.fov && image && !fov_image) {
        const int chained = fov_install(image, say);
        if (say)
            say("camera: Fast & Furious' nitrous FOV (+%.1f degrees, a %.1f degree squeeze at the start; cockpit "
                "+%.1f) on %d of 6 driving cameras",
                (double)settings.fov_degrees, (double)settings.impulse_degrees, (double)settings.cockpit_degrees,
                chained);
        if (!chained) fov_image = NULL;
    } else if (say && !settings.fov) {
        say("[Effects] Fov=0: no camera effect");
    }
    if (settings.sound && !sound_ready) {
        use_samples = folder && load_samples(folder);
        sound_ready = sound_open();
        if (say)
            say(!sound_ready  ? "sound: no audio device could be opened; no sound"
                : use_samples ? "sound: Fast & Furious' NOS samples (NitrousSounds), volume %.2f"
                              : "sound: our synthesized burst and hiss, volume %.2f (no NitrousSounds; "
                                "tools/ff_sounds.py makes them from Fast & Furious)",
                (double)settings.sound_volume);
    } else if (say && !settings.sound) {
        say("[Effects] Sound=0: no sound");
    }
}

static int game_focused(void) {
    HWND window = GetForegroundWindow();
    DWORD process = 0;
    if (window) GetWindowThreadProcessId(window, &process);
    return process == GetCurrentProcessId();
}

void effects_frame(const NitrousState *state, float delta_seconds, int input_enabled) {
    const int run = input_enabled && state->run;
    const int started = input_enabled && state->started;
    nitrous_camera_step(&camera, run, started, delta_seconds);
    fov_chase_kick = nitrous_camera_degrees(&camera, &settings, 0);
    fov_mounted_kick = nitrous_camera_degrees(&camera, &settings, 1);
    if (!sound_ready) return;
    sound_level = input_enabled ? state->level : 0.0f;
    sound_gain = input_enabled && game_focused() ? settings.sound_volume : 0.0f;
    InterlockedExchange(&sound_run, run);
    if (started) InterlockedExchange(&sound_started, 1);
    if (started || run || state->level > 0.0f) SetEvent(wave_event);
}
