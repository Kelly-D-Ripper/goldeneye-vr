#ifndef GEVR_GUARD_COVER_H
#define GEVR_GUARD_COVER_H

#include "bondtypes.h"

enum GevrGuardCoverPose {
    GEVR_GUARD_COVER_NONE,
    GEVR_GUARD_COVER_DUCK,
    GEVR_GUARD_COVER_LEAN_LEFT,
    GEVR_GUARD_COVER_LEAN_RIGHT,
    GEVR_GUARD_COVER_BLIND_LEFT,
    GEVR_GUARD_COVER_BLIND_RIGHT
};
#define GEVR_GUARD_COVER_MAX_OFFSET 96

/* Offset is in world X/Z units relative to the planted guard. */
void gevrGuardCoverPoseSet(ChrRecord *chr, int pose, float offsetX, float offsetZ);
int gevrGuardCoverPoseGet(const ChrRecord *chr, float offset[2], float *blend);
int gevrGuardCoverMuzzle(const ChrRecord *chr, int hand, coord3d *muzzle);
int gevrGuardCoverApply(ChrRecord *chr);
void gevrGuardCoverCaptureMuzzles(ChrRecord *chr);

#endif

