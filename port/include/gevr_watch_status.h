#ifndef GEVR_WATCH_STATUS_H
#define GEVR_WATCH_STATUS_H
#include <stdint.h>

enum { GEVR_WATCH_FACE_OFF, GEVR_WATCH_FACE_ON, GEVR_WATCH_FACE_ONLY };
/* Keep the tiny wrist transform precise when packed into a 16.16 matrix. */
#define GEVR_WATCH_STATUS_UNITS 128.0f
extern int VrWatchFaceStatus;
extern int VrWatchGesturePause;

static inline int gevrWatchStatusChoice(int value)
{
    return value >= GEVR_WATCH_FACE_OFF && value <= GEVR_WATCH_FACE_ONLY
        ? value : GEVR_WATCH_FACE_ON;
}

static inline int gevrWatchShowsStandard(int stereo, int paused, int choice)
{
    return !stereo || paused || gevrWatchStatusChoice(choice) != GEVR_WATCH_FACE_ONLY;
}

typedef struct { uint32_t heldSince, pressUntil; int armed; } GevrWatchGestureState;

/* Returns a new activation and its short pulse separately. Disabling cancels both. */
static inline int gevrWatchGestureTick(GevrWatchGestureState *state, uint32_t now,
                                      int enabled, int reading, int blocked)
{
    if (!enabled) {
        state->heldSince = state->pressUntil = 0;
        state->armed = 1;
        return 0;
    }
    if (blocked) {
        state->heldSince = state->pressUntil = 0;
        if (reading) state->armed = 0;
        else state->armed = 1;
        return 0;
    }
    if (!reading) {
        state->heldSince = 0;
        state->armed = 1;
    } else {
        if (!state->heldSince) state->heldSince = now ? now : 1;
        if (state->armed && now - state->heldSince >= 500) {
            state->pressUntil = now + 100;
            state->armed = 0;
            return 1;
        }
    }
    return 0;
}

enum {
    GEVR_COOP_WATCH_UNTRACKED,
    GEVR_COOP_WATCH_NEUTRAL,
    GEVR_COOP_WATCH_READING,
    GEVR_COOP_WATCH_LOWERED
};
enum { GEVR_COOP_WATCH_NONE, GEVR_COOP_WATCH_OPEN, GEVR_COOP_WATCH_CLOSE };

static inline int gevrCoopWatchPoseClassify(float distance, float height, float ahead, float facing)
{
    /* Height is relative to the eyes in play-space axes, even when the head turns. */
    if (height <= -0.48f) {
        return GEVR_COOP_WATCH_LOWERED;
    }
    if (height >= -0.36f && distance >= 0.08f && distance <= 0.55f
        && ahead >= 0.60f && facing >= 0.55f) {
        return GEVR_COOP_WATCH_READING;
    }
    return GEVR_COOP_WATCH_NEUTRAL;
}

typedef struct {
    uint32_t heldSince, pressUntil;
    int dwell, needsLower, closeArmed;
} GevrCoopWatchGestureState;

static inline int gevrCoopWatchGestureTick(GevrCoopWatchGestureState *state, uint32_t now,
                                         int enabled, int pose, int watchOpen,
                                         int canOpen, int canClose)
{
    if (!enabled) {
        const GevrCoopWatchGestureState reset = {0, 0, 0, 0, 0};
        *state = reset;
        return GEVR_COOP_WATCH_NONE;
    }
    if (watchOpen || !canOpen || (int32_t)(state->pressUntil - now) <= 0) {
        state->pressUntil = 0;
    }
    if (!watchOpen && !state->pressUntil) {
        state->closeArmed = 0;
    }
    if (pose == GEVR_COOP_WATCH_LOWERED) {
        state->needsLower = 0;
    }
    /* A button-opened watch stays open until the user raises their hand or
     * closes it with a button. A gesture-opened watch is already armed. */
    if (watchOpen && pose == GEVR_COOP_WATCH_READING) {
        state->closeArmed = 1;
        state->needsLower = 1;
    }

    int wanted = GEVR_COOP_WATCH_NONE;
    if (canClose && state->closeArmed && pose == GEVR_COOP_WATCH_LOWERED) {
        wanted = GEVR_COOP_WATCH_CLOSE;
    } else if (canOpen && !state->needsLower && !state->pressUntil
               && pose == GEVR_COOP_WATCH_READING) {
        wanted = GEVR_COOP_WATCH_OPEN;
    }
    if (wanted != state->dwell) {
        state->dwell = wanted;
        state->heldSince = now;
        return GEVR_COOP_WATCH_NONE;
    }
    if (!wanted || now - state->heldSince < (wanted == GEVR_COOP_WATCH_OPEN ? 350u : 220u)) {
        return GEVR_COOP_WATCH_NONE;
    }

    state->dwell = GEVR_COOP_WATCH_NONE;
    if (wanted == GEVR_COOP_WATCH_OPEN) {
        state->needsLower = state->closeArmed = 1;
        state->pressUntil = now + 100;
        if (!state->pressUntil) state->pressUntil = 1;
    } else {
        state->closeArmed = 0;
    }
    return wanted;
}

/* Row-vector basis: clock twelve is -Z, face normal is +Y. Counter-reflect
 * the readout's X on a mirrored wrist so red stays left and radar stays readable. */
static inline void gevrWatchStatusMatrix(const float wrist[4][4], const float pivot[3],
                                        float plane, float radius, int lefty, float out[4][4])
{
    float scale = radius / GEVR_WATCH_STATUS_UNITS;
    for (int i = 0; i < 3; i++) {
        out[0][i] = wrist[0][i] * scale * (lefty ? -1.0f : 1.0f);
        out[1][i] = -wrist[2][i] * scale;
        out[2][i] = wrist[1][i] * scale;
        out[3][i] = wrist[3][i] + pivot[0] * wrist[0][i]
            + plane * wrist[1][i] + pivot[2] * wrist[2][i];
    }
    out[0][3] = out[1][3] = out[2][3] = 0;
    out[3][3] = 1;
}
#endif
