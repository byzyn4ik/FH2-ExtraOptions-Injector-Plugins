// Nitrous for Forza Horizon 2: where nitrous is allowed and how the tank
// fills or drains (rules.c). Pure logic, no game memory: the plugin feeds it
// the game's ETX telemetry events (hooks.c) and the car's speed.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NITROUS_RULES_H
#define NITROUS_RULES_H
#include <wchar.h>

// What the player is doing, from Forza Horizon 2's own telemetry (the ETX
// events it sent to Xbox Live; the achievements plugin reads the same):
//   AnthemRaceStart / AnthemRaceEnd: a race (circuit, sprint, street, cross
//     country, championship rounds); RaceEncounterStart / End: a head-to-head
//     in the open world, counted as a race too;
//   ShowcaseRaceStart / End, (Island)BucketListChallengeStart / End,
//     RivalsStart / End, OnlineRoadTripRaceStart / End, MultiplayerRoundStart /
//     End, WagerRaceStart: other events;
//   free roam otherwise. A few events only happen in free roam
//   (ArrivedAtFestival, CarMeetEntered, RewardBoardSmashed, BarnFindFound,
//   RivalsEverywhereEnd, RoadTripEnd) and put it back there, in case an event
//   was left without its end event (a quit to the map).
enum { NITROUS_FREE_ROAM = 0, NITROUS_IN_RACE = 1, NITROUS_IN_OTHER_EVENT = 2 };

// Skills (Forza Horizon 2's SkillAchieved events, by name) in groups the INI tunes.
enum {
    NITROUS_SKILL_DRIFT,     // Drift, EDrift
    NITROUS_SKILL_NEAR_MISS, // NearMiss, ThreadingTheNeedle, LuckyEscape, DareDevil
    NITROUS_SKILL_DRAFT,     // Draft, SlingShot
    NITROUS_SKILL_AIR,       // Air, BigAir, Kangaroo, CrashLanding, AirbornePass (any name with "Air")
    NITROUS_SKILL_PASS,      // Pass, TriplePass, CleanStart, HardCharger
    NITROUS_SKILL_SPEED,     // CleanSpeed, FastAndClean, Speed...
    NITROUS_SKILL_WRECKER,   // Destruction, WreckingBall, Landscaping, Trashman
    NITROUS_SKILL_CONTACT,   // SideSwipe, TradingPaint, LoveTap
    NITROUS_SKILL_OTHER,     // the rest (Burnout, TwoWheels, Turn180, ShowOff, the chains...)
    NITROUS_SKILL_COUNT
};

enum { NITROUS_MODE_ARCADE = 0, NITROUS_MODE_UNDERGROUND = 1, NITROUS_MODE_CUSTOM = 2 };

typedef struct {
    int mode;              // [Nitrous] Mode
    int auto_recharge;     // [Nitrous] AutoRecharge: the tank refills over time (RechargeTime)
    int in_races;          // [Nitrous] InRaces
    int free_roam;         // [Nitrous] FreeRoam
    int in_other_events;   // [Nitrous] InOtherEvents
    int skill_recharge;    // [Nitrous] SkillRecharge: skills refill the tank ([SkillRecharge])
    float skill[NITROUS_SKILL_COUNT];  // seconds of tank per skill
    float chain_per_1000;  // [SkillRecharge] ChainPer1000: seconds per 1000 points of a banked chain
    float crash_penalty;   // [Nitrous] CrashPenalty: seconds of tank lost per crash (0: none)
    float crash_impact;    // [Nitrous] CrashMinImpact: km/h lost within 0.15 s that make a crash
    int crash_cancels;     // [Nitrous] CrashCancelsBoost
} NitrousRules;

// The preset's values, then the explicit keys over them. *warning names the
// first key that is not valid (else NULL).
void rules_read(const wchar_t *ini, NitrousRules *rules, const char **warning);
int rules_allowed(const NitrousRules *rules, int place);
// A skill's group (NITROUS_SKILL_*) from its name (UTF-16, as the event carries it).
int rules_skill_group(const wchar_t *name);
const char *rules_skill_key(int group);  // its INI key ("Drift", ...)

// The event state machine: an ETX event's name -> the new place (or `place` unchanged).
int rules_place_after(int place, const char *event);

// Crash detection from the car's speed: a loss of at least the minimum
// impact within 0.15 s (no brake stops a car that fast). One per second at most.
typedef struct {
    float speeds[16], times[16];
    unsigned next, count;
    float clock, cooldown;
} NitrousCrash;
// One update: the speed in m/s, the frame's seconds. 1 when a crash happened now.
int rules_crash_step(NitrousCrash *crash, float speed_mps, float delta_seconds, float min_impact_kmh);
#endif
