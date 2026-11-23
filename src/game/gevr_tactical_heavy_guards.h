#ifndef GEVR_TACTICAL_HEAVY_GUARDS_H
#define GEVR_TACTICAL_HEAVY_GUARDS_H

#include "bondconstants.h"
#include "bondtypes.h"
#include "boss.h"
#include "player.h"
#include "system.h"
#include "gevr_tactical_mesh.h"

extern bool netIsActive(void);

enum { GEVR_DAM_KORUND_GUARD = 11, GEVR_DAM_JANUS_GUARD = 12 };

static inline bool gevrTacticalHeavyGuards(int stage)
{
    return stage == LEVELID_DAM && sysArgCheck("--tactical-heavy-guards")
        && sysArgCheck("--tactical-guards") && sysArgCheck("--tactical-expanded")
        && sysArgCheck("--tactical-mesh") && sysArgCheck("--tactical-enemy-rigs")
        && !netIsActive() && getPlayerCount() == 1;
}

static inline bool gevrTacticalJanusGuard(const ChrRecord *chr)
{
    return chr->chrnum == GEVR_DAM_JANUS_GUARD && chr->bodynum == BODY_Janus_Special_Forces
        && gevrTacticalHeavyGuards(bossGetStageNum());
}

static inline bool gevrTacticalKorundGuard(const ChrRecord *chr)
{
    return chr->chrnum == GEVR_DAM_KORUND_GUARD && chr->bodynum == BODY_Russian_Soldier
        && gevrTacticalHeavyGuards(bossGetStageNum());
}

/* Hit durability also governs whether a helmet can detach at all. */
static inline int gevrTacticalHelmetHits(const PropRecord *hat)
{
    if (!hat || hat->type != PROP_TYPE_OBJ || !hat->obj || hat->obj->type != PROPDEF_HAT)
        return -1;
    const int kind = gevrTacticalHelmetKind(hat->obj->model);
    switch (kind) {
    case GEVR_HELMET_ALTYN:
    case GEVR_HELMET_FAST:
    case GEVR_HELMET_FAST_SIDE_TROOPER:
    case GEVR_HELMET_FAST_SHIELD:
        return 0;
    case GEVR_HELMET_ZSH:
        return 5;
    case GEVR_HELMET_FAST_SIDE:
    case GEVR_HELMET_AIRFRAME_EARS:
    case GEVR_HELMET_RATNIK_ARCTIC:
    case GEVR_HELMET_RATNIK_OD:
    case GEVR_HELMET_RATNIK_EMR:
    case GEVR_HELMET_RATNIK_MOH:
    case GEVR_HELMET_RATNIK_SYRIA:
    case GEVR_HELMET_RATNIK_SPRING:
    case GEVR_HELMET_RATNIK_PARTIZAN:
    case GEVR_HELMET_RATNIK_SURPAT:
    case GEVR_HELMET_ULACH_BLACK:
    case GEVR_HELMET_ULACH_TAN:
        return 4;
    default:
        /* The two standalone heavy prototypes also exist as native props. */
        if (hat->obj->obj == PROP_TACTICAL_ALTYN || hat->obj->obj == PROP_TACTICAL_FAST_TROOPER)
            return 0;
        return kind >= 0 ? 3 : -1;
    }
}

static inline bool gevrTacticalPermanentHelmet(const PropRecord *hat)
{
    return gevrTacticalHelmetHits(hat) == 0;
}

static inline bool gevrTacticalIsArmorShot(int item)
{
    extern int gevrTacticalItem(int item);
    return (item >= ITEM_WPPK && item <= ITEM_GOLDWPPK) || gevrTacticalItem(item) >= 0;
}

void gevrTacticalGuardIssueM4(PropRecord *prop);
void gevrTacticalGuardIssueReset(void);
int gevrTacticalGuardWeaponSuppressed(const PropRecord *prop);

#endif
