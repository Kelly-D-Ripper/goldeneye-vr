#ifndef GEVR_TACTICAL_STATS_H
#define GEVR_TACTICAL_STATS_H
#include "gevr_tactical_gear.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { GEVR_FIRE_SEMI = 1, GEVR_FIRE_BURST = 2, GEVR_FIRE_AUTO = 4 };
enum { GEVR_FIRE_WAIT, GEVR_FIRE_SHOT, GEVR_FIRE_DRY };

typedef struct GevrWeaponStats {
    int modes, rpm, semiRpm, burst;
    float mass, ergo, recoil, spread, damage, flash;
} GevrWeaponStats;

typedef struct GevrFireClock {
    double next;
    int trigger, burstLeft;
} GevrFireClock;

float gevrWeaponBaseFlash(int key);
int gevrWeaponStats(int key, const GevrGearAssembly *fitted, GevrWeaponStats *out);
int gevrFireMode(const GevrWeaponStats *stats, int selected);
int gevrNextFireMode(const GevrWeaponStats *stats, int selected);
int gevrFireStep(GevrFireClock *clock, const GevrWeaponStats *stats, int mode,
                 double now, int trigger, int ready, int loaded);
const char *gevrFireModeName(int mode);
const char *gevrFireModeList(int modes);

/* Solo Tactical gameplay queries. The editor uses the assembly calculator above. */
const GevrWeaponStats *gevrTacticalHeldStats(int hand, int item);
int gevrTacticalFireRequest(int hand, int item, int trigger, int ready, int loaded);
const char *gevrTacticalFireNotice(void);
float gevrTacticalShotDamage(int hand, int item, float fallback);
float gevrTacticalHandling(int ctrl);

#ifdef __cplusplus
}
#endif
#endif
