/* Ordinary Tactical reinforcement requests use the same host-owned budget
 * online and offline. Scripted objective actors use the cartridge AI path. */
typedef struct GevrReinforcementRules {
    u8 activeMax, missionMax, waveSize;
    f32 secondsBetween;
    bool open;
} GevrReinforcementRules;

static const GevrReinforcementRules s_reinforcementRules[LEVELID_SURFACE2 + 1] = {
    [LEVELID_DAM]      = {6, 18, 2, 4.2f, FALSE},
    [LEVELID_FACILITY] = {5, 15, 2, 5.0f, FALSE},
    [LEVELID_RUNWAY]   = {8, 24, 4, 4.2f, TRUE},
    [LEVELID_SURFACE]  = {9, 27, 5, 4.5f, TRUE},
    [LEVELID_BUNKER1]  = {4, 12, 2, 5.7f, FALSE},
    [LEVELID_SILO]     = {5, 15, 2, 5.2f, FALSE},
    [LEVELID_FRIGATE]  = {3, 10, 1, 2.8f, FALSE},
    [LEVELID_SURFACE2] = {9, 27, 5, 4.5f, TRUE},
    [LEVELID_BUNKER2]  = {5, 15, 2, 5.5f, FALSE},
    [LEVELID_STATUE]   = {5, 14, 2, 5.2f, FALSE},
    [LEVELID_ARCHIVES] = {5, 15, 2, 5.2f, FALSE},
    [LEVELID_STREETS]  = {9, 27, 5, 4.5f, TRUE},
    [LEVELID_DEPOT]    = {7, 22, 4, 4.6f, TRUE},
    [LEVELID_TRAIN]    = {3,  9, 1, 3.0f, FALSE},
    [LEVELID_JUNGLE]   = {9, 25, 5, 4.6f, TRUE},
    [LEVELID_CONTROL]  = {4, 12, 2, 6.0f, FALSE},
    [LEVELID_CAVERNS]  = {4, 12, 2, 6.0f, FALSE},
    [LEVELID_CRADLE]   = {3,  9, 1, 3.0f, FALSE},
    [LEVELID_AZTEC]    = {4, 11, 2, 5.8f, FALSE},
    [LEVELID_EGYPT]    = {3,  8, 1, 4.7f, FALSE},
};

static const struct GevrReinforcementApproaches {
    s16 stage, anchor;
    u16 pads[7];
} s_reinforcementApproaches[] = {
    {LEVELID_DAM,       18, {97, 105, 100}},
    {LEVELID_RUNWAY,   110, {90, 148, 125}},
    {LEVELID_SURFACE,  107, {104, 102, 105}},
    {LEVELID_SURFACE,  182, {183, 180, 181, 194}},
    {LEVELID_SURFACE2, 185, {173, 172, 184}},
    {LEVELID_STREETS,  138, {139, 228, 140}},
    {LEVELID_DEPOT,     46, {207, 308, 204}},
    {LEVELID_DEPOT,     59, {64, 63, 244, 242, 243}},
    {LEVELID_JUNGLE,   351, {382, 379, 472, 376, 378, 491, 381}},
    {LEVELID_JUNGLE,   477, {246, 404, 478, 158, 419, 412}},
};

enum { REINFORCEMENT_WAVE_MAX = 5, REINFORCEMENT_PENDING_MAX = 40 };
typedef struct GevrReinforcementWave {
    coord3d caller, contact;
    StandTile *callerFloor, *contactFloor;
    s16 callerId, body;
    u16 used[REINFORCEMENT_WAVE_MAX];
    int sent, remaining;
    f32 nextMember, deadline, heading;
} GevrReinforcementWave;

static const GevrReinforcementRules s_reinforcementDefault = {4, 12, 2, 5.5f, FALSE};
static const GevrReinforcementRules *s_reinforcementRule = &s_reinforcementDefault;
static GevrReinforcementWave s_reinforcementWave;
static int s_reinforcementCount, s_reinforcementSequence, s_reinforcementActors;
static int s_reinforcementPadCount;
static f32 s_reinforcementAfter;
static bool s_reinforcementOption;

static bool gevrTacticalReinforcementsEnabled(void)
{
    return s_reinforcementOption && !gevrTacticalRangeActive() && gevrTacticalCampaignRules()
        && (!netIsActive() || gevrCoopHostGuards());
}

void gevrTacticalReinforcementsReset(s32 stage)
{
    s_reinforcementRule = (stage >= 0 && stage < (s32)ARRAYCOUNT(s_reinforcementRules)
            && s_reinforcementRules[stage].activeMax)
        ? &s_reinforcementRules[stage] : &s_reinforcementDefault;
    memset(&s_reinforcementWave, 0, sizeof(s_reinforcementWave));
    s_reinforcementCount = s_reinforcementSequence = s_reinforcementActors = 0;
    s_reinforcementPadCount = 0;
    s_reinforcementAfter = 0.0f;
    s_reinforcementOption = sysArgCheck("--tactical-physical") && sysArgCheck("--tactical-guards")
        && !gevrRomIsVariant();
}

static int gevrTacticalLivingReinforcements(void)
{
    int active = 0;
    for (int i = 0; i < g_NumChrSlots; ++i) {
        const ChrRecord *chr = &g_ChrSlots[i];
        active += chr->model && chr->tacticalReinforcement
            && !(chr->hidden & CHRHIDDEN_REMOVE) && chr->actiontype != ACT_DIE && chr->actiontype != ACT_DEAD;
    }
    return active;
}

static bool gevrTacticalReinforcementHasRoom(void)
{
    return !s_reinforcementWave.remaining && s_reinforcementCount < s_reinforcementRule->missionMax
        && lvlGetCurrentMultiPlayerSec() >= s_reinforcementAfter
        && gevrTacticalLivingReinforcements() < s_reinforcementRule->activeMax;
}

static f32 reinforcementDist(const coord3d *a, const coord3d *b)
{
    return SQR(a->x - b->x) + SQR(a->z - b->z) + SQR(a->y - b->y);
}

bool gevrTacticalReinforcementRequest(const ChrRecord *guard, const coord3d *point, StandTile *floor, s32 scripted)
{
    if (!gevrTacticalReinforcementsEnabled() || (!scripted && !s_reinforcementRule->open)
        || !guard || !guard->model || !guard->prop || !guard->prop->stan || !point || !floor
        || guard->tacticalReinforcement || (guard->hidden & CHRHIDDEN_REMOVE)
        || (guard->chrflags & CHRFLAG_HIDDEN) || guard->actiontype == ACT_DIE || guard->actiontype == ACT_DEAD
        || lvlGetCurrentMultiPlayerSec() < 8.0f || !gevrTacticalReinforcementHasRoom()) return FALSE;
    if (!s_reinforcementPadCount && g_CurrentSetup.pads) {
        while (g_CurrentSetup.pads[s_reinforcementPadCount].plink) s_reinforcementPadCount++;
    }
    if (!s_reinforcementPadCount) return FALSE;
    const bool fast = gevrFastReinforcements();
    /* The legacy stage statistic stops after 30 seconds without a pad button.
     * The live mission clock continues while a VR player only looks around. */
    const f32 now = lvlGetCurrentMultiPlayerSec();
    const int available = s_reinforcementRule->activeMax - gevrTacticalLivingReinforcements();
    const int budget = s_reinforcementRule->missionMax - s_reinforcementCount;
    const f32 heading = (f32)(s_reinforcementSequence++ % 8) * (M_PI_F / 4.0f)
        + atan2f(point->x - guard->prop->pos.x, point->z - guard->prop->pos.z);
    s_reinforcementWave = (GevrReinforcementWave) {
        .caller = guard->prop->pos, .callerFloor = guard->prop->stan,
        .contact = *point, .contactFloor = floor, .body = guard->bodynum, .callerId = guard->chrnum,
        .heading = heading, .remaining = MIN(s_reinforcementRule->waveSize, MIN(available, budget)),
        .nextMember = now + 0.45f, .deadline = now + 8.0f
    };
    s_reinforcementAfter = now + s_reinforcementRule->secondsBetween * (fast ? 0.72f : 1.0f);
    if (sysArgCheck("--tactical-ai-trace"))
        sysLogPrintf(LOG_NOTE, "tactical reinforcement: queued wave %d source %d size %d at %.2fs",
            s_reinforcementSequence, guard->chrnum, s_reinforcementWave.remaining, (double)now);
    return s_reinforcementWave.remaining > 0;
}

extern waypoint *chrlvStanPathRelated(coord3d *pos, StandTile *stan);
extern s32 waypointFindRoute(waypoint *from, waypoint *to, waypoint **route, s32 capacity);
extern s32 chrGetNumFree(void);

static int reinforcementFindPad(const GevrReinforcementWave *wave, bool protectStart)
{
    PadRecord *pads = g_CurrentSetup.pads;
    int candidates[REINFORCEMENT_PENDING_MAX];
    f32 scores[REINFORCEMENT_PENDING_MAX];
    int count = 0;
    const bool open = s_reinforcementRule->open;
    const int stage = bossGetStageNum();
    const f32 targetDist = open ? 2450.0f : 1500.0f;
    const f32 heading = wave->heading + wave->sent * (open ? 0.70f : 0.16f);
    const f32 aheadX = sinf(heading), aheadZ = cosf(heading);
    const f32 callerGround = stanGetPositionYValue(wave->callerFloor, wave->caller.x, wave->caller.z);

    const struct GevrReinforcementApproaches *preferred = NULL;
    f32 nearest = SQR(4200.0f);
    for (size_t i = 0; i < ARRAYCOUNT(s_reinforcementApproaches); i++) {
        const struct GevrReinforcementApproaches *entry = &s_reinforcementApproaches[i];
        if (entry->stage != stage || entry->anchor >= s_reinforcementPadCount) continue;
        const f32 distance = reinforcementDist(&pads[entry->anchor].pos, &wave->caller);
        if (distance < nearest) {
            nearest = distance;
            preferred = entry;
        }
    }

    for (int i = 1; i < s_reinforcementPadCount; i++) {
        const PadRecord *pad = &pads[i];
        if (!pad->stan) continue;
        const f32 dx = pad->pos.x - wave->caller.x, dz = pad->pos.z - wave->caller.z;
        const f32 d2 = SQR(dx) + SQR(dz);
        if (d2 < SQR(520.0f) || d2 > SQR(open ? 4400.0f : 2400.0f)) continue;
        const f32 padGround = stanGetPositionYValue(pad->stan, pad->pos.x, pad->pos.z);
        if (fabsf(padGround - callerGround) > (open ? 380.0f : 110.0f)) continue;
        bool blocked = FALSE;
        for (int j = 0; j < wave->sent; j++) {
            const coord3d *used = &pads[wave->used[j]].pos;
            if (SQR(pad->pos.x - used->x) + SQR(pad->pos.z - used->z) < SQR(125.0f)) {
                blocked = TRUE;
                break;
            }
        }
        if (blocked) continue;

        const f32 distance = sqrtf(d2);
        f32 score = 580.0f * (aheadX * dx + aheadZ * dz) / distance - fabsf(distance - targetDist);
        if (preferred) {
            for (size_t j = 0; j < ARRAYCOUNT(preferred->pads); ++j)
                if (preferred->pads[j] == i) score += 600.0f;
        }
        int insert = count;
        while (insert > 0 && score > scores[insert - 1]) insert--;
        if (insert >= REINFORCEMENT_PENDING_MAX) continue;
        if (count < REINFORCEMENT_PENDING_MAX) count++;
        for (int j = count - 1; j > insert; j--) {
            candidates[j] = candidates[j - 1];
            scores[j] = scores[j - 1];
        }
        candidates[insert] = i;
        scores[insert] = score;
    }

    coord3d to = wave->caller;
    waypoint *destination = chrlvStanPathRelated(&to, wave->callerFloor);
    for (int i = 0; destination && i < count; i++) {
        const PadRecord *pad = &pads[candidates[i]];
        if (!gevrCampaignReinforcementEntryClear(pad, protectStart)) continue;
        coord3d from = pad->pos;
        waypoint *entry = chrlvStanPathRelated(&from, pad->stan);
        waypoint *route[MAX_CHRWAYPOINTS];
        if (entry && (entry == destination || waypointFindRoute(entry, destination, route, ARRAYCOUNT(route)) >= 2))
            return candidates[i];
    }
    return 0;
}

static const PropRecord *reinforcementSourceWeapon(const ChrRecord *source)
{
    if (!source) return NULL;
    for (int hand = GUNRIGHT; hand <= GUNLEFT; hand++) {
        const PropRecord *weapon = source->weapons_held[hand];
        if (weapon && weapon->type == PROP_TYPE_WEAPON && weapon->weapon
            && gevrCampaignGunIsFirearm(weapon->weapon->weaponnum)) return weapon;
    }
    return NULL;
}

void gevrTacticalReinforcementsTick(void)
{
    GevrReinforcementWave *wave = &s_reinforcementWave;
    if (!wave->remaining || !gevrTacticalReinforcementsEnabled()) return;
    const f32 now = lvlGetCurrentMultiPlayerSec();
    if (now < wave->nextMember) return;
    if (s_reinforcementCount >= s_reinforcementRule->missionMax
        || gevrTacticalLivingReinforcements() >= s_reinforcementRule->activeMax || chrGetNumFree() < 3) {
        wave->remaining = 0;
        return;
    }
    if (now >= wave->deadline) {
        if (sysArgCheck("--tactical-ai-trace"))
            sysLogPrintf(LOG_NOTE, "tactical reinforcement: wave %d stopped after %d members, no cleared entry before deadline",
                         s_reinforcementSequence, wave->sent);
        s_reinforcementAfter = now + s_reinforcementRule->secondsBetween;
        wave->remaining = 0;
        return;
    }
    const int index = reinforcementFindPad(wave, now < 24.0f);
    AIRecord *ai = ailistFindById(GAILIST_STANDARD_GUARD);
    if (!ai) {
        wave->remaining = 0;
        return;
    }
    if (!index) {
        /* The caller can wait for an unseen entry or send a smaller wave. */
        wave->heading += M_PI_F / 3.0f;
        wave->nextMember = now + 0.85f;
        return;
    }

    wave->remaining--;
    wave->nextMember = now + 0.6f;
    const PadRecord *pad = &g_CurrentSetup.pads[index];
    const f32 angle = atan2f(wave->contact.x - pad->pos.x, wave->contact.z - pad->pos.z);
    const int id = 24000 + s_reinforcementActors++;
    PropRecord *prop = gevrSpawnCampaignGuard(wave->body, pad, angle, ai, id);
    if (!prop || !prop->chr) return;
    ChrRecord *guard = prop->chr;
    const ChrRecord *source = chrFindByLiteralId(wave->callerId);
    const PropRecord *sourceWeapon = reinforcementSourceWeapon(source);
    if (sourceWeapon) gevrCloneGuardWeapon(guard, sourceWeapon, 0);
    if (!guard->weapons_held[GUNRIGHT]) gevrCampaignFinishGuard(guard, TRUE);
    if (!guard->weapons_held[GUNRIGHT]) chrGiveWeapon(guard, PROP_CHRKALASH, ITEM_AK47, 0);
    const PropRecord *weapon = guard->weapons_held[GUNRIGHT];
    if (!weapon || !weapon->weapon || !gevrCampaignGunIsFirearm(weapon->weapon->weaponnum)) {
        guard->hidden |= CHRHIDDEN_REMOVE;
        guard->chrflags |= CHRFLAG_HIDDEN;
        return;
    }

    guard->tacticalReinforcement = TRUE;
    guard->visionrange = source ? source->visionrange : 100.0f;
    gevrTacticalGuardReport(guard, &wave->contact, wave->contactFloor);
    wave->used[wave->sent++] = (u16)index;
    s_reinforcementCount++;
    sysLogPrintf(LOG_NOTE, "tactical reinforcement: wave %d member %d source %d guard %d pad %d (room %d), live %d total %d at %.2fs",
        s_reinforcementSequence, wave->sent, wave->callerId, id, index, pad->stan->room,
        gevrTacticalLivingReinforcements(), s_reinforcementCount, (double)now);
}
