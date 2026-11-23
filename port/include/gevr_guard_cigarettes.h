#ifndef GEVR_GUARD_CIGARETTES_H
#define GEVR_GUARD_CIGARETTES_H

#include <bondtypes.h>

void gevrGuardCigarettesReset(void);
void gevrGuardCigarettesForget(const ChrRecord *guard);
float gevrGuardCigarettePose(ChrRecord *guard, float foregrip[3]);
void gevrGuardCigaretteGrip(const ChrRecord *guard, const float target[3], const float wrist[3]);
void gevrGuardCigaretteHit(const ChrRecord *guard, const coord3d *shot);
Gfx *gevrGuardCigarettesDraw(Gfx *g);

#endif
