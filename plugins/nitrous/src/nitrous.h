// Nitrous for Forza Horizon 2: the car-side logic (nitrous.c).
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NITROUS_H
#define NITROUS_H
#include "fh2_guard.h"

// Forza Horizon 2 1.0.0.64 (forza_durango_release_final.exe, TimeDateStamp
// 0x565501ed, SizeOfImage 0x3a18000). Fast & Furious 1.0.0.6 is 0x3a89000.
#define NITROUS_FH2_IMAGE_SIZE 0x3a18000u
#define NITROUS_FH2_STAMP 0x565501edu
#define NITROUS_FF_IMAGE_SIZE 0x3a89000u

// The local car: a ForzaCar (vtable), its CCarDynamics at +0x4c08 (a pointer to
// the object's interface at +0x3a0, as ForzaCar's accessor 0xf605d0 returns it).
#define NITROUS_RVA_FORZA_CAR_VTABLE 0x2c5cb80u
#define NITROUS_RVA_DYNAMICS_VTABLE 0x2eb5d68u        // TRefCountedObjectThreadSafe<CCarDynamics>
#define NITROUS_RVA_DYNAMICS_IFACE_VTABLE 0x2eb5e18u  // its second vtable, at +0x3a0
#define NITROUS_CAR_DYNAMICS 0x4c08u
#define NITROUS_IFACE_OFFSET 0x3a0u

// CCarDynamics' nitrous fields (offsets in the whole object).
#define NITROUS_REQUEST 0x4f8u   // int: nitrous asked for (Fast & Furious copies its input byte here)
#define NITROUS_ACTIVE 0x4fcu    // int: boosting now (the physics step sets it)
#define NITROUS_USED 0x500u      // float: seconds of the tank used
#define NITROUS_BUILDUP 0x504u   // float: seconds since this boost started
#define NITROUS_PARAMS 0xdb0u    // 5 floats: min and max engine speed (rad/s), torque scale, build-up s, tank s
#define NITROUS_ENGINE_SPEED 0x4e8u  // float: engine speed (rad/s), tested against the range
#define NITROUS_THROTTLE 0x1530u     // float: the throttle the physics uses (after the gearbox's cut and smoothing)
#define NITROUS_SHIFT_TIME 0x540u    // float: > 0 while a gear change cuts the throttle (0x12f0b13)
#define NITROUS_THROTTLE_NEEDED 0.99f  // the step's test (the constant at 0x2363580)

// A boost that stops for less than this while the input stays held (a gear
// change, the rev limiter, traction control) keeps its build-up: the plugin
// puts +0x504 back, so the torque does not ramp up from 1x again.
#define NITROUS_CARRY_GAP 1.0f

typedef struct {
    float torque_scale;  // engine torque x this while boosting (Fast & Furious: 1.6)
    float duration;      // seconds of boost in a full tank (Fast & Furious: 20)
    float buildup;       // seconds to reach the full torque scale (Fast & Furious: 0.5)
    float recharge;      // seconds to refill an empty tank while not boosting; 0: never
} NitrousTuning;

enum {
    NITROUS_BOOSTING = 0,   // the car is boosting
    NITROUS_IDLE,           // armed, not boosting
    NITROUS_ARMED,          // the parameters were (re)written this frame
    NITROUS_NOT_LOCAL_CAR,  // not Forza Horizon 2's player car (a ForzaCar)
    NITROUS_NO_DYNAMICS,    // no CCarDynamics of the known build behind it
    NITROUS_UNREADABLE,     // its fields cannot be read
};

// Why a boost (a run of boosting, short cuts bridged) ended.
enum {
    NITROUS_END_NONE = 0,
    NITROUS_END_RELEASED,      // the key / button / NitrousHold let go (or input off)
    NITROUS_END_EMPTY,         // the tank is used up
    NITROUS_END_GEAR_CHANGE,   // the gearbox cut the throttle for longer than NITROUS_CARRY_GAP
    NITROUS_END_THROTTLE,      // the throttle the physics uses stayed below 0.99
    NITROUS_END_ENGINE_SPEED,  // the engine speed left the range
    NITROUS_END_OTHER,         // the physics stopped for another reason (a new car, a reset)
};

typedef struct {
    void *dynamics;        // the CCarDynamics object of the last frame (NULL: none)
    unsigned arms;         // times the parameters were written (a new car, the game rebuilt its engine)
    float remaining;       // the tank, 0..1 (1: full)
    int active;            // the physics boosted in its last step
    int held;              // the input asked for nitrous on the last frame
    float throttle;        // the throttle the physics tested (+0x1530)
    float buildup_time;    // this boost's time (+0x504, s)
    float level;           // the effects' envelope, 0..1: the torque build-up, eased off after the boost
    // The current run: boosting, with cuts shorter than NITROUS_CARRY_GAP bridged.
    int run;               // a run is open
    float run_seconds;     // seconds boosted in it
    float gap;             // seconds since the physics last boosted (0 while it boosts)
    float carry;           // the build-up to restore after a cut (s)
    unsigned bridged;      // cuts bridged in this run (the build-up put back)
    int cut_reason;        // NITROUS_END_* of the cut in progress
    float cut_throttle;    // the throttle at the cut
    // Set by nitrous_frame when a run ends (the plugin logs it, then clears ended).
    int ended;             // NITROUS_END_* of the run that just ended; 0: none
    float ended_seconds;   // its length
    unsigned ended_bridged;
    float ended_throttle;
    int started;           // a run started on this frame
} NitrousState;

// The code the plugin relies on is the one of Forza Horizon 2 1.0.0.64:
// NULL when every guarded site matches, else what does not.
const char *nitrous_check_game(const unsigned char *image, size_t image_size);

void nitrous_configure(const NitrousTuning *tuning);
void nitrous_reset(void);
// One local-car update: `held` is the player's nitrous input. One of the
// NITROUS_* values above; nothing is written unless the car is the known one.
int nitrous_frame(const unsigned char *image, void *car, int held, float delta_seconds);
const NitrousState *nitrous_state(void);
// Seconds of tank to add (skills; > 0) or take (crashes; < 0) on the next update.
void nitrous_adjust_tank(float seconds);
// Clears the ended run after the plugin logged it.
void nitrous_clear_ended(void);
const char *nitrous_end_text(int reason);
#endif
