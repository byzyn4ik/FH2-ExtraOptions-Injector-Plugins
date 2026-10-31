// Nitrous for Forza Horizon 2: where nitrous is allowed and how the tank
// fills or drains (rules.h has the details).
// SPDX-License-Identifier: GPL-3.0-or-later
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "rules.h"

#include <string.h>

static const char *const skill_keys[NITROUS_SKILL_COUNT] = {"Drift", "NearMiss", "Draft", "Air", "Pass",
                                                             "Speed", "Wrecker", "Contact", "Other"};
static const wchar_t *const skill_wkeys[NITROUS_SKILL_COUNT] = {L"Drift", L"NearMiss", L"Draft", L"Air", L"Pass",
                                                                 L"Speed", L"Wrecker", L"Contact", L"Other"};
// Underground: seconds of a 20 s tank per skill.
static const float underground_skill[NITROUS_SKILL_COUNT] = {2.0f, 1.5f, 1.5f, 2.0f, 1.0f, 1.0f, 0.5f, 0.5f, 0.5f};

const char *rules_skill_key(int group) {
    return group >= 0 && group < NITROUS_SKILL_COUNT ? skill_keys[group] : "?";
}

static int text_value(const wchar_t *ini, const wchar_t *section, const wchar_t *key, wchar_t *out, int size) {
    GetPrivateProfileStringW(section, key, L"", out, (DWORD)size, ini);
    wchar_t *start = out;
    while (*start == L' ' || *start == L'\t') ++start;
    size_t n = wcslen(start);
    while (n && (start[n - 1] == L' ' || start[n - 1] == L'\t')) --n;
    memmove(out, start, n * sizeof(wchar_t));
    out[n] = 0;
    return n > 0;
}

static void flag(const wchar_t *ini, const wchar_t *key, const char *name, int *out, const char **warning) {
    wchar_t text[32];
    if (!text_value(ini, L"Nitrous", key, text, 32)) return;
    if (!wcscmp(text, L"0") || !wcscmp(text, L"1"))
        *out = text[0] == L'1';
    else if (warning && !*warning)
        *warning = name;
}

static void number(const wchar_t *ini, const wchar_t *section, const wchar_t *key, const char *name, float low,
                   float high, float *out, const char **warning) {
    wchar_t text[32];
    if (!text_value(ini, section, key, text, 32)) return;
    wchar_t *end = NULL;
    const float value = wcstof(text, &end);
    if (!end || *end || !(value == value)) {
        if (warning && !*warning) *warning = name;
        return;
    }
    *out = value < low ? low : value > high ? high : value;
}

void rules_read(const wchar_t *ini, NitrousRules *r, const char **warning) {
    if (warning) *warning = NULL;
    wchar_t text[32];
    r->mode = NITROUS_MODE_ARCADE;
    if (text_value(ini, L"Nitrous", L"Mode", text, 32)) {
        if (CompareStringOrdinal(text, -1, L"Arcade", -1, TRUE) == CSTR_EQUAL)
            r->mode = NITROUS_MODE_ARCADE;
        else if (CompareStringOrdinal(text, -1, L"Underground", -1, TRUE) == CSTR_EQUAL)
            r->mode = NITROUS_MODE_UNDERGROUND;
        else if (CompareStringOrdinal(text, -1, L"Custom", -1, TRUE) == CSTR_EQUAL)
            r->mode = NITROUS_MODE_CUSTOM;
        else if (warning && !*warning)
            *warning = "Mode";
    }
    const int underground = r->mode == NITROUS_MODE_UNDERGROUND;
    // The presets (Custom starts from Arcade: what 0.0.3 did).
    r->auto_recharge = !underground;
    r->in_races = r->free_roam = r->in_other_events = 1;
    r->skill_recharge = underground;
    for (int i = 0; i < NITROUS_SKILL_COUNT; ++i) r->skill[i] = underground_skill[i];
    r->chain_per_1000 = underground ? 0.5f : 0.0f;
    r->crash_penalty = underground ? 4.0f : 0.0f;
    r->crash_impact = 25.0f;
    r->crash_cancels = underground;
    // Explicit keys win.
    flag(ini, L"AutoRecharge", "AutoRecharge", &r->auto_recharge, warning);
    flag(ini, L"InRaces", "InRaces", &r->in_races, warning);
    flag(ini, L"FreeRoam", "FreeRoam", &r->free_roam, warning);
    flag(ini, L"InOtherEvents", "InOtherEvents", &r->in_other_events, warning);
    flag(ini, L"SkillRecharge", "SkillRecharge", &r->skill_recharge, warning);
    flag(ini, L"CrashCancelsBoost", "CrashCancelsBoost", &r->crash_cancels, warning);
    number(ini, L"Nitrous", L"CrashPenalty", "CrashPenalty", 0.0f, 600.0f, &r->crash_penalty, warning);
    number(ini, L"Nitrous", L"CrashMinImpact", "CrashMinImpact", 5.0f, 500.0f, &r->crash_impact, warning);
    for (int i = 0; i < NITROUS_SKILL_COUNT; ++i)
        number(ini, L"SkillRecharge", skill_wkeys[i], skill_keys[i], 0.0f, 600.0f, &r->skill[i], warning);
    number(ini, L"SkillRecharge", L"ChainPer1000", "ChainPer1000", 0.0f, 600.0f, &r->chain_per_1000, warning);
}

int rules_allowed(const NitrousRules *r, int place) {
    return place == NITROUS_IN_RACE ? r->in_races : place == NITROUS_IN_OTHER_EVENT ? r->in_other_events : r->free_roam;
}

static int is(const wchar_t *a, const wchar_t *b) { return !wcscmp(a, b); }

int rules_skill_group(const wchar_t *n) {
    if (!n || !*n) return NITROUS_SKILL_OTHER;
    if (is(n, L"Drift") || is(n, L"EDrift")) return NITROUS_SKILL_DRIFT;
    if (is(n, L"NearMiss") || is(n, L"ThreadingTheNeedle") || is(n, L"LuckyEscape") || is(n, L"DareDevil"))
        return NITROUS_SKILL_NEAR_MISS;
    if (is(n, L"Draft") || is(n, L"SlingShot")) return NITROUS_SKILL_DRAFT;
    if (wcsstr(n, L"Air") || is(n, L"Kangaroo") || is(n, L"CrashLanding") || is(n, L"Jump"))
        return NITROUS_SKILL_AIR;
    if (is(n, L"Pass") || is(n, L"TriplePass") || is(n, L"CleanStart") || is(n, L"HardCharger"))
        return NITROUS_SKILL_PASS;
    if (wcsstr(n, L"Speed") || is(n, L"FastAndClean")) return NITROUS_SKILL_SPEED;
    if (is(n, L"Destruction") || is(n, L"WreckingBall") || is(n, L"Landscaping") || is(n, L"Trashman"))
        return NITROUS_SKILL_WRECKER;
    if (is(n, L"SideSwipe") || is(n, L"TradingPaint") || is(n, L"LoveTap")) return NITROUS_SKILL_CONTACT;
    return NITROUS_SKILL_OTHER;
}

int rules_place_after(int place, const char *e) {
    if (!e) return place;
    static const char *const race_start[] = {"AnthemRaceStart", "RaceEncounterStart"};
    static const char *const race_end[] = {"AnthemRaceEnd", "RaceEncounterEnd"};
    static const char *const other_start[] = {"ShowcaseRaceStart", "BucketListChallengeStart",
                                              "IslandBucketListChallengeStart", "RivalsStart",
                                              "OnlineRoadTripRaceStart", "MultiplayerRoundStart", "WagerRaceStart"};
    static const char *const other_end[] = {"ShowcaseRaceEnd", "BucketListChallengeEnd", "IslandBucketListChallengeEnd",
                                            "RivalsEnd", "OnlineRoadTripRaceEnd", "MultiplayerRoundEnd"};
    static const char *const free_roam[] = {"ArrivedAtFestival", "CarMeetEntered", "RewardBoardSmashed",
                                            "IslandRewardBoardSmashed", "BarnFindFound", "IslandBarnFindFound",
                                            "RivalsEverywhereEnd", "RoadTripEnd", "IslandRoadTripEnd"};
    for (size_t i = 0; i < sizeof(race_start) / sizeof(race_start[0]); ++i)
        if (!strcmp(e, race_start[i])) return NITROUS_IN_RACE;
    for (size_t i = 0; i < sizeof(other_start) / sizeof(other_start[0]); ++i)
        if (!strcmp(e, other_start[i])) return NITROUS_IN_OTHER_EVENT;
    for (size_t i = 0; i < sizeof(race_end) / sizeof(race_end[0]); ++i)
        if (!strcmp(e, race_end[i])) return NITROUS_FREE_ROAM;
    for (size_t i = 0; i < sizeof(other_end) / sizeof(other_end[0]); ++i)
        if (!strcmp(e, other_end[i])) return NITROUS_FREE_ROAM;
    for (size_t i = 0; i < sizeof(free_roam) / sizeof(free_roam[0]); ++i)
        if (!strcmp(e, free_roam[i])) return NITROUS_FREE_ROAM;
    return place;
}

#define CRASH_WINDOW 0.15f

int rules_crash_step(NitrousCrash *c, float speed, float dt, float min_impact_kmh) {
    if (!(dt > 0.0f && dt < 0.5f) || !(speed == speed)) return 0;
    c->clock += dt;
    if (c->cooldown > 0.0f) c->cooldown -= dt;
    if (speed < 0.0f) speed = -speed;
    c->speeds[c->next] = speed;
    c->times[c->next] = c->clock;
    c->next = (c->next + 1) % 16;
    if (c->count < 16) ++c->count;
    // The fastest speed within the window before now.
    float fastest = speed;
    for (unsigned i = 0; i < c->count; ++i)
        if (c->clock - c->times[i] <= CRASH_WINDOW && c->speeds[i] > fastest) fastest = c->speeds[i];
    if (c->cooldown > 0.0f || (fastest - speed) * 3.6f < min_impact_kmh) return 0;
    c->cooldown = 1.0f;
    for (unsigned i = 0; i < c->count; ++i) c->speeds[i] = speed;  // one crash, not one per frame of it
    return 1;
}

#ifdef NITROUS_TEST
__declspec(dllexport) const char *NitrousTestRules(const wchar_t *ini, NitrousRules *out) {
    const char *warning = NULL;
    rules_read(ini, out, &warning);
    return warning ? warning : "";
}
__declspec(dllexport) int NitrousTestAllowed(const NitrousRules *rules, int place) { return rules_allowed(rules, place); }
__declspec(dllexport) int NitrousTestSkillGroup(const wchar_t *name) { return rules_skill_group(name); }
__declspec(dllexport) int NitrousTestPlaceAfter(int place, const char *event) { return rules_place_after(place, event); }
// Feeds `count` speeds (m/s) at `dt` each; returns the crashes, *first the index of the first.
__declspec(dllexport) int NitrousTestCrash(const float *speeds, int count, float dt, float min_impact, int *first) {
    NitrousCrash crash = {0};
    int crashes = 0;
    *first = -1;
    for (int i = 0; i < count; ++i)
        if (rules_crash_step(&crash, speeds[i], dt, min_impact)) {
            if (!crashes) *first = i;
            ++crashes;
        }
    return crashes;
}
#endif