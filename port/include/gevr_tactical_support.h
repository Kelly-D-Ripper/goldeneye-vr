#ifndef GEVR_TACTICAL_SUPPORT_H
#define GEVR_TACTICAL_SUPPORT_H

/* Capture a weapon pose relative to an orthonormal tracked-hand frame. The
 * first three rows are directions and the fourth row is the position. */
static inline void gevrSupportCapture(const float hand[4][3], const float gun[4][3], float relative[4][3])
{
    for (int row = 0; row < 4; row++) for (int axis = 0; axis < 3; axis++) {
        float value = 0;
        for (int j = 0; j < 3; j++) value += (gun[row][j] - (row == 3 ? hand[3][j] : 0)) * hand[axis][j];
        relative[row][axis] = value;
    }
}
static inline void gevrSupportRestore(const float hand[4][3], const float relative[4][3], float gun[4][3])
{
    for (int row = 0; row < 4; row++) for (int j = 0; j < 3; j++) {
        float value = row == 3 ? hand[3][j] : 0;
        for (int axis = 0; axis < 3; axis++) value += relative[row][axis] * hand[axis][j];
        gun[row][j] = value;
    }
}
#endif
