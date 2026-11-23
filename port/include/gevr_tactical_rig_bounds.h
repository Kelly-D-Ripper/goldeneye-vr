#ifndef GEVR_TACTICAL_RIG_BOUNDS_H
#define GEVR_TACTICAL_RIG_BOUNDS_H
#include <math.h>
/* Index tip of the open Pavlov glove in controller grip coordinates, cm.
 * Derived from the source bone and the free-hand mesh scale and offset.
 * OpenXR tracks the controller, not individual finger movement. */
static inline void gevrTacticalFingertip(const float wrist[3], const float right[3],
    const float up[3], const float back[3], float cm, float size, int mirrored, float tip[3])
{
    for (int i = 0; i < 3; ++i)
        tip[i] = wrist[i] + cm * ((mirrored ? 1.f : -1.f) * 2.33f * size * right[i]
            - 3.50f * size * up[i] + (3.f - 10.88f * size) * back[i]);
}
/* Compare every physical slot, including empty ones. An empty slot that is
 * closest must not cause its neighbour's magazine to be taken instead. */
static inline int gevrTacticalRigNearestSlot(const float distance[5], float radius)
{
    int best = -1;
    float nearest = radius;
    for (int k = 0; k < 5; ++k) {
        float d = distance[k];
        if (isfinite(d) && d >= 0 && d < nearest) { nearest = d; best = k; }
    }
    return best;
}
/* Hand in torso-local centimetres, relative to a magazine's centre.
 * Magazine mesh axes are right/up/back; torso input is down/right/ahead. */
static inline float gevrTacticalRigSurfaceDistance(float down, float right, float ahead,
    const float low[3], const float high[3], float cmPerUnit)
{
    const float point[3] = {right, -down, -ahead};
    float sum = 0;
    for (int i = 0; i < 3; ++i) {
        const float lo = low[i] * cmPerUnit, hi = high[i] * cmPerUnit;
        const float delta = point[i] < lo ? lo - point[i] : point[i] > hi ? point[i] - hi : 0;
        sum += delta * delta;
    }
    return sqrtf(sum);
}
/* Turn the magazine so its broad face follows the chest and its feed
 * lips stay at the top of the pouch. */
static inline void gevrTacticalFlatRigAxes(float pose[4][3])
{
    for (int i = 0; i < 3; ++i) {
        const float right = pose[0][i];
        pose[0][i] = pose[2][i];
        pose[2][i] = -right;
    }
}

/* Distance to the centred, rotated mesh. The frame's translation is in
 * view units, its directions are unit vectors, and the half extents are cm. */
static inline float gevrTacticalFlatRigDistance(const float at[3], const float pose[4][3],
    float cm, const float half[3])
{
    const float delta[3] = {
        (at[0] - pose[3][0]) / cm, (at[1] - pose[3][1]) / cm, (at[2] - pose[3][2]) / cm
    };
    float sum = 0.f;
    for (int axis = 0; axis < 3; ++axis) {
        const float d = delta[0]*pose[axis][0] + delta[1]*pose[axis][1] + delta[2]*pose[axis][2];
        const float outside = fmaxf(0.f, fabsf(d) - half[axis]);
        sum += outside * outside;
    }
    return sqrtf(sum);
}

#endif
