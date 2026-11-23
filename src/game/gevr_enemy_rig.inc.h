/* Single-player enemy gear follows the chest and shoulders of the actor. */
extern bool netIsActive(void);
#include "gevr_tactical_inventory.h"
#include "gevr_tactical_mesh.h"
#include "gevr_tactical_loot.h"
#include "gevr_tactical_session.h"
#include "gevr_tactical_native_mags.h"
#ifndef GEVR_LOOT_NATIVE_TEST
#include "gevr_tactical_net.h"
#include "gevr_tactical_loot_net.h"
extern int gevrCoopIsHost(void);
#endif

typedef struct GevrEnemyRig {
    ChrRecord *chr;
    PropRecord *prop;
    s16 chrnum;
    int item, count;
    GevrTacticalVest vest;
    TacticalMagazine mags[3];
    coord3d position[3], backPosition[3];
    int positionValid;
    unsigned ragdollSerial;
    int released;
#ifndef GEVR_LOOT_NATIVE_TEST
    int bodyNum, campaign, chestMatrix;
    unsigned netRevision;
    const struct GevrEnemyOutfit *outfit;
    coord3d localFront[3], localBack[3];
#endif
} GevrEnemyRig;
static GevrEnemyRig s_enemyRigs[256];
static unsigned int s_enemyMagId = 0x80000000u;

#ifndef GEVR_LOOT_NATIVE_TEST
#include "gevr_tactical_facility.h"
#include "gevr_tactical_heavy_guards.h"
#include "gevr_tactical_gear.h"
#include "gevr_static_mesh.h"
#include "gevr_tactical_enemy_outfits.h"
#include "gevr_tactical_loadouts.h"

static int gevrEnemyCampaignGear(void)
{
    return sysArgCheck("--tactical-campaign-gear") && sysArgCheck("--tactical-enemy-rigs")
        && gevrTacticalCampaignRules();
}

static GevrTacticalVest gevrEnemyVestSelected(const ChrRecord *chr)
{
    const GevrGuardEquipment *remote = gevrTacticalRemoteGuardGear(chr);
    if (remote) return (GevrTacticalVest)remote->vest;
    const GevrGuardLoadout *loadout = gevrCampaignGuardLoadout(chr);
    if (loadout && loadout->vest >= GEVR_VEST_NONE) return gevrTacticalGuardVest(loadout->vest);
    const int trainVest = gevrTrainGuardVest(chr);
    if (trainVest >= GEVR_VEST_NONE) return (GevrTacticalVest)trainVest;
    if (gevrTacticalKorundGuard(chr)) return GEVR_VEST_KORUND;
    if (gevrTacticalJanusGuard(chr)) return GEVR_VEST_SLICK;
    const bool klmk = chr->bodynum == BODY_Russian_Soldier && sysArgCheck("--tactical-klmk-vest")
        && sysArgCheck("--tactical-enemy-rigs") && gevrTacticalCampaignRules()
        && ((bossGetStageNum() == LEVELID_DAM && chr->chrnum == sysArgGetInt("--tactical-klmk-guard", 11))
            || (gevrTacticalFacilityArmor() && gevrTacticalFacilityVest(chr->chrnum)));
    if (klmk) return GEVR_VEST_KLMK;
    const GevrEnemyOutfit *outfit = gevrEnemyCampaignGear() ? gevrEnemyOutfit(chr) : NULL;
    return outfit ? gevrEnemyOutfitVest(chr, outfit) : GEVR_VEST_NONE;
}
#endif

void gevrEnemyRigReset(void)
{
    memset(s_enemyRigs, 0, sizeof(s_enemyRigs));
    s_enemyMagId = 0x80000000u;
}

/* A retained corpse's script ID becomes -1 while its model/prop persist.
 * Retire the rig key too, preserving the same magazine IDs and counts. */
void gevrEnemyRigRetireChr(ChrRecord *chr)
{
    if (!chr || !chr->prop) return;
    for (int k = 0; k < 256; ++k)
        if (s_enemyRigs[k].chr == chr && s_enemyRigs[k].prop == chr->prop
            && s_enemyRigs[k].chrnum == chr->chrnum) {
            s_enemyRigs[k].chrnum = -1;
            if (sysArgCheck("--tactical-physical"))
                sysLogPrintf(LOG_NOTE, "enemy rig: retained corpse %d with magazine id %u", chr->chrnum, s_enemyRigs[k].mags[0].id);
        }
}

static int gevrEnemyMagazineGun(int item)
{
    const int gun = gevrTacticalItem(item);
    if (gun >= 0 && gun != TACTICAL_GUN_M32A1 && gun != TACTICAL_GUN_RPG) return gun;
#ifndef GEVR_LOOT_NATIVE_TEST
    if (gevrEnemyCampaignGear()) {
        const int native = gevrTacticalNativeMagazineGun(item);
        if (native >= 0 && (!tacticalGunIsExpanded(native) || (gevrTacticalExpandedEnabled()
                && gevrTacticalGearBuildAvailable(gevrTacticalGearKey(native))))) return native;
    }
#endif
    return -1;
}

static GevrEnemyRig *gevrEnemyRigFor(ChrRecord *chr, int item)
{
    GevrEnemyRig *rig = NULL, *freeRig = NULL;
    for (int k = 0; k < 256; ++k) {
        GevrEnemyRig *r = &s_enemyRigs[k];
        const int spent = r->released && !r->mags[0].id && !r->mags[1].id && !r->mags[2].id;
        if ((!r->ragdollSerial || spent) && r->chr && (r->chr->prop != r->prop || r->chr->chrnum != r->chrnum))
            memset(r, 0, sizeof(*r));
        if (r->chr == chr && r->prop == chr->prop && r->chrnum == chr->chrnum) {
            rig = r;
            break;
        }
        if (!r->chr && !freeRig) freeRig = r;
    }
    if (rig && (rig->ragdollSerial || item == ITEM_NOTHING || item == rig->item)) return rig;
    const int gun = gevrEnemyMagazineGun(item);
#ifndef GEVR_LOOT_NATIVE_TEST
    const GevrEnemyOutfit *outfit = gevrEnemyCampaignGear() ? gevrEnemyOutfit(chr) : NULL;
    const GevrGuardLoadout *loadout = gevrCampaignGuardLoadout(chr);
#else
    const void *outfit = NULL;
    const void *loadout = NULL;
#endif
    if (!rig) {
        if (!freeRig || (!loadout && !outfit && gun < 0)) return NULL;
#ifndef GEVR_LOOT_NATIVE_TEST
        if (gevrEnemyCampaignGear() && !loadout && !outfit) return NULL;
#endif
        rig = freeRig;
        rig->chr = chr;
        rig->prop = chr->prop;
        rig->chrnum = chr->chrnum;
        rig->item = ITEM_NOTHING;
#ifndef GEVR_LOOT_NATIVE_TEST
        rig->vest = gevrEnemyVestSelected(chr);
        rig->bodyNum = chr->bodynum;
        rig->campaign = gevrEnemyCampaignGear();
        rig->outfit = outfit;
#endif
    }
    if (gun < 0) {
        if (rig->count == 0) rig->item = item;
        return rig;
    }
    const int ppk = sysArgCheck("--tactical-ppk");
    const unsigned type = tacticalGunMagazineType(gun, ppk ? 4u : 2u);
    unsigned capacity = tacticalGunCapacity(gun, ppk), gearPart = 0;
    const int first = rig->count == 0;
    rig->item = item;
    if (first) rig->count = 1 + ((unsigned short)chr->chrnum % 3);
#ifndef GEVR_LOOT_NATIVE_TEST
    const PropRecord *weapon = chr->weapons_held[GUNRIGHT];
    if (!weapon || !weapon->weapon || weapon->weapon->weaponnum != item) weapon = chr->weapons_held[GUNLEFT];
    const TacticalMagazine *issued = gevrTacticalGuardMagazine(weapon);
    if (issued && issued->type == type) {
        if (first && item == ITEM_TACTICAL_M4A1 && gevrTacticalJanusGuard(chr) && !loadout) rig->count = 3;
        gearPart = issued->gear_part;
        capacity = issued->capacity;
    }
#endif
    const int magClass = tacticalGunIsPistol(gun) ? TACTICAL_MAG_PISTOL : TACTICAL_MAG_PRIMARY;
    for (int k = 0; k < rig->count; ++k) {
        TacticalMagazine *slot = &rig->mags[k];
        if (!first && (!slot->id || (slot->type == type && slot->capacity == capacity && slot->gear_part == gearPart)))
            continue;
        *slot = (TacticalMagazine){s_enemyMagId++, type, capacity, capacity, magClass, gearPart};
    }
    return rig;
}

static int gevrEnemyRigAvailable(const GevrEnemyRig *rig)
{
    if (!rig->positionValid) return FALSE;
    if (rig->ragdollSerial) return TRUE;
    return rig->chr && rig->chr->prop == rig->prop && rig->chr->chrnum == rig->chrnum
        && rig->chr->fadealpha == 255 && !gevrRagdollSuppress(rig->chr);
}

static GevrEnemyRig *gevrEnemyRigFindMag(unsigned int id, int *slot)
{
    for (int i = 0; i < 256; ++i) {
        GevrEnemyRig *r = &s_enemyRigs[i];
        if (!gevrEnemyRigAvailable(r)) continue;
        for (int k = 0; k < r->count; ++k) if (id && r->mags[k].id == id) {
            *slot=k; return r;
        }
    }
    return NULL;
}

unsigned int gevrEnemyRigFindReachable(const coord3d *tip, const coord3d *fingerPad,
                                      int corpsesOnly, float *distanceSquared)
{
    unsigned int best = 0;
    float bestDist = 1e30f;
    const int physical = sysArgCheck("--tactical-physical");
    if (!tip) return 0;
    for (int i = 0; i < 256; ++i) {
        GevrEnemyRig *r = &s_enemyRigs[i];
        if (!gevrEnemyRigAvailable(r)) continue;
        const int corpse = r->ragdollSerial || (physical && r->chr->actiontype == ACT_DEAD);
        if (!corpse && (corpsesOnly || !(r->prop->flags & PROPFLAG_ONSCREEN))) continue;
        for (int k = 0; k < r->count; ++k) {
            const TacticalMagazine *mag = &r->mags[k];
            const float limit = corpse ? 625.f : 400.f;
            if (tacticalMagazineGun(mag) < 0) continue;
            float dist = gevrLootDistanceSquared(tip->f, r->position[k].f, r->backPosition[k].f, corpse);
            /* A curled finger can reach a pouch just behind the index tip.
             * On corpses, both contacts refer to the same front/back rig. */
            if (corpse && fingerPad)
                dist = fminf(dist, gevrLootDistanceSquared(fingerPad->f, r->position[k].f, r->backPosition[k].f, TRUE));
            if (dist <= limit && dist < bestDist) {
                bestDist = dist;
                best = mag->id;
            }
        }
    }
    if (distanceSquared) *distanceSquared = bestDist;
    return best;
}

unsigned int gevrEnemyRigFindNear(const coord3d *world)
{
    return world ? gevrEnemyRigFindReachable(world, NULL, FALSE, NULL) : 0;
}

int gevrEnemyRigGripOffset(unsigned int id, const coord3d *world, float offset[3])
{
    int slot;
    GevrEnemyRig *r=gevrEnemyRigFindMag(id,&slot);
    if (!r || !world || !offset) return FALSE;
    for (int k=0;k<3;k++) offset[k]=world->f[k]-r->position[slot].f[k];
    return TRUE;
}

int gevrEnemyRigPullReady(unsigned int id, const coord3d *world, const float start[3])
{
    float offset[3],x,y,z;
    if (!start || !gevrEnemyRigGripOffset(id,world,offset)) return FALSE;
    x=offset[0]-start[0]; y=offset[1]-start[1]; z=offset[2]-start[2];
    return x*x+y*y+z*z >= 25.0f
        && sqrtf(offset[0]*offset[0]+offset[1]*offset[1]+offset[2]*offset[2])
           >= sqrtf(start[0]*start[0]+start[1]*start[1]+start[2]*start[2]) + 3.0f;
}

/* Commit removal only once the player's hand has accepted this exact ID. */
int gevrEnemyRigTakeId(unsigned int id, TacticalInventory *inventory, int hand)
{
    int slot;
    GevrEnemyRig *r=gevrEnemyRigFindMag(id,&slot);
    if (!r || !inventory || tacticalAddMagazine(inventory,hand,r->mags[slot]) != TACTICAL_OK) return FALSE;
    sysLogPrintf(LOG_NOTE, "enemy rig: taken chr %d item %d id %u at %.1f %.1f %.1f, ragdoll %u",
        r->chrnum,r->item,id,(double)r->position[slot].x,(double)r->position[slot].y,(double)r->position[slot].z,
        r->ragdollSerial);
    r->mags[slot].id=0;
    return TRUE;
}

/* Exercised only through the scripted desktop test hook. */
int gevrEnemyRigTestTakeType(TacticalInventory *inventory, int hand, unsigned int type, unsigned int capacity)
{
    for (int i = 0; i < 256; ++i) {
        GevrEnemyRig *r = &s_enemyRigs[i];
        if (!r->chr || !r->positionValid || r->chr->prop != r->prop) continue;
        for (int k = 0; k < r->count; ++k) if (r->mags[k].id
            && (!type || r->mags[k].type==type) && (!capacity || r->mags[k].capacity==capacity)) {
            coord3d start=r->position[k], withdrawn=start;
            unsigned int id=gevrEnemyRigFindNear(&start);
            float offset[3];
            if (type == 5 && sysArgCheck("--input-script"))
                sysLogPrintf(LOG_NOTE, "enemy rig test: chr %d mag %u nearest %u fade %d suppressed %d hand-id %u",
                    r->chrnum, r->mags[k].id, id, r->chr->fadealpha, gevrRagdollSuppress(r->chr), inventory->slots[hand].id);
            withdrawn.x+=6;
            if (id==r->mags[k].id && gevrEnemyRigGripOffset(id,&start,offset) && !gevrEnemyRigPullReady(id,&start,offset)
                && gevrEnemyRigPullReady(id,&withdrawn,offset)
                && gevrEnemyRigTakeId(id,inventory,hand)) return TRUE;
        }
    }
    return FALSE;
}

int gevrEnemyRigTestTakeRetired(TacticalInventory *inventory, int hand)
{
    for (int i = 0; i < 256; ++i) {
        GevrEnemyRig *r = &s_enemyRigs[i];
        if (!gevrEnemyRigAvailable(r)
            || (!r->ragdollSerial && (r->chrnum != -1 || r->chr->actiontype != ACT_DEAD))) continue;
        for (int k = 0; k < r->count; ++k)
            if (r->mags[k].id && gevrEnemyRigTakeId(r->mags[k].id, inventory, hand)) return TRUE;
    }
    return FALSE;
}

/* Desktop regression: reach from beyond a retained body's back, outside
 * the former chest-only radius, and take the exact selected magazine. */
unsigned int gevrEnemyRigTestBackProbe(coord3d *out, float *frontDistance)
{
    for (int i = 0; i < 256; ++i) {
        GevrEnemyRig *r = &s_enemyRigs[i];
        if (!gevrEnemyRigAvailable(r)
            || (!r->ragdollSerial && (r->chrnum != -1 || r->chr->actiontype != ACT_DEAD))) continue;
        for (int k = 0; k < r->count; k++) if (r->mags[k].id) {
            coord3d probe = r->backPosition[k];
            float d[3], len = 0;
            for (int j = 0; j < 3; j++) { d[j] = r->backPosition[k].f[j] - r->position[k].f[j]; len += d[j]*d[j]; }
            len = sqrtf(len);
            if (len < 1e-4f) continue;
            for (int j = 0; j < 3; j++) probe.f[j] += 20.0f * d[j] / len;
            unsigned id = gevrEnemyRigFindNear(&probe);
            int slot = -1;
            GevrEnemyRig *chosen = gevrEnemyRigFindMag(id, &slot);
            if (chosen != r || slot < 0) continue;
            const float distance = sqrtf(gevrLootDistanceSquared(probe.f,
                r->position[slot].f, r->backPosition[slot].f, FALSE));
            if (distance <= 20.f) continue;
            *out = probe;
            *frontDistance = distance;
            return id;
        }
    }
    return 0;
}

int gevrEnemyRigTestTakeFromBack(TacticalInventory *inventory, int hand)
{
    coord3d probe;
    float distance;
    unsigned id = gevrEnemyRigTestBackProbe(&probe, &distance);
    int slot;
    GevrEnemyRig *r = gevrEnemyRigFindMag(id, &slot);
    if (!r) return FALSE;
    const unsigned rounds = r->mags[slot].rounds;
    if (!gevrEnemyRigTakeId(id, inventory, hand)) return FALSE;
    sysLogPrintf(LOG_NOTE, "enemy rig: back-loot id %u rounds %u front distance %.1f cm same id %d same rounds %d",
        id, rounds, (double)distance, inventory->slots[hand].id == id, inventory->slots[hand].rounds == rounds);
    return TRUE;
}

#ifndef GEVR_LOOT_NATIVE_TEST
static ModelNode *gevrEnemyRigHitbox(ModelNode *node, int part)
{
    for (; node; node = node->Next) {
        ModelNode *found;
        if ((node->Opcode & 0xff) == MODELNODE_OPCODE_BBOX && node->Data
            && node->Data->BoundingBox.ModelNumber == part) return node;
        if ((found = gevrEnemyRigHitbox(node->Child, part))) return found;
    }
    return NULL;
}

static void gevrEnemyVestFit(const struct bbox *box, Mtxf *fit)
{
    matrix_4x4_set_identity(fit);
    fit->m[0][0] = (box->xmax - box->xmin) * .00102f;
    fit->m[1][1] = (box->ymax - box->ymin) * .00128f;
    fit->m[2][2] = (box->zmax - box->zmin) * .00118f;
    fit->m[3][0] = (box->xmin + box->xmax) * .5f;
    fit->m[3][1] = (box->ymin + box->ymax) * .5f - (box->ymax - box->ymin) * .115f;
    fit->m[3][2] = (box->zmin + box->zmax) * .5f;
}

static int gevrEnemyHeldItem(const ChrRecord *chr)
{
    const PropRecord *right = chr->weapons_held[GUNRIGHT];
    const PropRecord *left = chr->weapons_held[GUNLEFT];
    const int rightItem = right && right->weapon ? right->weapon->weaponnum : ITEM_NOTHING;
    if (rightItem != ITEM_NOTHING && (!left || !left->weapon || rightItem == left->weapon->weaponnum
            || gevrEnemyMagazineGun(rightItem) >= 0)) return rightItem;
    return left && left->weapon ? left->weapon->weaponnum : rightItem;
}

void gevrPreloadTacticalStageGear(int stage)
{
    extern ChrRecord *g_ChrSlots;
    extern s32 g_NumChrSlots;
    if (!gevrTacticalGearCacheEnabled() || !gevrTacticalCampaignRules()) return;
    const u64 begin = sysGetMicroseconds();
    const int campaign = gevrEnemyCampaignGear();
    bool green = FALSE;
    int actors = 0;
    gevrTacticalCacheBegin();
    for (int i = 0; i < g_NumChrSlots; ++i) {
        ChrRecord *chr = &g_ChrSlots[i];
        if (!chr->model || !chr->prop || chr->prop->type != PROP_TYPE_CHR) continue;
        const GevrEnemyOutfit *outfit = campaign ? gevrEnemyOutfit(chr) : NULL;
        const GevrGuardLoadout *loadout = gevrCampaignGuardLoadout(chr);
        const GevrTacticalVest selected = gevrEnemyVestSelected(chr);
        gevrTacticalCacheVest(selected, chr->bodynum);
        if (outfit && (!loadout || loadout->vest < 0)) {
            gevrTacticalCacheVest(outfit->vests[0], chr->bodynum);
            gevrTacticalCacheVest(outfit->vests[1], chr->bodynum);
            green |= chr->bodynum == BODY_Russian_Soldier;
        }
        PropRecord *hat = chr->handle_positiondata_hat;
        if (hat && hat->obj) gevrTacticalCacheModel(hat->obj->model, -1);
        for (int hand = 0; hand < 2; ++hand) {
            PropRecord *gun = chr->weapons_held[hand];
            if (gun && gun->weapon) gevrTacticalCacheModel(gun->obj->model, gevrTacticalItem(gun->weapon->weaponnum));
        }
        ++actors;
    }
    for (PropRecord *prop = chrpropGetActiveTail(); prop; prop = prop->prev) {
        if (prop->type == PROP_TYPE_WEAPON && !prop->parent && prop->weapon)
            gevrTacticalCacheModel(prop->obj->model, gevrTacticalItem(prop->weapon->weaponnum));
    }
    if (green) {
        /* Russian reinforcements can roll any of the default green outfits. */
        gevrTacticalCacheVest(GEVR_VEST_ZABRALO, BODY_Russian_Soldier);
        gevrTacticalCacheVest(GEVR_VEST_IOTV, BODY_Russian_Soldier);
        gevrTacticalCacheVest(GEVR_VEST_REDUT_T5, BODY_Russian_Soldier);
    }
    if (gevrTacticalMissionAllowsStartingKit(stage)) {
        int primary, secondary;
        if (gevrTacticalGearLoadout(stage, &primary, &secondary)
            || (gevrCampaignTryDeployGear(stage) && gevrTacticalGearLoadout(stage, &primary, &secondary))) {
            gevrTacticalCachePlayerWeapon(gevrTacticalItem(primary), gevrTacticalGearDeployedBuild(FALSE), FALSE);
            gevrTacticalCachePlayerWeapon(gevrTacticalItem(secondary), gevrTacticalGearDeployedBuild(TRUE),
                gevrTacticalWeaponSuppressed(secondary, GUNRIGHT));
        } else if (sysArgCheck("--tactical-ppk-start")) {
            gevrTacticalCachePlayerWeapon(TACTICAL_GUN_USP, -1, TRUE);
        }
    }
    gevrTacticalCachePlayerHands();
    gevrTacticalCacheEnd(stage, begin, actors);
}

void gevrEnemyRigAuditStage(void)
{
    extern ChrRecord *g_ChrSlots;
    extern s32 g_NumChrSlots;
    if (!gevrEnemyCampaignGear() || !sysArgCheck("--input-script")) return;
    int actors = 0, magazines = 0, missing = 0;
    for (int slot = 0; slot < g_NumChrSlots; ++slot) {
        ChrRecord *chr = &g_ChrSlots[slot];
        if (!chr->model || !chr->model->obj || !chr->prop || chr->prop->type != PROP_TYPE_CHR) continue;
        for (int hand = GUNRIGHT; hand <= GUNLEFT; ++hand) {
            const PropRecord *held = chrGetEquippedWeaponProp(chr, hand);
            if (!held || !held->weapon) continue;
            const int heldItem = held->weapon->weaponnum;
            if (gevrCampaignGunIsFirearm(heldItem) && gevrTacticalItem(heldItem) < 0)
                sysLogPrintf(LOG_NOTE, "campaign stock weapon: stage %d actor %d body %d head %d hand %d item %d",
                    bossGetStageNum(), chr->chrnum, chr->bodynum, chr->headnum, hand, heldItem);
        }
        if (!gevrEnemyOutfit(chr) && !gevrCampaignGuardLoadout(chr)) continue;
        const int item = gevrEnemyHeldItem(chr);
        GevrEnemyRig *rig = gevrEnemyRigFor(chr, item);
        const int count = rig ? rig->count : 0;
        const GevrTacticalVest vest = rig ? rig->vest : GEVR_VEST_NONE;
        ModelNode *chest = gevrEnemyRigHitbox(chr->model->obj->RootNode, HIT_CHEST);
        const int matrix = modelFindNodeMtxIndex(chest, 0);
        const int vestReady = vest == GEVR_VEST_NONE || (gevrTacticalVestRenderList(vest, chr->bodynum, FALSE)
            && (vest < GEVR_VEST_ZABRALO || vest > GEVR_VEST_REDUT_T5
                || (gevrTacticalVestArmList(vest, 0, FALSE) && gevrTacticalVestArmList(vest, 1, FALSE))));
        float low[3], high[3];
        const int magsReady = !count || (tacticalMagazineGun(&rig->mags[0]) >= 0
            && gevrTacticalLooseMagBounds(rig->mags[0].gear_part, tacticalMagazineGun(&rig->mags[0]), low, high));
        const int ready = rig && matrix >= 0 && matrix < chr->model->obj->numMatrices && vestReady && magsReady;
        actors++;
        magazines += count;
        missing += !ready;
        sysLogPrintf(LOG_NOTE, "campaign gear: actor %d body %d item %d vest %d mags %d type %u ready %d",
            chr->chrnum, chr->bodynum, item, vest, count, count ? rig->mags[0].type : 0u, ready);
    }
    sysLogPrintf(LOG_NOTE, "campaign gear: stage %d actors %d magazines %d missing %d",
        bossGetStageNum(), actors, magazines, missing);
}

static void gevrEnemyVestArmPose(const Mtxf *chest, Mtxf *arm)
{
    /* Follow the upper arm, with a little resistance to roll from the strap. */
    float armLength2 = 0.f, chestLength2 = 0.f, towardY = 0.f;
    for (int axis = 0; axis < 3; ++axis) {
        armLength2 += arm->m[2][axis] * arm->m[2][axis];
        chestLength2 += chest->m[2][axis] * chest->m[2][axis];
        towardY += chest->m[2][axis] * arm->m[1][axis];
    }
    const float length = sqrtf(armLength2 * chestLength2);
    if (length < 1e-5f) return;
    const float roll = .32f * asinf(fmaxf(-1.f, fminf(1.f, towardY / length)));
    const float sine = sinf(roll), cosine = cosf(roll);
    for (int axis = 0; axis < 3; ++axis) {
        const float y = arm->m[1][axis], z = arm->m[2][axis];
        arm->m[1][axis] = y*cosine - z*sine;
        arm->m[2][axis] = z*cosine + y*sine;
    }
}

static bool gevrEnemyVestArmMount(Model *model, int chest, int side, Mtxf *pose)
{
    const int part = side == 0 ? HIT_LEFT_SHOULDER : HIT_RIGHT_SHOULDER;
    ModelNode *node = gevrEnemyRigHitbox(model->obj->RootNode, part);
    const int index = modelFindNodeMtxIndex(node, 0);
    if (!node || index < 0 || index >= model->obj->numMatrices) return FALSE;
    *pose = model->render_pos[index].pos;
    gevrEnemyVestArmPose(&model->render_pos[chest].pos, pose);
    return TRUE;
}

static Gfx *gevrEnemyDrawVestArms(Gfx *g, Model *model, int chest, GevrTacticalVest vest, int lowDetail)
{
    for (int side = 0; side < 2; ++side) {
        Gfx *list = gevrTacticalVestArmList(vest, side, lowDetail);
        Mtxf pose;
        if (!list || !gevrEnemyVestArmMount(model, chest, side, &pose)) continue;
        Mtx *fixed = dynAllocateMatrix();
        guMtxF2L(pose.m, fixed);
        gSPMatrix(g++, osVirtualToPhysical(fixed), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
        gSPDisplayList(g++, list);
    }
    return g;
}

static ModelNode *gevrEnemyVestImpact(ChrRecord *chr, const ShotData *shot, coord3d *impact)
{
    const GevrTacticalVest vest = gevrEnemyVestSelected(chr);
    if (vest == GEVR_VEST_NONE || chr->fadealpha != 255 || !chr->model->render_pos
        || !(shot->viewDir.z < -1e-6f) || !gevrTacticalIsArmorShot(shot->weapon)) return NULL;
    Model *model = chr->model;
    ModelNode *chest = gevrEnemyRigHitbox(model->obj->RootNode, HIT_CHEST);
    if (!chest) return NULL;
    const int matrix = modelFindNodeMtxIndex(chest, 0);
    if (matrix < 0 || matrix >= model->obj->numMatrices) return NULL;

    Mtxf fit, pose, inverse;
    gevrEnemyVestFit(&chest->Data->BoundingBox.Bounds, &fit);
    if (fit.m[0][0] <= 0.f || fit.m[1][1] <= 0.f || fit.m[2][2] <= 0.f) return NULL;
    matrix_4x4_multiply_homogeneous(&model->render_pos[matrix].pos, &fit, &pose);
    matrix_4x4_invert_affine(&pose, &inverse);
    coord3d localOrigin = shot->viewOrigin, localDir = shot->viewDir;
    mtx4TransformVecInPlace(&inverse, &localOrigin);
    mtx4RotateVecInPlace(&inverse, &localDir);
    const float limit = (shot->maxdist + shot->viewOrigin.z) / -shot->viewDir.z;
    const float torso = gevrTacticalVestRaycast(vest, chr->bodynum, localOrigin.f, localDir.f, limit);
    float at = torso >= 0.f ? torso : limit;
    if (vest >= GEVR_VEST_ZABRALO && vest <= GEVR_VEST_REDUT_T5) {
        for (int side = 0; side < 2; ++side) {
            Mtxf arm;
            if (!gevrEnemyVestArmMount(model, matrix, side, &arm)) continue;
            matrix_4x4_invert_affine(&arm, &inverse);
            localOrigin = shot->viewOrigin;
            localDir = shot->viewDir;
            mtx4TransformVecInPlace(&inverse, &localOrigin);
            mtx4RotateVecInPlace(&inverse, &localDir);
            const float sleeve = gevrTacticalVestArmRaycast(vest, side, localOrigin.f, localDir.f, at);
            if (sleeve >= 0.f) at = sleeve;
        }
    }
    if (!(at < limit)) return NULL;
    for (int axis = 0; axis < 3; ++axis) impact->f[axis] = shot->viewOrigin.f[axis] + at * shot->viewDir.f[axis];
    return chest;
}

static Gfx *gevrEnemyRigBox(Gfx *g, float x0, float x1, float y0, float y1,
                            float z0, float z1, const u8 color[3], float tint, Vtx *v)
{
    if (!v) v = dynAllocateVertices(16);
    float c = fminf((x1-x0)*.16f, (y1-y0)*.10f);
    const float xy[8][2] = {
        {x0+c,y0},{x1-c,y0},{x1,y0+c},{x1,y1-c},
        {x1-c,y1},{x0+c,y1},{x0,y1-c},{x0,y0+c}
    };
    for (int i = 0; i < 16; ++i) {
        int j=i&7;
        float shade=(i<8 ? .68f : (j==3 || j==4 || j==5) ? 1.12f : .92f);
        v[i].v.ob[0] = (s16)xy[j][0]; v[i].v.ob[1] = (s16)xy[j][1]; v[i].v.ob[2] = (s16)(i<8 ? z0 : z1);
        v[i].v.flag = 0; v[i].v.tc[0] = v[i].v.tc[1] = 0;
        for (int k = 0; k < 3; ++k) v[i].v.cn[k] = (u8)fminf(255, color[k]*shade*tint);
        v[i].v.cn[3] = 255;
    }
    gSPVertex(g++, osVirtualToPhysical(v), 16, 0);
    for (int i=0; i<8; ++i) { int j=(i+1)&7; gSP2Triangles(g++,i,j,j+8,0,i,j+8,i+8,0); }
    for (int i=1; i<7; i+=2) { gSP2Triangles(g++,8,8+i,9+i,0,8,9+i,10+i,0); }
    return g;
}

/* Thin webbing, laid diagonally over the chest towards each shoulder. */
static Gfx *gevrEnemyRigWebbing(Gfx *g, float x0, float y0, float z0,
                                 float x1, float y1, float z1, float width, const u8 color[3], Vtx *v)
{
    if (!v) v = dynAllocateVertices(4);
    const float xyz[4][3]={{x0-width,y0,z0},{x0+width,y0,z0},{x1+width,y1,z1},{x1-width,y1,z1}};
    for (int i=0;i<4;++i) {
        for(int j=0;j<3;++j)v[i].v.ob[j]=(s16)xyz[i][j];
        v[i].v.flag=0; v[i].v.tc[0]=v[i].v.tc[1]=0;
        for (int j = 0; j < 3; ++j) v[i].v.cn[j] = (u8)(color[j]*.71f);
        v[i].v.cn[3]=255;
    }
    gSPVertex(g++,osVirtualToPhysical(v),4,0);
    gSP2Triangles(g++,0,1,2,0,0,2,3,0);
    return g;
}

typedef struct GevrRigCanvasFit {
    float x, y, width, height, front, depth;
    float pouchFront[3], pouchHalfWidth, pouchBottom, pouchTop;
    int fitted, lowDetail;
    u8 cloth[4];
} GevrRigCanvasFit;

static Gfx *gevrEnemyCanvasPanel(Gfx *g, const GevrRigCanvasFit *fit, Vtx *vertices)
{
    const float x = fit->x, y = fit->y, w = fit->width, h = fit->height;
    const float front = fit->front, depth = fit->depth;
    if (fit->fitted) return g;
    g = gevrEnemyRigBox(g, x-w*.34f, x+w*.34f, y-h*.27f, y+h*.15f,
        front-depth*.12f, front+depth*.015f, fit->cloth, .68f, vertices);
    Vtx *webbing = vertices ? vertices + 16 : NULL;
    for (int side = -1; side <= 1; side += 2) {
        g = gevrEnemyRigWebbing(g, x+side*w*.21f, y+h*.12f, front-depth*.01f,
            x+side*w*.31f, y+h*.39f, front-depth*.08f, w*.032f, fit->cloth, webbing);
        if (webbing) webbing += 4;
        g = gevrEnemyRigWebbing(g, x+side*w*.31f, y+h*.39f, front-depth*.08f,
            x+side*w*.29f, y+h*.44f, front-depth*.40f, w*.032f, fit->cloth, webbing);
        if (webbing) webbing += 4;
    }
    return g;
}

static Gfx *gevrEnemyCanvasPouch(Gfx *g, const GevrRigCanvasFit *fit, int pouch, Vtx *vertices)
{
    const float w = fit->width, h = fit->height, depth = fit->depth, y = fit->y;
    const float x = fit->x + (pouch-1)*w*.225f, z = fit->pouchFront[pouch], half = fit->pouchHalfWidth;
    g = gevrEnemyRigBox(g, x-half, x+half, fit->pouchBottom, fit->pouchTop,
        z-depth*.01f, z+depth*.22f, fit->cloth, 1.f, vertices);
    if (fit->lowDetail) return g;
    g = gevrEnemyRigBox(g, x-half+w*.012f, x+half-w*.012f, y+h*.035f, y+h*.083f,
        z+depth*.22f, z+depth*.235f, fit->cloth, .75f, vertices ? vertices + 16 : NULL);
    g = gevrEnemyRigBox(g, x-w*.018f, x+w*.018f, y+h*.005f, y+h*.055f,
        z+depth*.235f, z+depth*.245f, fit->cloth, .52f, vertices ? vertices + 32 : NULL);
    return gevrEnemyRigBox(g, x-half+w*.016f, x+half-w*.016f, fit->pouchBottom+h*.04f,
        fit->pouchBottom+h*.048f, z+depth*.221f, z+depth*.226f,
        fit->cloth, 1.25f, vertices ? vertices + 48 : NULL);
}

enum { GEVR_RIG_PANEL_VERTS = 16 + 4*4, GEVR_RIG_POUCH_VERTS = 4*16 };
static struct GevrRigCanvasMesh {
    GevrRigCanvasFit fit;
    Vtx vertices[GEVR_RIG_PANEL_VERTS + 3*GEVR_RIG_POUCH_VERTS];
    Gfx list[13*23 + 4*3 + 1];
    Gfx retained[2];
} s_rigCanvases[128];
static unsigned s_rigCanvasCount;

static Gfx *gevrEnemyCachedCanvas(const GevrRigCanvasFit *fit)
{
    if (!gevrTacticalGearCacheEnabled()) return NULL;
    for (unsigned i = 0; i < s_rigCanvasCount; ++i)
        if (memcmp(&s_rigCanvases[i].fit, fit, sizeof(*fit)) == 0)
            return s_rigCanvases[i].retained[0].words.w1
                ? s_rigCanvases[i].retained : s_rigCanvases[i].list;
    if (s_rigCanvasCount >= ARRAYCOUNT(s_rigCanvases)) return NULL;

    struct GevrRigCanvasMesh *canvas = &s_rigCanvases[s_rigCanvasCount++];
    canvas->fit = *fit;
    Gfx *g = gevrEnemyCanvasPanel(canvas->list, fit, canvas->vertices);
    for (int pouch = 0; pouch < 3; ++pouch)
        g = gevrEnemyCanvasPouch(g, fit, pouch, canvas->vertices+GEVR_RIG_PANEL_VERTS+pouch*GEVR_RIG_POUCH_VERTS);
    gSPEndDisplayList(g++);
    GevrStaticMesh *mesh = gevrStaticMeshBuildGear(canvas->list, FALSE);
    if (!mesh) return canvas->list;
    canvas->retained[0].words.w0 = (uintptr_t)G_GEVR_MESH << 24;
    canvas->retained[0].words.w1 = (uintptr_t)mesh;
    gSPBranchList(&canvas->retained[1], canvas->list);
    return canvas->retained;
}

static int gevrEnemyRigPouch(int count, int magazine)
{
    return count == 1 ? 1 : count == 2 ? magazine*2 : magazine;
}

static Gfx *gevrEnemySeatMagazine(int asset, unsigned part, const float rim[3], float width,
                                  float depth, float height, float exposed, Mtxf *pose)
{
    float low[3], high[3];
    if (!gevrTacticalLooseMagBounds(part, asset, low, high)) return NULL;
    /* Lay the broad face along the chest. Keep enough room for the curve
     * below the lip so a long magazine stays inside the pouch. */
    const float scale = fminf(1.f, fminf((width - 4.f) / fmaxf(1.f, high[2] - low[2]),
        fminf((depth - 4.f) / fmaxf(1.f, high[0] - low[0]),
            (height + exposed - 3.f) / fmaxf(1.f, high[1] - low[1]))));
    if (scale <= 0.f) return NULL;
    *pose = (Mtxf){.m = {{0,0,-scale,0}, {0,scale,0,0}, {scale,0,0,0},
        {rim[0] - scale * (low[2] + high[2]) * .5f,
         rim[1] + exposed - scale * high[1],
         rim[2] + scale * (low[0] + high[0]) * .5f, 1}}};
    return part ? gevrTacticalGearLooseMagList(part, asset) : gevrTacticalHeldMagList(asset);
}

static GevrEnemyRig *gevrEnemyRigForDraw(ChrRecord *chr)
{
    if (!sysArgCheck("--tactical-enemy-rigs") || !gevrTacticalCampaignRules() || !chr || !chr->prop
        || chr->prop->type != PROP_TYPE_CHR || !chr->model || !chr->model->obj) return NULL;
    if (!gevrEnemyCampaignGear() && (chr->chrflags & (CHRFLAG_COUNT_DEATH_AS_CIVILIAN | CHRFLAG_CAN_SHOOT_CHRS))
        && !gevrCampaignGuardLoadout(chr) && gevrEnemyVestSelected(chr) == GEVR_VEST_NONE) return NULL;
    if (gevrTacticalLootEnabled() && !gevrCoopIsHost()) {
        for (int i = 0; i < (int)ARRAYCOUNT(s_enemyRigs); ++i) {
            GevrEnemyRig *rig = &s_enemyRigs[i];
            if (rig->chr == chr && rig->prop == chr->prop && rig->chrnum == chr->chrnum && rig->netRevision)
                return rig;
        }
        return NULL;
    }
    return gevrEnemyRigFor(chr, gevrEnemyHeldItem(chr));
}

void gevrTacticalGuardEquipment(ChrRecord *chr, GevrGuardEquipment *gear)
{
    *gear = (GevrGuardEquipment){.helmet = GEVR_LOADOUT_NO_HELMET, .count = -1};
    const PropRecord *hat = chr->handle_positiondata_hat;
    if (hat && hat->obj && hat->obj->model) {
        const int kind = gevrTacticalHelmetKind(hat->obj->model);
        gear->helmet = kind >= 0 ? kind : GEVR_LOADOUT_DEFAULT_HELMET;
    }
    gear->vest = gevrEnemyVestSelected(chr);
    const GevrEnemyRig *rig = gevrEnemyRigForDraw(chr);
    if (!rig) return;
    gear->vest = rig->vest;
    gear->count = rig->count;
    memcpy(gear->mags, rig->mags, sizeof(gear->mags));
}

void gevrTacticalApplyGuardRig(ChrRecord *chr, const GevrGuardEquipment *gear, unsigned revision)
{
    if (gear->count < 0) return;
    GevrEnemyRig *slot = NULL;
    for (int i = 0; i < (int)ARRAYCOUNT(s_enemyRigs); ++i) {
        GevrEnemyRig *rig = &s_enemyRigs[i];
        if (rig->chr == chr && rig->prop == chr->prop && rig->chrnum == chr->chrnum) {
            if (rig->netRevision == revision) return;
            slot = rig;
            break;
        }
        if (!slot && (!rig->chr || (!rig->ragdollSerial &&
                (rig->chr->prop != rig->prop || rig->chr->chrnum != rig->chrnum)))) slot = rig;
    }
    if (!slot) return;
    if (slot->chr != chr || slot->prop != chr->prop || slot->chrnum != chr->chrnum) {
        *slot = (GevrEnemyRig){.chr = chr, .prop = chr->prop, .chrnum = chr->chrnum,
            .item = gevrEnemyHeldItem(chr), .bodyNum = chr->bodynum,
            .campaign = gevrEnemyCampaignGear(), .outfit = gevrEnemyOutfit(chr)};
    }
    slot->vest = (GevrTacticalVest)gear->vest;
    slot->count = gear->count;
    slot->netRevision = revision;
    memcpy(slot->mags, gear->mags, sizeof(slot->mags));
    for (int i = 0; i < slot->count; ++i)
        if (slot->mags[i].id >= s_enemyMagId) s_enemyMagId = slot->mags[i].id + 1;
    if (sysArgCheck("--tactical-coop-test") && (gear->vest || gear->helmet >= 0))
        sysLogPrintf(LOG_NOTE, "tactical co-op: applied guard rig chr %d vest %d helmet %d pouches %d",
            chr->chrnum, gear->vest, gear->helmet, slot->count);
}

int gevrTacticalLootReadRig(uint32_t id, GevrSharedLoot *loot)
{
    for (int i = 0; i < (int)ARRAYCOUNT(s_enemyRigs); ++i) {
        const GevrEnemyRig *rig = &s_enemyRigs[i];
        const int live = rig->chr && rig->prop && rig->chr->prop == rig->prop && rig->chr->chrnum == rig->chrnum;
        if (!live && !(rig->ragdollSerial && rig->positionValid)) continue;
        for (int k = 0; k < rig->count; ++k) {
            if (rig->mags[k].id != id) continue;
            const coord3d at = rig->positionValid ? rig->position[k] : rig->prop->pos;
            *loot = (GevrSharedLoot){.id = id, .kind = GEVR_LOOT_RIG, .magazine = rig->mags[k],
                .position = {at.x, at.y, at.z}};
            return TRUE;
        }
    }
    return FALSE;
}

void gevrTacticalLootEraseRig(uint32_t id)
{
    for (int i = 0; i < (int)ARRAYCOUNT(s_enemyRigs); ++i)
        for (int k = 0; k < s_enemyRigs[i].count; ++k)
            if (s_enemyRigs[i].mags[k].id == id) s_enemyRigs[i].mags[k] = (TacticalMagazine){0};
}

static int gevrEnemyRigFit(const GevrEnemyRig *rig, const struct bbox *box, int vest, int lowDetail,
    GevrRigCanvasFit *fit, float *exposed)
{
    static const u8 olive[3] = {76,81,53}, black[3] = {39,41,44};
    float x = (box->xmin + box->xmax) * .5f, y = (box->ymin + box->ymax) * .5f;
    float w = box->xmax - box->xmin, h = box->ymax - box->ymin;
    float front = box->zmax, thick = box->zmax - box->zmin;
    if (w < 1 || h < 1 || thick < 1) return FALSE;
    if (rig->bodyNum == BODY_Russian_Commandant) {
        y -= h * .25f;
        front -= thick * (rig->campaign ? .085f : .23f);
        w *= .78f; h *= .68f; thick *= .52f;
    }
    if (rig->outfit) w *= rig->outfit->rigWidth;
    if (vest && !rig->campaign) front += thick * (rig->vest == GEVR_VEST_KORUND ? .14f : .08f);
    const u8 *cloth = rig->vest == GEVR_VEST_SLICK ? black : rig->outfit ? rig->outfit->webbing : olive;
    const int pistol = rig->mags[0].mag_class == TACTICAL_MAG_PISTOL;
    *fit = (GevrRigCanvasFit){
        x, y, w, h, front, thick, {front, front, front},
        w * (pistol ? .065f : .102f), y - h * (pistol ? .16f : .235f),
        y + h * .075f, vest && (rig->campaign || rig->vest != GEVR_VEST_KLMK),
        lowDetail, {cloth[0], cloth[1], cloth[2], 0}
    };
    if (vest && rig->campaign)
        for (int pouch = 0; pouch < 3; ++pouch)
            fit->pouchFront[pouch] = fmaxf(front - thick*.04f, (box->zmin+box->zmax)*.5f
                + (box->zmax-box->zmin)*1.18f*gevrTacticalVestRigFront(rig->vest, rig->bodyNum, pouch));
    *exposed = h * (pistol ? .044f : .070f);
    return TRUE;
}

static void gevrEnemyRigAnchors(GevrEnemyRig *rig, const struct bbox *box, const GevrRigCanvasFit *fit, float exposed)
{
    for (int k = 0; k < rig->count; ++k) {
        const int pouch = gevrEnemyRigPouch(rig->count, k);
        rig->localFront[k] = (coord3d){.f = {
            fit->x + (pouch - 1) * fit->width * .225f, fit->pouchTop + exposed * .55f,
            fit->pouchFront[pouch] + fit->depth * .105f}};
        rig->localBack[k] = rig->localFront[k];
        rig->localBack[k].z = rig->campaign ? box->zmin - fit->depth * .05f : fit->front - fit->depth * 1.05f;
    }
}

static void gevrEnemyRigTransform(GevrEnemyRig *rig, const Mtxf *world)
{
    for (int k = 0; k < rig->count; ++k) {
        rig->position[k] = rig->localFront[k];
        rig->backPosition[k] = rig->localBack[k];
        mtx4TransformVecInPlace((Mtxf *)world, &rig->position[k]);
        mtx4TransformVecInPlace((Mtxf *)world, &rig->backPosition[k]);
    }
    rig->positionValid = TRUE;
}

int gevrEnemyRigAttachRagdoll(ChrRecord *chr, unsigned serial, int *matrix)
{
    GevrEnemyRig *rig = gevrEnemyRigForDraw(chr);
    if (!rig || rig->ragdollSerial) return 0;
    ModelNode *chest = gevrEnemyRigHitbox(chr->model->obj->RootNode, HIT_CHEST);
    if (!chest || (*matrix = modelFindNodeMtxIndex(chest, 0)) < 0
        || *matrix >= chr->model->obj->numMatrices) return 0;
    const struct bbox *box = &chest->Data->BoundingBox.Bounds;
    GevrRigCanvasFit fit;
    float exposed;
    const int vest = gevrTacticalVestRenderList(rig->vest, rig->bodyNum, FALSE) != NULL;
    if (!gevrEnemyRigFit(rig, box, vest, FALSE, &fit, &exposed)) return 0;
    gevrEnemyRigAnchors(rig, box, &fit, exposed);
    rig->chestMatrix = *matrix;
    rig->ragdollSerial = serial;
    if (sysArgCheck("--tactical-coop-test"))
        sysLogPrintf(LOG_NOTE, "tactical sim: corpse rig guard %d first magazine %u", chr->chrnum, rig->mags[0].id);
    if (sysArgCheck("--input-script"))
        sysLogPrintf(LOG_NOTE, "enemy rig: chr %d attached to ragdoll %u with %d magazines", rig->chrnum, serial, rig->count);
    return (int)(rig - s_enemyRigs) + 1;
}

void gevrEnemyRigRagdollPose(int rigId, unsigned serial, const Mtxf *world)
{
    GevrEnemyRig *rig = &s_enemyRigs[rigId - 1];
    if (rig->ragdollSerial == serial) gevrEnemyRigTransform(rig, world);
}

void gevrEnemyRigRagdollReleased(int rigId, unsigned serial, int restore)
{
    GevrEnemyRig *rig = &s_enemyRigs[rigId - 1];
    if (rig->ragdollSerial != serial) return;
    if (restore && rig->chr && rig->chr->prop == rig->prop && rig->chr->chrnum == rig->chrnum) {
        rig->ragdollSerial = 0;
        return;
    }

    /* Retiring the physics body leaves its magazines at the torso's last
     * location. A full loose-item pool leaves the original grab points usable. */
    extern int gevrTacticalDropCorpseMagazine(const coord3d *at, TacticalMagazine mag);
    rig->released = TRUE;
    int remaining = 0;
    for (int k = 0; k < rig->count; ++k) {
        if (!rig->mags[k].id) continue;
        if (gevrTacticalLootEnabled() && (!gevrCoopIsHost() || gevrTacticalLootReserved(rig->mags[k].id))) {
            /* A replica or a pending claim cannot mint a second magazine on retirement. */
            ++remaining;
            continue;
        }
        if (rig->positionValid && gevrTacticalDropCorpseMagazine(&rig->position[k], rig->mags[k])) {
            gevrTacticalLootRetireRig(rig->mags[k].id);
            rig->mags[k] = (TacticalMagazine){0};
        } else {
            ++remaining;
        }
    }
    if (!remaining && (!rig->chr || rig->chr->prop != rig->prop || rig->chr->chrnum != rig->chrnum))
        memset(rig, 0, sizeof(*rig));
}

static Gfx *gevrEnemyRigDrawModel(GevrEnemyRig *rig, Model *model, const coord3d *actor,
    ModelRenderData *body, Gfx *g)
{
    static const u8 magazineSilhouette[3] = {34,36,33};
    static bool loggedVest[GEVR_VEST_COUNT];
    ModelNode *chest = gevrEnemyRigHitbox(model->obj->RootNode, HIT_CHEST);
    if (!chest) return g;
    const int mi = rig->ragdollSerial ? rig->chestMatrix : modelFindNodeMtxIndex(chest, 0);
    if (mi < 0 || mi >= model->obj->numMatrices) return g;
    const struct bbox *box = &chest->Data->BoundingBox.Bounds;
    bool detail = TRUE, lowDetail = FALSE;
    if ((rig->campaign || gevrTacticalGearCacheEnabled()) && g_CurrentPlayer && g_CurrentPlayer->prop) {
        const coord3d *viewer = &g_CurrentPlayer->prop->pos;
        const float dx = actor->x-viewer->x, dy = actor->y-viewer->y, dz = actor->z-viewer->z;
        const float distance = dx*dx + dy*dy + dz*dz;
        detail = distance < 1450.f*1450.f;
        lowDetail = gevrTacticalGearCacheEnabled() && distance > 800.f*800.f;
    }
    if (sysArgCheck("--tactical-rig-visibility-test") && sysArgCheck("--tactical-sim")
        && sysArgCheck("--input-script")) {
        detail = FALSE;
        lowDetail = gevrTacticalGearCacheEnabled();
    }
    /* Armour remains visible wherever its wearer is drawn. Keep the cached
     * lower-detail mesh at range, alongside the existing magazine silhouettes. */
    Gfx *vest = gevrTacticalVestRenderList(rig->vest, rig->bodyNum, lowDetail);
    if (!detail && vest && sysArgCheck("--tactical-rig-visibility-test")) {
        static bool loggedFarVest;
        if (!loggedFarVest) {
            loggedFarVest = TRUE;
            sysLogPrintf(LOG_NOTE, "tactical rig visibility: distant vest drawn");
        }
    }
    GevrRigCanvasFit canvas;
    float exposed;
    if (!gevrEnemyRigFit(rig, box, vest != NULL, lowDetail, &canvas, &exposed)) return g;
    const float x = canvas.x, w = canvas.width, h = canvas.height, thick = canvas.depth;
    const float pouchTop = canvas.pouchTop, pouchBottom = canvas.pouchBottom;
    const float pouchHalfWidth = canvas.pouchHalfWidth;
    const float *pouchFront = canvas.pouchFront;
    const int count = rig->count;
    Gfx *magLists[3] = {NULL};
    Mtxf magPoses[3];
    int magAssets[3];
    for (int k = 0; k < count; ++k) {
        const TacticalMagazine *mag = &rig->mags[k];
        magAssets[k] = tacticalMagazineGun(mag);
        if (!detail || lowDetail || !mag->id) continue;
        const int pouch = gevrEnemyRigPouch(count,k);
        const float rim[3] = {x + (pouch-1)*w*.225f, pouchTop, pouchFront[pouch] + thick*.105f};
        magLists[k] = gevrEnemySeatMagazine(magAssets[k], mag->gear_part, rim, pouchHalfWidth * 2.f,
            thick * .23f, pouchTop - pouchBottom, exposed, &magPoses[k]);
    }
    if (!rig->ragdollSerial && camGetWorldToScreenMtxf()) {
        Mtxf viewToWorld, world;
        matrix_4x4_invert_affine(camGetWorldToScreenMtxf(), &viewToWorld);
        matrix_4x4_multiply(&viewToWorld, &model->render_pos[mi].pos, &world);
        gevrEnemyRigAnchors(rig, box, &canvas, exposed);
        gevrEnemyRigTransform(rig, &world);
    }
    body->gdl = g; modelApplyRenderModeType3(body, TRUE); g = body->gdl;
    gSPSegment(g++, SPSEGMENT_MODEL_MTX, osVirtualToPhysical(model->render_pos));
    gSPMatrix(g++, (void *)(uintptr_t)(0x03000000u + mi * sizeof(Mtx)), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    if (vest) {
        /* The vest is fitted to a 1000-unit chest reference. The chest joint
         * keeps it aligned through turns and moves it with the pouches. */
        Mtxf fit;
        Mtx *fixed = dynAllocateMatrix();
        gevrEnemyVestFit(box, &fit);
        guMtxF2L(fit.m, fixed);
        gSPMatrix(g++, osVirtualToPhysical(fixed), G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_NOPUSH);
        gSPDisplayList(g++, vest);
        if (rig->campaign) g = gevrEnemyDrawVestArms(g, model, mi, rig->vest, lowDetail);
        gSPMatrix(g++, (void *)(uintptr_t)(0x03000000u + mi * sizeof(Mtx)), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
        if (!loggedVest[rig->vest]) {
            loggedVest[rig->vest] = TRUE;
            sysLogPrintf(LOG_NOTE, "tactical vest: %s drawn on guard %d, chest bounds %.0f %.0f %.0f",
                gevrTacticalVestName(rig->vest), rig->chrnum, (double)w, (double)h, (double)thick);
        }
    }
    gDPPipeSync(g++);
    gSPClearGeometryMode(g++, G_LIGHTING | G_CULL_BOTH | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
    gSPSetGeometryMode(g++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);
    gSPTexture(g++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetCombineMode(g++, G_CC_SHADE, G_CC_SHADE);
    Gfx *cachedCanvas = gevrEnemyCachedCanvas(&canvas);
    if (cachedCanvas) {
        gSPDisplayList(g++, cachedCanvas);
    } else {
        g = gevrEnemyCanvasPanel(g, &canvas, NULL);
    }
    for (int pouch = 0; pouch < 3; ++pouch) {
        const float cx = x+(pouch-1)*w*.225f;
        const float z = pouchFront[pouch];
        int i;
        for (i = 0; i < count && gevrEnemyRigPouch(count, i) != pouch; ++i) {}
        if (!cachedCanvas) g = gevrEnemyCanvasPouch(g, &canvas, pouch, NULL);
        if (i<count && rig->mags[i].id) {
            const TacticalMagazine *mag = &rig->mags[i];
            Gfx *list = magLists[i];
            if (list) {
                Mtx *fixed=dynAllocateMatrix();
                body->gdl=g; modelApplyRenderModeType3(body, TRUE); g=body->gdl;
                guMtxF2L(magPoses[i].m,fixed);
                gSPMatrix(g++,osVirtualToPhysical(fixed),G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_NOPUSH);
                gSPDisplayList(g++,list);
                g=gevrTacticalDrawMagazineRounds(g,magAssets[i],mag->rounds,0,0,mag->gear_part);
                gSPMatrix(g++,(void *)(uintptr_t)(0x03000000u + mi * sizeof(Mtx)),G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
                gDPPipeSync(g++);
                gSPClearGeometryMode(g++,G_LIGHTING | G_CULL_BOTH | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
                gSPSetGeometryMode(g++,G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);
                gSPTexture(g++,0,0,0,G_TX_RENDERTILE,G_OFF);
                gDPSetCombineMode(g++,G_CC_SHADE,G_CC_SHADE);
            } else {
                /* Low detail beyond magazine interaction range. */
                g = gevrEnemyRigBox(g,cx-w*.055f,cx+w*.055f,pouchTop-h*.01f,pouchTop+exposed,
                    z+thick*.065f,z+thick*.145f,magazineSilhouette,1.f,NULL);
            }
        }
    }
    return g;
}
static Gfx *gevrEnemyRigDraw(ChrRecord *chr, ModelRenderData *body, Gfx *g, s32 withalpha, s32 fadealpha)
{
    if (!chr || !chr->field_20) return g;
    /* A fog-faded body is drawn only during the alpha pass. Opaque bodies
     * already received their gear in the preceding solid pass. */
    if (withalpha && fadealpha == 255 && !(chr->chrflags & CHRFLAG_00020000)) return g;
    GevrEnemyRig *rig = gevrEnemyRigForDraw(chr);
    if (!rig || rig->ragdollSerial) return g;
    if (withalpha && sysArgCheck("--tactical-rig-visibility-test")) {
        static bool loggedFadedRig;
        if (!loggedFadedRig) {
            loggedFadedRig = TRUE;
            sysLogPrintf(LOG_NOTE, "tactical rig visibility: faded rig drawn");
        }
    }
    return gevrEnemyRigDrawModel(rig, chr->model, &chr->prop->pos, body, g);
}

Gfx *gevrEnemyRigDrawRagdoll(int rigId, unsigned serial, Model *model, const coord3d *origin,
    ModelRenderData *render, Gfx *g)
{
    GevrEnemyRig *rig = &s_enemyRigs[rigId - 1];
    if (rig->ragdollSerial != serial) return g;
    return gevrEnemyRigDrawModel(rig, model, origin, render, g);
}
#endif /* GEVR_LOOT_NATIVE_TEST */
