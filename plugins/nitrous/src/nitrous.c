// Nitrous for Forza Horizon 2: Forza Horizon 2's own, dormant nitrous model,
// switched on for the player's car.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Forza Horizon 2's car physics (CCarDynamics) still carries the nitrous model
// that Fast & Furious uses; nothing in Forza Horizon 2 asks for it:
//   - the engine-setup builder (0x12e0a50) copies the car setup's nitrous
//     block (+0x98c: enabled, torque scale, rpm range, build-up, tank) into
//     the dynamics at +0xdac..+0xdc0; Forza Horizon 2's setups leave it empty,
//     so the rpm range is [0, 0] and the tank 0 s;
//   - each physics step (0x12eb284) boosts while +0x4f8 (the request) is set,
//     the throttle (+0x1530) is at least 0.99, the engine speed (+0x4e8) is in
//     the range and the tank is not used up (+0x500 <= +0xdc0): +0x4fc = 1,
//     +0x500 and +0x504 (this boost's time) grow by the step;
//   - the engine torque (0x12b42e0) of a positive (driving) torque is scaled
//     by 1 + (scale - 1) * min(+0x504 / build-up, 1) while +0x4fc is set.
// Fast & Furious fills the setup block from its NitrousPrototype entity and
// rebuilds the engine (0x5ef0d0), copies its nitrous input byte into +0x4f8
// (0x12fdf47) and changes only the throttle test (0x1315370). This file writes
// the same fields directly: the five parameters (again whenever the game
// rebuilds the engine and clears them), the request, and the tank's refill.
#include "nitrous.h"

typedef struct {
    unsigned rva;
    unsigned char size;
    const unsigned char *bytes;
    const char *what;
} Site;

// 0x12eb284: the physics step's nitrous test and timers (cmp [rbx+0x4f8],r14d ...).
static const unsigned char step_bytes[] = {
    0x44, 0x39, 0xb3, 0xf8, 0x04, 0x00, 0x00, 0x74, 0x5d, 0xc5, 0x78, 0x2f, 0x8b, 0x30, 0x15, 0x00, 0x00, 0x77,
    0x53, 0xc5, 0xfa, 0x10, 0x83, 0xe8, 0x04, 0x00, 0x00, 0xc5, 0xf8, 0x2f, 0x83, 0xb0, 0x0d, 0x00, 0x00, 0x72,
    0x41, 0xc5, 0xf8, 0x2f, 0x83, 0xb4, 0x0d, 0x00, 0x00, 0x77, 0x37, 0xc5, 0xfa, 0x10, 0x83, 0x00, 0x05, 0x00,
    0x00, 0xc5, 0xf8, 0x2f, 0x83, 0xc0, 0x0d, 0x00, 0x00, 0x77, 0x25, 0xc5, 0xca, 0x58, 0x8b, 0x04, 0x05, 0x00,
    0x00, 0xc5, 0xfa, 0x58, 0xc6, 0x44, 0x89, 0xbb, 0xfc, 0x04, 0x00, 0x00, 0xc5, 0xfa, 0x11, 0x8b, 0x04, 0x05,
    0x00, 0x00, 0xc5, 0xfa, 0x11, 0x83, 0x00, 0x05, 0x00, 0x00, 0xeb, 0x0e, 0x44, 0x89, 0xb3, 0xfc, 0x04, 0x00,
    0x00, 0x44, 0x89, 0xb3, 0x04, 0x05, 0x00, 0x00};
// 0x12b4332: the engine torque's nitrous scale (cmp [rbx+0x4fc],0 ... [rbx+0xdbc] ... [rbx+0xdb8]).
static const unsigned char torque_bytes[] = {
    0x83, 0xbb, 0xfc, 0x04, 0x00, 0x00, 0x00, 0xc5, 0xf8, 0x29, 0x74, 0x24, 0x70, 0xc5, 0xfa, 0x10, 0x35, 0x45,
    0xf2, 0x0a, 0x01, 0xc5, 0xf8, 0x29, 0x7c, 0x24, 0x60, 0xc5, 0x78, 0x29, 0x44, 0x24, 0x50, 0xc5, 0x78, 0x29,
    0x54, 0x24, 0x30, 0xc5, 0x78, 0x28, 0xd6, 0xc5, 0x78, 0x28, 0xc6, 0xc5, 0xf8, 0x28, 0xfe, 0x74, 0x33, 0xc5,
    0xfa, 0x10, 0x83, 0x04, 0x05, 0x00, 0x00, 0xc5, 0xfa, 0x5e, 0x93, 0xbc, 0x0d, 0x00, 0x00, 0xc5, 0xca, 0x5c,
    0xca, 0xc4, 0xc1, 0x78, 0x2f, 0xc9, 0x73, 0x04, 0xc5, 0xf8, 0x28, 0xd6, 0xc5, 0xfa, 0x10, 0x83, 0xb8, 0x0d,
    0x00, 0x00, 0xc5, 0xfa, 0x5c, 0xce, 0xc5, 0xf2, 0x59, 0xd2, 0xc5, 0x6a, 0x58, 0xd6};
// 0x12e0e42: the engine-setup builder's copy of the setup's nitrous block into +0xdac..+0xdc0.
static const unsigned char builder_bytes[] = {
    0x8b, 0x82, 0x8c, 0x09, 0x00, 0x00, 0x41, 0x89, 0x80, 0xac, 0x0d, 0x00, 0x00, 0xc5, 0xba, 0x59, 0x8a, 0x94,
    0x09, 0x00, 0x00, 0xc4, 0xc1, 0x7a, 0x11, 0x88, 0xb0, 0x0d, 0x00, 0x00, 0xc5, 0xba, 0x59, 0x82, 0x98, 0x09,
    0x00, 0x00, 0xc5, 0xfa, 0x10, 0x0d, 0xe0, 0x26, 0x08, 0x01, 0xc4, 0xc1, 0x7a, 0x11, 0x80, 0xb4, 0x0d, 0x00,
    0x00, 0xc4, 0x41, 0x7a, 0x11, 0x88, 0xb8, 0x0d, 0x00, 0x00, 0x8b, 0x82, 0xa0, 0x09, 0x00, 0x00, 0x41, 0x89,
    0x80, 0xc0, 0x0d, 0x00, 0x00, 0xc5, 0xfa, 0x10, 0x92, 0x9c, 0x09, 0x00, 0x00, 0xc5, 0xf2, 0x5c, 0xc2, 0xc5,
    0xf8, 0x2f, 0xc3, 0x73, 0x04, 0xc5, 0xf8, 0x28, 0xca, 0xc4, 0xc1, 0x7a, 0x11, 0x88, 0xbc, 0x0d, 0x00, 0x00};
// 0x12d67e0: CCarDynamics' IsNitrousActive (+0x15c of the interface = +0x4fc) and HasNitrous (+0xa0c = +0xdac).
static const unsigned char getters_bytes[] = {
    0x33, 0xc0, 0x39, 0x81, 0x5c, 0x01, 0x00, 0x00, 0x0f, 0x95, 0xc0, 0xc3, 0xcc, 0xcc, 0xcc, 0xcc,
    0x33, 0xc0, 0x39, 0x81, 0x0c, 0x0a, 0x00, 0x00, 0x0f, 0x95, 0xc0, 0xc3};
// 0xf605d0: ForzaCar's dynamics accessor (mov rax, [rcx+0x4c08]; ret).
static const unsigned char accessor_bytes[] = {0x48, 0x8b, 0x81, 0x08, 0x4c, 0x00, 0x00, 0xc3};

static const Site sites[] = {
    {0x12eb284u, sizeof(step_bytes), step_bytes, "the physics step's nitrous test (0x12eb284) differs"},
    {0x12b4332u, sizeof(torque_bytes), torque_bytes, "the engine torque's nitrous scale (0x12b4332) differs"},
    {0x12e0e42u, sizeof(builder_bytes), builder_bytes, "the engine-setup builder's nitrous copy (0x12e0e42) differs"},
    {0x12d67e0u, sizeof(getters_bytes), getters_bytes, "CCarDynamics' nitrous getters (0x12d67e0) differ"},
    {0xf605d0u, sizeof(accessor_bytes), accessor_bytes, "ForzaCar's dynamics accessor (0xf605d0) differs"},
};

static NitrousTuning tuning = {1.6f, 20.0f, 0.5f, 60.0f};
static NitrousState state;
static float pending_tank;  // seconds to add (+) or take (-) on the next update

void nitrous_adjust_tank(float seconds) {
    if (seconds == seconds) pending_tank += seconds;
}
// The engine-speed range the boost works in: all of it (rad/s).
static const float max_engine_speed = 1.0e6f;

static int same_bytes(const unsigned char *address, const unsigned char *expected, size_t size) {
    unsigned char actual[128];
    if (size > sizeof(actual) || !fh2_safe_read(address, actual, size)) return 0;
    for (size_t i = 0; i < size; ++i)
        if (actual[i] != expected[i]) return 0;
    return 1;
}

const char *nitrous_check_game(const unsigned char *image, size_t image_size) {
    if (!image) return "no game image";
    if (image_size != NITROUS_FH2_IMAGE_SIZE) return "the game image is not Forza Horizon 2 1.0.0.64's";
    unsigned char header[0x200];
    uint32_t pe, stamp;
    if (!fh2_safe_read(image, header, sizeof(header))) return "the game's PE header cannot be read";
    fh2_copy_bytes(&pe, header + 0x3c, 4);
    if (pe > sizeof(header) - 12 || header[pe] != 'P' || header[pe + 1] != 'E') return "the game image has no PE header";
    fh2_copy_bytes(&stamp, header + pe + 8, 4);
    if (stamp != NITROUS_FH2_STAMP) return "the game executable's time stamp is not Forza Horizon 2 1.0.0.64's";
    for (size_t i = 0; i < sizeof(sites) / sizeof(sites[0]); ++i)
        if (!same_bytes(image + sites[i].rva, sites[i].bytes, sites[i].size)) return sites[i].what;
    return NULL;
}

static float sane(float value, float low, float high, float fallback) {
    if (!(value == value) || value < low) return value == value && value < low ? low : fallback;
    return value > high ? high : value;
}

void nitrous_configure(const NitrousTuning *given) {
    tuning.torque_scale = sane(given->torque_scale, 1.0f, 5.0f, 1.6f);
    tuning.duration = sane(given->duration, 0.5f, 600.0f, 20.0f);
    tuning.buildup = sane(given->buildup, 0.01f, 10.0f, 0.5f);
    tuning.recharge = given->recharge == given->recharge && given->recharge <= 0.0f
                          ? 0.0f
                          : sane(given->recharge, 0.5f, 3600.0f, 60.0f);
}

void nitrous_reset(void) {
    NitrousState empty = {0};
    state = empty;
    pending_tank = 0.0f;
}

const NitrousState *nitrous_state(void) { return &state; }

void nitrous_clear_ended(void) { state.ended = NITROUS_END_NONE; }

static void end_run(int reason) {
    state.ended = reason;
    state.ended_seconds = state.run_seconds;
    state.ended_bridged = state.bridged;
    state.ended_throttle = state.cut_throttle;
    state.run = 0;
    state.carry = 0.0f;
    state.gap = 0.0f;
    state.cut_reason = NITROUS_END_NONE;
}

static float read_float_at(const unsigned char *at, float fallback) {
    float value;
    return fh2_safe_read(at, &value, sizeof(value)) && value == value ? value : fallback;
}

// A run of boosting: the build-up carried over short cuts, why it ended, and
// the effects' envelope. The physics alone decides when it boosts.
static void track_run(unsigned char *dynamics, int active, int held, float used, float boost_time, float delta) {
    const float dt = delta > 0.0f && delta < 1.0f ? delta : 0.0f;
    const float throttle = read_float_at(dynamics + NITROUS_THROTTLE, 0.0f);
    state.throttle = throttle;
    state.started = 0;
    if (active) {
        if (state.run && held && state.carry > 0.0f && boost_time + 1.0e-4f < state.carry &&
            state.gap < NITROUS_CARRY_GAP) {
            // The physics started the build-up again after a cut: put it back.
            boost_time = state.carry + (boost_time > 0.0f ? boost_time : 0.0f);
            *(volatile float *)(dynamics + NITROUS_BUILDUP) = boost_time;
            ++state.bridged;
        }
        if (!state.run) {
            state.run = 1;
            state.run_seconds = 0.0f;
            state.bridged = 0;
            state.started = 1;
        }
        state.run_seconds += dt;
        state.gap = 0.0f;
        state.carry = boost_time;
        state.cut_reason = NITROUS_END_NONE;
    } else if (state.run) {
        if (state.cut_reason == NITROUS_END_NONE) {
            const float speed = read_float_at(dynamics + NITROUS_ENGINE_SPEED, 0.0f);
            const float shifting = read_float_at(dynamics + NITROUS_SHIFT_TIME, 0.0f);
            state.cut_throttle = throttle;
            state.cut_reason = !held                                    ? NITROUS_END_RELEASED
                               : used >= tuning.duration - 0.01f         ? NITROUS_END_EMPTY
                               : throttle < NITROUS_THROTTLE_NEEDED && shifting > 0.0f ? NITROUS_END_GEAR_CHANGE
                               : throttle < NITROUS_THROTTLE_NEEDED      ? NITROUS_END_THROTTLE
                               : speed < 0.0f || speed > max_engine_speed ? NITROUS_END_ENGINE_SPEED
                                                                         : NITROUS_END_OTHER;
        }
        state.gap += dt > 0.0f ? dt : 0.001f;
        if (!held || used >= tuning.duration - 0.01f || state.gap >= NITROUS_CARRY_GAP) end_run(state.cut_reason);
    }
    state.buildup_time = boost_time;
    // The envelope follows the torque's build-up while boosting; it eases off
    // slowly through a bridged cut and quickly once the run is over.
    if (active) {
        float level = tuning.buildup > 0.0f ? boost_time / tuning.buildup : 1.0f;
        state.level = level < 0.0f ? 0.0f : level > 1.0f ? 1.0f : level;
    } else {
        state.level -= dt / (state.run ? 1.5f : 0.4f);
        if (state.level < 0.0f) state.level = 0.0f;
    }
}

const char *nitrous_end_text(int reason) {
    switch (reason) {
    case NITROUS_END_RELEASED: return "the input was released";
    case NITROUS_END_EMPTY: return "the tank is empty";
    case NITROUS_END_GEAR_CHANGE: return "a gear change cut the throttle for too long";
    case NITROUS_END_THROTTLE:
        return "the throttle the physics uses fell below 0.99 (pedal not fully down, the rev limiter, traction "
               "control or the launch)";
    case NITROUS_END_ENGINE_SPEED: return "the engine speed left the range";
    case NITROUS_END_OTHER: return "the physics stopped it (a new car or a reset)";
    default: return "?";
    }
}

// The CCarDynamics object behind the player's car, or NULL.
static unsigned char *player_dynamics(const unsigned char *image, void *car, int *why) {
    void *vtable = NULL, *iface = NULL, *iface_vtable = NULL, *object_vtable = NULL;
    if (!fh2_safe_read(car, &vtable, sizeof(vtable)) || vtable != image + NITROUS_RVA_FORZA_CAR_VTABLE) {
        *why = NITROUS_NOT_LOCAL_CAR;
        return NULL;
    }
    *why = NITROUS_NO_DYNAMICS;
    if (!fh2_safe_read((unsigned char *)car + NITROUS_CAR_DYNAMICS, &iface, sizeof(iface)) || !iface ||
        (uintptr_t)iface < NITROUS_IFACE_OFFSET ||
        !fh2_safe_read(iface, &iface_vtable, sizeof(iface_vtable)) ||
        iface_vtable != image + NITROUS_RVA_DYNAMICS_IFACE_VTABLE)
        return NULL;
    unsigned char *object = (unsigned char *)iface - NITROUS_IFACE_OFFSET;
    if (!fh2_safe_read(object, &object_vtable, sizeof(object_vtable)) ||
        object_vtable != image + NITROUS_RVA_DYNAMICS_VTABLE)
        return NULL;
    return object;
}

int nitrous_frame(const unsigned char *image, void *car, int held, float delta_seconds) {
    int why;
    unsigned char *dynamics = player_dynamics(image, car, &why);
    if (!dynamics) return why;
    int request = 0, active = 0;
    float timers[2], params[5];
    if (!fh2_safe_read(dynamics + NITROUS_REQUEST, &request, 4) || !fh2_safe_read(dynamics + NITROUS_ACTIVE, &active, 4) ||
        !fh2_safe_read(dynamics + NITROUS_USED, timers, sizeof(timers)) ||
        !fh2_safe_read(dynamics + NITROUS_PARAMS, params, sizeof(params)))
        return NITROUS_UNREADABLE;
    volatile float *out = (volatile float *)(dynamics + NITROUS_PARAMS);
    const float wanted[5] = {0.0f, max_engine_speed, tuning.torque_scale, tuning.buildup, tuning.duration};
    int armed = 0;
    for (int i = 0; i < 5; ++i) {
        if (params[i] != wanted[i]) {
            out[i] = wanted[i];
            armed = 1;
        }
    }
    if (dynamics != state.dynamics) {
        if (state.run) end_run(NITROUS_END_OTHER);
        state.dynamics = dynamics;
        state.level = 0.0f;
        armed = 1;
    }
    if (armed) ++state.arms;
    if (request != (held ? 1 : 0)) *(volatile int *)(dynamics + NITROUS_REQUEST) = held ? 1 : 0;
    float used = timers[0];
    int repair = 0;
    if (!(used == used) || used < 0.0f || used > 1.0e6f) {
        used = 0.0f;  // not a time the physics wrote: a full tank
        repair = !active;
    }
    // The tank refills while the player does not ask for nitrous and the
    // physics does not boost (it alone writes +0x500 while it boosts).
    if (!held && !active && tuning.recharge > 0.0f && used > 0.0f && delta_seconds > 0.0f &&
        delta_seconds < 1.0f) {
        used -= delta_seconds * tuning.duration / tuning.recharge;
        if (used < 0.0f) used = 0.0f;
        repair = 1;
    }
    if (pending_tank != 0.0f) {  // skills refill, crashes drain
        used -= pending_tank;
        pending_tank = 0.0f;
        if (used < 0.0f) used = 0.0f;
        if (used > tuning.duration) used = tuning.duration;
        repair = 1;
    }
    if (repair) *(volatile float *)(dynamics + NITROUS_USED) = used;
    float left = 1.0f - used / tuning.duration;
    state.remaining = left < 0.0f ? 0.0f : left > 1.0f ? 1.0f : left;
    state.active = active != 0;
    state.held = held != 0;
    track_run(dynamics, active, held, used, timers[1], delta_seconds);
    return armed ? NITROUS_ARMED : active ? NITROUS_BOOSTING : NITROUS_IDLE;
}
