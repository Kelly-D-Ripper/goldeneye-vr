#ifndef GEVR_TACTICAL_LOADOUTS_H
#define GEVR_TACTICAL_LOADOUTS_H

#include "bondconstants.h"
#include "gevr_tactical_gear.h"

static inline int gevrTacticalMissionAllowsStartingKit(int stage)
{
    /* The solo intro records give these two missions fists and the watch magnet. */
    return stage != LEVELID_BUNKER2 && stage != LEVELID_ARCHIVES;
}

struct ChrRecord;
struct PropRecord;
enum { GEVR_LOADOUT_DEFAULT_VEST = -1, GEVR_LOADOUT_DEFAULT_HELMET = -2, GEVR_LOADOUT_NO_HELMET = -1 };

typedef struct GevrLoadoutGun {
    int key;
    char preset[GEVR_GEAR_PRESET_NAME + 1];
} GevrLoadoutGun;

typedef struct GevrGuardLoadout {
    int body, vest, helmet;
    GevrLoadoutGun gun;
} GevrGuardLoadout;

typedef struct GevrGuardWeapon {
    int item, model, build;
    const char *preset;
    int offhandItem, offhandModel, offhandBuild;
    const char *offhandPreset;
} GevrGuardWeapon;

void gevrCampaignLoadoutsReset(int stage);
int gevrCampaignMissionGear(int stage, GevrLoadoutGun guns[2]);
int gevrCampaignTryDeployGear(int stage);
const GevrGuardLoadout *gevrCampaignGuardLoadout(const struct ChrRecord *chr);
int gevrCampaignGuardHelmet(const struct ChrRecord *chr);
int gevrTrainGuardVest(const struct ChrRecord *chr);
int gevrTrainGuardWeapon(const struct ChrRecord *chr);
int gevrTrainGuardSingleWeapon(const struct ChrRecord *chr);
int gevrCampaignGuardWeapon(const struct ChrRecord *chr, int *item, int *model, int *build);
int gevrCampaignRosterActive(void);
int gevrCampaignRosterWeapon(const struct ChrRecord *chr, int original, GevrGuardWeapon *weapon);
int gevrCampaignGuardWeaponFallback(const struct ChrRecord *chr, int original);
int gevrCampaignGunIsFirearm(int item);
void gevrCampaignFinishGuard(struct ChrRecord *chr, int equipUnarmed);
void gevrTacticalGuardIssueBuild(struct PropRecord *prop, int build);
void gevrTacticalGuardCopyBuild(struct PropRecord *target, const struct PropRecord *source);
const TacticalMagazine *gevrTacticalGuardMagazine(const struct PropRecord *prop);
void gevrTacticalForgetIssuedGear(const struct PropRecord *prop);

#endif
