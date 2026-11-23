#ifndef GEVR_HOLLYWOOD_FX_H
#define GEVR_HOLLYWOOD_FX_H

#include <PR/gbi.h>

#ifdef __cplusplus
extern "C" {
#endif

void gevrHollywoodReset(int stage);
void gevrHollywoodTick(void);
int gevrHollywoodWeapon(int item);
struct Model;
struct PropRecord;
struct coord3d;
void gevrHollywoodFired(int hand, int item);
unsigned gevrHollywoodShotSerial(int hand);
int gevrHollywoodGuardFired(const struct PropRecord *weapon);
void gevrHollywoodTankFired(const f32 origin[3], const f32 direction[3]);
Gfx *gevrHollywoodPlayerMuzzle(Gfx *gdl, int hand, int item, const struct Model *model);
Gfx *gevrHollywoodGuardMuzzle(Gfx *gdl, const struct PropRecord *weapon, const struct Model *detailed);
int gevrHollywoodImpact(int item, int texture, const f32 point[3], const f32 normal[3], const f32 direction[3]);
/* BloodyCombat uses the same world-sprite renderer, gated by Blood patch. */
int gevrBloodyCombatEffects(void);
void gevrHollywoodCigaretteSmoke(const struct coord3d *tip);
int gevrBloodyCombatImpact(int item, int room, const f32 point[3], const f32 direction[3]);
void gevrBloodyCombatHeadBurst(int room, const f32 point[3], const f32 direction[3]);
void gevrBloodyCombatFatalHit(struct Model *model, int matrix, const f32 point[3], const f32 direction[3]);
int gevrBloodyCombatMoveWound(const struct Model *from, const struct Model *to);
void gevrBloodyCombatForgetWound(const struct Model *model);
int gevrBloodyCombatFollowWound(const struct Model *model, const f32 joint[4][4], int room);
void gevrBloodyCombatAnimatedWound(const struct Model *model, int room);
Gfx *gevrHollywoodDrawWorld(Gfx *gdl);
enum { GEVR_DEVICE_LASER = 1, GEVR_DEVICE_LIGHT = 2 };
void gevrHollywoodAccessory(int mode, const float origin[3], const float direction[3], int room);
int gevrHollywoodTexture(const void *key, const u8 **rgba, int *width, int *height);

#ifdef __cplusplus
}
#endif

#endif
