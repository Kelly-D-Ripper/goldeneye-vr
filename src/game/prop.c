#ifdef GEVR
#include "net_core.h"
#include "bondaicommands.h"
#include "boss.h"
#include "net_horde.h"
#include "net_match.h"
#include "system.h"
#include "net_objects.h"
#include "gevr_romlayout.h"
#include "gevr_tactical_range.h"
#include "gevr_tactical_facility.h"
#include "gevr_tactical_mesh.h"
#include "gevr_tactical_loadouts.h"
#endif
#include <ultra64.h>
#include <math.h>
#include <memp.h>
#include <string.h>
#include "game/mp_weapon.h"
#include "game/bondview_r.h"
#include "bg.h"
#include "bondview_r.h"
#include "chr.h"
#include "chrai.h"
#include "chraction.h"
#include "propobj.h"
#include "inititemslots.h"
#include "initobjects.h"
#include "initpathtablesomething.h"
#include "limits.h"
#include "loadobjectmodel.h"
#include "language.h"
#include "math_atan2f.h"
#include "matrixmath.h"
#include "mp_weapon.h"
#include "ob.h"
#include "objective.h"
#include "objective_status.h"
#include "objecthandler.h"
#include "player.h"
#include "prop.h"
#include "stan.h"
#include "model.h"
#ifdef GEVR
#include "gevr_dam_boat.h"
#include "gevr_arena.h"
#include "gevr_tactical_gear.h"
#endif
#include "token.h"

/**
 * EU .bss 0x80068480
*/
ITEM_IDS lastmpweaponnum;

// redeclare with the element count so ARRAYCOUNT works in proplvreset2
extern ItemModelFileRecord *PitemZ_entries;

// forward declarations

s32 load_proptype(PROPDEF_TYPE type);
void sub_GAME_7F001BD4(struct BoundPadRecord *pad, struct coord3d *arg1);
void domakedefaultobj(s32 arg0, ObjectRecord *arg1, s32 cmdindex);
void weaponAssignToHome(s32 arg0, WeaponObjRecord* weapon, s32 cmdindex);
void setupHat(s32 arg0, ObjectRecord* hat, s32 cmdindex);
void setupKey(s32 arg0, ObjectRecord* key, s32 cmdindex);
void setupCctv(s32 arg0, CCTVRecord *arg1, s32 cmdindex);
void setupAutogun(s32 stageID, AutogunRecord *autogun, s32 cmdindex);
void setupHangingMonitors(s32 arg0, ObjectRecord* rack, s32 cmdindex);
void setupSingleMonitor(s32 stageID, MonitorObjRecord *monitor, s32 cmdindex);
void setupMultiMonitor(s32 stageID, MultiMonitorObjRecord* monitor, s32 cmdindex);
void sub_GAME_7F00324C(struct BoundPadRecord *arg0, s32 *arg1, s32 *arg2, struct coord3d *arg3, struct coord3d *arg4);
void setupDoor(s32 arg0, struct DoorRecord *door, s32 arg2);


s32 load_proptype(PROPDEF_TYPE type)
{
    PropDefHeaderRecord *propdef = (PropDefHeaderRecord *) g_CurrentSetup.propDefs;
    s32 count = 0;

    if (propdef != NULL)
    {
        while (propdef->type != PROPDEF_END)
        {
            if (propdef->type == (type & 0xFF))
            {
                count ++;
            }
            propdef = &propdef[sizepropdef((PropDefHeaderRecord* ) propdef)];
        }
    }
    return count;
}

#ifdef GEVR
enum {
    JUNGLE_COMBAT_AI = 0x040d,
    JUNGLE_PATROL_FIRST_ID = 20000,
    CAMPAIGN_PARTNER_FIRST_ID = 22000,
    CAMPAIGN_THIRD_FIRST_ID = 22100,
    SURFACE_ROOF_MARKSMAN = 23000,
    SURFACE2_ROOF_MARKSMAN = 14,
    CAMPAIGN_PATROL_FIRST_ID = 25000,
    CAMPAIGN_PATROL_STRIDE = 5,
    CAMPAIGN_EXPANSION_FIRST_ID = 26000,
    CAMPAIGN_EXPANSION_STRIDE = 3,
    /* Lone stock guards (chrnum below 100) get partners 22000+ and 22100+
     * their own number, the same ranges the authored posts use. */
    CAMPAIGN_BUDDY_SENTRY_LIMIT = 100,
    CAMPAIGN_BUDDY_MAX = 18,
    /* SVD marksmen on high ground found at load on open maps */
    CAMPAIGN_PERCH_FIRST_ID = 23100,
    CAMPAIGN_PERCH_MAX = 2
};

/* Only regular armed posts with an adjacent, clear pad on that same floor. */
static const struct {
    enum LEVELID stage;
    u16 sentry, pad;
    u8 partners;
} campaignGuardPosts[] = {
    {LEVELID_DAM,      12,  93, 1},
    {LEVELID_FACILITY, 16,  98, 1},
    {LEVELID_BUNKER1,  10,  36, 1},
    {LEVELID_BUNKER1,  15,  62, 1},
    {LEVELID_STREETS,  11, 212, 1},
    {LEVELID_DEPOT,    16, 118, 2},
    {LEVELID_DEPOT,    19, 339, 1},
    {LEVELID_CAVERNS,  12, 207, 1},
    {LEVELID_CAVERNS,  42, 283, 1},
#include "gevr_campaign_posts_open.inc.h"
#include "gevr_campaign_posts_tight.inc.h"
};

static const struct {
    enum LEVELID stage;
    u16 body, item, model;
    u8 size;
    u16 pads[8];
} campaignPatrols[] = {
#include "gevr_campaign_patrols_open.inc.h"
};

enum CampaignEncounterRole { ENCOUNTER_IDLE, ENCOUNTER_PATROL, ENCOUNTER_MARKSMAN };

typedef struct CampaignEncounter {
    enum LEVELID stage;
    u16 body, item, model;
    enum CampaignEncounterRole role;
    u8 size;
    u16 pads[8];
} CampaignEncounter;

static const CampaignEncounter campaignExpansions[] = {
#include "gevr_campaign_expanded_early.inc.h"
#include "gevr_campaign_expanded_late.inc.h"
};

typedef struct CampaignPatrolRoute {
    struct patrol_path path;
    s32 ids[ARRAYCOUNT(campaignExpansions[0].pads) + 1];
    PadRecord pads[ARRAYCOUNT(campaignExpansions[0].pads)];
} CampaignPatrolRoute;

static CampaignPatrolRoute campaignRoutes[ARRAYCOUNT(campaignExpansions)];

_Static_assert(CAMPAIGN_EXPANSION_FIRST_ID + CAMPAIGN_EXPANSION_STRIDE * ARRAYCOUNT(campaignExpansions)
    <= 0x8000, "Campaign encounter actor IDs must fit in s16");

static bool tacticalEncountersEnabled(void)
{
    return sysArgCheck("--tactical-physical") && gevrTacticalCampaignRules()
        && !gevrRomIsVariant() && !gevrTacticalRangeActive();
}

int gevrCampaignExtraGuardCapacity(int stage)
{
    if (!tacticalEncountersEnabled()) return 0;
    int count = stage == LEVELID_JUNGLE ? GEVR_JUNGLE_PATROL_CAPACITY : stage == LEVELID_SURFACE;
    count += CAMPAIGN_BUDDY_MAX + CAMPAIGN_PERCH_MAX; /* lone guards' partners and perches, placed at load */
    for (size_t i = 0; i < ARRAYCOUNT(campaignGuardPosts); i++) {
        if (campaignGuardPosts[i].stage == stage) count += campaignGuardPosts[i].partners;
    }
    for (size_t i = 0; i < ARRAYCOUNT(campaignPatrols); i++) {
        if (campaignPatrols[i].stage == stage) count += campaignPatrols[i].size;
    }
    for (size_t i = 0; i < ARRAYCOUNT(campaignExpansions); i++) {
        if (campaignExpansions[i].stage == stage) count += campaignExpansions[i].size;
    }
    return count;
}

static int campaignEncounterIndex(const ChrRecord *chr)
{
    if (!chr || chr->chrnum < CAMPAIGN_EXPANSION_FIRST_ID) return -1;
    const int actor = chr->chrnum - CAMPAIGN_EXPANSION_FIRST_ID;
    const int row = actor / CAMPAIGN_EXPANSION_STRIDE;
    return row < (int)ARRAYCOUNT(campaignExpansions)
        && campaignExpansions[row].stage == bossGetStageNum()
        && actor % CAMPAIGN_EXPANSION_STRIDE < campaignExpansions[row].size ? row : -1;
}

bool gevrCampaignGuardIsMarksman(const ChrRecord *chr)
{
    const int row = campaignEncounterIndex(chr);
    if (row >= 0) return campaignExpansions[row].role == ENCOUNTER_MARKSMAN;
    if (chr && chr->chrnum >= CAMPAIGN_PERCH_FIRST_ID && chr->chrnum < CAMPAIGN_PERCH_FIRST_ID + CAMPAIGN_PERCH_MAX)
        return tacticalEncountersEnabled();
    if (!chr || chr->bodynum != BODY_Siberian_Special_Forces || !tacticalEncountersEnabled()) return FALSE;
    const int stage = bossGetStageNum();
    return (stage == LEVELID_SURFACE && chr->chrnum == SURFACE_ROOF_MARKSMAN)
        || (stage == LEVELID_SURFACE2 && chr->chrnum == SURFACE2_ROOF_MARKSMAN)
        || (chr->chrnum >= CAMPAIGN_PERCH_FIRST_ID && chr->chrnum < CAMPAIGN_PERCH_FIRST_ID + CAMPAIGN_PERCH_MAX);
}

struct patrol_path *gevrCampaignGuardRoute(const ChrRecord *chr)
{
    const int row = campaignEncounterIndex(chr);
    return row >= 0 && campaignRoutes[row].path.data ? &campaignRoutes[row].path : NULL;
}

PadRecord *gevrCampaignPatrolPad(struct patrol_path *path, s32 step)
{
    /* Direct patrol paths are always the first member of a CampaignPatrolRoute. */
    CampaignPatrolRoute *route = (CampaignPatrolRoute *)path;
    return &route->pads[step];
}

int gevrJunglePatrolSquad(const ChrRecord *chr)
{
    if (!chr || chr->bodynum != BODY_Jungle_Commando || bossGetStageNum() != LEVELID_JUNGLE)
        return -1;
    const int member = chr->chrnum - JUNGLE_PATROL_FIRST_ID;
    return member >= 0 && member < GEVR_JUNGLE_PATROL_CAPACITY ? member / GEVR_JUNGLE_SQUAD_SIZE : -1;
}

extern s32 g_GlobalTimer; /* lv.c */

/* No added guard may stand where a door opens (user, 2026-11-23: a Facility
 * guard by the bathroom door kept it from opening). 2.6 m from a door's
 * centre on the same floor covers its leaf and swing. */
static bool campaignSpotBesideDoor(const coord3d *pos, StandTile *stan)
{
    /* compare floors: a spot's and a door's stored heights aren't both on the ground */
    const f32 floor = stan ? stanGetPositionYValue(stan, pos->x, pos->z) : pos->y;
    for (PropDefHeaderRecord *def = g_CurrentSetup.propDefs; def && def->type != PROPDEF_END; def += sizepropdef(def)) {
        if (def->type != PROPDEF_DOOR) continue;
        const ObjectRecord *door = (const ObjectRecord *)def;
        if (!door->prop) continue;
        const coord3d *at = &door->prop->pos;
        if (SQR(at->x - pos->x) + SQR(at->z - pos->z) >= SQR(260.0f)) continue;
        const f32 doorFloor = door->prop->stan ? stanGetPositionYValue(door->prop->stan, at->x, at->z) : at->y - 120.0f;
        if (fabsf(doorFloor - floor) < 150.0f || (at->y > floor && at->y - floor < 300.0f)) return TRUE;
    }
    return FALSE;
}

static f32 campaignChrFloor(const ChrRecord *chr)
{
    return chr->prop->stan ? stanGetPositionYValue(chr->prop->stan, chr->prop->pos.x, chr->prop->pos.z) : chr->prop->pos.y;
}

/* Another living guard within 10 m on the same floor: added idle guards are
 * placed with company, never dotted on their own. */
static bool campaignSpotHasCompany(const coord3d *pos, StandTile *stan, const ChrRecord *except)
{
    const f32 floor = stan ? stanGetPositionYValue(stan, pos->x, pos->z) : pos->y;
    for (int i = 0; i < g_NumChrSlots; i++) {
        const ChrRecord *chr = &g_ChrSlots[i];
        if (chr == except || !chr->model || !chr->prop || chr->prop->type != PROP_TYPE_CHR
            || chr->actiontype == ACT_DIE || chr->actiontype == ACT_DEAD
            || (chr->hidden & CHRHIDDEN_REMOVE) || (chr->chrflags & CHRFLAG_HIDDEN)) continue;
        if (SQR(chr->prop->pos.x - pos->x) + SQR(chr->prop->pos.z - pos->z) < SQR(1000.0f)
            && fabsf(campaignChrFloor(chr) - floor) < 200.0f) return TRUE;
    }
    return FALSE;
}

/* Whether stock guard n has a partner actor, rechecked once a second. */
static bool campaignHasBuddy(int sentry)
{
    static s32 checked[CAMPAIGN_BUDDY_SENTRY_LIMIT];
    static bool result[CAMPAIGN_BUDDY_SENTRY_LIMIT];
    static const void *setup;
    if (setup != g_CurrentSetup.pads) {
        memset(checked, 0, sizeof(checked));
        setup = g_CurrentSetup.pads;
    }
    if (!checked[sentry] || g_GlobalTimer - checked[sentry] > 60 || g_GlobalTimer < checked[sentry]) {
        checked[sentry] = g_GlobalTimer | 1;
        result[sentry] = chrFindByLiteralId(CAMPAIGN_PARTNER_FIRST_ID + sentry) != NULL;
    }
    return result[sentry];
}

int gevrCampaignGuardPost(const ChrRecord *chr, int *member)
{
    if (!chr) return -1;
    const int row = campaignEncounterIndex(chr);
    if (row >= 0) {
        if (campaignExpansions[row].role == ENCOUNTER_MARKSMAN) return -1;
        *member = (chr->chrnum - CAMPAIGN_EXPANSION_FIRST_ID) % CAMPAIGN_EXPANSION_STRIDE;
        return 100 + ARRAYCOUNT(campaignPatrols) + row;
    }
    const int stage = bossGetStageNum();
    const int patrolActor = chr->chrnum - CAMPAIGN_PATROL_FIRST_ID;
    if (patrolActor >= 0) {
        const int patrol = patrolActor / CAMPAIGN_PATROL_STRIDE;
        const int index = patrolActor % CAMPAIGN_PATROL_STRIDE;
        if (patrol >= (int)ARRAYCOUNT(campaignPatrols) || campaignPatrols[patrol].stage != stage
            || index >= campaignPatrols[patrol].size) return -1;
        *member = index;
        return 100 + patrol;
    }
    for (size_t i = 0; i < ARRAYCOUNT(campaignGuardPosts); i++) {
        if (campaignGuardPosts[i].stage != stage) continue;
        const int sentry = campaignGuardPosts[i].sentry;
        if (chr->chrnum == sentry) *member = 0;
        else if (chr->chrnum == CAMPAIGN_PARTNER_FIRST_ID + sentry) *member = 1;
        else if (campaignGuardPosts[i].partners == 2 && chr->chrnum == CAMPAIGN_THIRD_FIRST_ID + sentry) *member = 2;
        else continue;
        return sentry;
    }
    if (chr->chrnum >= CAMPAIGN_PARTNER_FIRST_ID && chr->chrnum < CAMPAIGN_PARTNER_FIRST_ID + CAMPAIGN_BUDDY_SENTRY_LIMIT) {
        *member = 1;
        return chr->chrnum - CAMPAIGN_PARTNER_FIRST_ID;
    }
    if (chr->chrnum >= CAMPAIGN_THIRD_FIRST_ID && chr->chrnum < CAMPAIGN_THIRD_FIRST_ID + CAMPAIGN_BUDDY_SENTRY_LIMIT) {
        *member = 2;
        return chr->chrnum - CAMPAIGN_THIRD_FIRST_ID;
    }
    if (chr->chrnum >= 0 && chr->chrnum < CAMPAIGN_BUDDY_SENTRY_LIMIT && campaignHasBuddy(chr->chrnum)) {
        *member = 0;
        return chr->chrnum;
    }
    return -1;
}

bool gevrCampaignGuardIsIdleGroupMember(const ChrRecord *chr)
{
    const int row = campaignEncounterIndex(chr);
    if (row >= 0) return campaignExpansions[row].role == ENCOUNTER_IDLE && campaignExpansions[row].size > 1;
    int member;
    return gevrJunglePatrolSquad(chr) >= 0 || gevrCampaignGuardPost(chr, &member) >= 0;
}

static void spawnJunglePatrols(int npads)
{
    int placed = 0;
    for (PropDefHeaderRecord *def = (PropDefHeaderRecord *)g_CurrentSetup.propDefs;
         def->type != PROPDEF_END; def += sizepropdef(def)) {
        if (def->type != PROPDEF_GUARD) continue;
        const GuardRecord *guard = (const GuardRecord *)def;
        /* The end cinematic uses the same commando body with a different list. */
        if (guard->BodyID == BODY_Jungle_Commando && guard->AIListID == JUNGLE_COMBAT_AI
            && guard->Data && guard->Data->model) placed++;
    }

    /* Each row is one encounter. Extra pads keep the squad together if trees,
     * props or a co-op start position make one of the first five unusable. */
    static const u16 squadPads[GEVR_JUNGLE_SQUAD_COUNT][9] = {
        {144, 334, 271, 267, 335, 217, 282, 367, 268},    /* Early forest */
        {190, 261, 329, 138, 327, 259, 191, 328, 260},   /* Clearing */
        {187, 252, 75, 319, 525, 320, 324, 162, 73},    /* Treeline */
        {401, 213, 400, 403, 316, 64, 506, 65, 315},     /* Forest path */
        {419, 416, 415, 50, 420, 244, 412, 414, 51},    /* Bridge approach */
        {409, 293, 114, 109, 410, 204, 105, 175, 117},  /* Across the bridge */
        {91, 237, 423, 424, 238, 128, 98, 296, 297},    /* Ravine */
        {516, 126, 205, 308, 430, 184, 431, 429, 207},  /* Hillside */
        {455, 456, 454, 538, 457, 537, 564, 460, 540},  /* Lift approach */
    };
    AIRecord *ai = ailistFindById(GAILIST_STANDARD_GUARD);
    int added = 0, armed = 0, full = 0;
    const bool rockets = cheatIsActive(CHEAT_ENEMY_ROCKETS);
    for (size_t i = 0; ai && i < ARRAYCOUNT(squadPads); i++) {
        int members = 0;
        for (size_t j = 0; j < ARRAYCOUNT(squadPads[i]) && members < GEVR_JUNGLE_SQUAD_SIZE; j++) {
            const u16 index = squadPads[i][j];
            if (!index || index >= npads) continue;
            const PadRecord *pad = &g_CurrentSetup.pads[index];
            if (campaignSpotBesideDoor(&pad->pos, pad->stan) || !gevrCampaignOpeningPointClear(pad)) continue;

            const int id = JUNGLE_PATROL_FIRST_ID + (int)i * GEVR_JUNGLE_SQUAD_SIZE + members;
            const f32 angle = atan2f(pad->look.x, pad->look.z);
            PropRecord *actor = gevrSpawnCampaignGuard(BODY_Jungle_Commando, pad, angle, ai, id);
            if (!actor) continue;
            actor->chr->visionrange = 100.0f; /* Regular Jungle sentry. */
            if (chrGiveWeapon(actor->chr, rockets ? PROP_CHRROCKETLAUNCH : PROP_CHRM16,
                              rockets ? ITEM_ROCKETLAUNCH : ITEM_M16, 0)) armed++;
            added++;
            members++;
            sysLogPrintf(LOG_NOTE, "tactical jungle: squad %u member %d actor %d pad %u",
                         (unsigned)i + 1, members, id, (unsigned)index);
        }
        if (members == GEVR_JUNGLE_SQUAD_SIZE) full++;
    }
    sysLogPrintf(LOG_NOTE, "tactical jungle: %d placed commandos, %d additional commandos (%d armed), %d full squads",
                 placed, added, armed, full);
}

static PropRecord *spawnSentryPartner(ChrRecord *sentry, const PadRecord *preferred,
                                    AIRecord *ai, s32 id, const PropRecord *first,
                                    int *attemptUsed, const char **failure)
{
    const coord3d *at = &sentry->prop->pos;
    StandTile *const origin = sentry->prop->stan;
    const f32 ground = stanGetPositionYValue(origin, at->x, at->z);
    const f32 bearing = atan2f(preferred->pos.x - at->x, preferred->pos.z - at->z);

    /* Try the authored pad first, then eight positions close to the sentry. */
    for (int attempt = 0; attempt <= 8; attempt++) {
        PadRecord spot = *preferred;
        if (attempt > 0) {
            const f32 heading = bearing + (attempt - 1) * (M_PI_F / 4.0f);
            spot.pos = *at;
            spot.pos.x += 145.0f * sinf(heading);
            spot.pos.z += 145.0f * cosf(heading);
            spot.stan = origin;
            if (!walkTilesBetweenPoints_NoCallback(&spot.stan, at->x, at->z, spot.pos.x, spot.pos.z)) {
                *failure = "no connected floor";
                continue;
            }
        }
        if (!spot.stan || origin->room != spot.stan->room) {
            *failure = "different room";
            continue;
        }
        const f32 floor = stanGetPositionYValue(spot.stan, spot.pos.x, spot.pos.z);
        if (fabsf(ground - floor) > 30.0f) {
            *failure = "different floor height";
            continue;
        }
        StandTile *tile = origin;
        if (!stanTestLineUnobstructed(&tile, at->x, at->z, spot.pos.x, spot.pos.z,
                                    CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER,
                                    ground + 100.0f, ground + 20.0f, floor + 100.0f, floor + 20.0f)) {
            *failure = "path obstructed";
            continue;
        }
        if (first) {
            const f32 dx = first->pos.x - spot.pos.x, dz = first->pos.z - spot.pos.z;
            if (dx * dx + dz * dz < 110.0f * 110.0f) {
                *failure = "too close to first partner";
                continue;
            }
        }
        if (campaignSpotBesideDoor(&spot.pos, spot.stan)) {
            *failure = "beside a door";
            continue;
        }
        if (!gevrCampaignOpeningPointClear(&spot)) {
            *failure = "start area, sightline or standing space rejected";
            continue;
        }
        const f32 angle = atan2f(at->x - spot.pos.x, at->z - spot.pos.z);
        PropRecord *partner = gevrSpawnCampaignGuard(sentry->bodynum, &spot, angle, ai, id);
        if (partner) {
            *attemptUsed = attempt;
            return partner;
        }
        *failure = "standing space, start proximity or spawn floor rejected";
    }
    return NULL;
}

static void spawnCampaignSentryPairs(int stage, int npads)
{
    AIRecord *ai = ailistFindById(GAILIST_STANDARD_GUARD);
    for (size_t i = 0; ai && i < ARRAYCOUNT(campaignGuardPosts); i++) {
        if (campaignGuardPosts[i].stage != stage || campaignGuardPosts[i].pad >= npads) continue;
        ChrRecord *sentry = chrFindByLiteralId(campaignGuardPosts[i].sentry);
        const bool eligible = sentry && sentry->prop && sentry->model && sentry->prop->stan
            && !(sentry->chrflags & (CHRFLAG_INVINCIBLE | CHRFLAG_COUNT_DEATH_AS_CIVILIAN | CHRFLAG_CAN_SHOOT_CHRS));
        const PropRecord *weapon = eligible ? sentry->weapons_held[GUNRIGHT] : NULL;
        if (eligible && !weapon) weapon = sentry->weapons_held[GUNLEFT];
        if (!weapon || !weapon->weapon || !gevrCampaignGunIsFirearm(weapon->weapon->weaponnum)) {
            sysLogPrintf(LOG_NOTE, "tactical guard pair: skipped stage %d sentry %u, original guard unavailable or unarmed",
                         stage, (unsigned)campaignGuardPosts[i].sentry);
            continue;
        }

        const u16 index = campaignGuardPosts[i].pad;
        const coord3d *at = &sentry->prop->pos;
        PropRecord *first = NULL;
        f32 towardX = 0.0f, towardZ = 0.0f;
        for (int member = 0; member < campaignGuardPosts[i].partners; member++) {
            const int id = (member == 0 ? CAMPAIGN_PARTNER_FIRST_ID : CAMPAIGN_THIRD_FIRST_ID) + sentry->chrnum;
            int attemptUsed = 0;
            const char *failure = "no usable position";
            PropRecord *partner = spawnSentryPartner(sentry, &g_CurrentSetup.pads[index], ai, id,
                                                    first, &attemptUsed, &failure);
            if (!partner) {
                sysLogPrintf(LOG_NOTE, "tactical guard pair: skipped stage %d sentry %d partner %d, %s",
                             stage, sentry->chrnum, id, failure);
                break;
            }
            partner->chr->visionrange = sentry->visionrange;
            if (!gevrCloneGuardWeapon(partner->chr, weapon, 0)) {
                sysLogPrintf(LOG_WARNING, "tactical guard pair: stage %d partner %d could not equip", stage, id);
                partner->chr->hidden |= CHRHIDDEN_REMOVE;
                partner->chr->chrflags |= CHRFLAG_HIDDEN;
                break;
            }
            if (!first) first = partner;
            towardX += partner->pos.x - at->x;
            towardZ += partner->pos.z - at->z;
            setsubroty(partner->chr->model, atan2f(at->x - partner->pos.x, at->z - partner->pos.z));
            sysLogPrintf(LOG_NOTE, "tactical guard pair: stage %d sentry %d partner %d pad %u fallback %d at %.0f %.0f %.0f",
                         stage, sentry->chrnum, id, (unsigned)index, attemptUsed,
                         partner->pos.x, partner->pos.y, partner->pos.z);
        }
        if (first) setsubroty(sentry->model, atan2f(towardX, towardZ));
    }
}

/* 0.17.1 (user, 2026-11-23): a guard stood alone gets a partner facing him,
 * and some get a third, so posts read as people talking, smoking or keeping
 * each other awake. Only plain idle sentries on the standard lists qualify;
 * crowded rooms, patrols, scripted and objective actors are left alone, and
 * every spot passes the same start-area, sightline, floor and standing-space
 * checks as the authored posts. */
static int s_buddyReject[6];
static bool campaignLoneSentry(ChrRecord *chr)
{
    extern u8 m_StandardGuard[], m_SimpleGuard[], m_SimpleGuardDeaf[], m_IdleAnimations[];
    if (!chr->prop || !chr->model || !chr->prop->stan || chr->chrnum < 0
        || chr->chrnum >= CAMPAIGN_BUDDY_SENTRY_LIMIT || chr->prop->type != PROP_TYPE_CHR
        || chr->actiontype == ACT_DIE || chr->actiontype == ACT_DEAD || chr->actiontype == ACT_PATROL
        || (chr->chrflags & (CHRFLAG_INVINCIBLE | CHRFLAG_COUNT_DEATH_AS_CIVILIAN | CHRFLAG_CAN_SHOOT_CHRS
                             | CHRFLAG_HIDDEN | CHRFLAG_CLONE))
        || (chr->hidden & CHRHIDDEN_REMOVE) || gevrCampaignGuardRoute(chr)) { s_buddyReject[0]++; return FALSE; }
    /* Guards on a level's own idle script qualify too (most of Facility's
     * do); campaignBuddyTurns keeps their facing for the script. */
    if (chr->chrflags & CHRFLAG_INVINCIBLE) { s_buddyReject[1]++; return FALSE; }
    const PropRecord *weapon = chr->weapons_held[GUNRIGHT] ? chr->weapons_held[GUNRIGHT] : chr->weapons_held[GUNLEFT];
    if (!weapon || !weapon->weapon || !gevrCampaignGunIsFirearm(weapon->weapon->weaponnum)) { s_buddyReject[2]++; return FALSE; }
    int member;
    if (gevrCampaignGuardPost(chr, &member) >= 0) { s_buddyReject[3]++; return FALSE; }
    /* Alone: no other guard within 6.5 m, and the room isn't already busy. */
    int roommates = 0;
    for (int i = 0; i < g_NumChrSlots; i++) {
        const ChrRecord *other = &g_ChrSlots[i];
        if (other == chr || !other->model || !other->prop || other->prop->type != PROP_TYPE_CHR || !other->prop->stan
            || other->actiontype == ACT_DIE || other->actiontype == ACT_DEAD) continue;
        const f32 d2 = SQR(other->prop->pos.x - chr->prop->pos.x) + SQR(other->prop->pos.z - chr->prop->pos.z);
        if (d2 < SQR(650.0f) && fabsf(other->prop->pos.y - chr->prop->pos.y) < 150.0f) { s_buddyReject[4]++; return FALSE; }
        if (other->prop->stan->room == chr->prop->stan->room) roommates++;
    }
    if (roommates >= 3) s_buddyReject[5]++;
    return roommates < 3;
}

static bool campaignBuddyTurns(const ChrRecord *chr)
{
    extern u8 m_StandardGuard[], m_SimpleGuard[], m_SimpleGuardDeaf[], m_IdleAnimations[];
    const void *list = chr->ailist;
    return list == m_StandardGuard || list == m_SimpleGuard || list == m_SimpleGuardDeaf || list == m_IdleAnimations;
}

static void spawnCampaignLoneBuddies(int stage)
{
    AIRecord *ai = ailistFindById(GAILIST_STANDARD_GUARD);
    if (!ai) return;
    int placed = 0, threes = 0;
    memset(s_buddyReject, 0, sizeof(s_buddyReject));
    for (int i = 0; i < g_NumChrSlots && placed < CAMPAIGN_BUDDY_MAX - 1; i++) {
        ChrRecord *sentry = &g_ChrSlots[i];
        if (!campaignLoneSentry(sentry)) continue;
        const PropRecord *weapon = sentry->weapons_held[GUNRIGHT] ? sentry->weapons_held[GUNRIGHT]
                                                                  : sentry->weapons_held[GUNLEFT];
        /* First choice: 1.5 m in front of him, where he is already looking. */
        const f32 yaw = getsubroty(sentry->model);
        PadRecord ahead = {0};
        ahead.pos = sentry->prop->pos;
        ahead.pos.x += 150.0f * sinf(yaw);
        ahead.pos.z += 150.0f * cosf(yaw);
        ahead.stan = sentry->prop->stan;
        if (!walkTilesBetweenPoints_NoCallback(&ahead.stan, sentry->prop->pos.x, sentry->prop->pos.z,
                                               ahead.pos.x, ahead.pos.z)) ahead.stan = NULL;
        ahead.pos.y = ahead.stan ? stanGetPositionYValue(ahead.stan, ahead.pos.x, ahead.pos.z) : sentry->prop->pos.y;
        const int want = (sentry->chrnum % 3 == 0 && placed < CAMPAIGN_BUDDY_MAX - 2) ? 2 : 1;
        const coord3d *at = &sentry->prop->pos;
        PropRecord *first = NULL;
        f32 towardX = 0.0f, towardZ = 0.0f;
        for (int member = 0; member < want; member++) {
            const int id = (member == 0 ? CAMPAIGN_PARTNER_FIRST_ID : CAMPAIGN_THIRD_FIRST_ID) + sentry->chrnum;
            int attemptUsed = 0;
            const char *failure = "no usable position";
            PropRecord *partner = spawnSentryPartner(sentry, &ahead, ai, id, first, &attemptUsed, &failure);
            if (!partner) {
                sysLogPrintf(LOG_NOTE, "tactical buddy: skipped stage %d guard %d partner %d, %s",
                             stage, sentry->chrnum, id, failure);
                break;
            }
            partner->chr->visionrange = sentry->visionrange;
            if (!gevrCloneGuardWeapon(partner->chr, weapon, 0)) {
                partner->chr->hidden |= CHRHIDDEN_REMOVE;
                partner->chr->chrflags |= CHRFLAG_HIDDEN;
                break;
            }
            if (!first) first = partner;
            towardX += partner->pos.x - at->x;
            towardZ += partner->pos.z - at->z;
            setsubroty(partner->chr->model, atan2f(at->x - partner->pos.x, at->z - partner->pos.z));
            placed++;
            threes += member == 1;
            sysLogPrintf(LOG_NOTE, "tactical buddy: stage %d guard %d partner %d attempt %d at %.0f %.0f %.0f",
                         stage, sentry->chrnum, id, attemptUsed, partner->pos.x, partner->pos.y, partner->pos.z);
        }
        if (first && campaignBuddyTurns(sentry)) setsubroty(sentry->model, atan2f(towardX, towardZ));
    }
    sysLogPrintf(LOG_NOTE, "tactical buddy: stage %d placed %d partners (%d threes); passed over %d inactive/patrol,"
        " %d invincible, %d unarmed, %d posted, %d with company, %d busy rooms", stage, placed, threes,
        s_buddyReject[0], s_buddyReject[1], s_buddyReject[2], s_buddyReject[3], s_buddyReject[4], s_buddyReject[5]);
}

static bool campaignPatrolConnected(const coord3d *from, StandTile *start, const PadRecord *pad, f32 maxDistance)
{
    if (!start || !pad->stan) return FALSE;
    const f32 d2 = SQR(from->x - pad->pos.x) + SQR(from->z - pad->pos.z);
    if (d2 < SQR(100.0f) || d2 > SQR(maxDistance)) return FALSE;
    const f32 fromY = stanGetPositionYValue(start, from->x, from->z);
    const f32 toY = stanGetPositionYValue(pad->stan, pad->pos.x, pad->pos.z);
    if (fabsf(fromY - toY) > 110.0f) return FALSE;
    return stanTestLineUnobstructed(&start, from->x, from->z, pad->pos.x, pad->pos.z,
        CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER,
        fromY + 95.0f, fromY + 25.0f, toY + 95.0f, toY + 25.0f) && start == pad->stan;
}

static bool campaignPatrolSegment(const PadRecord *from, const PadRecord *to)
{
    StandTile *tile = from->stan;
    if (!campaignPatrolConnected(&from->pos, tile, to, 1000.0f)) return FALSE;
    for (int quarter = 1; quarter < 4; quarter++) {
        const f32 t = quarter * .25f;
        const f32 x = from->pos.x + (to->pos.x - from->pos.x) * t;
        const f32 z = from->pos.z + (to->pos.z - from->pos.z) * t;
        tile = from->stan;
        if (!walkTilesBetweenPoints_NoCallback(&tile, from->pos.x, from->pos.z, x, z)) return FALSE;
        StandTile *space = tile;
        if (stanTestVolume(&space, x, z, 28.0f, CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER,
                           185.0f, 2.0f) >= 0) return FALSE;
    }
    return TRUE;
}

static void prepareCampaignPatrol(size_t row, int npads)
{
    const CampaignEncounter *encounter = &campaignExpansions[row];
    CampaignPatrolRoute *route = &campaignRoutes[row];
    int len = 0;
    for (; len < (int)ARRAYCOUNT(encounter->pads) && encounter->pads[len]; len++) {
        const u16 index = encounter->pads[len];
        if (index >= npads || !g_CurrentSetup.pads[index].stan) return;
        route->ids[len] = index;
        PadRecord *pad = &route->pads[len];
        *pad = g_CurrentSetup.pads[index];
        /* Off-screen native patrol motion uses the marker's full position.
         * Some ROM markers are up near eye level on an otherwise flat floor. */
        pad->pos.y = stanGetPositionYValue(pad->stan, pad->pos.x, pad->pos.z) + 1.0f;
    }
    if (len < 2 || len < encounter->size) return;
    route->ids[len] = -1;
    route->path = (struct patrol_path){route->ids, 0, GEVR_PATROL_DIRECT_PADS, len};
}

static bool campaignMarksmanSight(const PadRecord *post, const PadRecord *look)
{
    StandTile *tile = post->stan;
    const f32 fromY = stanGetPositionYValue(tile, post->pos.x, post->pos.z) + 150.0f;
    const f32 toY = stanGetPositionYValue(look->stan, look->pos.x, look->pos.z) + 100.0f;
    return stanTestLineUnobstructed(&tile, post->pos.x, post->pos.z, look->pos.x, look->pos.z,
        CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER | CDTYPE_AIOPAQUE,
        fromY + 1.0f, fromY - 1.0f, toY + 1.0f, toY - 1.0f) && tile == look->stan;
}

static void spawnCampaignExpansions(int stage, int npads, bool host)
{
    const char *const labels[] = {"idle", "patrol", "marksman"};
    AIRecord *ai = ailistFindById(GAILIST_STANDARD_GUARD);
    for (size_t row = 0; ai && row < ARRAYCOUNT(campaignExpansions); row++) {
        const CampaignEncounter *encounter = &campaignExpansions[row];
        if (encounter->stage != stage) continue;
        const bool patrol = encounter->role == ENCOUNTER_PATROL;
        const bool marksman = encounter->role == ENCOUNTER_MARKSMAN;
        if (patrol) prepareCampaignPatrol(row, npads);
        if (!host) continue;

        const struct patrol_path *path = &campaignRoutes[row].path;
        if (patrol) {
            int step = 0;
            for (; step < path->len; step++) {
                const PadRecord *pad = &g_CurrentSetup.pads[path->data[step]];
                if (!gevrCampaignSpawnPointClear(pad)) break;
                if (step && !campaignPatrolSegment(&g_CurrentSetup.pads[path->data[step - 1]], pad)) break;
            }
            if (!path->data || step < path->len) {
                sysLogPrintf(LOG_NOTE, "tactical encounter: stage %d row %u skipped patrol clearance at pad %d",
                    stage, (unsigned)row, path->data ? path->data[step] : -1);
                continue;
            }
        }

        PropRecord *lead = NULL;
        int placed = 0;
        const int candidates = marksman ? 1 : (int)ARRAYCOUNT(encounter->pads);
        for (int j = 0; j < candidates && placed < encounter->size; j++) {
            const u16 index = encounter->pads[j];
            if (!index || index >= npads) continue;
            const PadRecord *pad = &g_CurrentSetup.pads[index];
            if (campaignSpotBesideDoor(&pad->pos, pad->stan) || !gevrCampaignOpeningPointClear(pad)) continue;
            f32 angle = atan2f(pad->look.x, pad->look.z);
            if (marksman) {
                const u16 sight = encounter->pads[1];
                if (!sight || sight >= npads || !g_CurrentSetup.pads[sight].stan) continue;
                const PadRecord *look = &g_CurrentSetup.pads[sight];
                if (!campaignMarksmanSight(pad, look)) {
                    sysLogPrintf(LOG_NOTE, "tactical encounter: stage %d row %u blocked sightline %u to %u",
                        stage, (unsigned)row, (unsigned)index, (unsigned)sight);
                    continue;
                }
                angle = atan2f(look->pos.x - pad->pos.x, look->pos.z - pad->pos.z);
            } else if (lead && !patrol) {
                if (!campaignPatrolConnected(&lead->pos, lead->stan, pad, 800.0f)) continue;
                angle = atan2f(lead->pos.x - pad->pos.x, lead->pos.z - pad->pos.z);
            }
            const int id = CAMPAIGN_EXPANSION_FIRST_ID + (int)row * CAMPAIGN_EXPANSION_STRIDE + placed;
            PropRecord *actor = gevrSpawnCampaignGuard(encounter->body, pad, angle, ai, id);
            if (!actor) continue;
            if (!chrGiveWeapon(actor->chr, encounter->model, encounter->item, 0)) {
                actor->chr->hidden |= CHRHIDDEN_REMOVE;
                actor->chr->chrflags |= CHRFLAG_HIDDEN;
                break;
            }
            actor->chr->visionrange = marksman ? 160.0f : 100.0f;
            if (patrol) set_actor_on_path(actor->chr, &campaignRoutes[row].path);
            if (!lead) lead = actor;
            else if (!patrol && placed == 1) setsubroty(lead->chr->model,
                atan2f(actor->pos.x - lead->pos.x, actor->pos.z - lead->pos.z));
            placed++;
            sysLogPrintf(LOG_NOTE, "tactical encounter: stage %d actor %d role %s pad %u at %.0f %.0f %.0f",
                stage, id, labels[encounter->role], (unsigned)index,
                (double)actor->pos.x, (double)actor->pos.y, (double)actor->pos.z);
        }
        if (placed < encounter->size)
            sysLogPrintf(LOG_NOTE, "tactical encounter: stage %d row %u skipped %s placement (%d/%u)",
                stage, (unsigned)row, labels[encounter->role], placed, (unsigned)encounter->size);
        /* An idle post left with one man and nobody within 10 m is a guard
         * dotted on his own: stand him down before the mission starts. */
        if (lead && placed == 1 && encounter->role == ENCOUNTER_IDLE
            && !campaignSpotHasCompany(&lead->pos, lead->stan, lead->chr)) {
            lead->chr->hidden |= CHRHIDDEN_REMOVE;
            lead->chr->chrflags |= CHRFLAG_HIDDEN;
            sysLogPrintf(LOG_NOTE, "tactical encounter: stage %d row %u idle guard alone, not placed", stage, (unsigned)row);
        }
    }
}

static void spawnCampaignPatrols(int stage, int npads)
{
    AIRecord *ai = ailistFindById(GAILIST_STANDARD_GUARD);
    for (size_t i = 0; ai && i < ARRAYCOUNT(campaignPatrols); i++) {
        if (campaignPatrols[i].stage != stage) continue;
        PropRecord *first = NULL;
        int placed = 0;
        for (size_t j = 0; j < ARRAYCOUNT(campaignPatrols[i].pads) && placed < campaignPatrols[i].size; j++) {
            const u16 index = campaignPatrols[i].pads[j];
            if (index == 0 || index >= npads) continue;
            const PadRecord *pad = &g_CurrentSetup.pads[index];
            if (campaignSpotBesideDoor(&pad->pos, pad->stan) || !gevrCampaignOpeningPointClear(pad)) continue;
            f32 angle = atan2f(pad->look.x, pad->look.z);
            if (first) {
                if (!campaignPatrolConnected(&first->pos, first->stan, pad, 800.0f)) continue;
                angle = atan2f(first->pos.x - pad->pos.x, first->pos.z - pad->pos.z);
            } else if (campaignPatrols[i].size > 1) {
                /* Pick a lead only if a second space can actually be reached
                 * from it. Prefer leaving a blocked optional encounter out. */
                bool canPair = FALSE;
                for (size_t k = j + 1; k < ARRAYCOUNT(campaignPatrols[i].pads); k++) {
                    const int next = campaignPatrols[i].pads[k];
                    if (!next || next >= npads) continue;
                    const PadRecord *partner = &g_CurrentSetup.pads[next];
                    if (campaignPatrolConnected(&pad->pos, pad->stan, partner, 800.0f)
                        && gevrCampaignOpeningPointClear(partner)) {
                        canPair = TRUE;
                        break;
                    }
                }
                if (!canPair) continue;
            }
            const int id = CAMPAIGN_PATROL_FIRST_ID + (int)i * CAMPAIGN_PATROL_STRIDE + placed;
            PropRecord *actor = gevrSpawnCampaignGuard(campaignPatrols[i].body, pad, angle, ai, id);
            if (!actor) continue;
            if (!chrGiveWeapon(actor->chr, campaignPatrols[i].model, campaignPatrols[i].item, 0)) {
                actor->chr->hidden |= CHRHIDDEN_REMOVE;
                actor->chr->chrflags |= CHRFLAG_HIDDEN;
                break;
            }
            actor->chr->visionrange = 100.0f;
            if (!first) first = actor;
            else if (placed == 1) setsubroty(first->chr->model,
                atan2f(actor->pos.x - first->pos.x, actor->pos.z - first->pos.z));
            placed++;
            sysLogPrintf(LOG_NOTE, "tactical patrol: stage %d actor %d pad %u at %.0f %.0f %.0f",
                stage, id, (unsigned)index, (double)actor->pos.x, (double)actor->pos.y, (double)actor->pos.z);
        }
    }
}

static void spawnSurfaceMarksman(int npads)
{
    /* Authored walkable roof above the satellite control building. */
    static const u16 candidates[] = {292, 293, 294, 295};
    AIRecord *ai = ailistFindById(GAILIST_SIMPLE_GUARD_DEAF);
    for (size_t i = 0; ai && i < ARRAYCOUNT(candidates); i++) {
        const u16 index = candidates[i];
        if (index >= npads || !g_CurrentSetup.pads[index].stan) continue;
        const PadRecord *pad = &g_CurrentSetup.pads[index];
        if (campaignSpotBesideDoor(&pad->pos, pad->stan) || !gevrCampaignOpeningPointClear(pad)) continue;
        const f32 angle = atan2f(pad->look.x, pad->look.z);
        PropRecord *actor = gevrSpawnCampaignGuard(BODY_Siberian_Special_Forces, pad, angle, ai,
                                                  SURFACE_ROOF_MARKSMAN);
        if (!actor) continue;
        actor->chr->visionrange = 160.0f;
        if (chrGiveWeapon(actor->chr, PROP_CHRSNIPERRIFLE, ITEM_SNIPERRIFLE, 0)) {
            sysLogPrintf(LOG_NOTE, "tactical marksman: stage %d guard %d pad %u at %.0f %.0f %.0f",
                         LEVELID_SURFACE, SURFACE_ROOF_MARKSMAN, (unsigned)index,
                         actor->pos.x, actor->pos.y, actor->pos.z);
        }
        return;
    }
    sysLogPrintf(LOG_NOTE, "tactical marksman: Surface I roof has no clear stand");
}

/* High ground on the open maps (user, 2026-11-23: "on maps that are big and
 * open give guards elevated positions ... an SVD"). A perch stands at least
 * 2.5 m above most pads around it and sees down to four or more of them 12-65
 * m away. It must pass the same start-area, sightline and standing checks as
 * every added guard, and keep clear of the authored marksmen. */
static bool campaignOpenStage(int stage)
{
    switch (stage) {
    case LEVELID_DAM: case LEVELID_RUNWAY: case LEVELID_SURFACE: case LEVELID_SURFACE2:
    case LEVELID_STREETS: case LEVELID_DEPOT: case LEVELID_JUNGLE:
        return TRUE;
    default:
        return FALSE;
    }
}

static void spawnCampaignPerches(int stage, int npads)
{
    if (!campaignOpenStage(stage) || npads <= 0) return;
    AIRecord *ai = ailistFindById(GAILIST_STANDARD_GUARD);
    if (!ai) return;
    enum { CANDIDATES = 12 };
    int best[CANDIDATES], bestScore[CANDIDATES], found = 0;
    for (int i = 1; i < npads; i++) {
        const PadRecord *pad = &g_CurrentSetup.pads[i];
        if (!pad->stan) continue;
        const f32 ground = stanGetPositionYValue(pad->stan, pad->pos.x, pad->pos.z);
        int below = 0, around = 0;
        for (int j = 1; j < npads; j++) {
            const PadRecord *other = &g_CurrentSetup.pads[j];
            if (j == i || !other->stan) continue;
            const f32 d2 = SQR(other->pos.x - pad->pos.x) + SQR(other->pos.z - pad->pos.z);
            if (d2 < SQR(1200.0f) || d2 > SQR(6500.0f)) continue;
            around++;
            below += stanGetPositionYValue(other->stan, other->pos.x, other->pos.z) < ground - 250.0f;
        }
        if (around < 8 || below * 3 < around * 2) continue;
        int slot = found < CANDIDATES ? found++ : CANDIDATES;
        while (slot > 0 && below > bestScore[slot - 1]) {
            if (slot < CANDIDATES) { best[slot] = best[slot - 1]; bestScore[slot] = bestScore[slot - 1]; }
            slot--;
        }
        if (slot < CANDIDATES) { best[slot] = i; bestScore[slot] = below; }
    }
    int placed = 0, rejectNear = 0, rejectClear = 0, rejectSight = 0;
    coord3d used[CAMPAIGN_PERCH_MAX];
    for (int c = 0; c < found && placed < CAMPAIGN_PERCH_MAX; c++) {
        const PadRecord *pad = &g_CurrentSetup.pads[best[c]];
        bool near = FALSE;
        for (int k = 0; k < placed; k++)
            near |= SQR(used[k].x - pad->pos.x) + SQR(used[k].z - pad->pos.z) < SQR(2500.0f);
        for (int k = 0; k < g_NumChrSlots && !near; k++) {
            const ChrRecord *chr = &g_ChrSlots[k];
            near = chr->model && chr->prop && gevrCampaignGuardIsMarksman(chr)
                && SQR(chr->prop->pos.x - pad->pos.x) + SQR(chr->prop->pos.z - pad->pos.z) < SQR(2500.0f);
        }
        if (near || campaignSpotBesideDoor(&pad->pos, pad->stan)) { rejectNear++; continue; }
        if (!gevrCampaignOpeningPointClear(pad)) { rejectClear++; continue; }
        /* Count the lower pads it can actually see, and face their middle. */
        const f32 ground = stanGetPositionYValue(pad->stan, pad->pos.x, pad->pos.z);
        int seen = 0, tested = 0;
        f32 lookX = 0.0f, lookZ = 0.0f;
        for (int j = 1; j < npads && tested < 40; j++) {
            const PadRecord *other = &g_CurrentSetup.pads[(best[c] * 7 + j * 13) % (npads - 1) + 1];
            if (!other->stan || other == pad) continue;
            const f32 d2 = SQR(other->pos.x - pad->pos.x) + SQR(other->pos.z - pad->pos.z);
            if (d2 < SQR(1200.0f) || d2 > SQR(6500.0f)
                || stanGetPositionYValue(other->stan, other->pos.x, other->pos.z) > ground - 250.0f) continue;
            tested++;
            if (!campaignMarksmanSight(pad, other)) continue;
            seen++;
            lookX += other->pos.x - pad->pos.x;
            lookZ += other->pos.z - pad->pos.z;
        }
        if (seen < 4) { rejectSight++; continue; }
        /* Wear the uniform of the guards already on the map. */
        s32 body = BODY_Russian_Soldier;
        for (int k = 0; k < g_NumChrSlots; k++) {
            const ChrRecord *chr = &g_ChrSlots[k];
            if (chr->model && chr->prop && chr->prop->type == PROP_TYPE_CHR && chr->chrnum >= 0 && chr->chrnum < 100
                && !(chr->chrflags & (CHRFLAG_INVINCIBLE | CHRFLAG_COUNT_DEATH_AS_CIVILIAN))) {
                body = chr->bodynum;
                break;
            }
        }
        const int id = CAMPAIGN_PERCH_FIRST_ID + placed;
        PropRecord *actor = gevrSpawnCampaignGuard(body, pad, atan2f(lookX, lookZ), ai, id);
        if (!actor) continue;
        if (!chrGiveWeapon(actor->chr, PROP_CHRSNIPERRIFLE, ITEM_SNIPERRIFLE, 0)) {
            actor->chr->hidden |= CHRHIDDEN_REMOVE;
            actor->chr->chrflags |= CHRFLAG_HIDDEN;
            continue;
        }
        actor->chr->visionrange = 160.0f;
        used[placed++] = pad->pos;
        sysLogPrintf(LOG_NOTE, "tactical perch: stage %d marksman %d pad %d sees %d/%d lower pads at %.0f %.0f %.0f",
                     stage, id, best[c], seen, tested, actor->pos.x, actor->pos.y, actor->pos.z);
    }
    sysLogPrintf(LOG_NOTE, "tactical perch: stage %d placed %d of %d candidates (%d near a marksman, %d start or"
        " standing space, %d without a view)", stage, placed, found, rejectNear, rejectClear, rejectSight);
}

/* --tactical-placement-audit: every door and every guard after the added
 * guards are placed, with what is near each, for the placement review. */
static const char *campaignGuardKind(const ChrRecord *chr)
{
    const int n = chr->chrnum;
    if (n < 0) return "retired";
    if (n < 20000) return chr->chrflags & CHRFLAG_CLONE ? "clone" : "stock";
    if (n < 20900) return "jungle-squad";
    if (n >= CAMPAIGN_PARTNER_FIRST_ID && n < CAMPAIGN_PARTNER_FIRST_ID + 100) return "partner";
    if (n >= CAMPAIGN_THIRD_FIRST_ID && n < CAMPAIGN_THIRD_FIRST_ID + 100) return "third";
    if (n >= CAMPAIGN_PERCH_FIRST_ID && n < CAMPAIGN_PERCH_FIRST_ID + CAMPAIGN_PERCH_MAX) return "perch";
    if (n >= 23000 && n < 24000) return "marksman";
    if (n >= 24000 && n < 25000) return "reinforcement";
    if (n >= CAMPAIGN_PATROL_FIRST_ID && n < CAMPAIGN_EXPANSION_FIRST_ID) return "patrol";
    if (n >= CAMPAIGN_EXPANSION_FIRST_ID) return "expansion";
    return "other";
}

static void campaignPlacementAudit(int stage)
{
    if (!sysArgCheck("--tactical-placement-audit")) return;
    for (PropDefHeaderRecord *def = g_CurrentSetup.propDefs; def && def->type != PROPDEF_END; def += sizepropdef(def)) {
        if (def->type != PROPDEF_DOOR) continue;
        const ObjectRecord *door = (const ObjectRecord *)def;
        if (door->prop) sysLogPrintf(LOG_NOTE, "placement audit: stage %d door at %.0f %.0f %.0f room %d", stage,
            door->prop->pos.x, door->prop->pos.y, door->prop->pos.z, door->prop->stan ? door->prop->stan->room : -1);
    }
    for (s32 i = 0; i < startpadcount; i++)
        if (g_Startpad[i]) sysLogPrintf(LOG_NOTE, "placement audit: stage %d start at %.0f %.0f %.0f", stage,
            g_Startpad[i]->pos.x, g_Startpad[i]->pos.y, g_Startpad[i]->pos.z);
    for (int i = 0; i < g_NumChrSlots; i++) {
        const ChrRecord *chr = &g_ChrSlots[i];
        if (!chr->model || !chr->prop || chr->prop->type != PROP_TYPE_CHR) continue;
        const PropRecord *held = chr->weapons_held[GUNRIGHT] ? chr->weapons_held[GUNRIGHT] : chr->weapons_held[GUNLEFT];
        sysLogPrintf(LOG_NOTE, "placement audit: stage %d guard %d %s at %.0f %.0f %.0f room %d action %d item %d yaw %.2f",
            stage, chr->chrnum, campaignGuardKind(chr), chr->prop->pos.x, chr->prop->pos.y, chr->prop->pos.z,
            chr->prop->stan ? chr->prop->stan->room : -1, chr->actiontype,
            held && held->weapon ? held->weapon->weaponnum : -1, getsubroty(chr->model));
    }
}

void gevrCampaignEncountersStageLoaded(int stage)
{
    memset(campaignRoutes, 0, sizeof(campaignRoutes));
    if (!gevrCampaignExtraGuardCapacity(stage) || !g_CurrentSetup.propDefs || !g_CurrentSetup.pads) return;
    int npads = 0;
    while (g_CurrentSetup.pads[npads].plink) npads++;
    const bool host = !netIsActive() || netIsHost();
    if (host && stage == LEVELID_JUNGLE) {
        spawnJunglePatrols(npads);
    } else if (host) {
        if (stage == LEVELID_SURFACE) spawnSurfaceMarksman(npads);
        spawnCampaignSentryPairs(stage, npads);
        spawnCampaignPatrols(stage, npads);
    }
    spawnCampaignExpansions(stage, npads, host);
    if (host && !sysArgCheck("--tactical-classic-ai")) {
        spawnCampaignPerches(stage, npads);
        if (stage != LEVELID_JUNGLE) spawnCampaignLoneBuddies(stage);
    }
    campaignPlacementAudit(stage);
}
#endif


/**
 * perfect dark padGetCentre (pad.c)
 *
 * NTSC address 0x7F001BD4.
*/
void sub_GAME_7F001BD4(struct BoundPadRecord *pad, struct coord3d *arg1)
{
    struct coord3d normal;
    f32 scale;
    struct bbox bb;
    f32 temp;

    bb.zmax = pad->bbox.xmin;
    bb.zmin = pad->bbox.xmax;
    bb.ymax = pad->bbox.ymin;
    bb.ymin = pad->bbox.ymax;
    bb.xmax = pad->bbox.zmin;
    bb.xmin = pad->bbox.zmax;

    normal.f[0] = (pad->up.f[1] * pad->look.f[2]) - (pad->up.f[2] * pad->look.f[1]);
    normal.f[1] = (pad->up.f[2] * pad->look.f[0]) - (pad->up.f[0] * pad->look.f[2]);
    normal.f[2] = (pad->up.f[0] * pad->look.f[1]) - (pad->up.f[1] * pad->look.f[0]);

    temp = (normal.f[0] * normal.f[0]) + (normal.f[1] * normal.f[1]) + (normal.f[2] * normal.f[2]);
    scale = 1.0f / sqrtf(temp);

    normal.f[0] *= scale;
    normal.f[1] *= scale;
    normal.f[2] *= scale;

    arg1->f[0] = pad->pos.f[0] + (
			(bb.zmax + bb.zmin) * normal.f[0] +
			(bb.ymax + bb.ymin) * pad->up.f[0] +
			(bb.xmax + bb.xmin) * pad->look.f[0]) * 0.5f;

	arg1->f[1] = pad->pos.f[1] + (
			(bb.zmax + bb.zmin) * normal.f[1] +
			(bb.ymax + bb.ymin) * pad->up.f[1] +
			(bb.xmax + bb.xmin) * pad->look.f[1]) * 0.5f;

	arg1->f[2] = pad->pos.f[2] + (
			(bb.zmax + bb.zmin) * normal.f[2] +
			(bb.ymax + bb.ymin) * pad->up.f[2] +
			(bb.xmax + bb.xmin) * pad->look.f[2]) * 0.5f;

}

/**
 * NTSC address 0x7F001D9C.
*/
void domakedefaultobj(s32 arg0, ObjectRecord *arg1, s32 cmdindex)
{
    s32 padding;
    s32 spF0;
    f32 var_f0;
    struct coord3d spE0;
    struct StandTile *spDC;
    struct coord3d spD0;
    StandTile *spCC;
    Mtxf sp8C;
    struct coord3d sp80;
    struct PropRecord *var_v0;
    f32 sp78;
    s32 sp74;
    struct BoundPadRecord *var_s0;
    ChrRecord *sp6C;
    ModelRoData_BoundingBoxRecord *temp_v0_3;
    struct PadRecord *sp64;
    struct PropRecord *sp60;
    s32 padding2;
    f32 sp58;
    f32 sp54;
    f32 sp50;
    s32 padding3;
    f32 sp48;

    spF0 = arg1->obj;
    var_s0 = NULL;

    modelLoad(spF0);

    sp78 = arg1->extrascale * 0.00390625f;

    arg1->damage = *(s32*)&arg1->damage / 65536.0f;

#ifdef GEVR
    if (gevrMpRules())
#else
    if (getPlayerCount() >= 2)
#endif
    {
        sp74 = 1;

        if ((get_scenario() == SCENARIO_TLD) && (arg1->obj == PROP_FLAG))
        {
            sp74 = 0;
        }
        else if ((get_scenario() == SCENARIO_MWTGG) && (arg1->obj == PROP_CHRGOLDEN))
        {
            sp74 = 0;
        }

        if (sp74 != 0)
        {
            arg1->state |= PROPSTATE_RESPAWN; // respawn enabled
        }
    }

    if (arg1->flags & PROPFLAG_INSIDEANOTHEROBJ)
    {
        if (arg1->type == PROP_TYPE_SMOKE)
        {
            sub_GAME_7F051DD8(arg1, PitemZ_entries[spF0].header);
        }
        else
        {
            objInitWithModelDef(arg1, PitemZ_entries[spF0].header);
        }

        modelSetScale(arg1->model, arg1->model->scale * sp78);
    }
    else if (arg1->flags & PROPFLAG_ASSIGNEDTOCHR)
    {
        sp6C = chrFindByLiteralId(arg1->pad);

        if ((sp6C != NULL) && (sp6C->prop != NULL) && (sp6C->model != NULL))
        {
            if (arg1->type == 8)
            {
                var_v0 = sub_GAME_7F051DD8(arg1, PitemZ_entries[spF0].header);
            }
            else
            {
                var_v0 = objInitWithModelDef(arg1, PitemZ_entries[spF0].header);
            }

            modelSetScale(arg1->model, arg1->model->scale * sp78);
            chrpropReparent(var_v0, sp6C->prop);
        }
        #ifdef DEBUG
        else
        {
            osSyncPrintf("domakedefaultobj: no chr number %d for obj number %d!\n",arg1->pad,cmdindex + 1);
        }
        #endif
    }
    else
    {
        if (isNotBoundPad(arg1->pad))
        {
            sp64 = &g_CurrentSetup.pads[arg1->pad];

            matrix_4x4_set_basis_and_position_target(&sp8C, 0.0f, 0.0f, 0.0f, -sp64->look.f[0], -sp64->look.f[1], -sp64->look.f[2], sp64->up.f[0], sp64->up.f[1], sp64->up.f[2]);

            spD0.f[0] = sp64->pos.f[0];
            spD0.f[1] = sp64->pos.f[1];
            spD0.f[2] = sp64->pos.f[2];

            if (arg1->flags & PROPFLAG_ONSCREEN)
            {
                sp80.f[0] = sp64->pos.f[0];
                sp80.f[1] = sp64->pos.f[1];
                sp80.f[2] = sp64->pos.f[2];
            }
            else
            {
                // same as above?

                sp80.f[0] = sp64->pos.f[0];
                sp80.f[1] = sp64->pos.f[1];
                sp80.f[2] = sp64->pos.f[2];
            }

            spCC = sp64->stan;
        }
        else
        {
#ifdef GEVR
            /*
             * PORT probe. getBoundPadNum is just (pad - 10000) and nothing
             * bounds-checks it. The bound-pad array is NULL-plink terminated,
             * and gevrConvertSetup stops emitting at the first entry with no
             * plink - so a volume in the middle without a name truncates it
             * and every later id reads past the end. That is the shape of the
             * Dam fault (0xb4000071b4000077: two pointer high halves, i.e.
             * adjacent heap pointers being read as a record).
             */
            {
                struct BoundPadRecord *bscan = g_CurrentSetup.boundpads;
                s32 bcount = 0;
                while (bscan->plink != NULL) { bcount++; bscan++; }
                if (getBoundPadNum(arg1->pad) >= bcount || getBoundPadNum(arg1->pad) < 0) {
                    sysLogPrintf(LOG_NOTE,
                            "setup: bound pad %d (id %d) out of range (%d bound pads) - obj skipped",
                            (s32)getBoundPadNum(arg1->pad), (s32)arg1->pad, bcount);
                    return;
                }
            }
#endif
            var_s0 = &g_CurrentSetup.boundpads[getBoundPadNum(arg1->pad)];

            matrix_4x4_set_basis_and_position_target(&sp8C, 0.0f, 0.0f, 0.0f, -var_s0->look.f[0], -var_s0->look.f[1], -var_s0->look.f[2], var_s0->up.f[0], var_s0->up.f[1], var_s0->up.f[2]);

            if (!(arg1->flags2 & PROPFLAG2_00000001))
            {
                sub_GAME_7F001BD4(var_s0, &spD0);

                sp80.f[0] = spD0.f[0] + (var_s0->up.f[0] * ((var_s0->bbox.ymin - var_s0->bbox.ymax) * 0.5f));
                sp80.f[1] = spD0.f[1] + (var_s0->up.f[1] * ((var_s0->bbox.ymin - var_s0->bbox.ymax) * 0.5f));
                sp80.f[2] = spD0.f[2] + (var_s0->up.f[2] * ((var_s0->bbox.ymin - var_s0->bbox.ymax) * 0.5f));

                spCC = var_s0->stan;

                if (walkTilesBetweenPoints_NoCallback(&spCC, var_s0->pos.f[0], var_s0->pos.f[2], spD0.f[0], spD0.f[2]) == 0)
                {
                    spD0.f[0] = var_s0->pos.f[0];
                    spD0.f[1] = var_s0->pos.f[1];
                    spD0.f[2] = var_s0->pos.f[2];

                    spCC = var_s0->stan;

                    if (!(arg1->flags & PROPFLAG_ONSCREEN) && !(arg1->flags & PROPFLAG_00001000))
                    {
                        // removed
                        #ifdef DEBUG
                            osSyncPrintf("object number %d not positioned correctly!\n",cmdindex + 1);
                        #endif
                    }
                }
            }
            else
            {
                spD0.f[0] = var_s0->pos.f[0];
                spD0.f[1] = var_s0->pos.f[1];
                spD0.f[2] = var_s0->pos.f[2];

                spCC = var_s0->stan;

                sub_GAME_7F001BD4(var_s0, &sp80);

                sp80.f[0] += (var_s0->bbox.ymin - var_s0->bbox.ymax) * 0.5f * var_s0->up.f[0];
                sp80.f[1] += (var_s0->bbox.ymin - var_s0->bbox.ymax) * 0.5f * var_s0->up.f[1];
                sp80.f[2] += (var_s0->bbox.ymin - var_s0->bbox.ymax) * 0.5f * var_s0->up.f[2];
            }
        }

        if (getposstan(&spD0, spCC, 0.0f, &spE0, &spDC) != 0)
        {
            if (arg1->type == PROP_TYPE_SMOKE)
            {
                sp60 = sub_GAME_7F051DD8(arg1, PitemZ_entries[spF0].header);
            }
            else
            {
                sp60 = objInitWithAutoModel(arg1);
            }

            if (var_s0 != NULL)
            {
                temp_v0_3 = chrobjGetBboxFromObjectRecord(arg1);
                if (temp_v0_3 != NULL)
                {
                    sp58 = 1.0f;
                    sp54 = 1.0f;
                    sp50 = 1.0f;

                    if (arg1->flags & (PROPFLAG_00000010 | PROPFLAG_00000020))
                    {
                        if (temp_v0_3->Bounds.xmin < temp_v0_3->Bounds.xmax)
                        {
                            if (arg1->flags & PROPFLAG_ONSCREEN)
                            {
                                sp58 = (var_s0->bbox.xmax - var_s0->bbox.xmin) / ((temp_v0_3->Bounds.xmax - temp_v0_3->Bounds.xmin) * arg1->model->scale);
                            }
                            else
                            {
                                sp58 = (var_s0->bbox.xmax - var_s0->bbox.xmin) / ((temp_v0_3->Bounds.xmax - temp_v0_3->Bounds.xmin) * arg1->model->scale);
                            }
                        }
                    }

                    if (arg1->flags & (PROPFLAG_00000010 | PROPFLAG_00000040))
                    {
                        if (temp_v0_3->Bounds.ymin < temp_v0_3->Bounds.ymax)
                        {
                            if (arg1->flags & PROPFLAG_ONSCREEN)
                            {
                                sp50 = (var_s0->bbox.zmax - var_s0->bbox.zmin) / ((temp_v0_3->Bounds.ymax - temp_v0_3->Bounds.ymin) * arg1->model->scale);
                            }
                            else
                            {
                                sp54 = (var_s0->bbox.ymax - var_s0->bbox.ymin) / ((temp_v0_3->Bounds.ymax - temp_v0_3->Bounds.ymin) * arg1->model->scale);
                            }
                        }
                    }

                    if (arg1->flags & (PROPFLAG_00000010 | PROPFLAG_00000080))
                    {
                        if (temp_v0_3->Bounds.zmin < temp_v0_3->Bounds.zmax)
                        {
                            if (arg1->flags & PROPFLAG_ONSCREEN)
                            {
                                sp54 = (var_s0->bbox.ymax - var_s0->bbox.ymin) / ((temp_v0_3->Bounds.zmax - temp_v0_3->Bounds.zmin) * arg1->model->scale);
                            }
                            else
                            {
                                sp50 = (var_s0->bbox.zmax - var_s0->bbox.zmin) / ((temp_v0_3->Bounds.zmax - temp_v0_3->Bounds.zmin) * arg1->model->scale);
                            }
                        }
                    }

                    var_f0 = sp58;

                    if (sp54 < var_f0)
                    {
                        var_f0 = sp54;
                    }

                    if (sp50 < var_f0)
                    {
                        var_f0 = sp50;
                    }

                    sp48 = sp58;

                    if (sp58 < sp54)
                    {
                        sp48 = sp54;
                    }

                    if (sp48 < sp50)
                    {
                        sp48 = sp50;
                    }

                    if (arg1->flags & PROPFLAG_00000010)
                    {
                        sp50 = var_f0;
                        sp54 = var_f0;
                        sp58 = var_f0;
                    }
                    else
                    {
                        if (!(arg1->flags & PROPFLAG_00000020))
                        {
                            if (arg1->flags & PROPFLAG_ONSCREEN)
                            {
                                if (temp_v0_3->Bounds.xmax == temp_v0_3->Bounds.xmin)
                                {
                                    sp58 = sp48;
                                }
                            }
                            else if (temp_v0_3->Bounds.xmax == temp_v0_3->Bounds.xmin)
                            {
                                sp58 = sp48;
                            }
                        }

                        if (!(arg1->flags & PROPFLAG_00000040))
                        {
                            if (arg1->flags & PROPFLAG_ONSCREEN)
                            {
                                if (temp_v0_3->Bounds.ymax == temp_v0_3->Bounds.ymin)
                                {
                                    sp50 = sp48;
                                }
                            }
                            else if (temp_v0_3->Bounds.ymax == temp_v0_3->Bounds.ymin)
                            {
                                sp54 = sp48;
                            }
                        }

                        if (!(arg1->flags & PROPFLAG_00000080))
                        {
                            if (arg1->flags & PROPFLAG_ONSCREEN)
                            {
                                if (temp_v0_3->Bounds.zmax == temp_v0_3->Bounds.zmin)
                                {
                                    sp54 = sp48;
                                }
                            }
                            else if (temp_v0_3->Bounds.zmax == temp_v0_3->Bounds.zmin)
                            {
                                sp50 = sp48;
                            }
                        }
                    }

                    sp58 /= sp48;
                    sp54 /= sp48;
                    sp50 /= sp48;

                    if ((sp58 <= 0.000001f) || (sp54 <= 0.000001f) || (sp50 <= 0.000001f))
                    {
                        #ifdef DEBUG
                        osSyncPrintf("Scale warning: object number %d has a small scale: %f,%f,%f\n",cmdindex +1, sp58,sp54,sp50);
                        #endif
                        sp50 = 1.0f;
                        sp54 = 1.0f;
                        sp58 = 1.0f;
                    }

                    matrix_column_1_scalar_multiply(sp58, sp8C.m[0]);
                    matrix_column_2_scalar_multiply(sp54, sp8C.m[0]);
                    matrix_column_3_scalar_multiply_2(sp50, sp8C.m[0]);

                    modelSetScale(arg1->model, arg1->model->scale * sp48);
                }
            }

            modelSetScale(arg1->model, arg1->model->scale * sp78);
            matrix_scalar_multiply(arg1->model->scale, sp8C.m[0]);

            if (arg1->flags & PROPFLAG_ONSCREEN)
            {
                sub_GAME_7F040BA0(arg1, &spE0, &sp8C, spDC, &sp80);
            }
            else
            {
                sub_GAME_7F04088C(arg1, &spE0, &sp8C, spDC, &sp80);
            }

            setupUpdateObjectRoomPosition(arg1);
            chrpropActivate(sp60);
            chrpropEnable(sp60);
#ifdef GEVR
            gevrAmmoRememberSpawn(arg1, cmdindex);
#endif
        }
        #ifdef DEBUG
        else
        {
            osSyncPrintf("domakedefaultobj: prop obj number %d not reset!\n",cmdindex + 1);
        }
        #endif
    }
}

/**
 * NTSC address 0x7F002738.
 * PAL address 0x7F002738.
*/
void weaponAssignToHome(s32 arg0, WeaponObjRecord* weapon, s32 cmdindex)
{
    s32 padding;
    bool hastoken;
    ChrRecord* chr;
    bool giveweapon;
    s32 temp_a0;
    struct s_mp_weapon_set* weapon_set;

    if ((weapon->flags & PROPFLAG_ASSIGNEDTOCHR))
    {
        chr = chrFindByLiteralId(weapon->pad);

        if (chr && chr->prop && chr->model)
        {
#ifdef GEVR
            const bool surfaceMarksman = arg0 == LEVELID_SURFACE2 && tacticalEncountersEnabled()
                && chr->chrnum == SURFACE2_ROOF_MARKSMAN && chr->bodynum == BODY_Siberian_Special_Forces
                && !(weapon->flags & (PROPFLAG_IS_DOUBLE | PROPFLAG_WEAPON_LEFTHANDED));
            if (surfaceMarksman) {
                /* Reuse the authored rooftop guard and its patrol script. */
                weapon->weaponnum = ITEM_SNIPERRIFLE;
                weapon->obj = PROP_CHRSNIPERRIFLE;
            }
            /* The two final-car guards have a pair of Uzis in the setup.
             * A Tactical kit gives each one rifle, with an optional editor build. */
            if ((weapon->flags & PROPFLAG_IS_DOUBLE) && gevrTrainGuardSingleWeapon(chr)) {
                if (weapon->flags & PROPFLAG_WEAPON_LEFTHANDED) return;
                weapon->flags &= ~PROPFLAG_IS_DOUBLE;
            }
#endif
            if (cheatIsActive(CHEAT_ENEMY_ROCKETS))
            {
                switch ((s8)GEVR_ITEM_AS(weapon->weaponnum))
                {
                    case ITEM_KNIFE:
                    case ITEM_THROWKNIFE:
                    case ITEM_WPPK:
                    case ITEM_WPPKSIL:
                    case ITEM_TT33:
                    case ITEM_SKORPION:
                    case ITEM_AK47:
                    case ITEM_UZI:
                    case ITEM_MP5K:
                    case ITEM_MP5KSIL:
                    case ITEM_SPECTRE:
                    case ITEM_M16:
                    case ITEM_FNP90:
                    case ITEM_SHOTGUN:
                    case ITEM_AUTOSHOT:
                    case ITEM_SNIPERRIFLE:
                    case ITEM_RUGER:
                    case ITEM_GOLDENGUN:
                    case ITEM_SILVERWPPK:
                    case ITEM_GOLDWPPK:
                    case ITEM_LASER:
                    case ITEM_WATCHLASER:
                    case ITEM_REMOTEMINE:
                    case ITEM_TRIGGER:
                    case ITEM_TASER:
                        weapon->weaponnum = ITEM_ROCKETLAUNCH;
                        weapon->obj = PROP_CHRROCKETLAUNCH;
                        weapon->extrascale = 256;
                        break;
                }
            }

            weaponLoadProjectileModels((s8)weapon->weaponnum);
            sub_GAME_7F052030(weapon, chr);
#ifdef GEVR
            if (surfaceMarksman && chr->weapons_held[GUNRIGHT]
                && chr->weapons_held[GUNRIGHT]->weapon->weaponnum == ITEM_SNIPERRIFLE) {
                sysLogPrintf(LOG_NOTE, "tactical marksman: stage %d guard %d pad %u at %.0f %.0f %.0f",
                             arg0, chr->chrnum, 283u, chr->prop->pos.x, chr->prop->pos.y, chr->prop->pos.z);
            }
#endif
        }
        #ifdef DEBUG
        else
        {
            osSyncPrintf("domakeweaponobj: no chr number %d for obj number %d!\n",weapon->pad, cmdindex + 1);
        }
        #endif
    }
    else
    {
        hastoken = 1;
        giveweapon = 1;

#ifdef GEVR
        if (gevrMpRules())
#else
        if (getPlayerCount() >= 2)
#endif
        {
            lastmpweaponnum = -1;

            switch ((u8)weapon->weaponnum)
            {
                case ITEM_UNARMED + 0xF0:
                case ITEM_FIST + 0xF0:
                case ITEM_KNIFE + 0xF0:
                case ITEM_THROWKNIFE + 0xF0:
                case ITEM_WPPK + 0xF0:
                case ITEM_WPPKSIL + 0xF0:
                case ITEM_TT33 + 0xF0:
                case ITEM_SKORPION + 0xF0:
                    weapon_set = getPtrMPWeaponSetData();

                    temp_a0 = (u8)weapon->weaponnum - 0xF0;
                    lastmpweaponnum = temp_a0;

                    weapon->weaponnum = weapon_set[temp_a0].itemID;
                    weapon->obj = weapon_set[temp_a0].propID;
#if defined(VERSION_EU)
                    weapon->extrascale = (weapon_set[temp_a0].size16);
#else
                    weapon->extrascale = (weapon_set[temp_a0].size * 256.0f);
#endif

                    giveweapon = weapon_set[temp_a0].allowpickup;

                    break;

                case ITEM_TOKEN:

                    hastoken = 1;
                    giveweapon = 1;

                    if (get_scenario() != SCENARIO_TLD)
                    {
                        giveweapon = 0;
                    }
                    break;
            }
        }

        if ((weapon->weaponnum != ITEM_UNARMED) && giveweapon)
        {
            weaponLoadProjectileModels(weapon->weaponnum);
            domakedefaultobj(arg0, (struct ObjectRecord*)weapon, cmdindex);
        }
    }
}

//i should be object hat
void setupHat(s32 arg0, ObjectRecord* hat, s32 cmdindex)
{
    if (hat->flags & PROPFLAG_ASSIGNEDTOCHR) {
        ChrRecord* chr = chrFindByLiteralId(hat->pad);
        if (chr && chr->prop && chr->model) {
#ifdef GEVR
            const int helmet = gevrCampaignGuardHelmet(chr);
            if (helmet == GEVR_LOADOUT_NO_HELMET) return;
            if (helmet >= 0 && gevrTacticalHelmetAvailable(helmet)) {
                hat->obj = helmet == GEVR_HELMET_ALTYN ? PROP_TACTICAL_ALTYN : PROP_HATHELMET;
            } else if (gevrTacticalFacilityArmor() && gevrTacticalFacilityVest(chr->chrnum)) {
                if (chr->chrnum == GEVR_FACILITY_CORRIDOR_CENTER && sysArgCheck("--tactical-heavy-guards")
                    && gevrTacticalHelmetAvailable(GEVR_HELMET_ALTYN)) {
                    hat->obj = PROP_TACTICAL_ALTYN;
                } else if (gevrTacticalHelmetAvailable(GEVR_HELMET_SSH68)) {
                    hat->obj = PROP_HATHELMET;
                }
            }
#endif
            hatAssignToChr(hat, chr);
        }
        #ifdef DEBUG
        else
        {
            osSyncPrintf("domakehatobj: no chr number %d for obj number %d!\n",hat->pad, cmdindex + 1);
        }
        #endif
    } else {
        domakedefaultobj(arg0, hat, cmdindex);
    }
}

//i should be object key
void setupKey(s32 arg0, ObjectRecord* key, s32 cmdindex)
{
    domakedefaultobj(arg0, key, cmdindex);
}


/**
 * NTSC address 0x7F002A3C.
*/
void setupCctv(s32 arg0, CCTVRecord *arg1, s32 cmdindex)
{
    struct coord3d *temp_a2;
    struct PadRecord *sp50;
    struct coord3d sp44;
    Mtxf *sp3C;

    domakedefaultobj(arg0, (struct ObjectRecord*)arg1, cmdindex);
#ifdef GEVR
    /*
     * Issue #11 (gepc-ref D307, open there): the camera turned to face its own
     * mounting pad (ObjectRecord.pad), i.e. into the wall, so it never saw Bond.
     * The pad it watches is lookpad (N64 +0x80), which nothing read: a field
     * name slip in the decomp source (a 4-byte read at +0x80 cannot compile
     * from arg1->pad, a 2-byte one at +6). Perfect Dark aims at lookatpadnum.
     */
#define CCTV_LOOKPAD lookpad
#else
#define CCTV_LOOKPAD pad
#endif

    if (arg1->CCTV_LOOKPAD >= 0)
    {
        temp_a2 = (struct coord3d*)arg1->model->obj->Switches[0]->Data;

        if (isNotBoundPad(arg1->CCTV_LOOKPAD))
        {
            sp50 = &g_CurrentSetup.pads[arg1->CCTV_LOOKPAD];
        }
        else
        {
            sp50 = (struct PadRecord *)&g_CurrentSetup.boundpads[getBoundPadNum(arg1->CCTV_LOOKPAD)];
        }

        sp44.f[0] = temp_a2->f[0];
        sp44.f[1] = temp_a2->f[1];
        sp44.f[2] = temp_a2->f[2];

        mtx4RotateVecInPlace(&arg1->mtx, &sp44);

        sp3C = &arg1->unk84;

        sp44.f[0] += arg1->prop->pos.f[0];
        sp44.f[1] += arg1->prop->pos.f[1];
        sp44.f[2] += arg1->prop->pos.f[2];

        matrix_4x4_set_basis_and_position_target(sp3C, 0.0f, 0.0f, 0.0f, sp44.f[0] - sp50->pos.f[0], sp44.f[1] - sp50->pos.f[1], sp44.f[2] - sp50->pos.f[2], 0.0f, 1.0f, 0.0f);
        matrix_scalar_multiply(arg1->model->scale, sp3C->m[0]);

        if (arg1->convert_to_f32 == 0)
        {
            arg1->convert_to_f32 = 1;
            arg1->unkCC = (*(s32*)&arg1->unkCC * M_TAU_F) / 65536.0f;
            arg1->unkD0 = (*(s32*)&arg1->unkD0 * M_TAU_F) / 65536.0f;
            arg1->unkDC = (*(s32*)&arg1->unkDC * M_TAU_F) / 65536.0f;
            arg1->unkE8 = *(s32*)&arg1->unkE8;
        }

        arg1->unkD4 = 0;
        arg1->unkD8 = 0.0f;
        arg1->unkC8 = arg1->unkCC;
        arg1->unkC4 = atan2f(sp44.f[0] - sp50->pos.f[0], sp44.f[2] - sp50->pos.f[2]);
        arg1->timer = 0;
    }
}
#undef CCTV_LOOKPAD

void setupAutogun(s32 stageID, AutogunRecord *autogun, s32 cmdindex)
{
    s8 *beam;

    domakedefaultobj(stageID, (ObjectRecord *) autogun, cmdindex);

#ifdef VERSION_EU
    autogun->speed = ((*((s32 *) (&autogun->speed))) * 7.5398226f) / 65536.0f;
    autogun->aimdist = ((*((s32 *) (&autogun->aimdist))) * 100.0f) / 65536.0f;
    autogun->unk88 = ((*((s32 *) (&autogun->unk88))) * M_TAU_F) / 65536.0f;
    autogun->unk8C = ((*((s32 *) (&autogun->unk8C))) * M_TAU_F) / 65536.0f;
#endif

    autogun->unkAC = 0;
    autogun->unkB8 = -1;
    autogun->unkBC = -1;
    autogun->unkC0 = -1;
    autogun->unkC4 = 0;
    autogun->unkC8 = 0;
    autogun->unk90 = 0.0f;
    autogun->unk94 = 0.0f;
    autogun->rot_related = 0.0f;
    autogun->unk9C = 0.0f;
    autogun->unkA0 = 0.0f;
    autogun->unk98 = 0.0f;
    autogun->unkB0 = 0.0f;
    autogun->unkB4 = 0.0f;

#ifndef VERSION_EU
    autogun->speed = ((*((s32 *) (&autogun->speed))) * M_TAU_F) / 65536.0f;
    autogun->aimdist = ((*((s32 *) (&autogun->aimdist))) * 100.0f) / 65536.0f;
    autogun->unk88 = ((*((s32 *) (&autogun->unk88))) * M_TAU_F) / 65536.0f;
    autogun->unk8C = ((*((s32 *) (&autogun->unk8C))) * M_TAU_F) / 65536.0f;
#endif

    beam          = mempAllocBytesInBank(0x30U, MEMPOOL_STAGE);
    autogun->beam = beam;
    *beam = -1;

    autogun->is_active = FALSE;
    autogun->unkD4 = 0.0f;

    if (autogun->padID >= 0)
    {
        s32 stack1;
        f32 xdiff;
        f32 ydiff;
        f32 zdiff;
        PadRecord *pad;
        PropRecord *prop;

        if (autogun->padID < 0x2710)
        {
            if (1);
            pad = &g_CurrentSetup.pads[autogun->padID];
        }
        else
        {
            pad = &g_CurrentSetup.boundpads[getBoundPadNum(autogun->padID)];
        }

        prop = autogun->prop;

        xdiff = pad->pos.x - prop->pos.x;
        ydiff = pad->pos.y - prop->pos.y;
        zdiff = pad->pos.z - prop->pos.z;

        autogun->rot_related = atan2f(xdiff, zdiff);
        autogun->unk98 = atan2f(ydiff, sqrtf((xdiff * xdiff) + (zdiff * zdiff)));
    }
}


//i should be object rack
void setupHangingMonitors(s32 arg0, ObjectRecord* rack, s32 cmdindex)
{
    domakedefaultobj(arg0, rack, cmdindex);
}


void setupSingleMonitor(s32 stageID, MonitorObjRecord *monitor, s32 cmdindex)
{
    MonitorRecord *record;
    s32 unused;
    s32 modelnum;
    ObjectRecord *owner;
    PropRecord *prop;
    f32 scale;

    monitor->Monitor = g_MonitorAnimController;
    record = &monitor->Monitor;
    monitorSetImageByNum(&monitor->Monitor, monitor->ImageNum);

    if (monitor->pad < 0 && (monitor->flags & PROPFLAG_INSIDEANOTHEROBJ) == 0)
    {
        modelnum = monitor->obj;
        owner = (struct ObjectRecord *)setupGetPtrToCommandByIndex(cmdindex + monitor->OwnerOffset);

        modelLoad(modelnum);

        scale = monitor->extrascale * (1.0f / 256.0f);
        monitor->damage = *(s32*)&monitor->damage / M_U16_MAX_VALUE_F;

#ifdef GEVR
        if (gevrMpRules())
#else
        if (getPlayerCount() >= 2)
#endif
        {
            monitor->state |= PROPSTATE_RESPAWN;
        }

        prop = objInitWithAutoModel((ObjectRecord*)monitor);
        monitor->embedment = embedmentAllocate();

        if (prop && monitor->embedment)
        {
            monitor->runtime_bitflags |= RUNTIMEBITFLAG_EMBEDDED;
            modelSetScale(monitor->model, monitor->model->scale * scale);
            monitor->model->attachedto = owner->model;

            if (monitor->OwnerPart == 0)
            {
                monitor->model->attachedto_objinst = owner->model->obj->Switches[0];
            }
            else if (monitor->OwnerPart == 1)
            {
                monitor->model->attachedto_objinst = owner->model->obj->Switches[1];
            }
            else if (monitor->OwnerPart == 2)
            {
                monitor->model->attachedto_objinst = owner->model->obj->Switches[2];
            }
            else
            {
                monitor->model->attachedto_objinst = owner->model->obj->Switches[3];;
            }

            chrpropReparent(prop, owner->prop);
            matrix_4x4_set_rotation_around_x(0.36651915f, (Mtxf*)&monitor->embedment->matrix);
            matrix_scalar_multiply(monitor->model->scale / owner->model->scale, (f32*)&monitor->embedment->matrix);
        }
    }
    else
    {
        domakedefaultobj(stageID, (ObjectRecord*)monitor, cmdindex);
    }

    if ((monitor->flags & PROPFLAG_MONITOR_RENDERPOSTBG) && monitor->prop)
    {
        monitor->prop->flags |= PROPFLAG_RENDERPOSTBG;
    }
}


void setupMultiMonitor(s32 stageID, MultiMonitorObjRecord* monitor, s32 cmdindex)
{
    monitor->Monitor[0] = g_MonitorAnimController;
    monitorSetImageByNum(&monitor->Monitor[0], monitor->ImageNums[0]);

    monitor->Monitor[1] = g_MonitorAnimController;
    monitorSetImageByNum(&monitor->Monitor[1], monitor->ImageNums[1]);

    monitor->Monitor[2] = g_MonitorAnimController;
    monitorSetImageByNum(&monitor->Monitor[2], monitor->ImageNums[2]);

    monitor->Monitor[3] = g_MonitorAnimController;
    monitorSetImageByNum(&monitor->Monitor[3], monitor->ImageNums[3]);

    domakedefaultobj(stageID, monitor, cmdindex);
}

void sub_GAME_7F00324C(struct BoundPadRecord *arg0, s32 *arg1, s32 *arg2, struct coord3d *arg3, struct coord3d *arg4)
{
    StandTile *sp4C;
    struct coord3d normal;
    s32 padding;
    struct coord3d center;
    StandTile *sp2C;
    f32 scale;

    sub_GAME_7F001BD4(arg0, &center);
    sp2C = (StandTile *)arg0->stan;

#ifdef GEVR
    /* A door volume that found no floor tile at load has no room on either
     * side. The N64 read the room from address 3 and went on with junk; here
     * that read faults during the level load (report 48253132, v0.5.13). */
    if (sp2C == NULL)
    {
        sysLogPrintf(LOG_NOTE, "setup: door bound pad %d has no floor tile - no portal",
                (s32)(arg0 - g_CurrentSetup.boundpads));
        *arg1 = -1;
        *arg2 = -1;
        return;
    }
#endif

    if (walkTilesBetweenPoints_NoCallback(&sp2C, arg0->pos.f[0], arg0->pos.f[2], center.f[0], center.f[2]) == 0)
    {
        sp2C = (StandTile *)arg0->stan;
        center.f[0] = arg0->pos.f[0];
        center.f[1] = arg0->pos.f[1];
        center.f[2] = arg0->pos.f[2];
    }

    normal.f[0] = (arg0->up.f[1] * arg0->look.f[2]) - (arg0->up.f[2] * arg0->look.f[1]);
    normal.f[1] = (arg0->up.f[2] * arg0->look.f[0]) - (arg0->up.f[0] * arg0->look.f[2]);
    normal.f[2] = (arg0->up.f[0] * arg0->look.f[1]) - (arg0->up.f[1] * arg0->look.f[0]);

    scale = 1.0f / sqrtf(((normal.f[0] * normal.f[0]) + (normal.f[1] * normal.f[1])) + (normal.f[2] * normal.f[2]));
    sp4C = sp2C;

    normal.f[0] *= scale;
    normal.f[1] *= scale;
    normal.f[2] *= scale;

    arg3->f[0] = center.f[0] + (normal.f[0] * 50.0f);
    arg3->f[1] = center.f[1];
    arg3->f[2] = center.f[2] + (normal.f[2] * 50.0f);


    walkTilesBetweenPoints_NoCallback(&sp4C, center.f[0], center.f[2], arg3->f[0], arg3->f[2]);

    if (1);

    *arg1 = (s32) sp4C->room;
    sp4C = sp2C;

    arg4->f[0] = center.f[0] - (normal.f[0] * 50.0f);
    arg4->f[1] = center.f[1];
    arg4->f[2] = center.f[2] - (normal.f[2] * 50.0f);

    walkTilesBetweenPoints_NoCallback(&sp4C, center.f[0], center.f[2], arg4->f[0], arg4->f[2]);

    if (1);

    *arg2 = (s32) sp4C->room;

    if (*arg2 == *arg1)
    {
        *arg2 = -1;
    }
}


extern f32 g_DoorScale;
/**
 *
 * NTSC ADDRESS: 7F003480
 * PAL ADDRESS: 7F0033F0
 * Perfect Dark: void setupCreateDoor(struct doorobj *door, s32 cmdindex)
*/
void setupDoor(s32 arg0, struct DoorRecord *door, s32 arg2)
{
    s32 padding; // no sp
    s32 modelnum;
    struct BoundPadRecord *pad;
    StandTile *sp1C8_stan;
    PropRecord *prop;
    struct coord3d sp1B8;
    s32 portalnum; //sp1b4
    s32 sp1B0;
    s32 sp1AC;
    struct coord3d sp1A0;
    struct coord3d sp194;
    struct PortalMetric sp180;
    struct ModelRoData_BoundingBoxRecord *temp_v0;
    struct coord3d sp170;
    StandTile *sp16C;
    Mtxf sp12C;
    f32 temp_f2; // no sp
    ModelFileHeader *sp124;
    struct coord3d sp118;                           /* compiler-managed */
    StandTile *sp114_stan;
    Mtxf spD4;
    struct coord3d spC8;
    Mtxf sp88;
    struct coord3d sp7C;
    struct bbox bb2;
    f32 xscale;
    f32 yscale;
    f32 zscale;
    f32 scale;
    //StandTile *stan;
    u8 *padding2;

    modelnum = door->obj;

    portalnum = -1;
    sp1B0 = -1;
    sp1AC = -1;

    modelLoad(modelnum);

    pad = &(g_CurrentSetup.boundpads[door->pad]);

    if ((door->flags & PROPFLAG_CULL_BEHIND_DOOR) || (door->flags & PROPFLAG_NO_PORTAL_CLOSE))
    {
        sub_GAME_7F00324C(pad, &sp1B0, &sp1AC, &sp1A0, &sp194);

        if ((door->flags & PROPFLAG_CULL_BEHIND_DOOR) && (sp1B0 >= 0) && (sp1AC >= 0))
        {
            portalnum = bgGetPortalBetweenRooms(sp1B0, sp1AC, &sp1A0, &sp194);
        }
    }

    if (g_DoorScale != 1.0f)
    {
        if (portalnum >= 0)
        {
            sub_GAME_7F0B96CC(portalnum, &sp180);
            sp180.min *= get_room_data_float2();

            temp_f2 = (pad->pos.f[0] * sp180.normal.f[0]) + (pad->pos.f[1] * sp180.normal.f[1]) + (pad->pos.f[2] * sp180.normal.f[2]);

            if (g_DoorScale < 1.0f)
            {
                temp_f2 = (temp_f2 - sp180.min) * (1.0f - g_DoorScale);
                sp170.f[0] = pad->pos.f[0] - (sp180.normal.f[0] * temp_f2);
                sp170.f[1] = pad->pos.f[1] - (sp180.normal.f[1] * temp_f2);
                sp170.f[2] = pad->pos.f[2] - (sp180.normal.f[2] * temp_f2);
            }
            else
            {
                temp_f2 = (temp_f2 - sp180.min) * (g_DoorScale - 1.0f);
                sp170.f[0] = pad->pos.f[0] + (sp180.normal.f[0] * temp_f2);
                sp170.f[1] = pad->pos.f[1] + (sp180.normal.f[1] * temp_f2);
                sp170.f[2] = pad->pos.f[2] + (sp180.normal.f[2] * temp_f2);
            }

            sp16C = pad->stan;
            if (walkTilesBetweenPoints_NoCallback(&sp16C, pad->pos.f[0], pad->pos.f[2], sp170.f[0], sp170.f[2]) != 0)
            {
                pad->stan = sp16C;
                pad->pos.f[0] = sp170.f[0];
                pad->pos.f[1] = sp170.f[1];
                pad->pos.f[2] = sp170.f[2];
                pad->bbox.xmin *= g_DoorScale;
                pad->bbox.xmax *= g_DoorScale;
            }
 #ifdef DEBUG
            else
            {
                osSyncPrintf("volume for door object number %d did not have depth changed!\n",arg2 + 1);
            }
            #endif
        }
        else
        {
            pad->bbox.xmin *= g_DoorScale;
            pad->bbox.xmax *= g_DoorScale;
        }
    }

    if (getposstan(&pad->pos, pad->stan, 0.0f, &sp1B8, &sp1C8_stan) != 0)
    {
        matrix_4x4_set_basis_and_position_target(&sp12C, 0, 0, 0, -pad->look.f[0], -pad->look.f[1], -pad->look.f[2], pad->up.f[0], pad->up.f[1], pad->up.f[2]);
        sp124 = PitemZ_entries[modelnum].header;
        sp114_stan = sp1C8_stan;

        bb2.zmax = pad->bbox.xmin;
        bb2.zmin = pad->bbox.xmax; //78
        bb2.ymax = pad->bbox.ymin; //74
        bb2.ymin = pad->bbox.ymax; //70
        bb2.xmax = pad->bbox.zmin; //6c
        bb2.xmin = pad->bbox.zmax; //68

        matrix_4x4_set_rotation_around_x(M_HALF_PI, &spD4);
        matrix_4x4_set_rotation_around_z(M_HALF_PI, &sp88);
        matrix_4x4_multiply_in_place(&sp88, &spD4);
        matrix_4x4_multiply_in_place(&sp12C, &spD4);
        sub_GAME_7F001BD4(pad, &sp118);

        temp_v0 = (struct ModelRoData_BoundingBoxRecord *)sp124->RootNode->Child->Data;

        xscale = (bb2.ymin - bb2.ymax) / (temp_v0->Bounds.xmax - temp_v0->Bounds.xmin);
        yscale = (bb2.xmin - bb2.xmax) / (temp_v0->Bounds.ymax - temp_v0->Bounds.ymin);
        zscale = (bb2.zmin - bb2.zmax) / (temp_v0->Bounds.zmax - temp_v0->Bounds.zmin);

        if ((xscale <= 0.000001f) || (yscale <= 0.000001f) || (zscale <= 0.000001f))
        {
            #ifdef DEBUG
            osSyncPrintf("Scale warning: door object number %d has a small scale: %f,%f,%f\n",arg2 +1, xscale,yscale,zscale);
            #endif
            xscale =
                yscale =
                zscale = 1.0f;
        }

        matrix_column_1_scalar_multiply(xscale, spD4.m[0]);
        matrix_column_2_scalar_multiply(yscale, spD4.m[0]);
        matrix_column_3_scalar_multiply_2(zscale, spD4.m[0]);

        spC8.f[0] = sp118.f[0];
        spC8.f[1] = sp118.f[1];
        spC8.f[2] = sp118.f[2];

        if (!(door->flags2 & 1))
        {
            if (walkTilesBetweenPoints_NoCallback(&sp114_stan, sp1B8.f[0], sp1B8.f[2], sp118.f[0], sp118.f[2]) != 0)
            {
                sp1C8_stan = sp114_stan;
            }
            else
            {
                sp118.f[0] = sp1B8.f[0];
                sp118.f[2] = sp1B8.f[2];

                if (!(door->flags & 0x1000)) // prop flag PROPFLAG_00001000 "Absolute Position"
                {
                    #ifdef DEBUG
                    osSyncPrintf("door object number %d not positioned correctly!\n",arg2 +1);
                    #endif

                }
            }
        }
        else
        {
            sp118.f[0] = sp1B8.f[0];
            sp118.f[1] = sp1B8.f[1];
            sp118.f[2] = sp1B8.f[2];
        }

        if ((door->doorType == DOORTYPE_VERTICAL) || (door->doorType == DOORTYPE_FALLAWAY))
        {
            sp7C.f[0] = pad->look.f[0] * (bb2.xmin - bb2.xmax);
            sp7C.f[1] = pad->look.f[1] * (bb2.xmin - bb2.xmax);
            sp7C.f[2] = pad->look.f[2] * (bb2.xmin - bb2.xmax);
        }
        else
        {
            sp7C.f[0] = pad->up.f[0] * (bb2.ymax - bb2.ymin);
            sp7C.f[1] = pad->up.f[1] * (bb2.ymax - bb2.ymin);
            sp7C.f[2] = pad->up.f[2] * (bb2.ymax - bb2.ymin);
        }

        // These values are stored in the setup files as integers, but at
		// runtime they are floats. Hence reading a "float" as an integer,
		// converting it to a float and writing it back to the same property.
		door->maxFrac = *(s32 *) &door->maxFrac / 65536.0f;
		door->perimFrac = *(s32 *) &door->perimFrac / 65536.0f;
#if defined(VERSION_EU)
        door->accel = (*(s32 *) &door->accel) * 1.2f / 65536.0f;
		door->decel = (*(s32 *) &door->decel) * 1.2f / 65536.0f;
		door->maxSpeed = (*(s32 *) &door->maxSpeed) * 1.2f / 65536.0f;
#else
		door->accel = (*(s32 *) &door->accel) / 65536.0f;
		door->decel = (*(s32 *) &door->decel) / 65536.0f;
		door->maxSpeed = (*(s32 *) &door->maxSpeed) / 65536.0f;
#endif

        prop = doorInit(door, &sp118, &spD4, sp1C8_stan, &sp7C, &spC8);
        if (door->flags & PROPFLAG_CULL_BEHIND_DOOR)
        {
            door->portalNumber = portalnum;
            if ((portalnum >= 0) && (door->openPosition == 0.0f))
            {
                doorDeactivatePortal(door);
            }
            #ifdef DEBUG
            else
            {
                osSyncPrintf("No portal for door object number %d ",arg2 + 1);
            }
            #endif
        }

        prop->rooms[0] = prop->stan->room;
        chrpropRegisterRoom(prop, prop->stan->room);
        prop->rooms[1] = 0xFFU;
        prop->rooms[2] = 0xFFU;

        if ((door->flags & PROPFLAG_CULL_BEHIND_DOOR) || (door->flags & PROPFLAG_NO_PORTAL_CLOSE))
        {
            if (sp1B0 != prop->stan->room)
            {
                if (sp1B0 >= 0)
                {
                    prop->rooms[1] = sp1B0;
                    chrpropRegisterRoom(prop, sp1B0);
                }
            }
            else if (sp1AC >= 0)
            {
                prop->rooms[1] = sp1AC;
                chrpropRegisterRoom(prop, sp1AC);
            }

            if (prop->rooms[1] != 0xff && 1)
            {
                if (!prop->stan->room)
                {
                    #ifdef DEBUG
                        osSyncPrintf("3 rooms for door object number %d\n",arg2 + 1);
                    #endif
                }

            }
            #ifdef DEBUG
            else
            {
                osSyncPrintf("No 2nd room for door object number %d\n",arg2 + 1);
            }
            #endif
        }

        if (door->model != NULL)
        {
            scale = xscale;

            if (scale < yscale)
            {
                scale = yscale;
            }

            if (scale < zscale)
            {
                scale = zscale;
            }

            modelSetScale(door->model, door->model->scale * scale);
        }

        chrpropActivate(prop);
        chrpropEnable(prop);

        if (door->linkedDoorOffset != 0)
        {
            door->linkedDoor = (struct DoorRecord *)setupGetPtrToCommandByIndex(door->linkedDoorOffset + arg2);
        }
    }
    else
    {
        door->prop = NULL;
        #ifdef DEBUG
            osSyncPrintf("proplvreset: prop door object number %d not reset!\n",arg2 + 1);
        #endif
    }

}


// Perfect Dark void setupLoadFiles(s32 stagenum)
void proplvreset2(enum LEVELID stageId)
{
#ifdef GEVR
    gevrCampaignLoadoutsReset(stageId);
    gevrTacticalReinforcementsReset(stageId);
#endif
#ifdef GEVR
    gevrAmmoResetSpawns();
    { extern void gevrTpStageReset(void); gevrTpStageReset(); }   /* gevr_thirdperson.c */
    { extern void gevrGhostStageReset(s32 stage); gevrGhostStageReset(stageId); }   /* gevr_ghost.c */
    { extern void gevrLiveSplitStageReset(void); gevrLiveSplitStageReset(); }   /* gevr_livesplit.c */
    { extern void gevrSpeedrunStageReset(s32 stage); gevrSpeedrunStageReset(stageId); }   /* gevr_speedrun.c */
#endif
    ItemModelFileRecord *pitem;
    s32 withchrs;
    s32 withobjs;

    withchrs = (((void *) tokenFind(1, "-nochr")) == NULL) && (((void *) tokenFind(1, "-noprop")) == NULL);
    withobjs = (((void *) tokenFind(1, "-noobj")) == NULL) && (((void *) tokenFind(1, "-noprop")) == NULL);

    g_DoorScale = 1.0f;

    /**
     * Mark every prop model as "not resident" so the model loads later in this function actually fetch data. Essentially
     * RootNode is doubling as a loaded flag for function modelLoad().
     * 
     * The last entry in the PitemZ_entries table is a terminator which is why 1 is subtracted from the loop length.
     */
    for (pitem = PitemZ_entries; pitem < &PitemZ_entries[g_GevrPropCount]; pitem++)
    {
        pitem->header->RootNode = NULL;
    }

    if ((stageId <= (LEVELID_MAX + 1)) && setup_text_pointers[stageId])
    {
        char strResource[0x100] = ""; // Scratch buffer for synthesizing the setup file's name at runtime.
        s32 numAnimatedObjects = 0;
        s32 numObjects = 0;
        s32 i1 = 0;
        s32 i2 = 0;
        s32 i3 = 0;
        f32 roompos_1;
        s32 i5 = 0;
        s32 i8;
        f32 roompos_2;
        struct stagesetup *local_stage;

        strResource[0] = setup_text_pointers[stageId][0]; // 'U' -> "U"
        strResource[1] = 0; // Terminate so strcat has a valid string.

        /**
         * There are no slots for the mp stages in setup_text_pointers. The name is created
         * by adding "mp_" after the "U" e.g. "Ump_setuparchZ"
         */
#ifdef GEVR
        if ((gevrMpRules() && netStageUsesMpSetup(stageId, NET_MODE_DEATHMATCH) && !netRoundFullMap()) ||
            (netHordeActive() && netStageUsesMpSetup(stageId, NET_MODE_HORDE)))
#else
        if (getPlayerCount() >= 2)
#endif
        {
            strcat(strResource, "mp_"); // -> "Ump_"
        }

        strcat(strResource, setup_text_pointers[stageId] + 1); // Add remaining text back U[mp_] + setupxxxZ

        g_ptrStageSetupFile = _fileNameLoadToBank(strResource, FILELOADMETHOD_DEFAULT, 256, MEMPOOL_STAGE);

        local_stage = g_ptrStageSetupFile;
        langLoadToAddr(langGetLangBankIndexFromStagenum(stageId));

        /**
         * The setup file stores every internal reference as a byte offset from the start of the file,
         * so rebase them all onto the RAM copy at local_stage.
         */
        g_CurrentSetup.pathwaypoints = local_stage->pathwaypoints ? (void *) ((uintptr_t) local_stage + (uintptr_t) local_stage->pathwaypoints) : NULL;
        g_CurrentSetup.waypointgroups = local_stage->waypointgroups ? (void *) ((uintptr_t) local_stage + (uintptr_t) local_stage->waypointgroups) : NULL;
        g_CurrentSetup.intro = local_stage->intro ? (void *) ((uintptr_t) local_stage + (uintptr_t) local_stage->intro) : NULL;
        g_CurrentSetup.propDefs = local_stage->propDefs ? (void *) ((uintptr_t) local_stage + (uintptr_t) local_stage->propDefs) : NULL;
        g_CurrentSetup.patrolpaths = local_stage->patrolpaths ? (void *) ((uintptr_t) local_stage + (uintptr_t) local_stage->patrolpaths) : NULL;
        g_CurrentSetup.ailists = local_stage->ailists ? (void *) ((uintptr_t) local_stage + (uintptr_t) local_stage->ailists) : NULL;
        g_CurrentSetup.pads = local_stage->pads ? (void *) ((uintptr_t) local_stage + (uintptr_t) local_stage->pads) : NULL;
        g_CurrentSetup.boundpads = local_stage->boundpads ? (void *) ((uintptr_t) local_stage + (uintptr_t) local_stage->boundpads) : NULL;

        // Pad names and bound names are optional. An offset of 0 means absent.
        if (local_stage->padnames != 0)
        {
            g_CurrentSetup.padnames = (void *) ((uintptr_t) local_stage + (uintptr_t) local_stage->padnames);
        }
        else
        {
            g_CurrentSetup.padnames = NULL;
        }

        if (local_stage->boundpadnames != 0)
        {
            g_CurrentSetup.boundpadnames = (void *) ((uintptr_t) local_stage + (uintptr_t) local_stage->boundpadnames);
        }
        else
        {
            g_CurrentSetup.boundpadnames = NULL;
        }

        if (g_CurrentSetup.pathwaypoints)
        {
            for (i1 = 0; g_CurrentSetup.pathwaypoints[i1].padID >= 0; i1++)
            {
                g_CurrentSetup.pathwaypoints[i1].neighbours = (void *) ((uintptr_t) g_CurrentSetup.pathwaypoints[i1].neighbours + (uintptr_t) local_stage);
            }
        }

        if (g_CurrentSetup.waypointgroups)
        {
            for (i2 = 0; g_CurrentSetup.waypointgroups[i2].neighbours; i2++)
            {
                g_CurrentSetup.waypointgroups[i2].neighbours = (void *) ((uintptr_t) g_CurrentSetup.waypointgroups[i2].neighbours + (uintptr_t) local_stage);
                g_CurrentSetup.waypointgroups[i2].waypoints = (void *) ((uintptr_t) g_CurrentSetup.waypointgroups[i2].waypoints + (uintptr_t) local_stage);
            }
        }

        // Convert ailist pointers a.k.a. Action Blocks from file-local to proper pointers
        {
            AIListRecord *ailists = g_CurrentSetup.ailists;
            if (ailists)
            {
                for (i3 = 0; g_CurrentSetup.ailists[i3].ailist != 0; i3++)
                {
                    g_CurrentSetup.ailists[i3].ailist = (void *) ((uintptr_t) g_CurrentSetup.ailists[i3].ailist + (uintptr_t) local_stage);
                }
            }
        }

        if (g_CurrentSetup.patrolpaths)
        {
            for (i3 = 0; g_CurrentSetup.patrolpaths[i3].waypoints != NULL; i3++)
            {
                g_CurrentSetup.patrolpaths[i3].waypoints = (void *) ((uintptr_t) g_CurrentSetup.patrolpaths[i3].waypoints + (uintptr_t) local_stage);

                for (i5 = 0; g_CurrentSetup.patrolpaths[i3].waypoints[i5] >= 0; i5++)
                {
                    // Empty
                }

                g_CurrentSetup.patrolpaths[i3].len = i5;
            }
        }

        if (g_CurrentSetup.pads)
        {
            struct PadRecord *pad;
    
            roompos_1 = get_room_data_float2();
            pad = g_CurrentSetup.pads;

            for (; pad->plink != NULL; pad++)
            {
                pad->plink = (void *) ((uintptr_t) local_stage + (uintptr_t) pad->plink);
                pad->pos.f[0] *= roompos_1;
                pad->pos.f[1] *= roompos_1;
                pad->pos.f[2] *= roompos_1;
        
#ifdef DEBUG
                {
                    s32 sret = init_pathtable_something(pad, pad->plink, &pad->stan);
                    if (sret == 0)
                    {
                        osSyncPrintf("pad number %d has no stan! (%s)\n", (s32)(pad - g_CurrentSetup.pads), pad->plink);
                    }
                    else if (sret == 2)
                    {
                        osSyncPrintf("pad number %d changed stan from %s to %s\n", (s32)(pad - g_CurrentSetup.pads), pad->plink, GetStanName(pad->stan));
                    }
                }
#else
                init_pathtable_something(pad, pad->plink, &pad->stan);
#endif

                if (1);
            }
        }

        if (g_CurrentSetup.boundpads)
        {
            struct BoundPadRecord *vol;

            roompos_2 = get_room_data_float2();
            vol = g_CurrentSetup.boundpads;
            
            for (; vol->plink != NULL; vol++)
            {
                vol->plink = (void *) ((uintptr_t) local_stage + (uintptr_t) vol->plink);
                vol->pos.f[0] *= roompos_2;
                vol->pos.f[1] *= roompos_2;
                vol->pos.f[2] *= roompos_2;
                vol->bbox.xmin *= roompos_2;
                vol->bbox.xmax *= roompos_2;
                vol->bbox.ymin *= roompos_2;
                vol->bbox.ymax *= roompos_2;
                vol->bbox.zmin *= roompos_2;
                vol->bbox.zmax *= roompos_2;

#ifdef DEBUG
                {
                    s32 sret = init_pathtable_something((struct PadRecord *) vol, vol->plink, &vol->stan);
                    if (sret == 0)
                    {
                        osSyncPrintf("vol number %d has no stan! (%s)\n", (s32)(vol - g_CurrentSetup.boundpads), vol->plink);
                    }
                    else if (sret == 2)
                    {
                        osSyncPrintf("vol number %d changed stan from %s to %s\n", (s32)(vol - g_CurrentSetup.boundpads), vol->plink, GetStanName(vol->stan));
                    }
                }
#else
                init_pathtable_something((struct PadRecord *) vol, vol->plink, &vol->stan);
#endif

                if (1);
            }
        }

        if (g_CurrentSetup.padnames)
        {
            for (i1 = 0; g_CurrentSetup.padnames[i1].p; i1++)
            {
                g_CurrentSetup.padnames[i1].p = (void *) ((uintptr_t) g_CurrentSetup.padnames[i1].p + (uintptr_t) local_stage);
            }
        }

        if (g_CurrentSetup.boundpadnames)
        {
            // Required for matching.
            if (g_CurrentSetup.ailists && g_CurrentSetup.ailists);

            for (i1 = 0; g_CurrentSetup.boundpadnames[i1].p; i1++)
            {
                g_CurrentSetup.boundpadnames[i1].p = (void *) ((uintptr_t) g_CurrentSetup.boundpadnames[i1].p + (uintptr_t) local_stage);
            }
        }

#ifdef GEVR
        gevrArenaPrepareSetup(stageId);
        gevrHordePrepareSetup();
        if (gevrTacticalRangeActive()) {
            g_CurrentSetup.propDefs = NULL;
            /* Runway's background AI can spawn reinforcements without any placed guards. */
            g_CurrentSetup.ailists = NULL;
            numAnimatedObjects += GEVR_RANGE_TARGETS * 2;
            numObjects += 24;
        }
#endif
        // PD rejoins here

        if (withchrs)
        {
            s32 guards = load_proptype(PROPDEF_GUARD);
            s32 guardObjects = 0;
#ifdef GEVR
            const int extras = gevrCampaignExtraGuardCapacity(stageId);
            guards += extras;
            guardObjects = extras * 2; /* rifle and optional helmet */
#endif
            alloc_init_GUARDdata_entries(guards);
            numAnimatedObjects += guards;
            numObjects += load_proptype(PROPDEF_COLLECTABLE) + guardObjects;
            numObjects += load_proptype(PROPDEF_KEY);
            numObjects += load_proptype(PROPDEF_HAT);
        }
        else
        {
            alloc_init_GUARDdata_entries(0); // chrmgrConfigure
        }

        if (withobjs)
        {
            // load std props for all stages
            numObjects += load_proptype(PROPDEF_DOOR);
            numObjects += load_proptype(PROPDEF_CCTV);
            numObjects += load_proptype(PROPDEF_AUTOGUN);
            numObjects += load_proptype(PROPDEF_RACK);
            numObjects += load_proptype(PROPDEF_MONITOR);
            numObjects += load_proptype(PROPDEF_MULTI_MONITOR);
            numObjects += load_proptype(PROPDEF_ARMOUR);
            numObjects += load_proptype(PROPDEF_PROP);
            numObjects += load_proptype(PROPDEF_GLASS);
            numObjects += load_proptype(PROPDEF_TINTED_GLASS);
            numObjects += load_proptype(PROPDEF_SAFE);
            numObjects += load_proptype(PROPDEF_UNK41);
            numObjects += load_proptype(PROPDEF_GAS_RELEASING);
            numObjects += load_proptype(PROPDEF_ALARM);
            numObjects += load_proptype(PROPDEF_MAGAZINE);
            numObjects += load_proptype(PROPDEF_AMMO);
            numObjects += load_proptype(PROPDEF_VEHICHLE);
            numObjects += load_proptype(PROPDEF_TANK);
            numAnimatedObjects += load_proptype(PROPDEF_AIRCRAFT);
        }

#ifdef GEVR
        if (netHordeActive()) {
            alloc_init_GUARDdata_entries(HORDE_GUARD_SLOTS - 10);
            numAnimatedObjects += HORDE_GUARD_SLOTS;
            numObjects += HORDE_GUARD_SLOTS * 2 + HORDE_AMMO_DROPS + 7;
        }
#endif
        s32 spareModels = weaponGetSlotCapacity();
        if (spareModels > BASE_WEAPON_SLOTS) {
            /* Weapon creation can need a model before it recycles an old
             * weapon. Keep the general-purpose model reserve available. */
            spareModels += BASE_WEAPON_SLOTS;
        }
        modelmgrAllocateModelSlots(numObjects, spareModels);
        modelmgrAllocateAnimModelSlots(numAnimatedObjects);

        for (i8 = 0; i8 < getPlayerCount(); i8++)
        {
            set_cur_player(i8);
            alloc_additional_item_slots(load_proptype(PROPDEF_LINK));
        }

        if (g_CurrentSetup.propDefs)
        {
            PropDefHeaderRecord *phead;
            s32 flags;
            s32 pdefIndex;

            // per-difficulty "Don't Load" mask: PROPFLAG2_00000010/20/40 for Agent/Secret/00
            flags = 1 << (lvlGetSelectedDifficulty() + 4);

            /**
             * Complete the skip loading mask started on the line above. Checks for:
             * - don't load on 2 players
             * - don't load on 3 players
             * - don't load on 4 players
             * - don't load in multiplayer
             */
#ifdef GEVR
            if (gevrMpRules())
#else
            if (getPlayerCount() >= 2)
#endif
            {
#ifdef GEVR
                /* the setup files' bits stop at four players; past four
                 * (online) the stage loads as it does for four */
                flags |= 1 << (MIN(getPlayerCount(), 4) + 20);
#else
                flags |= 1 << (getPlayerCount() + 20);
#endif
            }

            phead = g_CurrentSetup.propDefs;
            pdefIndex = 0;

            while ((i1 = phead->type) != PROPDEF_END)
            {
#ifdef GEVR
                if (gevrHordeSkipProp(phead->type) || gevrArenaSkipProp(phead)) {
                    phead=(PropDefHeaderRecord *)((u32 *)phead+sizepropdef(phead));
                    pdefIndex++; continue;
                }
#endif
                switch (phead->type)
                {
                    case PROPDEF_GUARD_ATTRIBUTE:
                    {
                        GuardAttributeRecord *pdef_guarda;
                        u8 prob;
                        ChrRecord *chr;
                        pdef_guarda = (GuardAttributeRecord *) phead;
                        prob = (u8) pdef_guarda->GrenadeProb;
                        chr = chrFindByLiteralId(pdef_guarda->chrnum);
                        if ((chr && chr->prop) && chr->model)
                        {
                            chr->grenadeprob = prob;
                        }
#ifdef DEBUG
                        else
                        {
                            osSyncPrintf("grenade prob: no chr number %d for obj number %d! ", pdef_guarda->GrenadeProb, pdefIndex + 1);
                        }
#endif
                        break;
                    }
                    case PROPDEF_GUARD:
                        if (withchrs)
                        {
                            expand_09_characters(stageId, (struct GuardRecord *) phead, pdefIndex);
                        }
                        break;
                    case PROPDEF_DOOR:
                        if (withobjs && (!(((struct DoorRecord *) phead)->flags2 & flags)))
                        {
#ifdef GEVR
                            gevrArenaDoorRules(stageId, pdefIndex, (struct DoorRecord *) phead);
#endif
                            setupDoor(stageId, (struct DoorRecord *) phead, pdefIndex);
#ifdef GEVR
                            if ((netHordeActive() || gevrArenaCampaignDeathmatch(stageId)) &&
                                gevrArenaDoorKeyOpens(pdefIndex, ((struct DoorRecord *)phead)->keyflags))
                                ((struct DoorRecord *)phead)->keyflags=0;
#endif
                        }
                        break;
                    case PROPDEF_DOOR_SCALE:
                        g_DoorScale = ((struct GlobalDoorScaleRecord *) phead)->Scale / M_U16_MAX_VALUE_F;
                        break;
                    case PROPDEF_COLLECTABLE:
                        if (withchrs && (!(((struct WeaponObjRecord *) phead)->flags2 & flags)))
                        {
                            WeaponObjRecord *weapon = (WeaponObjRecord *)phead;
#ifdef GEVR
                            /* These two campaign pickups come with 40 mm supply
                             * crates. Give them the existing physical M32. */
                            if (!gevrMpRules() && sysArgCheck("--tactical-physical")
                                && (stageId == LEVELID_SURFACE || stageId == LEVELID_STREETS)
                                && weapon->weaponnum == ITEM_GRENADELAUNCH
                                && !(weapon->flags & PROPFLAG_ASSIGNEDTOCHR)
                                && gevrTacticalExpandedEnabled()) {
                                weapon->weaponnum = ITEM_TACTICAL_M32A1;
                                weapon->obj = PROP_TACTICAL_M32A1;
                            }
#endif
                            weaponAssignToHome(stageId, weapon, pdefIndex);
                        }
                        break;
                    case PROPDEF_KEY:
                        if (withchrs && (!(((struct KeyRecord *) phead)->flags2 & flags)))
                        {
                            setupKey(stageId, (struct ObjectRecord *) phead, pdefIndex);
                        }
                        break;
                    case PROPDEF_HAT:
                        if (withchrs && (!(((struct ObjectRecord *) phead)->flags2 & flags)))
                        {
                            setupHat(stageId, (struct ObjectRecord *) phead, pdefIndex);
                        }
                        break;
                    case PROPDEF_CCTV:
                        if (withobjs && (!(((struct CCTVRecord *) phead)->flags2 & flags)))
                        {
                            setupCctv(stageId, (struct CCTVRecord *) phead, pdefIndex);
                        }
                        break;
                    case PROPDEF_AUTOGUN:
                        if (withobjs && (!(((struct AutogunRecord *) phead)->flags2 & flags)))
                        {
                            setupAutogun(stageId, (struct AutogunRecord *) phead, pdefIndex);
                        }
                        break;
                    case PROPDEF_RACK:
                        if (withobjs && (!(((struct ObjectRecord *) phead)->flags2 & flags)))
                        {
                            setupHangingMonitors(stageId, (struct ObjectRecord *) phead, pdefIndex);
                        }
                        break;
                    case PROPDEF_MONITOR:
                        if (withobjs && (!(((struct MonitorObjRecord *) phead)->flags2 & flags)))
                        {
                            setupSingleMonitor(stageId, (struct MonitorObjRecord *) phead, pdefIndex);
                        }
                        break;
                    case PROPDEF_MULTI_MONITOR:
                        if (withobjs && (!(((struct MultiMonitorObjRecord *) phead)->flags2 & flags)))
                        {
                            setupMultiMonitor(stageId, (struct MultiMonitorObjRecord *) phead, pdefIndex);
                        }
                        break;
                    case PROPDEF_ARMOUR:
                    {
                        struct BodyArmourRecord *pdef_ba = (struct BodyArmourRecord *) phead;
#ifndef VERSION_US
                        if (withobjs && (((pdef_ba->flags2 & flags) == 0) || j_text_trigger)) // JP: armour setup also proceeds when j_text_trigger is set
#else
                        if (withobjs && ((pdef_ba->flags2 & flags) == 0))
#endif
                        {
                            pdef_ba->initialamount = (*((s32 *) (&pdef_ba->initialamount))) / M_U16_MAX_VALUE_F;
                            pdef_ba->amount = pdef_ba->initialamount;
                            domakedefaultobj(stageId, (struct ObjectRecord *) phead, pdefIndex);
                        }
                        break;
                    }
                    case PROPDEF_TINTED_GLASS:
                    {
                        if (withobjs && (!(((struct TintedGlassRecord *) phead)->flags2 & flags)))
                        {
                            if (((struct TintedGlassRecord *) phead)->flags & PROPFLAG_GLASS_HASPORTAL)
                            {
                                if (!(((struct TintedGlassRecord *) phead)->pad < 10000))
                                {
                                    struct coord3d up;
                                    struct coord3d up2;
                                    BoundPadRecord *pad3d;

                                    pad3d = &g_CurrentSetup.boundpads[((struct TintedGlassRecord *) phead)->pad - 10000];
                                    sub_GAME_7F001BD4(pad3d, &up);
                                    up2.x = (10.0f * pad3d->up.x) + up.x;
                                    up2.y = (10.0f * pad3d->up.y) + up.y;
                                    up2.z = (10.0f * pad3d->up.z) + up.z;
                                    up.x -= 10.0f * pad3d->up.x;
                                    up.y -= 10.0f * pad3d->up.y;
                                    up.z -= 10.0f * pad3d->up.z;

                                    ((struct TintedGlassRecord *) phead)->portalnum = sub_GAME_7F0B9E04(&up, &up2);
                                    ((struct TintedGlassRecord *) phead)->unk90 = (*((s32 *) (&((struct TintedGlassRecord *) phead)->unk90))) / M_U16_MAX_VALUE_F;
                                }
                            }
                            domakedefaultobj(stageId, (struct ObjectRecord *) phead, pdefIndex);
                        }
                        break;
                    }
                    case PROPDEF_PROP:
                    case PROPDEF_ALARM:
                    case PROPDEF_MAGAZINE:
                    case PROPDEF_GAS_RELEASING:
                    case PROPDEF_UNK41:
                    case PROPDEF_GLASS:
                    case PROPDEF_SAFE:
                        if (withobjs && (!(((ObjectRecord *) phead)->flags2 & flags)))
                        {
                            domakedefaultobj(stageId, (struct ObjectRecord *) phead, pdefIndex);
                        }
                        break;
                    case PROPDEF_AMMO:
                    {
                        struct MultiAmmoCrateRecord *pdef_macr = (struct MultiAmmoCrateRecord *) phead;
                        s32 ammoqty = 1;
                        s32 i9;

#ifdef GEVR
                        if (gevrMpRules())
#else
                        if (getPlayerCount() >= 2)
#endif
                        {
                            struct s_mp_weapon_set *mpweapon = &getPtrMPWeaponSetData()[lastmpweaponnum];

                            /* A multiplayer crate belongs to the selected weapon set.
                             * The map's original slots must not grant every ammo type. */
                            for (i9 = 0; i9 < AMMOTYPE_GLOBAL_MAX; i9++)
                            {
                                pdef_macr->slots[i9].quantity = 0;
                            }
                            ammoqty = mpweapon->ammoamount;
                            if (mpweapon->ammotype > AMMO_NONE &&
                                mpweapon->ammotype <= AMMOTYPE_GLOBAL_MAX)
                            {
                                pdef_macr->slots[mpweapon->ammotype - 1].quantity = ammoqty;
                            }
                            else
                            {
                                ammoqty = 0;
                            }
                        }

                        if (((ammoqty > 0) && withobjs) && (!(pdef_macr->flags2 & flags)))
                        {
                            for (i9 = 0; i9 < AMMOTYPE_GLOBAL_MAX; i9++)
                            {
                                if ((pdef_macr->slots[i9].quantity > 0) && (pdef_macr->slots[i9].modelnum != 0xFFFF))
                                {
                                    modelLoad(pdef_macr->slots[i9].modelnum);
                                }
                            }

                            domakedefaultobj(stageId, (struct ObjectRecord *) pdef_macr, pdefIndex);
                        }
                        break;
                    }
                    case PROPDEF_TANK:
#ifdef GEVR
                        /* port/src/gevr_setup.c converts these records by hand */
                        _Static_assert(offsetof(struct TankRecord, collision) == 0x90, "tank host layout");
                        _Static_assert(offsetof(struct TankRecord, rect) == 0x98, "tank host layout");
                        _Static_assert(offsetof(struct TankRecord, unkD8) == 0xEC, "tank host layout");
                        _Static_assert(sizeof(struct TankRecord) == 248, "tank host layout");
                        _Static_assert(offsetof(struct AutogunRecord, unkC4) == 0xD8, "autogun host layout");
                        _Static_assert(offsetof(struct AutogunRecord, is_active) == 0xF0, "autogun host layout");
                        _Static_assert(offsetof(struct AutogunRecord, unkD4) == 0xF4, "autogun host layout");
                        _Static_assert(sizeof(struct AutogunRecord) == 248, "autogun host layout");
                        _Static_assert(offsetof(struct criteria_picture, next) == 16 && sizeof(struct criteria_picture) == 24, "photo criteria host layout");
                        _Static_assert(offsetof(struct criteria_roomentered, next) == 16 && sizeof(struct criteria_roomentered) == 24, "room criteria host layout");
                        _Static_assert(offsetof(struct criteria_deposit, next) == 16 && sizeof(struct criteria_deposit) == 24, "deposit criteria host layout");
#endif
                        if (withobjs && (!(((struct TankRecord *) phead)->flags2 & flags)))
                        {
                            struct TankRecord *pdef_tank = (struct TankRecord *) phead;
                            struct PropRecord *tank_prop;

                            s32 padding;
                            f32 stan_y = 0.0f;
                            s32 paddinggg[4];

                            weaponLoadProjectileModels(ITEM_TANKSHELLS);
                            domakedefaultobj(stageId, (struct ObjectRecord *) pdef_tank, pdefIndex);
                            pdef_tank->turret_vertical_angle = 0.0f;
                            pdef_tank->turret_orientation_angle = 0.0f;
                            pdef_tank->tank_orientation_angle = M_TAU_F - atan2f(pdef_tank->mtx.m[2][0], pdef_tank->mtx.m[2][2]);
                            tank_prop = pdef_tank->prop;

                            if (tank_prop)
                            {
                                stan_y = stanGetPositionYValue(tank_prop->stan, tank_prop->pos.f[0], tank_prop->pos.f[2]);
                            }

                            pdef_tank->stan_y = stan_y;
#ifdef VERSION_EU
                            pdef_tank->unkD0 = stan_y / 0.2004f; // EU-tuned constant
#else
                            pdef_tank->unkD0 = stan_y / 0.17000002f;
#endif
                        }
                        break;
                    case PROPDEF_VEHICHLE:
                        if (withobjs && (!(((struct VehichleRecord *) phead)->flags2 & flags)))
                        {
                            struct VehichleRecord *pdef_veh = (struct VehichleRecord *) phead;

                            domakedefaultobj(stageId, (struct ObjectRecord *) pdef_veh, pdefIndex);

                            if (pdef_veh->model != NULL)
                            {
                                if (pdef_veh->model->obj->Switches[5] != NULL)
                                {
                                    modelGetNodeRwData(pdef_veh->model, pdef_veh->model->obj->Switches[5])->Raw.unk00 = (pdef_veh->flags & 0x10000000) == 0;
                                }
                            }

                            pdef_veh->speed        = 0.0f;
                            pdef_veh->wheelxrot    = 0.0f;
                            pdef_veh->wheelyrot    = 0.0f;
                            pdef_veh->speedaim     = 0.0f;
                            pdef_veh->turnrot60    = 0.0f;
                            pdef_veh->roty         = 0.0f;
                            pdef_veh->speedtime60  = -1.0f;
                            pdef_veh->ailist       = ailistFindById(pdef_veh->ailist);
                            pdef_veh->aioffset     = 0;
                            pdef_veh->aireturnlist = -1;
                            pdef_veh->path         = 0;
                            pdef_veh->nextstep     = 0;
                            pdef_veh->Sound        = 0;
                        }
                        break;
                    case PROPDEF_AIRCRAFT:
                        if (withobjs && (!(((struct AircraftRecord *) phead)->flags2 & flags)))
                        {
                            struct AircraftRecord *pdef_air = (struct AircraftRecord *) phead;

                            domakedefaultobj(stageId, (struct ObjectRecord *) pdef_air, pdefIndex);
                            pdef_air->speed           = 0.0f;
                            pdef_air->speedaim        = 0.0f;
                            pdef_air->rotoryrot       = 0.0f;
                            pdef_air->rotaryspeed     = 0.0f;
                            pdef_air->rotaryspeedaim  = 0.0f;
                            pdef_air->yrot            = 0.0f;
                            pdef_air->speedtime60     = -1.0f;
                            pdef_air->rotaryspeedtime = -1.0f;
                            pdef_air->ailist          = ailistFindById(pdef_air->ailist);
                            pdef_air->aioffset        = 0;
                            pdef_air->aireturnlist    = -1;
                            pdef_air->nextstep        = 0;
                            pdef_air->path            = 0;
                            pdef_air->Sound           = 0;
                        }
                        break;
                    case PROPDEF_TAG:
                    {
                        struct TagObjectRecord *pdef_tag;
                        struct ObjectRecord *taggedobj;

                        pdef_tag = (struct TagObjectRecord *) phead;
                        taggedobj = setupCommandGetObject(stageId, pdefIndex + ((s32) pdef_tag->OffsetToObj));
                        pdef_tag->TaggedObject = taggedobj;

                        if (taggedobj)
                        {
                            taggedobj->runtime_bitflags |= RUNTIMEBITFLAG_TAGGED;
                        }

                        set_parent_cur_tag_entry(pdef_tag);
                        break;
                    }
                    case PROPDEF_RENAME:
                    {
                        struct RenameObjectRecord *pdef_ren;
                        struct ObjectRecord *targetobj;

                        pdef_ren = (struct RenameObjectRecord *) phead;
                        i3 = pdef_ren->TagID + pdefIndex;
                        targetobj = setupCommandGetObject(stageId, i3);
                        pdef_ren->renobj = targetobj;

                        if (targetobj)
                        {
                            targetobj->runtime_bitflags |= RUNTIMEBITFLAG_DESTROYED;
                        }

                        bondinvAddTextOverride((struct textoverride *) pdef_ren);
                        break;
                    }
                    case PROPDEF_WATCH_MENU_OBJECTIVE_TEXT:
                        setup_briefing_text_entry_parent((struct setup_objective_text *) phead);
                        break;
                    case PROPDEF_CAMERAPOS:
                    {
                        struct CutsceneRecord *pdef_cam = (struct CutsceneRecord *) phead;

                        pdef_cam->pos.f[0] = (*((s32 *) (&pdef_cam->pos.f[0]))) / 100.0f;
                        pdef_cam->pos.f[1] = (*((s32 *) (&pdef_cam->pos.f[1]))) / 100.0f;
                        pdef_cam->pos.f[2] = (*((s32 *) (&pdef_cam->pos.f[2]))) / 100.0f;
                        pdef_cam->theta = (*((s32 *) (&pdef_cam->theta))) / M_U16_MAX_VALUE_F;
                        pdef_cam->verta = (*((s32 *) (&pdef_cam->verta))) / M_U16_MAX_VALUE_F;
                        break;
                    }
                    case PROPDEF_OBJECTIVE_START:
                        add_ptr_to_objective((struct objective_entry *) phead);
                        break;
                    case PROPDEF_OBJECTIVE_ENTER_ROOM:
                        set_parent_cur_obj_enter_room((struct criteria_roomentered *) phead);
                        break;
                    case PROPDEF_OBJECTIVE_DEPOSIT_OBJECT_IN_ROOM:
                        set_parent_cur_obj_deposited_in_room((struct criteria_deposit *) phead);
                        break;
                    case PROPDEF_OBJECTIVE_PHOTOGRAPH:
                        set_parent_cur_obj_photograph((struct criteria_picture *) phead);
                        break;
                }

                phead = (PropDefHeaderRecord *) (((u32 *) phead) + sizepropdef(phead));
                pdefIndex++;
            }

            phead = g_CurrentSetup.propDefs;
            pdefIndex = 0;

            while (phead->type != PROPDEF_END)
            {
#ifdef GEVR
                if (gevrHordeSkipProp(phead->type) || gevrArenaSkipProp(phead)) {
                    phead=(PropDefHeaderRecord *)((u32 *)phead+sizepropdef(phead));
                    pdefIndex++; continue;
                }
#endif
                switch (phead->type)
                {
                    case PROPDEF_PROP:
                    case PROPDEF_KEY:
                    case PROPDEF_MAGAZINE:
                    case PROPDEF_COLLECTABLE:
                    case PROPDEF_MONITOR:
                    case PROPDEF_AMMO:
                    case PROPDEF_ARMOUR:
                    case PROPDEF_GAS_RELEASING:
                    case PROPDEF_UNK41:
                    case PROPDEF_GLASS:
                    case PROPDEF_SAFE:
                    case PROPDEF_TINTED_GLASS:
                    {
                        struct ObjectRecord *pdef_obj = (struct ObjectRecord *) phead;

                        if (pdef_obj->prop && (pdef_obj->flags & PROPFLAG_INSIDEANOTHEROBJ))
                        {
                            u32 offset = pdef_obj->pad;
                            struct ObjectRecord *inobj = setupCommandGetObject(stageId, offset + pdefIndex);

                            if (inobj && inobj->prop)
                            {
                                pdef_obj->runtime_bitflags |= RUNTIMEBITFLAG_HASOWNER;
                                modelSetScale(pdef_obj->model, pdef_obj->model->scale);
                                chrpropReparent(pdef_obj->prop, inobj->prop);
                            }

#ifdef DEBUG
                            //possibly wrong place
                            else
                            {
                                osSyncPrintf("inobj link not found for object number %d\n", pdefIndex + 1);
                            }
#endif

                        }
                        break;
                    }
                    case PROPDEF_LINK:
                    {
                        struct LinkRecord *pdef_link = (struct LinkRecord *) phead;
                        struct WeaponObjRecord *guna = (struct WeaponObjRecord *) setupGetPtrToCommandByIndex(pdef_link->Index1 + pdefIndex);
                        struct WeaponObjRecord *gunb = (struct WeaponObjRecord *) setupGetPtrToCommandByIndex(pdef_link->Index2 + pdefIndex);

                        if (guna && gunb)
                        {
                            if ((guna->type == PROPDEF_COLLECTABLE) && (gunb->type == PROPDEF_COLLECTABLE))
                            {
                                propweaponSetDual(guna, gunb);
                            }
#ifdef DEBUG
                            else
                            {
                                osSyncPrintf("link type wrong for doublegun object number %d\n", pdefIndex + 1);
                            }
                        }
                        else
                        {
                            osSyncPrintf("link not found for doublegun object number %d\n", pdefIndex + 1);
#endif

                        }

                        break;
                    }
                    case PROPDEF_SWITCH:
                    {
                        struct LinkRecord *pdef_switch;
                        struct ObjectRecord *doorA;
                        struct ObjectRecord *doorB;
                        s32 index1;
                        s32 index2;

                        pdef_switch = (struct LinkRecord *) phead;
                        index1 = pdef_switch->Index1;
                        index2 = pdef_switch->Index2;
                        doorA = (struct ObjectRecord *) setupCommandGetObject(stageId, pdefIndex + index1);
                        doorB = (struct ObjectRecord *) setupGetPtrToCommandByIndex(pdefIndex + index2);

                        if ((((doorA && doorA->prop) && doorB) && (doorB->type == PROPDEF_DOOR)) && doorB->prop)
                        {
                            pdef_switch->first = doorA->prop;
                            pdef_switch->second = doorB->prop;
                            initSetLevelLoadPropSwitch(pdef_switch);
                            doorA->runtime_bitflags |= RUNTIMEBITFLAG_00000001; // linked door
                        }

#ifdef DEBUG
                        else
                        {
                            osSyncPrintf("doorlink object number %d not initialised\n", pdefIndex + 1);
                        }
#endif

                        break;
                    }
                    case PROPDEF_SAFE_ITEM:
                    {
                        s32 index1;
                        struct SafeObjectRecord *pdef_safe;
                        s32 index2;
                        s32 index3;
                        struct ObjectRecord *safe_item;
                        struct SafeRecord *safe;
                        struct DoorRecord *door;

                        pdef_safe = (struct SafeObjectRecord *) phead;
                        index1 = pdef_safe->Index1;
                        index2 = pdef_safe->Index2;
                        index3 = pdef_safe->Index3;
                        safe_item = setupCommandGetObject(stageId, pdefIndex + index1);
                        safe = (struct SafeRecord *) setupCommandGetObject(stageId, pdefIndex + index2);
                        door = (struct DoorRecord *) setupCommandGetObject(stageId, pdefIndex + index3);

                        if (((((((safe_item && safe_item->prop) && safe) && safe->prop) && (safe->type == PROPDEF_SAFE)) && door) && door->prop) && (door->type == PROPDEF_DOOR))
                        {
                            pdef_safe->item = safe_item;
                            pdef_safe->safe = safe;
                            pdef_safe->door = door;
                            initSetLevelLoadPropSafeItem((struct ObjectRecord *) pdef_safe);
                            safe_item->flags2 |= PROPFLAG2_LINKEDTOSAFE;
                            door->flags2 |= PROPFLAG2_LINKEDTOSAFE;
                        }
#ifdef DEBUG
                        else
                        {
                            osSyncPrintf("safelink object number %d not initialised\n", pdefIndex + 1);
                        }
#endif
                        break;
                    }
                    case PROPDEF_LOCK_DOOR:
                    {
                        struct LockDoorRecord *pdef_lock_door;
                        struct DoorRecord *door;
                        struct ObjectRecord *lock;
                        s32 index1;
                        s32 index2;

                        pdef_lock_door = (struct LockDoorRecord *) phead;

                        index1 = pdef_lock_door->Index1;
                        index2 = pdef_lock_door->Index2;

                        door = (struct DoorRecord *) setupCommandGetObject(stageId, pdefIndex + index1);
                        lock = setupCommandGetObject(stageId, pdefIndex + index2);

                        if ((((door && door->prop) && lock) && lock->prop) && (door->type == PROPDEF_DOOR))
                        {
                            pdef_lock_door->door = door;
                            pdef_lock_door->lock = lock;
                            initSetLevelLoadPropLockDoor(pdef_lock_door);
                            door->runtime_bitflags |= RUNTIMEBITFLAG_PADLOCKEDDOOR;
                        }
#ifdef DEBUG
                        else
                        {
                            osSyncPrintf("doorlock object number %d not initialised\n", pdefIndex + 1);
                        }
#endif
                        break;
                    }
                }

                phead = (PropDefHeaderRecord *) (((u32 *) phead) + sizepropdef(phead));
                pdefIndex += 1;
            }
        }
#ifdef GEVR
        for (int i = 0; i < g_NumChrSlots; ++i) gevrCampaignFinishGuard(&g_ChrSlots[i], TRUE);
        gevrDamBoatPrepare(stageId);
#endif
    }
    else
    {
        g_CurrentSetup.pathwaypoints = NULL;
        g_CurrentSetup.waypointgroups = NULL;
        g_CurrentSetup.intro = 0;
        g_CurrentSetup.propDefs = 0;
        g_CurrentSetup.patrolpaths = NULL;
        g_CurrentSetup.ailists = NULL;
        g_CurrentSetup.pads = NULL;
        g_CurrentSetup.boundpads = NULL;
        g_CurrentSetup.padnames = NULL;
        g_CurrentSetup.boundpadnames = NULL;
        alloc_init_GUARDdata_entries(0);
        modelmgrAllocateModelSlots(0, BASE_WEAPON_SLOTS);
        modelmgrAllocateAnimModelSlots(0);
    }

    alloc_false_GUARDdata_to_exec_global_action();
}
