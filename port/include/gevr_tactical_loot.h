#ifndef GEVR_TACTICAL_LOOT_H
#define GEVR_TACTICAL_LOOT_H

/* A corpse's magazine can be reached from either chest or back. Measure to
 * the segment through the torso, without creating a second magazine. */
static inline float gevrLootDistanceSquared(const float hand[3], const float front[3],
                                            const float back[3], int throughBody)
{
    float d[3], v[3], length = 0, along = 0, result = 0;
    for (int k = 0; k < 3; k++) {
        d[k] = hand[k] - front[k];
        v[k] = back[k] - front[k];
        length += v[k] * v[k];
        along += d[k] * v[k];
    }
    float t = throughBody && length > 1e-8f ? along / length : 0;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    for (int k = 0; k < 3; k++) { float x = d[k] - t * v[k]; result += x * x; }
    return result;
}
#endif
