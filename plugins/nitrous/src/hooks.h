// Nitrous for Forza Horizon 2: Fast & Furious' nitrous code in Forza Horizon 2's
// own exhaust-flame and camera code (hooks.c).
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NITROUS_HOOKS_H
#define NITROUS_HOOKS_H
#include "nitrous.h"

// ---------------------------------------------------------------- exhaust flames
//
// Forza Horizon 2's ForzaCar update (0xf7b310) runs two exhaust effects on
// the object at ForzaCar+0x4e20, each given the car's CCarDynamics (a
// reference the callee releases):
//   0xf7d8a0  the backfire: sets +0xc (0: none, 2 or 3: a backfire) from the
//             throttle, rpm and gear; while +0xc != 0 the update asks the
//             car's presentation for flames at every exhaust (StartBackfire,
//             slot 0x260 of ForzaCar+0x4d38);
//   0xf7dd70  the burble: sets +0x70 (a pop) on throttle lifts.
// Fast & Furious' copies of both (0xfa3c40, 0xfa4130) start with one test
// more: while the dynamics' IsNitrousActive (interface slot 0x388) is true,
// the backfire sets +0xc = 2 and the burble +0x70 = 1, and both return. So
// its cars spit flames for as long as they boost. The hooks below are those
// tests, at the entry of Forza Horizon 2's functions.
#define NITROUS_RVA_BACKFIRE_UPDATE 0xf7d8a0u
#define NITROUS_RVA_BURBLE_UPDATE 0xf7dd70u
#define NITROUS_BACKFIRE_STATE 0x0cu  // int: 2 while boosting (Fast & Furious)
#define NITROUS_BURBLE_STATE 0x70u    // int: 1 while boosting (Fast & Furious)

// ---------------------------------------------------------------- camera layers
//
// Forza Horizon 2's camera physics (CameraPhysics.xml in media\Camera.zip)
// is a list of layers per camera, each an input (InputType) shaped by its
// attack, decay and curve into an output (FOV, offsets, shake, MotionBlur).
// 0x1037df0 computes a layer's raw input (a switch on the layer's input type
// at +0x48 into the float at +0). Fast & Furious' copy (0x105d860) has two
// inputs more, Nitrous (0x2d) and NitrousImpulse (0x2e), and its
// CameraPhysics.xml has 52 layers on them. Forza Horizon 2's parser does not
// know their names, so tools\ff_camera.py writes Fast & Furious' file into
// the mod's media with two input types Forza Horizon 2 knows but never
// computes (0x17 ShoulderYaw, 0x18 ShoulderRoll: their case writes 0) in
// their place, and the hook fills them as Fast & Furious does.
#define NITROUS_RVA_CAMERA_INPUT 0x1037df0u
#define NITROUS_CAMERA_INPUT_TYPE 0x48u
#define NITROUS_CARRIER_NITROUS 0x17u  // "ShoulderYaw" carries Nitrous
#define NITROUS_CARRIER_IMPULSE 0x18u  // "ShoulderRoll" carries NitrousImpulse
// Fast & Furious' GameTunableSettings.ini, CameraNitrous: both inputs scale
// from 0 at 1 mph to 1 at 30 mph.
#define NITROUS_CAMERA_MIN_MPH 1.0f
#define NITROUS_CAMERA_MAX_MPH 30.0f

// The dynamics interface's fields the inputs read (offsets in the interface,
// at CCarDynamics+0x3a0): speed (m/s, slot 0x2a0's), boosting (slot 0x388's),
// this boost's time and the build-up time (Fast & Furious' slot 0x3a0 divides
// them).
#define NITROUS_IFACE_SPEED 0xe28u
#define NITROUS_IFACE_ACTIVE 0x15cu
#define NITROUS_IFACE_BUILDUP 0x164u
#define NITROUS_IFACE_BUILDUP_TIME 0xa1cu

// Fast & Furious' input values (0x105dfc0 and 0x105e014).
// Nitrous: the speed's ramp times the build-up (boost time / build-up time, at most 1).
float nitrous_camera_nitrous(float speed_mps, float boost_seconds, float buildup_seconds);
// NitrousImpulse: the speed's ramp on the first update a boost is active
// (*previous, the layer's +4, holds the last activity), else 0.
float nitrous_camera_impulse(float speed_mps, int active, float *previous);

// ---------------------------------------------------------------- pad rumble
//
// Forza Horizon 2's rumble (0x1076010; the controller's or the wheel's
// ControllerRumble.xml curves) adds every input's curve per motor, clamps the
// sum to 0..1 and writes motor * 65535 * strength (+8) as an int at +0 (left)
// and +4 (right); +0xc set: paused, nothing written. Fast & Furious' copy
// (0x109c4d0) adds one term more, its Nitrous curves (ControllerRumble.xml
// <Nitrous>, evaluated by 0x109c780 when the input is not 0), the input
// being IsNitrousActive * (1 - the tank left) (0x303d40). The hook adds the
// same term after Forza Horizon 2's function: min(motor + curve * 65535 *
// strength, 65535 * strength), which is the same sum unless a motor was
// already saturated. Fast & Furious' curves (x, y; flat outside):
//   pad:   left 0.001/0.1, 0.7/0.2, 1.0/0.1   right 0.001/0.1, 0.7/0.1, 1.0/1.0
//   wheel: left 0.001/0.2, 0.7/0.2, 1.0/0.1   right as the pad's
// (its impulse-trigger curve, ControllerHaptics.xml, goes through another
// sum this plugin does not reach.)
#define NITROUS_RVA_RUMBLE_UPDATE 0x1076010u
typedef struct {
    float x[3], y[3];
} NitrousCurve;
float nitrous_curve(const NitrousCurve *curve, float x);
// The rumble input, set on every local-car update: active ? 1 - remaining : 0.
void hooks_set_rumble(float input);

// ---------------------------------------------------------------- the end of a boost
//
// Fast & Furious ends a boost at once: its physics step zeroes the build-up
// (+0x504) with the boost flag (0x1315400, like Forza Horizon 2's), so its
// camera input Nitrous drops to 0 and only the layers with an attack/decay
// (the follow cameras' FOV, offset and blur: 0.5 per second) ease out; its
// backfire test simply stops. On top of that the plugin keeps (EaseOut, on
// the player's car only): the camera's Nitrous input and the rumble fading
// from where they were over EaseOut seconds, and the flames for ExhaustTail
// seconds more, ended by one pop.
void hooks_set_player(const void *iface);            // the player car's dynamics interface (NULL: none)
void hooks_set_ending(float envelope, int flames);   // the eased build-up 0..1; flames 0: no tail
void hooks_set_pop(void);                             // one burble pop on the next update
// One local-car update: while boosting the envelope is the build-up (boost
// time / build-up time, at most 1); after it, it falls to 0 over ease_out
// seconds, and the flames go on for tail seconds, then one pop. Sets the
// above; returns the envelope.
float hooks_ending_frame(int active, float buildup_fraction, float delta_seconds, float ease_out, float tail);

// ---------------------------------------------------------------- telemetry
//
// Forza Horizon 2 reports gameplay as ETX events through its import of
// EtwPlus.dll!EtxEventWrite (the IAT slot 0x1dd0060; the event descriptors,
// 131 of 0x38 bytes, at 0x2cb9280, each with its name at +0x10). The slot is
// pointed at a wrapper that hands each event to `sink`, then calls what the
// slot held (the import, or another plugin's wrapper: OpenBox Achievements
// wraps the same slot; either order works, each calls the other).
#define NITROUS_RVA_ETX_SLOT 0x1dd0060u
#define NITROUS_RVA_ETX_TABLE 0x2cb9280u
#define NITROUS_ETX_EVENTS 131u
#define NITROUS_RVA_ETX_FIRST_NAME 0x1e64338u  // "AnnaRecommendation"
typedef struct {
    unsigned long long pointer;
    unsigned size, reserved;
} NitrousEtxData;
typedef void (*NitrousEtxSink)(const char *event, unsigned count, const NitrousEtxData *data);
// Arms the wrapper; hooks_etx_ensure (on every update; cheap) installs it once
// the game resolved the import. 1: in place.
int hooks_etx_prepare(const unsigned char *image, NitrousEtxSink sink);
int hooks_etx_ensure(void);

// NULL when the hooked functions are Forza Horizon 2 1.0.0.64's, else which differs.
const char *hooks_check(const unsigned char *image);

// Installs what is asked (once; each function's first bytes are checked
// before it is taken over, and one already taken over is not again): flames = the exhaust hooks, camera = the
// camera-input hook. Says what was done. The camera hook is only worth it
// with the mod's media\Camera\CameraPhysics.xml (tools\ff_camera.py).
// Returns a bit set: 1 flames installed, 2 camera installed, 4 rumble installed.
int hooks_start(const unsigned char *image, int flames, int camera, int rumble, void (*say)(const char *format, ...));
#endif
