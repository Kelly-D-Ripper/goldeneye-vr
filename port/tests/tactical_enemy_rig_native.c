#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <PR/ultratypes.h>
#include "gevr_tactical_inventory.h"

typedef struct coord3d { union { struct { float x,y,z; }; float f[3]; }; } coord3d;
typedef struct PropRecord { int flags; coord3d pos; } PropRecord;
typedef struct ChrRecord { PropRecord *prop; s16 chrnum; int actiontype, fadealpha; } ChrRecord;
enum { ITEM_NOTHING=-1, ITEM_WPPK=1, ITEM_WPPKSIL, ITEM_TT33, ITEM_RUGER, ITEM_GOLDENGUN,
       ITEM_SILVERWPPK, ITEM_AK47, ITEM_SPECTRE, ITEM_MP5K, ITEM_SHOTGUN,
       ITEM_AUTOSHOT, ITEM_SKORPION, ITEM_GOLDWPPK=20, ITEM_TACTICAL_AK74N=21,
       ITEM_TACTICAL_PKM, ITEM_TACTICAL_M4A1, ITEM_TACTICAL_P90, ITEM_SNIPERRIFLE,
       ITEM_M16, ITEM_UZI, ITEM_FNP90, ITEM_MP5KSIL, ACT_DEAD=1, PROPFLAG_ONSCREEN=2, LOG_NOTE=0 };
#include "gevr_tactical_session.h"
int gevrTacticalItem(int item)
{
    switch (item) {
    case ITEM_AK47: return TACTICAL_GUN_AK;
    case ITEM_TACTICAL_AK74N: return TACTICAL_GUN_AK74N;
    case ITEM_SKORPION: return TACTICAL_GUN_SKORPION;
    case ITEM_TACTICAL_M4A1: return TACTICAL_GUN_M4A1;
    case ITEM_TACTICAL_P90: return TACTICAL_GUN_P90;
    case ITEM_SNIPERRIFLE: return TACTICAL_GUN_SVD;
    default: return -1;
    }
}
static int sysArgCheck(const char *arg) { return !strcmp(arg,"--tactical-physical"); }
int gevrTacticalSkorpionEnabled(void) { return 1; }
static int suppressed;
static int gevrRagdollSuppress(ChrRecord *chr) { (void)chr; return suppressed; }
static void sysLogPrintf(int level, const char *fmt, ...) { (void)level; (void)fmt; }
bool netIsActive(void) { return false; }
#define GEVR_LOOT_NATIVE_TEST 1
#include "../../src/game/gevr_enemy_rig.inc.h"

int main(void)
{
    PropRecord prop = {0};
    ChrRecord chr = {&prop, 3, ACT_DEAD, 255};
    TacticalInventory inv; tacticalInventoryInit(&inv); gevrEnemyRigReset();
    GevrEnemyRig *rig = gevrEnemyRigFor(&chr, ITEM_AK47);
    assert(rig && rig->count == 1);
    rig->positionValid = 1;
    rig->position[0] = (coord3d){.f={0,0,0}};
    rig->backPosition[0] = (coord3d){.f={0,20,0}};
    rig->mags[0].rounds = 23;
    unsigned id = rig->mags[0].id;
    coord3d hand = {.f={0,35,0}}, tip = {.f={0,48,0}}, fingerPad = {.f={0,44,0}};
    /* A face-down, offscreen corpse is reachable from above its back. */
    assert(gevrEnemyRigFindNear(&hand) == id);
    assert(!gevrEnemyRigFindNear(&tip));
    assert(gevrEnemyRigFindReachable(&tip, &fingerPad, 1, NULL) == id);
    assert(gevrEnemyRigTakeId(id, &inv, TACTICAL_LEFT_HAND));
    assert(inv.slots[TACTICAL_LEFT_HAND].id == id && inv.slots[TACTICAL_LEFT_HAND].rounds == 23);
    assert(!rig->mags[0].id && !gevrEnemyRigFindNear(&hand));
    /* A living guard does not get the extra behind-the-back reach. */
    rig->mags[0] = (TacticalMagazine){id+1,1,30,17,TACTICAL_MAG_PRIMARY};
    /* A full hand cannot remove or duplicate the remaining magazine. */
    assert(!gevrEnemyRigTakeId(id+1, &inv, TACTICAL_LEFT_HAND));
    assert(rig->mags[0].id == id+1 && rig->mags[0].rounds == 17);
    chr.actiontype = 0; prop.flags = PROPFLAG_ONSCREEN;
    assert(!gevrEnemyRigFindNear(&hand));
    hand.y=10; assert(gevrEnemyRigFindNear(&hand) == id+1);
    assert(!gevrEnemyRigFindReachable(&hand, &hand, 1, NULL));
    hand.x=50; assert(!gevrEnemyRigFindNear(&hand));

    gevrEnemyRigReset();
    tacticalInventoryInit(&inv);
    chr.actiontype = ACT_DEAD;
    rig = gevrEnemyRigFor(&chr, ITEM_SKORPION);
    assert(rig && rig->mags[0].type == TACTICAL_SKORPION_MAG_TYPE && rig->mags[0].capacity == 20);
    rig->positionValid = 1;
    hand = rig->position[0];
    id = gevrEnemyRigFindReachable(&hand, &hand, 1, NULL);
    assert(id && gevrEnemyRigTakeId(id, &inv, TACTICAL_LEFT_HAND));
    assert(inv.slots[TACTICAL_LEFT_HAND].id == id && inv.slots[TACTICAL_LEFT_HAND].rounds == 20);
    assert(tacticalMagazineGun(&inv.slots[TACTICAL_LEFT_HAND]) == TACTICAL_GUN_SKORPION);
    gevrEnemyRigReset();
    tacticalInventoryInit(&inv);
    rig = gevrEnemyRigFor(&chr, ITEM_TACTICAL_M4A1);
    assert(rig && rig->mags[0].capacity == 30);
    rig->positionValid = 1;
    hand = rig->position[0];
    id = gevrEnemyRigFindReachable(&hand, &hand, 1, NULL);
    assert(id && gevrEnemyRigTakeId(id, &inv, TACTICAL_LEFT_HAND));
    assert(inv.slots[TACTICAL_LEFT_HAND].id == id && inv.slots[TACTICAL_LEFT_HAND].rounds == 30);
    assert(tacticalMagazineGun(&inv.slots[TACTICAL_LEFT_HAND]) == TACTICAL_GUN_M4A1);

    /* Reissue a supported gun while the first slot is empty from looting. */
    rig = gevrEnemyRigFor(&chr, ITEM_TACTICAL_P90);
    assert(rig->count == 1 && !rig->mags[0].id);
    gevrEnemyRigReset();
    rig = gevrEnemyRigFor(&chr, ITEM_SNIPERRIFLE);
    assert(rig && rig->mags[0].type == TACTICAL_SVD_MAG_TYPE && rig->mags[0].capacity == 10);
    rig = gevrEnemyRigFor(&chr, ITEM_TACTICAL_P90);
    assert(rig->mags[0].capacity == 50 && tacticalMagazineGun(&rig->mags[0]) == TACTICAL_GUN_P90);
    assert(gevrTacticalNativeMagazineGun(ITEM_M16) == TACTICAL_GUN_M4A1);

    /* A ragdoll is independent of the original actor being hidden or recycled. */
    gevrEnemyRigReset();
    tacticalInventoryInit(&inv);
    rig = gevrEnemyRigFor(&chr, ITEM_AK47);
    rig->positionValid = 1;
    rig->position[0] = (coord3d){.f = {20,5,30}};
    rig->backPosition[0] = (coord3d){.f = {20,26,30}};
    rig->mags[0].rounds = 19;
    id = rig->mags[0].id;
    hand = (coord3d){.f = {20,42,30}};
    suppressed = 1;
    assert(!gevrEnemyRigFindReachable(&hand, NULL, 1, NULL));
    rig->ragdollSerial = 15;
    chr.prop = NULL;
    assert(gevrEnemyRigFindReachable(&hand, NULL, 1, NULL) == id);
    assert(gevrEnemyRigTakeId(id, &inv, TACTICAL_LEFT_HAND));
    assert(inv.slots[TACTICAL_LEFT_HAND].id == id && inv.slots[TACTICAL_LEFT_HAND].rounds == 19);
    assert(!gevrEnemyRigTakeId(id, &inv, TACTICAL_RIGHT_HAND));

    puts("enemy rig: through-body ragdoll reach, magazine identity and weapon changes passed");
    return 0;
}
