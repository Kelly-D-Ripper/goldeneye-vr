#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "bondtypes.h"
#include "bondconstants.h"
#include "bondaicommands.h"
#include "gevr_guard_cover.h"

/* PRODUCTION_TYPES */

#define LOG_NOTE 1
static struct player { s32 bonddead; } player, *g_CurrentPlayer = &player;
static StandTile lower, upper;
static ChrRecord guard;
static PropRecord rifle;
static PropRecord stand, target;
static TacticalGuardMind mind;
static s32 g_GlobalTimer;
static bool s_traceGuardAi;
static bool enabled, ordinary, online, targetable, facing, visibleToGuards;
static bool hasMuzzle, clearStep, clearWidth;
static f32 walkLimit, riseAt;
static int pose, aimType, aimValue, aimCount, steps, kneels, stops, spotted;
static f32 poseOffset[2], poseBlend, stepDirection[2];
static coord3d actualMuzzle;
static struct Wall { f32 z, left, right; int mask; } walls[3];
static size_t wallCount;

static void addWall(f32 z, f32 left, f32 right, int mask)
{
    assert(wallCount < ARRAYCOUNT(walls));
    walls[wallCount++] = (struct Wall){z, left, right, mask};
}

static StandTile *tileAt(f32 x)
{
    return fabsf(x) >= riseAt ? &upper : &lower;
}

static f32 stanGetPositionYValue(StandTile *tile, f32 x, f32 z)
{
    assert(tile == &lower || tile == &upper);
    return tile == &upper ? 80.0f : 0.0f;
}

static s32 walkTilesBetweenPoints_NoCallback(StandTile **tile, f32 x, f32 z, f32 toX, f32 toZ)
{
    if (fabsf(toX) > walkLimit) return FALSE;
    *tile = tileAt(toX);
    return TRUE;
}

static s32 stanTestLineUnobstructed(StandTile **tile, f32 x, f32 z, f32 toX, f32 toZ,
    s32 mask, f32 yTop, f32 yBottom, f32 toYTop, f32 toYBottom)
{
    assert(yTop > yBottom && toYTop > toYBottom);
    for (size_t i = 0; i < wallCount; i++) {
        const struct Wall *wall = &walls[i];
        if (!(wall->mask & mask) || z == toZ) continue;
        const f32 fraction = (wall->z - z) / (toZ - z);
        const f32 crossingX = x + (toX - x) * fraction;
        if (fraction > 0 && fraction < 1 && crossingX >= wall->left && crossingX <= wall->right)
            return FALSE;
    }
    *tile = tileAt(toX);
    return TRUE;
}

static void chrSetMoving(ChrRecord *chr, s32 flag) {}
static void chrStopFiring(ChrRecord *chr) { stops++; }
static void setSeenBondTimeToNow(ChrRecord *chr) { spotted++; }
static void chrlvSetTargetToPlayer(ChrRecord *chr) {}
static void sysLogPrintf(int level, const char *fmt, ...) {}
static bool netIsActive(void) { return online; }
static int gevrCoopPlayerTargetable(int slot) { return targetable; }
static int get_cur_playernum(void) { return 0; }
static int bondviewGetVisibleToGuardsFlag(void) { return visibleToGuards; }
PropRecord *getCurrentPlayerProp(void) { return &target; }
static bool tacticalGuardFacing(ChrRecord *chr, const coord3d *at, bool run) { return facing; }
static bool tacticalGuardAiAllowed(void) { return enabled; }
static bool tacticalGuardOrdinary(ChrRecord *chr) { return ordinary; }
static TacticalGuardMind *tacticalGuardMind(const ChrRecord *chr) { return &mind; }
static void tacticalGuardFaceContact(ChrRecord *chr, const TacticalGuardMind *state)
{
    kneels++; /* kneels and aims at the contact without firing */
    chr->actiontype = ACT_ATTACK;
}
static void tacticalGuardOrder(TacticalGuardMind *state, enum TacticalGuardOrder order) { state->order = order; }

static bool chrlvIsPosClearOfObjectBounds(coord3d *point, StandTile *tile) { return clearStep; }
static s32 chrlvCall7F0B0E24WithChrWidthHeight(PropRecord *prop, coord3d *point, coord3d *direction)
{
    stepDirection[0] = direction->x;
    stepDirection[1] = direction->z;
    return clearWidth;
}

static void chrlvDirectRoute(ChrRecord *chr, coord3d *point, StandTile *tile, SPEED speed)
{
    assert(speed == SPEED_RUN && tile == tileAt(point->x));
    steps++;
    chr->actiontype = ACT_GOPOS;
}

static bool check_if_able_to_then_kneel(ChrRecord *chr)
{
    kneels++;
    chr->actiontype = ACT_KNEEL;
    return TRUE;
}

static bool actor_kneel_aim_at_actor(ChrRecord *chr, s32 type, s32 id)
{
    aimType = type;
    aimValue = id;
    aimCount++;
    chr->actiontype = ACT_ATTACK;
    return TRUE;
}

void gevrGuardCoverPoseSet(ChrRecord *chr, int value, float x, float z)
{
    pose = value;
    poseOffset[0] = x;
    poseOffset[1] = z;
}

int gevrGuardCoverPoseGet(const ChrRecord *chr, float offset[2], float *blend)
{
    offset[0] = poseOffset[0];
    offset[1] = poseOffset[1];
    *blend = poseBlend;
    return pose;
}

int gevrGuardCoverMuzzle(const ChrRecord *chr, int hand, coord3d *muzzle)
{
    if (!hasMuzzle || hand != GUNRIGHT) return FALSE;
    *muzzle = actualMuzzle;
    return TRUE;
}

/* PRODUCTION_TRACE */
#include "gevr_tactical_cover_ai.inc.h"

static void reset(void)
{
    stand = (PropRecord){.stan = &lower};
    target = (PropRecord){.stan = &lower, .pos = {.y = 170.0f, .z = 1000.0f}, .type = PROP_TYPE_PLAYER};
    guard = (ChrRecord){.prop = &stand, .chrheight = 185.0f, .chrnum = 1, .actiontype = ACT_STAND};
    guard.weapons_held[GUNRIGHT] = &rifle;
    mind = (TacticalGuardMind){.location = target.pos, .floor = target.stan, .expires = 800, .contact = CONTACT_TEAM};
    player.bonddead = FALSE;
    enabled = ordinary = targetable = visibleToGuards = facing = hasMuzzle = clearStep = clearWidth = TRUE;
    online = FALSE;
    walkLimit = riseAt = 1e6f;
    poseBlend = 1.0f;
    pose = aimType = aimValue = aimCount = steps = kneels = stops = spotted = g_GlobalTimer = 0;
    wallCount = 0;
    actualMuzzle = (coord3d){.x = 46.0f, .y = 110.0f, .z = 24.0f};
}

static void geometry(void)
{
    reset();
    assert(!tacticalGuardFindCover(&guard, &mind, FALSE, FALSE, FALSE));
    assert(!mind.coverFloor && !steps); /* an exposed location cannot be cover */
    addWall(120.0f, -33.0f, 33.0f, CDTYPE_OBJS);
    coord3d point;
    StandTile *tile;
    int side;
    assert(tacticalCoverPeekPoint(&guard, &mind, &stand.pos, stand.stan, 1, &point, &tile, &side));
    assert(side == 1 && point.x == 46.0f && tile == &lower);
    assert(tacticalCoverPeekPoint(&guard, &mind, &stand.pos, stand.stan, -1, &point, &tile, &side));
    assert(side == -1 && point.x == -46.0f);
    addWall(120.0f, 0.0f, 150.0f, CDTYPE_CHRS);
    assert(tacticalCoverPeekPoint(&guard, &mind, &stand.pos, stand.stan, 1, &point, &tile, &side));
    assert(side == -1); /* the preferred side intersects another actor */
    riseAt = 40.0f;
    assert(!tacticalCoverPeekPoint(&guard, &mind, &stand.pos, stand.stan, 1, &point, &tile, &side));
    riseAt = 1e6f;
    walkLimit = 30.0f;
    assert(!tacticalCoverPeekPoint(&guard, &mind, &stand.pos, stand.stan, 1, &point, &tile, &side));

    reset();
    addWall(60.0f, 20.0f, 130.0f, CDTYPE_OBJS); /* a crate beside the spot: strict cover is within 1.4 m */
    clearStep = FALSE;
    assert(!tacticalGuardFindCover(&guard, &mind, FALSE, FALSE, FALSE));
    clearStep = TRUE;
    clearWidth = FALSE;
    assert(!tacticalGuardFindCover(&guard, &mind, FALSE, FALSE, FALSE));
    clearWidth = TRUE;
    assert(tacticalGuardFindCover(&guard, &mind, FALSE, FALSE, FALSE));
    assert(steps == 1 && mind.cover.x > 0.0f && mind.cover.z < 0.0f && mind.order == ORDER_COVER);
    assert(fabsf(stepDirection[0] * stepDirection[0] + stepDirection[1] * stepDirection[1] - 1.0f) < .001f);
    assert(tacticalGuardHoldCover(&guard, &mind) && !kneels); /* still en route */
    stand.pos = mind.cover;
    assert(tacticalGuardHoldCover(&guard, &mind) && pose == GEVR_GUARD_COVER_NONE && kneels == 1);

    reset();
    addWall(200.0f, -400.0f, 400.0f, CDTYPE_OBJS);
    assert(!tacticalGuardFindCover(&guard, &mind, FALSE, FALSE, FALSE)); /* hidden, but 2 m from the wall */
    reset();
    addWall(100.0f, -6.0f, 6.0f, CDTYPE_OBJS);
    assert(!tacticalGuardFindCover(&guard, &mind, FALSE, FALSE, FALSE)); /* a post is no cover */
}

static void plantedCover(void)
{
    addWall(120.0f, -33.0f, 33.0f, CDTYPE_OBJS);
    assert(tacticalGuardFindCover(&guard, &mind, FALSE, FALSE, FALSE));
    assert(!steps && mind.coverFloor == &lower);
    assert(tacticalGuardHoldCover(&guard, &mind));
    assert(pose == GEVR_GUARD_COVER_NONE && kneels == 1 && !aimCount);
    g_GlobalTimer = mind.nextPeek;
}

/* Down in cover he never shows himself to look: no leans, no blind fire. */
static void hidden(void)
{
    reset();
    plantedCover();
    mind.contact = CONTACT_TEAM;
    mind.expires = g_GlobalTimer + 300;
    assert(tacticalGuardHoldCover(&guard, &mind));
    assert(!aimCount && !spotted && pose == GEVR_GUARD_COVER_NONE);
}

/* A player who steps into plain view of the kneeling guard is shot at. */
static void inView(void)
{
    reset();
    plantedCover();
    target.pos.x = 400.0f; /* the line now passes the crate's edge */
    assert(tacticalGuardHoldCover(&guard, &mind));
    assert(aimCount == 1 && aimType == TARGET_BOND && spotted == 1);
    assert(pose == GEVR_GUARD_COVER_NONE && mind.order == ORDER_FIRE && mind.crouched);
    facing = FALSE; /* behind him: not seen */
    g_GlobalTimer = mind.peekUntil + 1;
    guard.actiontype = ACT_KNEEL;
    g_GlobalTimer = mind.nextPeek > g_GlobalTimer ? mind.nextPeek : g_GlobalTimer;
    assert(tacticalGuardHoldCover(&guard, &mind) && aimCount == 1);
}

/* He holds the spot a few seconds, then moves on. */
static void dwell(void)
{
    reset();
    plantedCover();
    g_GlobalTimer = mind.coverUntil;
    assert(!tacticalGuardHoldCover(&guard, &mind) && !mind.coverFloor && mind.nextCover > g_GlobalTimer);
}

static void shots(void)
{
    reset();
    coord3d muzzle;
    StandTile *tile = NULL;
    plantedCover();
    assert(tacticalGuardCoverShot(&guard, GUNRIGHT, &muzzle, &tile) == -1); /* always the ordinary shot */
}

int main(void)
{
    geometry();
    hidden();
    inView();
    dwell();
    shots();
    puts("PASS: Tactical cover geometry, strict solid cover, step and stance, hidden without leans or blind fire, fire in plain view, hold and move on");
    return 0;
}
