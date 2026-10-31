// Nitrous for Forza Horizon 2: the boost's camera and sound effects (effects.c).
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NITROUS_EFFECTS_H
#define NITROUS_EFFECTS_H
#include "nitrous.h"

// The driving cameras' FOV getters (vtable slot 0x70), chained after
// whatever is there (Extra Options' own FOV option keeps working).
#define NITROUS_RVA_FOV_CHASE 0x1034120u    // CCamFollow/Low/High: settings(+0x180)->+0x1c degrees
#define NITROUS_RVA_FOV_MOUNTED 0x10340f0u  // CCamDriver/Hood/BumperHigh: +0x1d0 radians
#define NITROUS_FOV_SLOT 0x70u

typedef struct {
    int fov;                  // [Effects] Fov
    float fov_degrees;        // [Effects] FovDegrees: the follow cameras at full boost (F&F: +5)
    float impulse_degrees;    // [Effects] FovImpulseDegrees: the squeeze when a boost starts (F&F: 5)
    float cockpit_degrees;    // [Effects] FovCockpitDegrees: driver, hood, bumper (F&F: none)
    int sound;                // [Effects] Sound
    float sound_volume;       // [Effects] SoundVolume, 0..1
    int flames;               // [Effects] ExhaustFlames: the exhaust backfires while boosting (hooks.c)
    int camera_layers;        // [Effects] CameraLayers: Fast & Furious' camera layers from the mod's media (hooks.c)
    int rumble;               // [Effects] Rumble: Fast & Furious' nitrous pad rumble (hooks.c)
} NitrousEffects;

// One number of [Effects] (missing or not a number: the fallback; clamped).
float effects_number(const wchar_t *ini, const wchar_t *key, float fallback, float low, float high);
// Reads [Effects] (missing keys: the defaults; out of range: clamped);
// *warning names a value that is not a number (else NULL).
void effects_read(const wchar_t *ini, NitrousEffects *effects, const char **warning);

// ---------------------------------------------------------------- camera

// Fast & Furious' CameraPhysics.xml, its follow cameras' FOV layers: input
// "Nitrous" (held while boosting; attack ~instant, decay 0.5 per second)
// to FOV +5, and "NitrousImpulse" (a pulse when a boost starts; decay 0.6
// per second) to FOV -5: the view holds, then widens by 5 degrees as the
// impulse fades, and narrows back over 2 s after the boost.
typedef struct {
    float nitrous;   // the "Nitrous" input after its attack and decay, 0..1
    float impulse;   // the "NitrousImpulse" input after its attack, 0..1
    float pulse;     // the impulse's input: 1 when a boost starts, then its decay
} NitrousCamera;
void nitrous_camera_step(NitrousCamera *camera, int boosting, int started, float delta_seconds);
// The degrees added to a follow (chase) or a mounted camera.
float nitrous_camera_degrees(const NitrousCamera *camera, const NitrousEffects *effects, int mounted);
// The factor for a camera whose own field of view is `base_degrees`: (base + kick) / base, kept >= 10 degrees.
float nitrous_fov_factor(float base_degrees, float kick_degrees);

// ---------------------------------------------------------------- sound

#define NITROUS_SOUND_RATE 48000  // stereo, 16-bit

// Fast & Furious' own nitrous samples (HZ2_SFX_NOS_Start_V3, Sustain_V4,
// Stop_V3 from its HZ2_SFX_UI.bank, decoded from the player's install by
// tools/ff_sounds.py into NitrousSounds\ beside the DLL), played as its NOS
// event does: the start, the sustain looped while boosting, the stop.
typedef struct {
    short *data;      // stereo frames
    unsigned frames;
} NitrousClip;
typedef struct {
    NitrousClip start, sustain, stop;
    int start_pos, stop_pos;  // frames played; -1: not playing
    unsigned sustain_pos;
    float sustain_gain, gain;
    int run;
} NitrousPlayer;
void nitrous_player_render(NitrousPlayer *player, short *out, unsigned frames, int run, int started, float volume);
int nitrous_player_idle(const NitrousPlayer *player);
// A 16-bit PCM WAV, mono or stereo, at NITROUS_SOUND_RATE, as stereo frames (malloc'd).
int nitrous_load_wav(const wchar_t *path, NitrousClip *clip);

// Without those files: ours, synthesized (a burst when a boost starts and a
// hiss while it lasts), mono, at NITROUS_SOUND_RATE.
typedef struct {
    unsigned seed;
    float burst, burst_time;
    float low, band, high_last, hiss_low;
    float gain, level;
} NitrousSynth;
void nitrous_synth_reset(NitrousSynth *synth);
void nitrous_synth_render(NitrousSynth *synth, short *out, unsigned count, float level, int burst, float volume);

// ---------------------------------------------------------------- plugin side

// Once, after the game checks passed: the FOV chain and the sound device, as
// [Effects] asks. `folder` is the plugin's folder (NitrousSounds\ is there).
// game_camera: Fast & Furious' own camera layers run (hooks.c), so the FOV
// kick here stays off.
void effects_start(const wchar_t *ini, const wchar_t *folder, const unsigned char *image, int game_camera,
                   void (*say)(const char *format, ...));
// On every local-car update (the game thread).
void effects_frame(const NitrousState *state, float delta_seconds, int input_enabled);
// The follow cameras' kick at full boost (0: the camera effect is off).
float effects_fov_degrees(void);
#endif
