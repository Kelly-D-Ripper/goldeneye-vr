#ifndef GEVR_TACTICAL_SCALE_H
#define GEVR_TACTICAL_SCALE_H

/* Scale the AK about its existing grip, in native first-person model units. */
static inline void gevrTacticalScaleAkPoint(float *x, float *y, float *z)
{
    *x *= 1.2f;
    *y = -68.0f + 1.2f * (*y + 68.0f);
    *z = -20.0f + 1.2f * (*z + 20.0f);
}

/* Initial empirical correction for the reported high impacts. The model's
 * front post and rear shoulders are close to level; final zero needs VR. */
#define GEVR_AK_SIGHT_UP_SLOPE (-0.020f)
static inline void gevrTacticalAkSightDirection(const float back[3], const float up[3], float out[3])
{
    for (int i = 0; i < 3; i++) out[i] = -back[i] + GEVR_AK_SIGHT_UP_SLOPE * up[i];
}
#endif
