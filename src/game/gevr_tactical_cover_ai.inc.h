/* A guard plants its feet in cover. Only the upper body and muzzle are
 * exposed during a peek, and a shot must pass a second check at the muzzle.
 * The lean carries the hands and gun 72% of the pose offset (gevr_avatar.c
 * coverBendMatrices), so peek points are checked where the gun will be and
 * the pose is set to reach them. 0.16 checked the full offset, so the real
 * muzzle often stayed behind the corner and blind fire never happened. */
#define TACTICAL_COVER_REACH 0.72f
static bool tacticalCoverProbeAvailable(void)
{
    static s32 tick = -1, probes;
    if (tick != g_GlobalTimer) {
        tick = g_GlobalTimer;
        probes = 0;
    }
    if (probes >= 3) return FALSE;
    probes++;
    return TRUE;
}

static f32 tacticalCoverEye(const ChrRecord *guard)
{
    return fmaxf(70.0f, guard->chrheight - 48.0f);
}

static f32 tacticalEvidenceHeight(const TacticalGuardMind *mind)
{
    const f32 ground = stanGetPositionYValue(mind->floor, mind->location.x, mind->location.z);
    return fminf(150.0f, fmaxf(55.0f, mind->location.y - ground - 14.0f));
}

static bool tacticalPeekTile(const coord3d *base, StandTile **tile, coord3d *point)
{
    if (!*tile || ((base->x != point->x || base->z != point->z)
        && !walkTilesBetweenPoints_NoCallback(tile, base->x, base->z, point->x, point->z))) return FALSE;
    point->y = stanGetPositionYValue(*tile, point->x, point->z);
    return TRUE;
}

static bool tacticalCoverPeekPoint(ChrRecord *guard, const TacticalGuardMind *mind, const coord3d *base,
    StandTile *floor, int preferredSide, coord3d *point, StandTile **tile, int *side)
{
    const f32 dx = mind->location.x - base->x, dz = mind->location.z - base->z;
    const f32 length = sqrtf(SQR(dx) + SQR(dz));
    if (length < 150.0f) return FALSE;
    const f32 height = tacticalCoverEye(guard);
    const f32 ground = stanGetPositionYValue(floor, base->x, base->z);
    const f32 targetHeight = tacticalEvidenceHeight(mind);
    static const f32 offsets[] = {46.0f, 58.0f, 69.0f}; /* reach of 64-96 unit leans */
    for (size_t i = 0; i < ARRAYCOUNT(offsets); i++) {
        for (int mirror = 0; mirror < 2; mirror++) {
            const int sign = mirror ? -preferredSide : preferredSide;
            const f32 offset = offsets[i] * sign / length;
            *point = (coord3d){.x = base->x + dz * offset, .z = base->z - dx * offset};
            *tile = floor;
            if (!tacticalPeekTile(base, tile, point) || fabsf(point->y - ground) > 32.0f) continue;
            if (!tacticalTrace(guard, base, floor, point, *tile, height - 18.0f, height - 18.0f, CDTYPE_CHRS)
                || !tacticalTrace(guard, base, floor, point, *tile, height + 8.0f, height + 8.0f, 0)) continue;
            /* the gun rides a little below the eye: the lane must clear at both */
            if (!tacticalTrace(guard, point, *tile, &mind->location, mind->floor,
                    height, targetHeight, CDTYPE_CHRS)
                || !tacticalTrace(guard, point, *tile, &mind->location, mind->floor,
                    height - 14.0f, targetHeight, CDTYPE_CHRS)) continue;
            *side = sign;
            return TRUE;
        }
    }
    return FALSE;
}

/* Strict cover (user, 2026-11-23: "use crates, corners of hallways, doors
 * ... keep the rules strict on what they can use so they don't get caught
 * up on weird geometry"). The thing hiding the guard must stand within
 * 1.4 m of his spot, toward the threat, and block the line at kneeling eye
 * and chest height across his kneeling width (+/-14 units). A wall or corner
 * blocks the floor walk itself; a crate or door blocks the object test.
 * Thin trunks, posts and gaps don't qualify. */
static bool tacticalCoverSolid(ChrRecord *guard, const coord3d *point, StandTile *floor,
    const TacticalGuardMind *mind)
{
    const f32 dx = mind->location.x - point->x, dz = mind->location.z - point->z;
    const f32 length = sqrtf(SQR(dx) + SQR(dz));
    if (length < 1.0f) return FALSE;
    const f32 ux = dx / length, uz = dz / length;
    const f32 eye = tacticalCoverEye(guard);
    static const f32 laterals[] = {-14.0f, 0.0f, 14.0f};
    for (size_t i = 0; i < ARRAYCOUNT(laterals); i++) {
        coord3d side = {.x = point->x + uz * laterals[i], .z = point->z - ux * laterals[i]};
        StandTile *sideFloor = floor;
        if (!tacticalPeekTile(point, &sideFloor, &side)) continue; /* a wall at his shoulder */
        coord3d ahead = {.x = side.x + ux * 140.0f, .z = side.z + uz * 140.0f};
        StandTile *aheadFloor = sideFloor;
        if (!tacticalPeekTile(&side, &aheadFloor, &ahead)) continue; /* wall or edge in front */
        if (fabsf(ahead.y - side.y) > 40.0f) return FALSE; /* a step or drop, not cover */
        if (tacticalTrace(guard, &side, sideFloor, &ahead, aheadFloor, eye - 8.0f, eye - 8.0f, 0)
            || tacticalTrace(guard, &side, sideFloor, &ahead, aheadFloor, eye - 38.0f, eye - 38.0f, 0)) return FALSE;
    }
    return TRUE;
}

static void tacticalGuardLeaveCover(ChrRecord *guard, TacticalGuardMind *mind)
{
    if (!mind->coverFloor) return;
    if (mind->peekPose) chrStopFiring(guard);
    mind->coverFloor = NULL;
    mind->peekPose = GEVR_GUARD_COVER_NONE;
    gevrGuardCoverPoseSet(guard, GEVR_GUARD_COVER_NONE, 0.0f, 0.0f);
}

/* advance: a rifleman's bound to concealment 3.4-5.8 m nearer the contact,
 * so a squad closes in cover to cover instead of charging in the open. */
static bool tacticalGuardFindCover(ChrRecord *guard, TacticalGuardMind *mind, bool retreat, bool flank, bool advance)
{
    const PropRecord *prop = guard->prop;
    const coord3d start = prop->pos;
    const f32 dx = mind->location.x - start.x, dz = mind->location.z - start.z;
    const f32 length = sqrtf(SQR(dx) + SQR(dz));
    if (!mind->floor || length < 180.0f || length > 5300.0f) return FALSE;
    const f32 ux = dx / length, uz = dz / length;
    const int preferredSide = guard->chrnum & 1 ? 1 : -1;
    const f32 eye = tacticalCoverEye(guard), targetHeight = tacticalEvidenceHeight(mind);
    static const f32 distances[] = {0.0f, 55.0f, -55.0f, 110.0f, -110.0f, 220.0f, -220.0f, 345.0f, -345.0f, 0.0f};
    if (advance && length < 760.0f) return FALSE;
    for (size_t i = advance ? 1 : 0; i < ARRAYCOUNT(distances); i++) {
        const f32 offset = distances[i] * preferredSide;
        const f32 depth = i == 0 ? 0.0f : advance ? (i < 5 ? 340.0f : 470.0f)
            : retreat ? -220.0f : flank ? (i < 5 ? 80.0f : -130.0f) : i < 5 ? -55.0f : -100.0f;
        coord3d point = {.x = start.x + ux * depth + uz * offset, .y = start.y + 30.0f,
            .z = start.z + uz * depth - ux * offset};
        StandTile *floor = prop->stan;
        if (!tacticalPeekTile(&start, &floor, &point) || fabsf(point.y - guard->ground) > 58.0f) continue;
        if (advance && tacticalDistance(&point, &mind->location) < SQR(380.0f)) continue;
        if (i != 0) {
            const f32 stepLength = sqrtf(SQR(offset) + SQR(depth));
            coord3d step = {.x = (point.x - start.x) / stepLength, .z = (point.z - start.z) / stepLength};
            if (!chrlvIsPosClearOfObjectBounds(&point, floor)
                || !chrlvCall7F0B0E24WithChrWidthHeight(guard->prop, &point, &step)
                || !tacticalSightLine(guard, &start, prop->stan, &point, floor, 65.0f, CDTYPE_CHRS)) continue;
        }
        if (tacticalTrace(guard, &point, floor, &mind->location, mind->floor,
                eye - 8.0f, targetHeight, 0)) continue;
        if (!tacticalCoverSolid(guard, &point, floor, mind)) continue;
        coord3d peek;
        StandTile *peekFloor;
        int side;
        if (!retreat && !tacticalCoverPeekPoint(guard, mind, &point, floor, preferredSide, &peek, &peekFloor, &side))
            continue;
        mind->cover = point;
        mind->coverFloor = floor;
        mind->peekSide = preferredSide;
        mind->nextPeek = g_GlobalTimer + 28;
        mind->coverArriveBy = g_GlobalTimer + 165;
        mind->coverUntil = g_GlobalTimer + 165 + (advance ? 140 : retreat ? 300 : 230) + ((u16)guard->chrnum % 80);
        if (i != 0) chrlvDirectRoute(guard, &point, floor, SPEED_RUN);
        else tacticalGuardFaceContact(guard, mind);
        tacticalGuardOrder(mind, advance ? ORDER_ADVANCE : retreat ? ORDER_FALLBACK : ORDER_COVER);
        return TRUE;
    }
    return FALSE;
}

static bool tacticalGuardHoldCover(ChrRecord *guard, TacticalGuardMind *mind)
{
    const s32 now = g_GlobalTimer;
    const PropRecord *prop = guard->prop;
    if (!mind->coverFloor) return FALSE;
    if (SQR(prop->pos.x - mind->cover.x) + SQR(prop->pos.z - mind->cover.z) > SQR(70.0f)) {
        if (guard->actiontype == ACT_GOPOS && now < mind->coverArriveBy) return TRUE;
        /* Didn't get there: hold off before trying cover or a bound again,
         * so a blocked guard doesn't shuffle back and forth. */
        tacticalGuardLeaveCover(guard, mind);
        mind->nextCover = now + 300 + ((u16)guard->chrnum % 90);
        mind->nextAdvance = now + 240 + ((u16)guard->chrnum % 60);
        if (s_traceGuardAi) sysLogPrintf(LOG_NOTE, "tactical AI: guard %d gave up on cover", guard->chrnum);
        return FALSE;
    }
    const f32 eye = tacticalCoverEye(guard);
    if (now < mind->clearUntil && tacticalTrace(guard, &prop->pos, prop->stan, &mind->location,
            mind->floor, eye - 8.0f, tacticalEvidenceHeight(mind), 0)) {
        tacticalGuardLeaveCover(guard, mind);
        return FALSE;
    }

    /* 0.18.1 (user, 2026-11-23): no leans and no blind fire. A guard stays
     * down behind cover, fires only at a player he can see from where he
     * kneels, and moves on after a short hold. */
    mind->peekPose = GEVR_GUARD_COVER_NONE;
    gevrGuardCoverPoseSet(guard, GEVR_GUARD_COVER_NONE, 0.0f, 0.0f);
    if (guard->actiontype == ACT_ATTACK && now < mind->peekUntil) return TRUE;
    if (now >= mind->coverUntil) {
        tacticalGuardLeaveCover(guard, mind);
        mind->nextCover = now + 200 + ((u16)guard->chrnum % 60);
        return FALSE;
    }
    if (guard->actiontype != ACT_ATTACK) {
        /* down on one knee, weapon on the contact */
        mind->crouched = TRUE;
        tacticalGuardFaceContact(guard, mind);
    }
    if (now < mind->nextPeek || !tacticalCoverProbeAvailable()) return TRUE;
    mind->nextPeek = now + 30 + ((u16)guard->chrnum % 13);
    const PropRecord *target = getCurrentPlayerProp();
    const bool visible = target && target->stan && bondviewGetVisibleToGuardsFlag() && !g_CurrentPlayer->bonddead
        && (!netIsActive() || gevrCoopPlayerTargetable(get_cur_playernum()))
        && tacticalGuardFacing(guard, &target->pos, TRUE)
        && tacticalTrace(guard, &prop->pos, prop->stan, &target->pos, target->stan,
            eye, tacticalTargetHeight(target, eye), CDTYPE_CHRS);
    if (!visible || !actor_kneel_aim_at_actor(guard, TARGET_BOND, 0)) return TRUE;
    mind->peekUntil = now + 90;
    mind->crouched = TRUE;
    setSeenBondTimeToNow(guard);
    chrlvSetTargetToPlayer(guard);
    tacticalGuardOrder(mind, ORDER_FIRE);
    return TRUE;
}

/* Guards fire only their ordinary, aimed shots now. */
static int tacticalGuardCoverShot(ChrRecord *guard, s32 hand, coord3d *muzzle, StandTile **floor)
{
    (void)guard; (void)hand; (void)muzzle; (void)floor;
    return -1;
}
