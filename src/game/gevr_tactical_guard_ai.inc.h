/* Tactical campaign combat for ordinary guards. Mission scripts retain their
 * original AI lists. In co-op only the host runs this; guests pose its actors. */
#include "gevr_tactical_guard_ai.h"
#include "gevr_tactical_mesh.h"
#include "gevr_tactical_range.h"
#include "gevr_ally.h"
#include "gevr_guard_cover.h"

enum { TACTICAL_AI_SLOTS = 512, TACTICAL_SENSE_TICKS = 6, TACTICAL_GUARD_REPORTS = 32 };
enum TacticalGuardRole { GUARD_ASSAULT, GUARD_SUPPORT, GUARD_CROUCH, GUARD_FLANK, GUARD_COVER };
enum TacticalContact { CONTACT_NONE, CONTACT_IMPACT, CONTACT_BODY, CONTACT_COMBAT, CONTACT_SOUND, CONTACT_TEAM, CONTACT_SIGHT };
enum TacticalGuardOrder { ORDER_NONE, ORDER_SEARCH, ORDER_CHARGE, ORDER_FIRE, ORDER_FLANK, ORDER_COVER, ORDER_PEEK };

typedef struct TacticalGuardMind {
    Model *model;
    s16 chrnum, corpse, group;
    s8 member, scriptPlayer;
    s32 nextScriptSight;
    s32 nextSense, nextBody, nextOrder, nextCover, expires, clearUntil, lastNoise, nextBlockedTrace, nextCall;
    s32 lastGuardNoise, lastDischarge;
    coord3d location;
    StandTile *floor;
    coord3d cover;
    StandTile *coverFloor;
    s32 coverArriveBy, nextPeek, peekUntil;
    s8 peekPose, peekSide;
    enum TacticalContact contact;
    enum TacticalGuardOrder order;
    bool active, crouched, scriptSight;
} TacticalGuardMind;

typedef struct TacticalGunNoise {
    coord3d location;
    StandTile *floor;
    coord3d aim;          /* what a firing guard was shooting at */
    StandTile *aimFloor;
    f32 radius;
    s32 time;
    s16 guard;
} TacticalGunNoise;

static TacticalGuardMind s_guardMinds[TACTICAL_AI_SLOTS];
static TacticalGunNoise s_gunNoises[MAX_PLAYER_COUNT];
static TacticalGunNoise s_guardNoises[TACTICAL_GUARD_REPORTS];
static unsigned s_nextGuardNoise;
static s32 s_pathTick, s_pathsTried;
static bool s_traceGuardAi, s_tacticalGuardAi;

void gevrTacticalGuardAiReset(void)
{
    memset(s_guardMinds, 0, sizeof(s_guardMinds));
    memset(s_gunNoises, 0, sizeof(s_gunNoises));
    memset(s_guardNoises, 0, sizeof(s_guardNoises));
    s_nextGuardNoise = 0;
    /* --tactical-classic-ai leaves ordinary guards on their native lists, as in
     * 0.08, for side-by-side tests of the 0.11-0.16 combat behaviour. */
    s_tacticalGuardAi = sysArgCheck("--tactical-physical") && sysArgCheck("--tactical-guards")
        && !sysArgCheck("--tactical-classic-ai");
    s_traceGuardAi = sysArgCheck("--tactical-ai-trace");
    s_pathTick = -1;
    s_pathsTried = 0;
}

static bool tacticalGuardAiAllowed(void)
{
    return s_tacticalGuardAi && !gevrRomIsVariant() && !gevrTacticalRangeActive() && gevrTacticalCampaignRules()
        && (!netIsActive() || gevrCoopHostGuards());
}

static bool tacticalGuardAlive(const ChrRecord *guard)
{
    return guard && guard->model && guard->prop && guard->prop->stan && guard->chrnum >= 0
        && !(guard->hidden & CHRHIDDEN_REMOVE) && !(guard->chrflags & CHRFLAG_HIDDEN)
        && guard->actiontype != ACT_DEAD && guard->actiontype != ACT_DIE;
}

static bool tacticalGuardHostile(ChrRecord *guard)
{
    if ((guard->chrflags & (CHRFLAG_INVINCIBLE
            | CHRFLAG_CAN_SHOOT_CHRS | CHRFLAG_COUNT_DEATH_AS_CIVILIAN))
        || guard->headnum == HEAD_Male_Mishkin || gevrAllyIs(guard)) return FALSE;
    switch (guard->bodynum) {
    case BODY_Jungle_Commando: case BODY_St_Petersburg_Guard:
    case BODY_Russian_Soldier: case BODY_Russian_Infantry: case BODY_Russian_Commandant:
    case BODY_Janus_Special_Forces: case BODY_Janus_Marine:
    case BODY_Siberian_Guard_1_Mishkin: case BODY_Siberian_Special_Forces:
    case BODY_Siberian_Guard_2: case BODY_Arctic_Commando:
    case BODY_Moonraker_Elite_1_Male: case BODY_Moonraker_Elite_2_Female:
        return TRUE;
    default: return FALSE;
    }
}

int gevrTacticalGuardCorpseEligible(ChrRecord *guard)
{
    return tacticalGuardAiAllowed() && guard && guard->chrnum >= 0 && guard->prop
        && guard->prop->type == PROP_TYPE_CHR && tacticalGuardHostile(guard);
}

static bool tacticalGuardOrdinary(ChrRecord *guard)
{
    if (!tacticalGuardAlive(guard) || !tacticalGuardHostile(guard)) return FALSE;
    for (int hand = GUNRIGHT; hand <= GUNLEFT; hand++) {
        const PropRecord *gun = guard->weapons_held[hand];
        if (gun && gun->type == PROP_TYPE_WEAPON && gun->weapon
            && gevrCampaignGunIsFirearm(gun->weapon->weaponnum)) return TRUE;
    }
    return FALSE;
}

static bool tacticalGuardManaged(ChrRecord *guard)
{
    if (!tacticalGuardOrdinary(guard) || (guard->chrflags & CHRFLAG_CLONE)) return FALSE;

    /* Only combat/idle lists that are safe to drive directly. Guards in
     * cinematics, alarm scripts, objective scenes and clone triggers are excluded. */
    extern u8 m_StandardGuard[], m_SimpleGuard[], m_SimpleGuardDeaf[];
    extern u8 m_AttackBond[], m_RunToBond[], m_IdleAnimations[];
    const void *list = guard->ailist;
    if (guard->aireturnlist >= 0x0400
        && (list == m_AttackBond || list == m_RunToBond || list == m_IdleAnimations)) return FALSE;
    return list == m_StandardGuard || list == m_SimpleGuard || list == m_SimpleGuardDeaf
        || list == m_AttackBond || list == m_RunToBond || list == m_IdleAnimations;
}

static TacticalGuardMind *tacticalGuardMind(const ChrRecord *guard)
{
    const ptrdiff_t slot = guard - g_ChrSlots;
    if (slot < 0 || slot >= TACTICAL_AI_SLOTS) return NULL;
    TacticalGuardMind *mind = &s_guardMinds[slot];
    if (mind->model != guard->model || mind->chrnum != guard->chrnum) {
        memset(mind, 0, sizeof(*mind));
        mind->model = guard->model;
        mind->chrnum = guard->chrnum;
        mind->corpse = CHR_FREE;
        mind->lastNoise = -1;
        mind->lastGuardNoise = -1;
        mind->lastDischarge = -60;
        mind->scriptPlayer = -1;
        mind->group = gevrJunglePatrolSquad(guard);
        if (mind->group >= 0) {
            mind->member = guard->chrnum % GEVR_JUNGLE_SQUAD_SIZE;
        } else {
            int member = 0;
            const int post = gevrCampaignGuardPost(guard, &member);
            if (post >= 0) {
                mind->group = 100 + post;
                mind->member = member;
            }
        }
        mind->nextSense = g_GlobalTimer + slot % TACTICAL_SENSE_TICKS;
        mind->nextBody = g_GlobalTimer + slot % 15;
    }
    return mind;
}

static f32 tacticalDistance(const coord3d *a, const coord3d *b)
{
    return SQR(a->x - b->x) + SQR(a->y - b->y) + SQR(a->z - b->z);
}

static bool tacticalTrace(ChrRecord *guard, const coord3d *from, StandTile *tile,
    const coord3d *to, StandTile *dest, f32 height, f32 targetHeight, int extraCollision)
{
    if (!tile || !dest) return FALSE;
    const f32 startY = stanGetPositionYValue(tile, from->x, from->z) + height;
    const f32 endY = stanGetPositionYValue(dest, to->x, to->z) + targetHeight;
    const bool wasMoving = (guard->hidden & CHRHIDDEN_MOVING) != 0;
    chrSetMoving(guard, FALSE);
    const bool clear = stanTestLineUnobstructed(&tile, from->x, from->z, to->x, to->z,
        CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER | CDTYPE_AIOPAQUE | extraCollision,
        startY + 1.0f, startY - 1.0f, endY + 1.0f, endY - 1.0f) && tile == dest;
    chrSetMoving(guard, !wasMoving);
    return clear;
}

static bool tacticalSightLine(ChrRecord *guard, const coord3d *from, StandTile *tile,
    const coord3d *to, StandTile *dest, f32 height, int extraCollision)
{
    return tacticalTrace(guard, from, tile, to, dest, height, height, extraCollision);
}

static f32 tacticalTargetHeight(const PropRecord *target, f32 fallback)
{
    if (target->type == PROP_TYPE_PLAYER || target->type == PROP_TYPE_VIEWER) {
        const f32 ground = stanGetPositionYValue(target->stan, target->pos.x, target->pos.z);
        return fminf(fmaxf(target->pos.y - ground - 14.0f, 45.0f), 150.0f);
    } else if (target->type == PROP_TYPE_CHR && target->chr
            && (target->chr->actiontype == ACT_DEAD || target->chr->actiontype == ACT_DIE)) {
        return 20.0f;
    }
    return fallback;
}

static bool tacticalGuardSight(ChrRecord *guard, const PropRecord *target, f32 height, int chars)
{
    if (!target || !target->stan) return FALSE;
    return tacticalTrace(guard, &guard->prop->pos, guard->prop->stan,
        &target->pos, target->stan, height, tacticalTargetHeight(target, height), chars);
}

static bool tacticalGuardSightFromCover(ChrRecord *guard, const PropRecord *target);

void gevrTacticalGuardNoise(int player, float radius)
{
    if (!tacticalGuardAiAllowed() || player < 0 || player >= MAX_PLAYER_COUNT
        || !g_playerPointers[player] || !g_playerPointers[player]->prop) return;
    const PropRecord *prop = g_playerPointers[player]->prop;
    if (!prop->stan || g_playerPointers[player]->bonddead) return;
    TacticalGunNoise *event = &s_gunNoises[player];
    /* Keep one recent report per player. Repeated firing updates the position
     * at most ten times per second and never extends the original radius. */
    if (event->floor && g_GlobalTimer - event->time < 6 && radius <= event->radius) return;
    const bool freshReport = !event->floor || g_GlobalTimer - event->time > 30;
    event->location = prop->pos;
    event->floor = prop->stan;
    event->time = g_GlobalTimer;
    event->radius = fminf(fmaxf(radius, 1000.0f), 6200.0f);
    if (s_traceGuardAi && freshReport)
        sysLogPrintf(LOG_NOTE, "tactical AI: player %d report radius %.0f", player, (double)event->radius);
}

static void tacticalGuardDischarge(ChrRecord *guard, bool suppressed)
{
    if (!tacticalGuardAiAllowed() || !tacticalGuardOrdinary(guard)) return;
    TacticalGuardMind *mind = tacticalGuardMind(guard);
    if (!mind || g_GlobalTimer - mind->lastDischarge < 10) return;
    mind->lastDischarge = g_GlobalTimer;
    /* Guards who hear this turn toward the threat, not toward the friendly
     * shooter (0.16 had them kneel facing the guard who fired). */
    coord3d aim = guard->prop->pos;
    StandTile *aimFloor = guard->prop->stan;
    if (mind->floor && mind->expires > g_GlobalTimer && mind->contact >= CONTACT_SOUND) {
        aim = mind->location;
        aimFloor = mind->floor;
    } else if (guard->targetTile) {
        aim = guard->lastknowntargetpos;
        aimFloor = guard->targetTile;
    }
    s_guardNoises[s_nextGuardNoise++ % TACTICAL_GUARD_REPORTS] = (TacticalGunNoise){
        .location = guard->prop->pos, .floor = guard->prop->stan, .aim = aim, .aimFloor = aimFloor,
        .radius = suppressed ? 1100.0f : 2200.0f, .time = g_GlobalTimer, .guard = guard->chrnum};
}

static void tacticalGuardNotice(TacticalGuardMind *mind, const coord3d *pos, StandTile *tile,
    enum TacticalContact contact, int lifetime)
{
    const bool first = mind->expires <= g_GlobalTimer;
    if (!first && contact < mind->contact && (mind->contact != CONTACT_SIGHT || mind->clearUntil > g_GlobalTimer)) return;
    if (s_traceGuardAi && contact != mind->contact) {
        static const char *const kinds[] = {"none", "near miss", "body", "nearby combat", "sound", "squad", "sight"};
        sysLogPrintf(LOG_NOTE, "tactical AI: guard %d contact %s at %.0f,%.0f,%.0f",
            mind->chrnum, kinds[contact], (double)pos->x, (double)pos->y, (double)pos->z);
    }
    if (first || contact != mind->contact) mind->nextOrder = g_GlobalTimer;
    mind->location = *pos;
    mind->floor = tile;
    mind->expires = g_GlobalTimer + lifetime;
    mind->contact = contact;
}

void gevrTacticalGuardReport(ChrRecord *guard, const coord3d *point, StandTile *floor)
{
    TacticalGuardMind *mind = tacticalGuardMind(guard);
    if (mind && floor) tacticalGuardNotice(mind, point, floor, CONTACT_TEAM, 330);
}

static bool tacticalGuardFacing(ChrRecord *guard, const coord3d *at, bool alert)
{
    const f32 dx = at->x - guard->prop->pos.x;
    const f32 dz = at->z - guard->prop->pos.z;
    const f32 d2 = SQR(dx) + SQR(dz);
    if (d2 < SQR(480.0f)) return TRUE;
    const f32 yaw = getsubroty(guard->model);
    const f32 dot = dx * sinf(yaw) + dz * cosf(yaw);
    /* Close lateral movement is visible. At range the rear remains blind
     * even for an alert sentry. */
    const f32 edge = alert || d2 < SQR(700.0f) ? -0.58f : -0.22f;
    return dot > edge * sqrtf(d2);
}

static const TacticalGunNoise *tacticalLoudestNoise(ChrRecord *guard, const TacticalGunNoise *events,
    size_t count, s32 lastHeard, bool guardReports)
{
    const coord3d *from = &guard->prop->pos;
    const TacticalGunNoise *best = NULL;
    f32 bestStrength = 0.0f;
    for (size_t i = 0; i < count; i++) {
        const TacticalGunNoise *noise = &events[i];
        if (!noise->floor || noise->time <= lastHeard || g_GlobalTimer - noise->time > 42
            || (guardReports && noise->guard == guard->chrnum)) continue;
        f32 radius = noise->radius * fminf(fmaxf(guard->hearingscale, 0.75f), 1.35f);
        const f32 d2 = tacticalDistance(from, &noise->location);
        if (d2 >= SQR(radius)) continue;
        if (1.0f - d2 / SQR(radius) <= bestStrength) continue;
        if (!tacticalSightLine(guard, from, guard->prop->stan, &noise->location,
                noise->floor, guard->chrheight * 0.65f, 0)) radius *= 0.48f;
        if (d2 >= SQR(radius)) continue;
        const f32 strength = 1.0f - d2 / SQR(radius);
        if (strength > bestStrength) {
            best = noise;
            bestStrength = strength;
        }
    }
    return best;
}

static void tacticalGuardHear(ChrRecord *guard, TacticalGuardMind *mind, bool hearGuards)
{
    const TacticalGunNoise *best = tacticalLoudestNoise(guard, s_gunNoises, MAX_PLAYER_COUNT, mind->lastNoise, FALSE);
    if (best) {
        mind->lastNoise = best->time;
        tacticalGuardNotice(mind, &best->location, best->floor, CONTACT_SOUND, 410);
    } else if (hearGuards) {
        best = tacticalLoudestNoise(guard, s_guardNoises, TACTICAL_GUARD_REPORTS, mind->lastGuardNoise, TRUE);
        if (best) {
            mind->lastGuardNoise = best->time;
            tacticalGuardNotice(mind, best->aimFloor ? &best->aim : &best->location,
                best->aimFloor ? best->aimFloor : best->floor, CONTACT_COMBAT, 370);
        }
    }
}

static void tacticalGuardTeam(ChrRecord *guard, TacticalGuardMind *mind, int squad)
{
    if (mind->contact >= CONTACT_TEAM) return;
    for (int i = 0; i < g_NumChrSlots && i < TACTICAL_AI_SLOTS; i++) {
        const TacticalGuardMind *partner = &s_guardMinds[i];
        ChrRecord *other = &g_ChrSlots[i];
        const bool sameGroup = squad >= 0 && squad == partner->group;
        if (partner == mind || partner->contact != CONTACT_SIGHT || partner->clearUntil <= g_GlobalTimer
            || partner->model != other->model || !tacticalGuardAlive(other) || !tacticalGuardHostile(other)
            || tacticalDistance(&guard->prop->pos, &other->prop->pos) > SQR(sameGroup ? 1800.0f : 1050.0f)) continue;
        if (!tacticalGuardSight(guard, other->prop, guard->chrheight - 20.0f, 0)) continue;
        tacticalGuardNotice(mind, &partner->location, partner->floor, CONTACT_TEAM, 345);
        return;
    }
}

static void tacticalGuardFindBody(ChrRecord *guard, TacticalGuardMind *mind)
{
    if (g_GlobalTimer < mind->nextBody) return;
    mind->nextBody = g_GlobalTimer + 19;
    /* The physics body outlives the native actor and can move away from its
     * original spot. Use the currently visible body as evidence first. */
    for (int slot = 0; ; slot++) {
        coord3d body;
        int actor;
        const int present = gevrRagdollGuardBody(slot, &actor, &body);
        if (present < 0) break;
        if (!present || actor == guard->chrnum || actor == mind->corpse || tacticalDistance(&guard->prop->pos, &body) > SQR(1550.0f)
            || !tacticalGuardFacing(guard, &body, TRUE)) continue;
        coord3d probe = body;
        probe.y += 25.0f;
        f32 ground;
        StandTile *floor = stanFindTileBelowPos(&probe, NULL, &ground);
        if (!floor || !tacticalTrace(guard, &guard->prop->pos, guard->prop->stan, &body, floor,
                guard->chrheight - 20.0f, fmaxf(12.0f, body.y - ground), 0)) continue;
        mind->corpse = actor;
        if (s_traceGuardAi) sysLogPrintf(LOG_NOTE, "tactical AI: guard %d noticed ragdoll %d", guard->chrnum, actor);
        tacticalGuardNotice(mind, &body, floor, CONTACT_BODY, 450);
        return;
    }
    /* Kept bodies give up their chrnum for mission scripts about 1.5 s after
     * death (gevrBodyRetireForAi), so a retired body is known by its slot.
     * 0.16 skipped them, and patrols walked past anything but a fresh kill. */
    for (int i = 0; i < g_NumChrSlots; i++) {
        ChrRecord *body = &g_ChrSlots[i];
        const int id = body->chrnum >= 0 ? body->chrnum : -2 - i;
        if (body == guard || !body->model || !body->prop || !body->prop->stan
            || id == mind->corpse || gevrRagdollHasBody(body)
            || (body->actiontype != ACT_DIE && body->actiontype != ACT_DEAD)
            || body->prop->type != PROP_TYPE_CHR || (body->hidden & CHRHIDDEN_REMOVE)
            || (body->chrflags & CHRFLAG_HIDDEN) || !tacticalGuardHostile(body)
            || tacticalDistance(&guard->prop->pos, &body->prop->pos) > SQR(1550.0f)
            || !tacticalGuardFacing(guard, &body->prop->pos, TRUE)
            || !tacticalGuardSight(guard, body->prop, guard->chrheight - 20.0f, 0)) continue;
        mind->corpse = id;
        if (s_traceGuardAi) sysLogPrintf(LOG_NOTE, "tactical AI: guard %d noticed body %d", guard->chrnum, id);
        tacticalGuardNotice(mind, &body->prop->pos, body->prop->stan, CONTACT_BODY, 450);
        return;
    }
}

static bool tacticalGuardCanSpot(ChrRecord *guard, int slot, bool alert)
{
    const struct player *player = g_playerPointers[slot];
    const PropRecord *target = player ? player->prop : NULL;
    const bool marksman = gevrCampaignGuardIsMarksman(guard);
    const f32 cap = marksman ? (alert ? 9000.0f : 7200.0f) : (alert ? 4600.0f : 3600.0f);
    const f32 range = fminf(fmaxf(guard->visionrange * 100.0f, 700.0f), cap);
    const TacticalGuardMind *mind = tacticalGuardMind(guard);
    const f32 sightHeight = guard->chrheight - ((guard->actiontype == ACT_KNEEL || (mind && mind->coverFloor)) ? 48.0f : 20.0f);
    return target && bondviewGetVisibleToGuardsFlag() && !player->bonddead
        && (!netIsActive() || gevrCoopPlayerTargetable(slot))
        && tacticalDistance(&guard->prop->pos, &target->pos) < SQR(range)
        && tacticalGuardFacing(guard, &target->pos, alert)
        && (tacticalGuardSightFromCover(guard, target)
            || tacticalGuardSight(guard, target, sightHeight, 0));
}

int gevrTacticalGuardVisiblePlayer(ChrRecord *guard, int player)
{
    if (!tacticalGuardAiAllowed() || !tacticalGuardOrdinary(guard)) return -1;
    if (player < 0 || player >= MAX_PLAYER_COUNT) return FALSE;
    const TacticalGuardMind *mind = tacticalGuardMind(guard);
    return tacticalGuardCanSpot(guard, player, mind && mind->expires > g_GlobalTimer);
}

/* A native AI list keeps its decisions and side effects. These queries only
 * upgrade the evidence at the three ordinary guard perception commands. */
int gevrTacticalGuardScriptSight(ChrRecord *guard)
{
    if (!tacticalGuardAiAllowed() || !tacticalGuardOrdinary(guard)) return -1;
    TacticalGuardMind *mind = tacticalGuardMind(guard);
    if (!mind) return -1;
    const int player = get_cur_playernum();
    if (netIsActive() && !gevrCoopPlayerTargetable(player)) return FALSE;
    if (mind->scriptPlayer != player || g_GlobalTimer >= mind->nextScriptSight) {
        const PropRecord *target = getCurrentPlayerProp();
        mind->scriptPlayer = player;
        mind->nextScriptSight = g_GlobalTimer + 5 + (guard->chrnum & 3);
        mind->scriptSight = tacticalGuardCanSpot(guard, player, mind->expires > g_GlobalTimer);
        if (mind->scriptSight) {
            if (s_traceGuardAi && mind->contact != CONTACT_SIGHT)
                sysLogPrintf(LOG_NOTE, "tactical AI: scripted guard %d sight", guard->chrnum);
            tacticalGuardNotice(mind, &target->pos, target->stan, CONTACT_SIGHT, 305);
            mind->clearUntil = mind->nextScriptSight;
            setSeenBondTimeToNow(guard);
            chrlvSetTargetToPlayer(guard);
        }
    }
    return mind->scriptSight;
}

int gevrTacticalGuardScriptHear(ChrRecord *guard, int recent)
{
    if (!tacticalGuardAiAllowed() || !tacticalGuardOrdinary(guard)) return -1;
    TacticalGuardMind *mind = tacticalGuardMind(guard);
    if (!mind) return -1;
    if (g_GlobalTimer >= mind->nextSense) {
        const s32 previous = mind->lastNoise;
        mind->nextSense = g_GlobalTimer + TACTICAL_SENSE_TICKS;
        tacticalGuardHear(guard, mind, FALSE);
        if (mind->lastNoise != previous) {
            guard->lastheartarget60 = mind->lastNoise;
            guard->lastknowntargetpos = mind->location;
            guard->targetTile = mind->floor;
        }
    }
    return mind->lastNoise >= 0
        && g_GlobalTimer - mind->lastNoise <= (recent ? CHRLV_RECENT_TIME_CHECK : TACTICAL_SENSE_TICKS + 3);
}

int gevrTacticalGuardScriptBody(ChrRecord *guard)
{
    if (!tacticalGuardAiAllowed() || !tacticalGuardOrdinary(guard)) return FALSE;
    TacticalGuardMind *mind = tacticalGuardMind(guard);
    if (!mind || mind->clearUntil > g_GlobalTimer) return FALSE;
    const int old = mind->corpse;
    tacticalGuardFindBody(guard, mind);
    if (mind->corpse == old) return FALSE;
    guard->chrseedie = mind->corpse >= 0 ? mind->corpse : CHR_FREE;
    return TRUE;
}

static bool tacticalGuardRoute(ChrRecord *guard, coord3d *point, StandTile *floor, SPEED speed)
{
    if (s_pathTick != g_GlobalTimer) {
        s_pathTick = g_GlobalTimer;
        s_pathsTried = 0;
    }
    if (!floor || s_pathsTried >= 4) return FALSE;
    s_pathsTried++;
    return plot_course_for_actor(guard, point, floor, speed);
}

static void tacticalGuardFaceContact(ChrRecord *guard, const TacticalGuardMind *mind)
{
    const coord3d *pos = &guard->prop->pos;
    if (SQR(mind->location.x - pos->x) + SQR(mind->location.z - pos->z) < SQR(40.0f)) return;
    const f32 angle = atan2f(mind->location.x - pos->x, mind->location.z - pos->z);
    check_set_actor_standing_still(guard, TARGET_COMPASS, (s32)(angle * 65536.0f / M_TAU_F) & 0xffff);
}

static void tacticalGuardOrder(TacticalGuardMind *mind, enum TacticalGuardOrder order)
{
    if (s_traceGuardAi && mind->order != order) {
        static const char *const names[] = {"hold", "search", "charge", "fire", "flank", "cover", "peek"};
        sysLogPrintf(LOG_NOTE, "tactical AI: guard %d order %s", mind->chrnum, names[order]);
    }
    mind->order = order;
}

#include "gevr_tactical_cover_ai.inc.h"

static void tacticalGuardResumePatrol(ChrRecord *guard, TacticalGuardMind *mind)
{
    struct patrol_path *path = gevrCampaignGuardRoute(guard);
    if (!path || guard->actiontype == ACT_PATROL || guard->actiontype == ACT_GOPOS) return;

    PadRecord *nearest = NULL;
    f32 distance = INFINITY;
    const PropRecord *prop = guard->prop;
    const f32 floor = stanGetPositionYValue(prop->stan, prop->pos.x, prop->pos.z);
    for (int step = 0; step < path->len; step++) {
        PadRecord *pad = chrlvPatrolPad(path, step);
        if (!pad->stan) continue;
        const f32 lateral = SQR(prop->pos.x - pad->pos.x) + SQR(prop->pos.z - pad->pos.z);
        if (lateral < SQR(90.0f) && pad->stan == prop->stan) {
            set_actor_on_path(guard, path);
            return;
        }
        const f32 d2 = lateral + SQR(floor - stanGetPositionYValue(pad->stan, pad->pos.x, pad->pos.z));
        if (d2 < distance) {
            nearest = pad;
            distance = d2;
        }
    }
    if (nearest) tacticalGuardRoute(guard, &nearest->pos, nearest->stan, SPEED_WALK);
    mind->nextOrder = g_GlobalTimer + 45;
}

static bool tacticalGuardThink(ChrRecord *guard)
{
    if (!tacticalGuardAiAllowed() || !tacticalGuardManaged(guard)) return FALSE;
    TacticalGuardMind *mind = tacticalGuardMind(guard);
    if (!mind) return FALSE;
    mind->active = TRUE;
    if (guard->actiontype == ACT_INIT || guard->actiontype == ACT_ARGH
        || guard->actiontype == ACT_PREARGH || guard->actiontype == ACT_SURRENDER) {
        tacticalGuardLeaveCover(guard, mind);
        return TRUE;
    }
    const s32 now = g_GlobalTimer;
    const int squad = mind->group;
    const enum TacticalGuardRole role = squad < 0
        ? ((u16)guard->chrnum * 7u + (u8)guard->headnum) % 5
        : squad < 100 ? mind->member
        : mind->member == 0 ? GUARD_SUPPORT : mind->member == 1 ? GUARD_CROUCH : GUARD_FLANK;

    if (now >= mind->nextSense) {
        mind->nextSense = now + TACTICAL_SENSE_TICKS;
        if (mind->expires <= now) mind->contact = CONTACT_NONE;
        const PropRecord *target = getCurrentPlayerProp();
        const bool alert = mind->contact != CONTACT_NONE;
        if (tacticalGuardCanSpot(guard, get_cur_playernum(), alert)) {
            if (now >= mind->nextCall) {
                mind->nextCall = now + 175 + (guard->chrnum % 47);
                gevrTacticalReinforcementRequest(guard, &target->pos, target->stan, FALSE);
            }
            tacticalGuardNotice(mind, &target->pos, target->stan, CONTACT_SIGHT, 305);
            mind->clearUntil = now + TACTICAL_SENSE_TICKS + 2;
            setSeenBondTimeToNow(guard);
            chrlvSetTargetToPlayer(guard);
        } else {
            mind->clearUntil = 0;
            if (mind->contact < CONTACT_TEAM || mind->contact == CONTACT_SIGHT) tacticalGuardHear(guard, mind, TRUE);
            tacticalGuardTeam(guard, mind, squad);
            if (mind->contact < CONTACT_COMBAT) tacticalGuardFindBody(guard, mind);
            if (mind->contact == CONTACT_NONE && chrIfNearMiss(guard)) {
                /* 0.16 noted the guard's own position, so the guard knelt
                 * and aimed at compass 0, usually away from the shooter. */
                const int shooter = get_cur_playernum();
                const TacticalGunNoise *shot = shooter >= 0 && shooter < MAX_PLAYER_COUNT
                    ? &s_gunNoises[shooter] : NULL;
                if (shot && shot->floor && now - shot->time <= 30)
                    tacticalGuardNotice(mind, &shot->location, shot->floor, CONTACT_IMPACT, 90);
                else if (target && target->stan)
                    tacticalGuardNotice(mind, &target->pos, target->stan, CONTACT_IMPACT, 90);
            }
        }
    }

    if (mind->contact != CONTACT_NONE && guard->actiontype == ACT_PATROL) {
        tacticalGuardFaceContact(guard, mind);
        mind->nextOrder = now;
        if (s_traceGuardAi) sysLogPrintf(LOG_NOTE, "tactical AI: guard %d broke patrol", guard->chrnum);
    }
    if (mind->contact == CONTACT_NONE) {
        tacticalGuardLeaveCover(guard, mind);
        if (now < mind->nextOrder) return TRUE;
        if (mind->order != ORDER_NONE) {
            if (guard->actiontype != ACT_PATROL) chrlvMergeKneelToStand(guard, 12.0f);
            tacticalGuardOrder(mind, ORDER_NONE);
        }
        tacticalGuardResumePatrol(guard, mind);
        return TRUE;
    }
    if (tacticalGuardHoldCover(guard, mind)) return TRUE;
    if (now < mind->nextOrder) return TRUE;

    /* Give a valid movement order time to complete. A fresh firing lane can
     * interrupt a search, but doesn't make all five guards abandon cover. */
    const bool clear = now < mind->clearUntil;
    const f32 evidenceDist = tacticalDistance(&guard->prop->pos, &mind->location);
    const f32 standOff = mind->contact >= CONTACT_TEAM ? 345.0f : 630.0f;
    if (guard->actiontype == ACT_GOPOS && mind->order != ORDER_NONE
        && (mind->order != ORDER_SEARCH || (!clear && evidenceDist > SQR(standOff)))) {
        mind->nextOrder = now + 10;
        return TRUE;
    }

    const bool wounded = guard->numarghs > 0 && guard->maxdamage > 0.0f
        && guard->damage >= guard->maxdamage * 0.50f;
    mind->nextOrder = now + 28 + ((u16)guard->chrnum % 12);
    if (gevrCampaignGuardIsMarksman(guard)) {
        /* Overwatch stays on its post. A lost firing line results in facing
         * the last contact, with no route or charge order. */
        if (clear) {
            if (guard->actiontype != ACT_ATTACK) actor_aim_at_actor(guard, TARGET_BOND, 0);
            tacticalGuardOrder(mind, ORDER_FIRE);
        } else {
            if (guard->actiontype == ACT_ATTACK) chrStopFiring(guard);
            tacticalGuardFaceContact(guard, mind);
            tacticalGuardOrder(mind, ORDER_NONE);
        }
        return TRUE;
    }
    if (now >= mind->nextCover && (role != GUARD_ASSAULT || wounded)
        && (clear || mind->contact >= CONTACT_BODY) && tacticalCoverProbeAvailable()) {
        mind->nextCover = now + 135 + ((u16)guard->chrnum % 63);
        if (tacticalGuardFindCover(guard, mind, wounded, role == GUARD_FLANK)) return TRUE;
    }
    if (clear) {
        if (guard->actiontype == ACT_ATTACK) return TRUE;
        mind->crouched = role != GUARD_ASSAULT;
        if (role == GUARD_ASSAULT) {
            if (guard->actiontype == ACT_ATTACKWALK || actor_runs_and_fires(guard)) {
                tacticalGuardOrder(mind, ORDER_CHARGE);
                return TRUE;
            }
            actor_aim_at_actor(guard, TARGET_BOND, 0);
        } else {
            actor_kneel_aim_at_actor(guard, TARGET_BOND, 0);
            if (role == GUARD_SUPPORT && guard->actiontype == ACT_ATTACK && guard->act_attack.unk37)
                guard->act_attack.unk34 = 7;
        }
        tacticalGuardOrder(mind, ORDER_FIRE);
        return TRUE;
    }

    if (guard->actiontype == ACT_ATTACKWALK || guard->actiontype == ACT_ATTACK) {
        chrStopFiring(guard);
        chrlvMergeKneelToStand(guard, 10.0f);
    }
    if (mind->contact >= CONTACT_TEAM && evidenceDist < SQR(2000.0f)) {
        if (role == GUARD_SUPPORT || role == GUARD_CROUCH) {
            tacticalGuardFaceContact(guard, mind);
            tacticalGuardOrder(mind, ORDER_NONE);
            return TRUE;
        }
    }
    if (mind->floor && evidenceDist > SQR(standOff) && mind->contact != CONTACT_IMPACT
        && tacticalGuardRoute(guard, &mind->location, mind->floor, SPEED_RUN)) {
        tacticalGuardOrder(mind, ORDER_SEARCH);
    } else {
        tacticalGuardFaceContact(guard, mind);
        if (role != GUARD_ASSAULT && guard->actiontype != ACT_KNEEL) check_if_able_to_then_kneel(guard);
        tacticalGuardOrder(mind, ORDER_NONE);
    }
    return TRUE;
}

static bool tacticalGuardCanFire(ChrRecord *guard)
{
    if (!tacticalGuardAiAllowed() || !tacticalGuardOrdinary(guard)) return TRUE;
    TacticalGuardMind *mind = tacticalGuardMind(guard);
    if (!mind) return TRUE;
    const bool managed = mind->active && tacticalGuardManaged(guard);
    if (!managed && guard->actiontype != ACT_ATTACKWALK && guard->actiontype != ACT_ATTACKROLL
        && !(guard->actiontype == ACT_ATTACK && (guard->act_attack.attacktype & TARGET_BOND))) return TRUE;
    if (managed && mind->coverFloor)
        return mind->peekPose && mind->peekUntil > g_GlobalTimer && bondviewGetVisibleToGuardsFlag();
    const PropRecord *target = getCurrentPlayerProp();
    const bool crouched = managed && guard->actiontype == ACT_ATTACK && mind->crouched;
    const bool clear = target && bondviewGetVisibleToGuardsFlag() && !g_CurrentPlayer->bonddead
        && (!netIsActive() || gevrCoopPlayerTargetable(get_cur_playernum()))
        && tacticalGuardFacing(guard, &target->pos, TRUE)
        && tacticalGuardSight(guard, target, guard->chrheight - (crouched ? 50.0f : 20.0f), CDTYPE_CHRS);
    if (s_traceGuardAi && !clear && g_GlobalTimer >= mind->nextBlockedTrace) {
        sysLogPrintf(LOG_NOTE, "tactical AI: guard %d held fire for a blocked lane", guard->chrnum);
        mind->nextBlockedTrace = g_GlobalTimer + 30;
    }
    return clear;
}
