/* A guard plants its feet in cover. Only the upper body and muzzle are
 * exposed during a peek, and a shot must pass a second check at the muzzle. */
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
    static const f32 offsets[] = {46.0f, 70.0f, 94.0f};
    for (size_t i = 0; i < ARRAYCOUNT(offsets); i++) {
        for (int mirror = 0; mirror < 2; mirror++) {
            const int sign = mirror ? -preferredSide : preferredSide;
            const f32 offset = offsets[i] * sign / length;
            *point = (coord3d){.x = base->x + dz * offset, .z = base->z - dx * offset};
            *tile = floor;
            if (!tacticalPeekTile(base, tile, point) || fabsf(point->y - ground) > 32.0f) continue;
            if (!tacticalTrace(guard, base, floor, point, *tile, height - 18.0f, height - 18.0f, CDTYPE_CHRS)
                || !tacticalTrace(guard, base, floor, point, *tile, height + 8.0f, height + 8.0f, 0)) continue;
            if (!tacticalTrace(guard, point, *tile, &mind->location, mind->floor,
                    height, targetHeight, CDTYPE_CHRS)) continue;
            *side = sign;
            return TRUE;
        }
    }
    return FALSE;
}

static bool tacticalGuardSightFromCover(ChrRecord *guard, const PropRecord *target)
{
    if (!target || !target->stan) return FALSE;
    float offset[2], blend;
    const int pose = gevrGuardCoverPoseGet(guard, offset, &blend);
    if ((pose != GEVR_GUARD_COVER_LEAN_LEFT && pose != GEVR_GUARD_COVER_LEAN_RIGHT) || blend < .8f)
        return FALSE;
    const PropRecord *prop = guard->prop;
    coord3d peek = {.x = prop->pos.x + offset[0], .z = prop->pos.z + offset[1]};
    StandTile *tile = prop->stan;
    if (!tacticalPeekTile(&prop->pos, &tile, &peek)) return FALSE;
    const f32 height = tacticalCoverEye(guard);
    return tacticalTrace(guard, &peek, tile, &target->pos, target->stan,
        height, tacticalTargetHeight(target, height), 0);
}

static void tacticalGuardLeaveCover(ChrRecord *guard, TacticalGuardMind *mind)
{
    if (!mind->coverFloor) return;
    if (mind->peekPose) chrStopFiring(guard);
    mind->coverFloor = NULL;
    mind->peekPose = GEVR_GUARD_COVER_NONE;
    gevrGuardCoverPoseSet(guard, GEVR_GUARD_COVER_NONE, 0.0f, 0.0f);
}

static bool tacticalGuardFindCover(ChrRecord *guard, TacticalGuardMind *mind, bool retreat, bool flank)
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
    for (size_t i = 0; i < ARRAYCOUNT(distances); i++) {
        const f32 offset = distances[i] * preferredSide;
        const f32 depth = i == 0 ? 0.0f : retreat ? -220.0f : flank ? (i < 5 ? 80.0f : -130.0f) : i < 5 ? -55.0f : -100.0f;
        coord3d point = {.x = start.x + ux * depth + uz * offset, .y = start.y + 30.0f,
            .z = start.z + uz * depth - ux * offset};
        StandTile *floor = prop->stan;
        if (!tacticalPeekTile(&start, &floor, &point) || fabsf(point.y - guard->ground) > 58.0f) continue;
        if (i != 0) {
            const f32 stepLength = sqrtf(SQR(offset) + SQR(depth));
            coord3d step = {.x = (point.x - start.x) / stepLength, .z = (point.z - start.z) / stepLength};
            if (!chrlvIsPosClearOfObjectBounds(&point, floor)
                || !chrlvCall7F0B0E24WithChrWidthHeight(guard->prop, &point, &step)
                || !tacticalSightLine(guard, &start, prop->stan, &point, floor, 65.0f, CDTYPE_CHRS)) continue;
        }
        if (tacticalTrace(guard, &point, floor, &mind->location, mind->floor,
                eye - 8.0f, targetHeight, 0)) continue;
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
        if (i != 0) chrlvDirectRoute(guard, &point, floor, SPEED_RUN);
        else tacticalGuardFaceContact(guard, mind);
        tacticalGuardOrder(mind, ORDER_COVER);
        return TRUE;
    }
    return FALSE;
}

static bool tacticalCoverBlind(int pose)
{
    return pose == GEVR_GUARD_COVER_BLIND_LEFT || pose == GEVR_GUARD_COVER_BLIND_RIGHT;
}

static bool tacticalGuardHoldCover(ChrRecord *guard, TacticalGuardMind *mind)
{
    const s32 now = g_GlobalTimer;
    const PropRecord *prop = guard->prop;
    if (!mind->coverFloor) return FALSE;
    if (SQR(prop->pos.x - mind->cover.x) + SQR(prop->pos.z - mind->cover.z) > SQR(50.0f)) {
        if (guard->actiontype == ACT_GOPOS && now < mind->coverArriveBy) return TRUE;
        tacticalGuardLeaveCover(guard, mind);
        return FALSE;
    }
    const f32 eye = tacticalCoverEye(guard);
    if (now < mind->clearUntil && tacticalTrace(guard, &prop->pos, prop->stan, &mind->location,
            mind->floor, eye - 8.0f, tacticalEvidenceHeight(mind), 0)) {
        tacticalGuardLeaveCover(guard, mind);
        return FALSE;
    }

    if (mind->peekPose) {
        if (now < mind->peekUntil && guard->actiontype == ACT_ATTACK) return TRUE;
        chrStopFiring(guard);
        mind->peekPose = GEVR_GUARD_COVER_NONE;
        mind->nextPeek = now + 38 + ((u16)guard->chrnum % 25);
        tacticalGuardOrder(mind, ORDER_COVER);
    }
    if (guard->actiontype != ACT_KNEEL) {
        tacticalGuardFaceContact(guard, mind);
        check_if_able_to_then_kneel(guard);
    }
    gevrGuardCoverPoseSet(guard, GEVR_GUARD_COVER_DUCK, 0.0f, 0.0f);
    if (now < mind->nextPeek || !tacticalCoverProbeAvailable()) return TRUE;
    mind->nextPeek = now + 40 + ((u16)guard->chrnum % 13);

    coord3d peek;
    StandTile *tile;
    int side;
    if (!tacticalCoverPeekPoint(guard, mind, &prop->pos, prop->stan, mind->peekSide, &peek, &tile, &side))
        return TRUE;
    const PropRecord *target = getCurrentPlayerProp();
    const bool visible = target && target->stan && bondviewGetVisibleToGuardsFlag() && !g_CurrentPlayer->bonddead
        && (!netIsActive() || gevrCoopPlayerTargetable(get_cur_playernum()))
        && tacticalGuardFacing(guard, &target->pos, TRUE)
        && tacticalTrace(guard, &peek, tile, &target->pos, target->stan,
            eye, tacticalTargetHeight(target, eye), CDTYPE_CHRS);
    const bool suppress = !visible && mind->contact >= CONTACT_TEAM && now + 125 < mind->expires
        && tacticalDistance(&prop->pos, &mind->location) < SQR(2700.0f)
        && fabsf(mind->cover.y - stanGetPositionYValue(mind->floor, mind->location.x, mind->location.z)) < 75.0f;
    if (!visible && !suppress) return TRUE;
    const f32 angle = atan2f(mind->location.x - prop->pos.x, mind->location.z - prop->pos.z);
    const int compass = (s32)(angle * 65536.0f / M_TAU_F) & 0xffff;
    if (!actor_kneel_aim_at_actor(guard, visible ? TARGET_BOND : TARGET_COMPASS, visible ? 0 : compass)) return TRUE;
    mind->peekPose = visible ? (side < 0 ? GEVR_GUARD_COVER_LEAN_LEFT : GEVR_GUARD_COVER_LEAN_RIGHT)
        : (side < 0 ? GEVR_GUARD_COVER_BLIND_LEFT : GEVR_GUARD_COVER_BLIND_RIGHT);
    mind->peekSide = -side;
    mind->peekUntil = now + (visible ? 104 : 78);
    mind->crouched = TRUE;
    if (visible) {
        setSeenBondTimeToNow(guard);
        chrlvSetTargetToPlayer(guard);
    }
    gevrGuardCoverPoseSet(guard, mind->peekPose, peek.x - prop->pos.x, peek.z - prop->pos.z);
    tacticalGuardOrder(mind, ORDER_PEEK);
    if (s_traceGuardAi) sysLogPrintf(LOG_NOTE, "tactical AI: guard %d %s %s", guard->chrnum,
        visible ? "leaned" : "blind peek", side < 0 ? "left" : "right");
    return TRUE;
}

/* -1 uses the ordinary shot, 0 blocks it, and 1 supplies a checked muzzle. */
static int tacticalGuardCoverShot(ChrRecord *guard, s32 hand, coord3d *muzzle, StandTile **floor)
{
    if (!tacticalGuardAiAllowed() || !tacticalGuardOrdinary(guard)) return -1;
    TacticalGuardMind *mind = tacticalGuardMind(guard);
    if (!mind || !mind->coverFloor) return -1;
    if (!mind->peekPose || !gevrGuardCoverMuzzle(guard, hand, muzzle)) return 0;
    if (tacticalDistance(muzzle, &guard->prop->pos) > SQR(245.0f)) return 0;
    *floor = guard->prop->stan;
    if (!walkTilesBetweenPoints_NoCallback(floor, guard->prop->pos.x, guard->prop->pos.z, muzzle->x, muzzle->z)) return 0;
    const f32 height = muzzle->y - stanGetPositionYValue(*floor, muzzle->x, muzzle->z);
    if (height < 25.0f || height > guard->chrheight + 30.0f
        || !tacticalTrace(guard, &guard->prop->pos, guard->prop->stan, muzzle, *floor,
            tacticalCoverEye(guard) - 20.0f, height, CDTYPE_CHRS)) return 0;
    const bool blind = tacticalCoverBlind(mind->peekPose);
    bool clear;
    if (blind) {
        clear = tacticalTrace(guard, muzzle, *floor, &mind->location, mind->floor,
            height, tacticalEvidenceHeight(mind), CDTYPE_CHRS);
    } else {
        const PropRecord *target = getCurrentPlayerProp();
        clear = target && target->stan && !g_CurrentPlayer->bonddead
            && (!netIsActive() || gevrCoopPlayerTargetable(get_cur_playernum()))
            && tacticalTrace(guard, muzzle, *floor, &target->pos, target->stan,
                height, tacticalTargetHeight(target, height), CDTYPE_CHRS);
    }
    if (clear && s_traceGuardAi) sysLogPrintf(LOG_NOTE, "tactical AI: guard %d fired %s from cover", guard->chrnum,
        blind ? "blind" : "aimed");
    return clear;
}
