#ifndef GEVR_TACTICAL_FIT_H
#define GEVR_TACTICAL_FIT_H

#include <math.h>

#include "gevr_tactical_scale.h"
#include "gevr_tactical_session.h"

/* Midpoint between the index and thumb pads of the baked pinch pose. */
static const float gevrTacticalPinchPoint[3] = {51.f, -40.f, 71.f};

/* Offsets measured in the imported Scorpio viewmodel. */
#define GEVR_SKORPION_MAG_Y (-40.f)
#define GEVR_SKORPION_MAG_Z 83.f
#define GEVR_SKORPION_MUZZLE_Y 27.7f
#define GEVR_SKORPION_MUZZLE_Z 203.f
#define GEVR_SVD_MAG_Y (-72.f)
#define GEVR_SVD_MAG_Z 208.f
#define GEVR_SVD_MUZZLE_Y 30.f
#define GEVR_SVD_MUZZLE_Z 1088.f
#define GEVR_RPG_MUZZLE_Y 57.48f
#define GEVR_RPG_MUZZLE_Z 180.14f
#define GEVR_RPG_ROUND_TAIL (-219.84f)

/* Assets are exported right/forward/up. The first-person engine basis is
 * left/up/forward, so the axis swap needs a sign change to keep handedness. */
static inline void gevrTacticalViewAxes(const float source[3], float model[3])
{
    model[0] = -source[0];
    model[1] = source[2];
    model[2] = source[1];
}

/* The native dropped and NPC weapon roots use a different proper rotation. */
static inline void gevrTacticalPropAxes(const float source[3], float model[3])
{
    model[0] = -source[1];
    model[1] = source[0];
    model[2] = source[2];
}

/* Turn the katana from the asset's forward axis into GknifeZ's closed
 * grip. The lead hand sits on the wrap just below the guard. */
static inline void gevrTacticalKatanaGripPoint(const float source[3], float model[3])
{
    const float height = source[1] + 11.f;
    const float length = source[2] - 33.f;
    model[0] = source[0];
    model[1] = 4.f + .3420201f*height + .9396926f*length;
    model[2] = 52.f - .9396926f*height + .3420201f*length;
}

/* Both controllers reach the same scabbard on the physical left shoulder. */
static const float gevrTacticalKatanaMount[3] = {13.f, 22.f, -15.f};

static inline int gevrTacticalKatanaAtMount(const float body[3], int controllerSide)
{
    const float side = body[1] * controllerSide;
    return fabsf(body[0] - gevrTacticalKatanaMount[0]) <= 28.f
        && side >= -51.f && side <= -5.f
        && fabsf(body[2] - gevrTacticalKatanaMount[2]) <= 26.f;
}

static inline int gevrTacticalKatanaAudibleSwing(float gripSpeed, float bladeSpeed, int alreadySwinging)
{
    return gripSpeed >= (alreadySwinging ? .65f : 1.15f)
        && bladeSpeed >= (alreadySwinging ? 1.30f : 2.20f);
}

/* A left glove is a reflection. A weapon or magazine riding that frame is
 * still the same object. Correct just its lateral basis, about its origin. */
static inline void gevrTacticalUnmirrorBasis(float pose[4][4])
{
    const float *x = pose[0], *y = pose[1], *z = pose[2];
    const float det = x[0] * (y[1]*z[2] - y[2]*z[1])
        + x[1] * (y[2]*z[0] - y[0]*z[2]) + x[2] * (y[0]*z[1] - y[1]*z[0]);
    if (det < 0.f)
        for (int axis = 0; axis < 3; ++axis) pose[0][axis] = -pose[0][axis];
}

/* The imported rocket points along its local Y. A held rocket points along
 * the fingers, with its shaft passing through the pinch. */
static inline void gevrTacticalRocketGrip(const float glove[4][4], float rocket[4][4])
{
    for (int col = 0; col < 3; ++col) {
        rocket[0][col] = glove[0][col];
        rocket[1][col] = glove[2][col];
        rocket[2][col] = -glove[1][col];
        rocket[3][col] = glove[3][col] + gevrTacticalPinchPoint[0]*glove[0][col]
            + gevrTacticalPinchPoint[1]*glove[1][col] + gevrTacticalPinchPoint[2]*glove[2][col];
    }
    rocket[0][3] = rocket[1][3] = rocket[2][3] = 0.f;
    rocket[3][3] = 1.f;
    gevrTacticalUnmirrorBasis(rocket);
}

static inline float gevrTacticalRocketSeat(const float rocket[4][4], const float bore[3],
    const float barrel[3], float barrelLength, float cm)
{
    float forward = 0.f, alignment = 0.f, offset[3], axis[3];
    const float *pointing = rocket[1];
    const float length = sqrtf(pointing[0]*pointing[0] + pointing[1]*pointing[1] + pointing[2]*pointing[2]);
    if (!(length > 1e-6f)) return INFINITY;
    for (int col = 0; col < 3; ++col) {
        axis[col] = barrel[col] / barrelLength;
        offset[col] = (rocket[3][col] + GEVR_RPG_ROUND_TAIL*pointing[col] - bore[col]) / cm;
        forward += offset[col]*axis[col];
        alignment += pointing[col]*axis[col] / length;
    }
    if (alignment < .87f || forward < -15.f || forward > 3.f) return INFINITY;
    float lateral = 0.f;
    for (int col = 0; col < 3; ++col) {
        const float error = offset[col] - forward*axis[col];
        lateral += error*error;
    }
    return sqrtf(lateral);
}

typedef struct GevrTacticalExpandedFit {
    float scale, lateral, lift, forward;
    float action[3], ejection[3], muzzle[3], support[3];
} GevrTacticalExpandedFit;

/* Mesh contacts in the viewmodel's left/up/forward basis. These match the
 * stock assembly; the action and feed mount of each build stay fixed. */
static inline const GevrTacticalExpandedFit *gevrTacticalExpandedFit(int gun)
{
    static const GevrTacticalExpandedFit fits[] = {
        /* Uzi */  {1.85f,  1, 15,    0, { 0, 71, 141}, {28, 41,  89}, {0, 27, 340}, {0, -9, 184}},
        /* SR-2M */{1.85f,  1, 15,   10, {-34, 27,  24}, {-24, 44,  77}, {0, 28, 254}, {0,-31, 150}},
        /* MP9 */  {1.88f,  1, 18,  -65, { 0, 56, -47}, {20, 42,  48}, {0, 47, 244}, {0,-40, 180}},
        /* EVO 3 */{1.f,   -1,-18,    3, {31, 83, 237}, {-20, 62, 139}, {0, 53, 335}, {0,-37, 230}},
        /* PM-9 */ {1.f,   -1,-21,    0, { 0, 98,  75}, {-23, 70,  14}, {0, 59, 274}, {0,-42, 135}},
        /* Mk 23 */{1.f,   -1,-35,   23, { 0, 39,  21}, {-13, 43, 105}, {0, 35, 235}, {0,-35,  26}},
        /* PKM */  {1.84f,  1, 10,   12, { 39, 27, 205}, {-59, 42, 193}, {0, 37, 820}, {0,-21, 363}},
        /* M4A1 */{1.82f,  1, 34,   45, { 25, 58,  -5}, {-20, 55, 151}, {0, 33, 605}, {0,-17, 290}},
        /* HK416 */{1.82f,  1, 34,   45, { 32, 58,  -5}, {-22, 55, 153}, {0, 32, 616}, {0,-18, 314}},
        /* AS VAL */{1.82f, 1, 20, 45, {-34, 24, 180}, {-24, 36, 190}, {0, 22, 575}, {0,-22, 300}},
        /* 1911 */  {1.78f, 1, 29, 13, {  0, 39, -33}, {-15, 37,  89}, {0, 33, 174}, {0,-29,  24}},
        /* CZ 75B */{1.f,  -1,-32, 20, {  0, 27,   2}, {-15, 28,  96}, {0, 26, 186}, {0,-31,  30}},
        /* P90 */   {1.82f, 1, 17, 18, { 30, 53, 248}, {  0,  3,  86}, {0, 47, 332}, {0,-39, 221}},
        /* G17 */   {1.80f, 1, 28, 14, {  0, 40, -28}, {-15, 37, 102}, {0, 29, 184}, {0,-30,  30}},
        /* G18C */  {1.80f, 1, 28, 14, {  0, 40, -28}, {-15, 37, 102}, {0, 29, 184}, {0,-30,  30}},
        /* G19X */  {1.80f, 1, 28, 14, {  0, 40, -28}, {-15, 37, 102}, {0, 29, 171}, {0,-30,  30}},
        /* DEagle */{1.82f, 1, 29, 15, {  0, 38, -32}, {-15, 37, 118}, {0, 32, 250}, {0,-38,  36}},
        /* M32A1 */ {1.64f, 1, 28, 44, {  0, 43, 215}, {  0, 15, 227}, {0, 35, 434}, {0,-32, 364}},
        /* Saiga */ {1.82f, 1, 25, 48, {-34, 20, 215}, {-24, 35, 176}, {0, 27, 653}, {0,-16, 375}},
        [TACTICAL_GUN_FN57 - TACTICAL_GUN_UZI] = {1.80f, 1, 31, -96, {0, 67, 26}, {-31, 63, 51}, {0, 53, 196}, {0, -1, 118}},
        [TACTICAL_GUN_MP7A2 - TACTICAL_GUN_UZI] = {1.84f, 1, 23, -61, {-1, 42, -73}, {-31, 56, 47}, {0, 45, 281}, {0, -10, 176}},
        [TACTICAL_GUN_MP7A1 - TACTICAL_GUN_UZI] = {1.84f, 1, 23, -61, {-1, 42, -73}, {-31, 56, 47}, {0, 45, 281}, {0, -10, 176}},
        [TACTICAL_GUN_KACPDW - TACTICAL_GUN_UZI] = {1.f, -1, 0, 4, {23, 117, -33}, {-24, 97, 158}, {0, 80, 324}, {0, 8, 254}},
        [TACTICAL_GUN_VITYAZ - TACTICAL_GUN_UZI] = {1.82f, 1, 24, 12, {-32, 28, 134}, {-31, 57, 136}, {0, 46, 396}, {0, -9, 279}},
        [TACTICAL_GUN_MPX - TACTICAL_GUN_UZI] = {1.82f, 1, 26, 33, {0, 55, -21}, {-31, 59, 143}, {0, 48, 234}, {0, -7, 161}},
        [TACTICAL_GUN_M1A - TACTICAL_GUN_UZI] = {1.82f, 1, 28, -75, {-28, 60, 269}, {-31, 61, 229}, {0, 50, 587}, {0, -5, 426}},
        [TACTICAL_GUN_SR3M - TACTICAL_GUN_UZI] = {1.82f, 1, 24, 14, {-33, 24, 150}, {-31, 57, 154}, {0, 46, 384}, {0, -9, 280}},
        [TACTICAL_GUN_AUGA3 - TACTICAL_GUN_UZI] = {1.82f, 1, 21, 20, {19, 21, 283}, {-31, 54, -28}, {0, 43, 306}, {0, -12, 155}},
        [TACTICAL_GUN_TOKAREV - TACTICAL_GUN_UZI] = {1.80f, 1, 29, 13, {2, 43, 51}, {-24, 61, 39}, {0, 51, 177}, {0, -3, 104}},
        [TACTICAL_GUN_STM9 - TACTICAL_GUN_UZI] = {1.82f, 1, 28, 27, {0, 47, -25}, {-31, 61, 142}, {0, 50, 233}, {0, -5, 159}},
        [TACTICAL_GUN_L85A2 - TACTICAL_GUN_UZI] = {1.f, -1, 0, 4, {-33, 100, -107}, {-25, 95, -108}, {0, 80, 344}, {0, 8, 214}},
        [TACTICAL_GUN_FAMAS - TACTICAL_GUN_UZI] = {1.f, -1, 0, 4, {0, 95, 37}, {-24, 72, -80}, {0, 80, 344}, {0, 8, 214}},
        [TACTICAL_GUN_SKS - TACTICAL_GUN_UZI] = {1.82f, 1, 28, 40, {-28, 76, 146}, {-25, 49, 204}, {0, 72, 664}, {0, 31, 261}},
    };
    return tacticalGunIsExpanded(gun) ? &fits[gun - TACTICAL_GUN_UZI] : 0;
}

/* Packed gear uses forward Y and down Z. Unity imports that retain their
 * source lateral axis need the same correction in previews and in play. */
static inline int gevrTacticalGearLateral(int kind)
{
    const GevrTacticalExpandedFit *fit = gevrTacticalExpandedFit(kind);
    if (fit) return fit->lateral;
    return kind == TACTICAL_GUN_USP || kind == TACTICAL_GUN_SKORPION || kind == TACTICAL_GUN_SVD ? -1 : 1;
}

static inline float gevrTacticalChargeTravel(int kind, float action, float manual)
{
    switch (kind) {
    case TACTICAL_GUN_UZI:
    case TACTICAL_GUN_MP9:
    case TACTICAL_GUN_EVO3:
    case TACTICAL_GUN_PM9:
    case TACTICAL_GUN_PKM:
    case TACTICAL_GUN_P90:
    case TACTICAL_GUN_M4A1:
    case TACTICAL_GUN_HK416:
    case TACTICAL_GUN_KACPDW:
    case TACTICAL_GUN_MPX:
    case TACTICAL_GUN_STM9:
    case TACTICAL_GUN_MP7A1:
    case TACTICAL_GUN_MP7A2:
    case TACTICAL_GUN_AUGA3:
    case TACTICAL_GUN_MP5:
    case TACTICAL_GUN_MP5K:
        return manual;
    default:
        return action;
    }
}

/* A shot opens the action immediately and the spring returns it over the
 * remaining game ticks. The displayed stroke is independent of hand reloads. */
static inline float gevrTacticalShotTravel(int kind, int age, int duration)
{
    if (age < 0 || age >= duration || kind == TACTICAL_GUN_RPG) return 0.f;
    const float open = 1.f - (float)age / (float)(duration - 1);
    const float stroke = kind == TACTICAL_GUN_FN57 ? 3.6f
        : tacticalGunIsPistol(kind) || kind == TACTICAL_GUN_M32A1 ? 3.2f : 4.4f;
    return stroke * open * (.4f + .6f * open);
}

/* Contacts measured on the imported player meshes in model units, before
 * applying the compact AK's size adjustment or the hand mirror. */
static inline void gevrTacticalActionSocket(int kind, int ppk, int physical, float out[3])
{
    const GevrTacticalExpandedFit *fit = gevrTacticalExpandedFit(kind);
    if (fit) {
        for (int axis = 0; axis < 3; ++axis) out[axis] = fit->action[axis];
        return;
    }
    static const float points[TACTICAL_GUN_COUNT][3] = {
        {-39.6f, 28.9f, 174.8f}, {0.f, 39.f, 25.f},
        {33.4f, 45.5f, 249.3f}, {33.9f, 46.f, 214.f},
        {-45.2f, 30.2f, 177.3f}, {0.f, 33.f, -81.f}, {18.9f, 37.5f, 124.9f}, {-47.f, 45.f, 226.f}
    };
    for (int axis = 0; axis < 3; ++axis) out[axis] = points[kind][axis];
    if (kind == TACTICAL_GUN_USP && !ppk) out[2] = 19.f;
    if (kind == TACTICAL_GUN_AK && physical)
        gevrTacticalScaleAkPoint(&out[0], &out[1], &out[2]);
}

static inline float gevrTacticalActionShift(int kind, float travelCm, int physical)
{
    return travelCm * (kind == TACTICAL_GUN_AK && physical ? 12.f : 10.f);
}

static inline void gevrTacticalEjectionPort(int kind, int ppk, int physical, float out[3])
{
    const GevrTacticalExpandedFit *fit = gevrTacticalExpandedFit(kind);
    if (fit) {
        for (int axis = 0; axis < 3; ++axis) out[axis] = fit->ejection[axis];
        return;
    }
    static const float points[TACTICAL_GUN_COUNT][3] = {
        {-22.f, 35.f, 178.f}, {-11.f, 31.f, 78.f},
        {-16.f, 30.f, 106.f}, {-16.f, 30.f, 112.f},
        {-22.f, 35.f, 178.f}, {-12.f, 25.f, -22.f}, {-5.f, 45.f, 99.f}, {-25.f, 37.f, 237.f}
    };
    for (int axis = 0; axis < 3; ++axis) out[axis] = points[kind][axis];
    if (kind == TACTICAL_GUN_USP && !ppk) out[2] = 88.f;
    if (kind == TACTICAL_GUN_AK && physical)
        gevrTacticalScaleAkPoint(&out[0], &out[1], &out[2]);
}

#endif
