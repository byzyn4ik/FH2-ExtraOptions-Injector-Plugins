// The Nitrous plugin's status export, for other plugins (Performance draws
// its gauge and screen effect from it).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Nitrous.dll exports BOOL WINAPI NitrousStatus(FH2NitrousStatus *status):
// set status->size first; FALSE while the plugin is not active (another
// game or build, Enabled=0) or before the first armed car. Any thread; the
// values are those of the last local-car update.
#ifndef FH2_NITROUS_H
#define FH2_NITROUS_H
#include <stdint.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

enum {
    FH2_NITROUS_BOOSTING = 1,   // the physics boosts now
    FH2_NITROUS_HELD = 2,       // the input asks for nitrous
    FH2_NITROUS_DRIVING = 4,    // a local-car update with gameplay input within the last half second
    FH2_NITROUS_REFILLING = 8,  // the tank is not full and refills (not held, not boosting)
    FH2_NITROUS_RUN = 16,       // a boost run is open (boosting, or a short cut being bridged)
    FH2_NITROUS_BLOCKED = 32,   // nitrous is not allowed here ([Nitrous] InRaces / FreeRoam / InOtherEvents)
    FH2_NITROUS_GAINED = 64,    // a skill just refilled the tank (for 0.6 s)
};

typedef struct FH2NitrousStatus {
    uint32_t size;      // in: sizeof(FH2NitrousStatus)
    uint32_t flags;     // FH2_NITROUS_*
    float remaining;    // the tank, 0..1
    float level;        // the effects' envelope, 0..1 (the torque build-up, eased off after a boost)
    float run_seconds;  // seconds boosted in the open run
    float fov_degrees;  // the camera FOV kick at full level (0: the camera effect is off)
} FH2NitrousStatus;

typedef BOOL(WINAPI *FH2NitrousStatusFn)(FH2NitrousStatus *status);
#endif
