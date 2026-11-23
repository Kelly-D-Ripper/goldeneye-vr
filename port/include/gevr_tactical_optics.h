#ifndef GEVR_TACTICAL_OPTICS_H
#define GEVR_TACTICAL_OPTICS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Host display-list op for a depth-tested lens at the assembled weapon pose. */
#define G_GEVR_OPTIC_SURFACE 0x67
#define GEVR_OPTIC_SCOPE_RECORD 0x566d0000u
#define GEVR_OPTIC_SCOPE_WARM 0x566d0001u
#define GEVR_OPTIC_SCOPE_COLD 0x566d0002u
enum { GEVR_OPTIC_COLLIMATOR = 1, GEVR_OPTIC_SCOPE, GEVR_OPTIC_NIGHT, GEVR_OPTIC_THERMAL };
enum { GEVR_OPTIC_POINTS = 64 };

typedef struct GevrOpticMode {
    unsigned kind, folded, palette, firstFocalPlane, points, width, height;
    float centre[3], right[3], up[3], forward[3];
    float half[2], reticleAxis[2], angular, lowZoom, highZoom;
    float boundary[GEVR_OPTIC_POINTS][2];
    unsigned char *reticle;
} GevrOpticMode;

typedef struct GevrOpticSurface {
    const GevrOpticMode *mode;
    float centre[3], right[3], up[3], forward[3], aim[3], zero[3], half[2], zoom;
    int live;
} GevrOpticSurface;

void gevrTacticalOpticZero(GevrOpticSurface *surface, const float origin[3], const float shot[3], float distance);
const GevrOpticMode *gevrTacticalOpticMode(int part, float *zoom);
int gevrTacticalOpticMagnification(int part, float *low, float *high);
int gevrTacticalOpticCycle(int part);
int gevrTacticalOpticAdjustZoom(int part, float amount);
int gevrTacticalOpticCanAdjust(int part);
int gevrTacticalOpticCanFlip(int part);
int gevrTacticalOpticFlick(int part, unsigned tick, float alongCm, float acrossSquaredCm);
void gevrTacticalOpticFlickReset(void);
void gevrTacticalOpticFocus(int part);
int gevrTacticalOpticInput(int enabled, int click, float y);
void gevrTacticalOpticsReset(void);
int gevrTacticalOpticsWantScope(void);

#ifdef __cplusplus
}
#endif
#endif
