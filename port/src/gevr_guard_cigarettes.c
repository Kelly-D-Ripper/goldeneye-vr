#include <stdbool.h>
#include <ultra64.h>
#include "gevr_guard_cigarettes.h"

#include <bondconstants.h>
#include <bondaicommands.h>
#include <boss.h>
#include <math.h>
#include <string.h>
#include "dyn.h"
#include "gevr_hollywood_fx.h"
#include "game/bondview.h"
#include "game/chr.h"
#include "game/lv.h"
#include "game/gevr_tactical_heavy_guards.h"
#include "game/matrixmath.h"
#include "game/model.h"
#include "game/player.h"
#include "game/prop.h"
#include "game/stan.h"
#include "net/net_core.h"
#include "net/net_coop.h"
#include "system.h"

enum { CIGARETTE_LENGTH = 9, CIGARETTE_FLIGHT_TICKS = 86 };
typedef struct GuardCigarette {
    const ChrRecord *guard;
    ModelNode *head;
    coord3d base, direction;
    int start, lastPose, puffAt;
    unsigned phaseOffset;
    bool ejected, inHand;
} GuardCigarette;
static GuardCigarette s_guards[512];
static size_t s_guardSlotsUsed;
static struct {
    coord3d base, direction, velocity;
    int born, puffAt;
    bool active;
} s_flying[12];
static unsigned s_nextFlight, s_seed;
static bool s_enabled, s_groups, s_trace;
static int s_previewGuard;
extern f32 D_800364CC;
extern s32 g_gevrExtraPass;

static void normalize(coord3d *v)
{
    const float len = sqrtf(v->x*v->x + v->y*v->y + v->z*v->z);
    if (len > 1e-6f)
        for (int axis = 0; axis < 3; ++axis) v->f[axis] /= len;
}

void gevrGuardCigarettesReset(void)
{
    memset(s_guards, 0, sizeof(s_guards));
    s_guardSlotsUsed = 0;
    memset(s_flying, 0, sizeof(s_flying));
    s_nextFlight = 0;
    s_seed = (unsigned)sysGetMicroseconds();
    s_enabled = sysArgCheck("--tactical-guard-smoking");
    const int stage = bossGetStageNum();
    s_groups = gevrCampaignExtraGuardCapacity(stage) > 0;
    s_trace = sysArgCheck("--input-script") && sysArgCheck("--tactical-sim");
    s_previewGuard = s_trace ? sysArgGetInt("--tactical-preview-smoker", -1) : -1;
}

static GuardCigarette *guardCigarette(const ChrRecord *guard)
{
    if (!guard || !g_ChrSlots) return NULL;
    const ptrdiff_t slot = guard - g_ChrSlots;
    return slot >= 0 && slot < (ptrdiff_t)ARRAYCOUNT(s_guards) ? &s_guards[slot] : NULL;
}

void gevrGuardCigarettesForget(const ChrRecord *guard)
{
    GuardCigarette *state = guardCigarette(guard);
    if (state && state->guard == guard) *state = (GuardCigarette){0};
}

static ModelNode *mouthHitbox(ModelNode *root)
{
    int visited = 0;
    for (ModelNode *n = root; n && visited++ < 512;) {
        if ((n->Opcode & 255) == MODELNODE_OPCODE_BBOX && n->Data->BoundingBox.ModelNumber == HIT_HEAD)
            return n;
        if (n->Child) {
            n = n->Child;
        } else {
            while (n && !n->Next) n = n->Parent;
            if (n) n = n->Next;
        }
    }
    return NULL;
}

static float ease(float progress)
{
    const float t = fmaxf(0.f, fminf(1.f, progress));
    return t*t*(3.f - 2.f*t);
}

float gevrGuardCigarettePose(ChrRecord *guard, float foregrip[3])
{
    if ((!s_enabled && !s_groups) || !guard || guard->chrnum < 0 || g_gevrExtraPass
        || guard->actiontype == ACT_DIE || guard->actiontype == ACT_DEAD) return 0.f;
    const bool grouped = s_groups && gevrCampaignGuardIsIdleGroupMember(guard);
    if (!grouped && (!s_enabled || netIsActive() || gevrCampaignGuardRoute(guard)
        || gevrCampaignGuardIsMarksman(guard))) return 0.f;
    unsigned roll = ((unsigned)guard->chrnum ^ (grouped ? 0u : s_seed)) * 0x9e3779b9u;
    roll ^= roll >> 16;
    const bool preview = s_previewGuard == guard->chrnum;
    if (!preview && (s_previewGuard >= 0 || (!grouped && roll % 100 >= 12))) return 0.f;

    GuardCigarette *state = guardCigarette(guard);
    if (!state || (guard->chrflags & CHRFLAG_HIDDEN) || gevrTacticalPermanentHelmet(guard->handle_positiondata_hat)) return 0.f;
    const bool lit = state->guard == guard;
    const bool quiet = gevrCoopPuppets() ? netCoopGuardIdle(guard) : guard->actiontype == ACT_STAND
        && !(guard->hidden & (CHRHIDDEN_FIRE_WEAPON_RIGHT | CHRHIDDEN_FIRE_WEAPON_LEFT));
    if (!lit && (!quiet || (guard->chrflags & (CHRFLAG_INVINCIBLE | CHRFLAG_COUNT_DEATH_AS_CIVILIAN)))) return 0.f;
    if (lit && state->ejected) return 0.f;

    Model *model = guard->model;
    ModelNode *head = lit ? state->head : mouthHitbox(model->obj->RootNode);
    if (!head) return 0.f;
    const struct bbox *box = &head->Data->BoundingBox.Bounds;
    const Mtxf *headLocal = modelFindNodeMtx(model, head, 0);
    Mtxf *viewToWorld = currentPlayerGetViewToWorldMtxf();
    if (!headLocal || !viewToWorld) return 0.f;
    Mtxf pose;
    matrix_4x4_multiply(viewToWorld, (Mtxf *)headLocal, &pose);
    coord3d mouth = {.f = {box->xmin + .35f*(box->xmax-box->xmin),
        box->ymin + .33f*(box->ymax-box->ymin), box->zmax - .08f*(box->zmax-box->zmin)}};
    mtx4TransformVecInPlace(&pose, &mouth);
    coord3d out, up, across;
    memcpy(across.f, pose.m[0], sizeof(across.f));
    memcpy(up.f, pose.m[1], sizeof(up.f));
    memcpy(out.f, pose.m[2], sizeof(out.f));
    normalize(&across); normalize(&up); normalize(&out);

    if (!lit) {
        const size_t used = (size_t)(state - s_guards) + 1;
        if (s_guardSlotsUsed < used) s_guardSlotsUsed = used;
        *state = (GuardCigarette){.guard=guard, .head=head, .start=g_GlobalTimer + (preview ? 20 : (int)(roll%200)),
            .lastPose=-1, .phaseOffset=roll%220};
        if (s_trace)
            sysLogPrintf(LOG_NOTE, "guard cigarette: guard %d lit", guard->chrnum);
    }

    if (!quiet) state->start = g_GlobalTimer + 170;
    const int phase = g_GlobalTimer < state->start ? -1
        : (g_GlobalTimer - state->start) % (520 + state->phaseOffset);
    const float reach = !foregrip || phase < 0 || phase >= 222 ? 0.f
        : ease(phase/33.f)*ease((222-phase)/34.f);
    const bool inHand = foregrip && phase >= 33 && phase < 188;
    const float lowered = inHand ? ease((phase-33)/38.f)*ease((188-phase)/40.f) : 0.f;
    if (s_trace && state->lastPose >= 0 && state->inHand != inHand)
        sysLogPrintf(LOG_NOTE, "guard cigarette: guard %d %s%s", guard->chrnum,
            inHand ? "in off hand" : "in mouth", quiet ? "" : " in combat");

    state->lastPose = g_GlobalTimer;
    state->inHand = inHand;
    state->base = mouth;
    for (int k = 0; k < 3; ++k) {
        const float down = -20.f*up.f[k] + 23.f*across.f[k] + 24.f*out.f[k];
        const float pinch = mouth.f[k] + lowered*down;
        const float wrist = pinch + 6.f*out.f[k] - 7.f*up.f[k] + 4.f*across.f[k];
        if (foregrip) foregrip[k] += reach*(wrist - foregrip[k]);
        if (inHand) state->base.f[k] = pinch;
        state->direction.f[k] = out.f[k] + .23f*across.f[k] - (.08f+.30f*lowered)*up.f[k];
    }
    normalize(&state->direction);
    return reach;
}

void gevrGuardCigaretteGrip(const ChrRecord *guard, const float target[3], const float wrist[3])
{
    GuardCigarette *state = guardCigarette(guard);
    if (!state || state->guard != guard || !state->inHand) return;
    /* Keep the cigarette with the solved hand if IK shortened the reach. */
    for (int k = 0; k < 3; ++k) state->base.f[k] += wrist[k] - target[k];
}

void gevrGuardCigaretteHit(const ChrRecord *guard, const coord3d *shot)
{
    if (!s_enabled && !s_groups) return;
    GuardCigarette *state = guardCigarette(guard);
    if (state && state->guard == guard && !state->ejected && state->lastPose >= 0) {
        const unsigned slot = s_nextFlight++ % ARRAYCOUNT(s_flying);
        s_flying[slot].active = true;
        s_flying[slot].base = state->base;
        s_flying[slot].direction = state->direction;
        s_flying[slot].velocity = *shot;
        normalize(&s_flying[slot].velocity);
        for (int k = 0; k < 3; ++k)
            s_flying[slot].velocity.f[k] = 97.f*s_flying[slot].velocity.f[k] + 62.f*state->direction.f[k];
        s_flying[slot].velocity.y += 53.f;
        s_flying[slot].born = s_flying[slot].puffAt = g_GlobalTimer;
        state->ejected = true;
        if (sysArgCheck("--input-script"))
            sysLogPrintf(LOG_NOTE, "guard cigarette: guard %d knocked loose", guard->chrnum);
    }
}

static Gfx *drawCigarette(Gfx *g, coord3d base, coord3d axis, int *begun)
{
    Mtxf *view = camGetWorldToScreenMtxf();
    mtx4TransformVecInPlace(view, &base);
    if (base.z > -3.f || base.z < -1500.f) return g;
    mtx4RotateVecInPlace(view, &axis);
    coord3d right = {.f = {axis.z, 0.f, -axis.x}};
    if (fabsf(right.x)+fabsf(right.z) < .02f) right = (coord3d){.f={1.f,0.f,0.f}};
    normalize(&right);
    const coord3d up = {.f = {axis.y*right.z-axis.z*right.y,
        axis.z*right.x-axis.x*right.z, axis.x*right.y-axis.y*right.x}};
    Mtxf m = {.m={{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,1}}};
    for (int k = 0; k < 3; ++k) {
        const float scale = D_800364CC*.05f;
        m.m[0][k] = right.f[k]*scale;
        m.m[1][k] = up.f[k]*scale;
        m.m[2][k] = axis.f[k]*scale;
        m.m[3][k] = base.f[k]*D_800364CC;
    }
    if (!*begun) {
        *begun = TRUE;
        gDPPipeSync(g++);
        gSPMatrix(g++, osVirtualToPhysical(currentPlayerGetProjectionMatrix()),
            G_MTX_PROJECTION | G_MTX_NOPUSH | G_MTX_LOAD);
        gSPClearGeometryMode(g++, G_CULL_BOTH | G_LIGHTING | G_FOG | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
        gSPSetGeometryMode(g++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);
        gSPTexture(g++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
        gDPSetCycleType(g++, G_CYC_1CYCLE);
        gDPSetCombineMode(g++, G_CC_SHADE, G_CC_SHADE);
        gDPSetRenderMode(g++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
    }
    Mtx *fixed = dynAllocateMatrix();
    guMtxF2L(m.m, fixed);
    gSPMatrix(g++, osVirtualToPhysical(fixed), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_PUSH);
    static const s16 ring[6][2] = {{10,0},{5,9},{-5,9},{-10,0},{-5,-9},{5,-9}};
    static const s16 ends[] = {0, 55, 166, CIGARETTE_LENGTH*20};
    static const u8 colours[3][3] = {{177,123,66},{219,216,204},{235,91,32}};
    for (int material = 0; material < 3; ++material) {
        Vtx *v = dynAllocateVertices(12);
        for (int j = 0; j < 12; ++j) {
            v[j] = (Vtx){0};
            v[j].v.ob[0] = ring[j%6][0];
            v[j].v.ob[1] = ring[j%6][1];
            v[j].v.ob[2] = ends[material+j/6];
            for (int k = 0; k < 3; ++k) v[j].v.cn[k] = colours[material][k] * (j%6 < 3 ? 1.f : .72f);
            v[j].v.cn[3] = 255;
        }
        gSPVertex(g++, osVirtualToPhysical(v), 12, 0);
        for (int j = 0; j < 6; ++j) {
            const int next = (j+1)%6;
            gSP2Triangles(g++, j, next, j+6, 0, next, next+6, j+6, 0);
        }
        if (material == 2) {
            gSP2Triangles(g++, 6,7,8,0, 6,8,9,0);
            gSP2Triangles(g++, 6,9,10,0, 6,10,11,0);
        }
    }
    gSPPopMatrix(g++, G_MTX_MODELVIEW);
    return g;
}

static void smokeTip(const coord3d *base, const coord3d *axis, int *puffAt)
{
    if (g_GlobalTimer < *puffAt) return;
    *puffAt = g_GlobalTimer + 10;
    const Mtxf *camera = currentPlayerGetViewToWorldMtxf();
    if (!camera) return;
    float distance2 = 0.f;
    for (int k = 0; k < 3; ++k) {
        const float d = base->f[k] - camera->m[3][k];
        distance2 += d*d;
    }
    if (distance2 > 1200.f*1200.f) return;
    coord3d tip;
    for (int k = 0; k < 3; ++k) tip.f[k] = base->f[k] + CIGARETTE_LENGTH*axis->f[k];
    gevrHollywoodCigaretteSmoke(&tip);
}

Gfx *gevrGuardCigarettesDraw(Gfx *g)
{
    if ((!s_enabled && !s_groups) || g_gevrExtraPass || (netIsActive() && !s_groups)) return g;
    int begun = FALSE;
    for (size_t i = 0; i < s_guardSlotsUsed; ++i) {
        GuardCigarette *state = &s_guards[i];
        if (!state->guard || state->ejected || state->lastPose != g_GlobalTimer
            || state->guard->actiontype == ACT_DIE || state->guard->actiontype == ACT_DEAD) continue;
        g = drawCigarette(g, state->base, state->direction, &begun);
        smokeTip(&state->base, &state->direction, &state->puffAt);
    }
    for (int i = 0; i < ARRAYCOUNT(s_flying); ++i) {
        if (!s_flying[i].active) continue;
        const int age = g_GlobalTimer - s_flying[i].born;
        if (age >= CIGARETTE_FLIGHT_TICKS) {
            s_flying[i].active = false;
            continue;
        }
        const float t = age/60.f;
        coord3d pos = s_flying[i].base, axis = s_flying[i].direction;
        for (int k = 0; k < 3; ++k) pos.f[k] += s_flying[i].velocity.f[k]*t;
        pos.y -= 365.f*t*t;
        float floor;
        coord3d probe = pos;
        probe.y = fmaxf(pos.y, s_flying[i].base.y + 30.f);
        if (stanFindTileBelowPos(&probe, NULL, &floor) && pos.y < floor+.6f) pos.y = floor+.6f;
        const float spin = t*15.f;
        axis.x *= cosf(spin);
        axis.z *= cosf(spin);
        axis.y += sinf(spin);
        normalize(&axis);
        g = drawCigarette(g, pos, axis, &begun);
        smokeTip(&pos, &axis, &s_flying[i].puffAt);
    }
    return g;
}
