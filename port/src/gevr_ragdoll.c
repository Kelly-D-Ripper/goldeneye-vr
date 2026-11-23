/* GoldenEye adapter for Perfect Dark CE's Jolt interface. Death bodies are
 * presentation snapshots, independent of actor lifetime and damage. */
#include "gevr_ragdoll_internal.h"
#include <string.h>
#include "model.h"
#include "chr.h"
#include "bondaicommands.h"
#include "player.h"
#include "bondview.h"
#include "lv.h"
#include "bg.h"
#include "dyn.h"
#include "stan.h"
#include "system.h"
#include "gevr_bean.h"
#include "gevr_avatar_math.h"
#include "gevr_hollywood_fx.h"
#include "gevr_tactical_loot_net.h"
#include "gevr_tactical_mesh.h"
#include "gevr_tactical_guard_ai.h"
#include "net_coop.h"
#include "chrobjdata.h"
#include "matrixmath.h"
#include "gun.h"
#include "bgfog.h"
#include "random.h"

extern int VrRagdolls, VrDismemberment, VrBodiesStay, VrDismemberChance;
extern int netHordeActive(void);

/* Bodies kept: Bodies stay, at most RD_BODIES; a Horde run at most a dozen
 * (user, 2026-10-18: "massive slowdowns as bodies pile up" after three
 * waves, 22 ragdolls down with Bodies stay at 48). */
#define RD_HORDE_BODIES 12
#define RD_TACTICAL_COOP_BODIES 12
static int rdEnabled(void) { return VrRagdolls || gevrTacticalLootEnabled(); }
static int rdDismember(void) { return VrDismemberment || gevrTacticalLootEnabled(); }

static int rdBodyLimit(void)
{
    if (gevrTacticalLootEnabled()) return RD_TACTICAL_COOP_BODIES;
    int limit=VrBodiesStay>0?VrBodiesStay:RD_BODIES;
    if(limit>RD_BODIES)limit=RD_BODIES;
    if(netHordeActive() && limit>RD_HORDE_BODIES)limit=RD_HORDE_BODIES;
    return limit;
}
extern float VrDismemberDamage;
extern ModelRenderData D_8002CCBC;
extern f32 chrobjFogVisRangeRelated(PropRecord *prop, f32 size); /* propobj.c */
extern f32 getinstsize(Model *model);                             /* model.c */
extern void lerp_rgba_s32_with_rgba_f32(rgba_s32 *dest, s32 enable, rgba_f32 *src); /* propobj.c */
extern s32 g_gevrExtraPass;
extern int gevrEnemyRigAttachRagdoll(ChrRecord *chr, unsigned serial, int *matrix);
extern void gevrEnemyRigRagdollPose(int rigId, unsigned serial, const Mtxf *world);
extern void gevrEnemyRigRagdollReleased(int rigId, unsigned serial, int restore);
extern Gfx *gevrEnemyRigDrawRagdoll(int rigId, unsigned serial, Model *model,
    const coord3d *origin, ModelRenderData *render, Gfx *g);

static RdBody s_bodies[RD_BODIES];

int gevrRagdollGuardBody(int slot, int *actorId, coord3d *point)
{
    if (slot < 0 || slot >= RD_BODIES) return -1;
    const RdBody *body = &s_bodies[slot];
    if (!body->used || body->guardActor < 0 || body->coopReplica || body->alpha < 35.0f) return 0;
    const int centre = body->body[1] >= 0 ? body->body[1] : body->root;
    memcpy(point->f, body->joints[centre].m[3], sizeof(point->f));
    *actorId = body->guardActor;
    return 1;
}
static RdMemory *s_garbage;
static unsigned s_frame, s_serial;
static int s_failed, s_warned, s_tacticalDeaths;

/* CE g_RdSkelHuman (ragdoll.c:205): an average adult after Winter / de Leva;
 * chest = upper + middle trunk, forearm + hand, shin + foot. */
const RdSkel g_RdSkelHuman = {
    { -1, 0, 1, 1, 1, 3, 4, 5, 6, 0, 0, 9, 10, 11, 12 },
    { 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0 },
    { -1, 2, -1, 5, 6, 7, 8, -1, -1, 11, 12, 13, 14, -1, -1 },
    { 0, 0, 0, 0, 0, -1, 1, 0, 0, 0, 0, 1, -1, 0, 0 },
    { 11.0f, 26.0f, 5.5f, 2.2f, 2.2f, 1.8f, 1.8f, 0, 0, 11.4f, 11.4f, 4.5f, 4.5f, 0, 0 },
    { 0, 0.30f, 0.17f, 0.15f, 0.15f, 0.12f, 0.12f, 0, 0, 0.19f, 0.19f, 0.16f, 0.16f, 0, 0 },
};

/* CE ragdoll.c:41, 44, 651-654, 12; rd_internal.h RD_KILLFLOOR_Y, RD_COLROOM_MOVE.
 * Damage needed and chance are CE's two settings (VrDismemberDamage/Chance). */
#define RD_DISMEMBER_DAMAGE VrDismemberDamage
#define RD_DISMEMBER_CHANCE VrDismemberChance
#define RD_SPLIT_DAMAGE     50.0f /* torso hits only split the waist this hard */
#define RD_PUSH_HIT   525.0f
#define RD_PUSH_ALL   180.0f
#define RD_PUSH_LIFT  60.0f
#define RD_PUSH_MAXMULT 1.45f
#define RD_PUSH_SCALE 0.8f
#define RD_KILLFLOOR_Y -32000.0f
#define RD_COLROOM_MOVE 12.0f
#define RD_FAR 2000.0f
#define RD_REACTION_SECONDS 8.0f
#define RD_COOP_POSE_INTERVAL 0.125f
#define RD_BLADE_POSE_GRACE 12 /* ticks at 60 Hz */

typedef struct RdSeen {
    ChrRecord *chr;
    Model *model;
    int suppressed, attempted, matrix, hitpart, hitTick, weapon, blastTick;
    int immediate, fatalHit, headBurst, stabbed;
    unsigned coopId, coopCuts;
    float dir[3], blast[3], blastOrigin[3], blastRadius, blastDamage;
} RdSeen;
static RdSeen s_seen[512];

typedef struct RdBladeHold {
    RdBody *body;
    ChrRecord *pending;
    int item, part;
    unsigned poseTick, hitTick;
    float base[3], axis[3], length, depth;
    float lastBase[3], anchor[3], target[3];
} RdBladeHold;
static RdBladeHold s_blades[2];

static void rdReleaseBlade(RdBladeHold *blade, const char *reason)
{
    if (blade->body) {
        pdphysRagdollReleasePoint(blade->body->handle);
        blade->body->bladeHeld = FALSE;
        blade->body->fadeEnd = fmaxf(blade->body->fadeEnd, blade->body->age + 2.5f);
        if (sysArgCheck("--input-script"))
            sysLogPrintf(LOG_NOTE, "ragdoll: body %u blade released (%s)", blade->body->serial, reason);
        blade->body = NULL;
    }
    blade->pending = NULL;
}

void gevrRagdollReleaseBlade(int controller)
{
    RdBladeHold *blade = &s_blades[controller];
    rdReleaseBlade(blade, "weapon released");
    blade->item = 0;
}

int gevrRagdollBladePose(int controller, int item, const float base[3], const float tip[3])
{
    if (!rdEnabled()) return FALSE;
    RdBladeHold *blade = &s_blades[controller];
    if (blade->item != item) gevrRagdollReleaseBlade(controller);
    float axis[3], length2 = 0.f;
    for (int k = 0; k < 3; ++k) {
        axis[k] = tip[k] - base[k];
        length2 += axis[k] * axis[k];
    }
    if (!(length2 > 1e-3f) || !isfinite(length2)) return blade->body || blade->pending;
    blade->length = sqrtf(length2);
    for (int k = 0; k < 3; ++k) {
        blade->axis[k] = axis[k] / blade->length;
        blade->base[k] = base[k];
    }
    blade->item = item;
    blade->poseTick = g_GlobalTimer;
    return blade->body || blade->pending;
}

/* Cuts are cosmetic and local: never draw from the game's own random
 * sequence, which co-op and replays depend on. */
static unsigned s_random = 0x9e3779b9u;
static int rdChance(float percent) { s_random = s_random * 1664525u + 1013904223u; return (float)((s_random >> 8) % 100) < percent; }

static float rdDamage(int weapon) { return weapon > 0 && weapon < ITEM_IDS_MAX ? gunItemGetDestructionAmount(weapon) : 0; }

/* CE ragdollGetPushSpeeds (ragdoll.c:656): damage 1 -> 1.0, 2 and above -> 2.0 */
static void rdPushSpeeds(int weapon, float *hit, float *all, float *lift)
{
    float weight = fminf(2.0f, fmaxf(0.5f, rdDamage(weapon)));
    float force = weapon > 0 && weapon < ITEM_IDS_MAX ? fmaxf(0, bondwalkItemGetForceOfImpact(weapon)) : 0;
    float mult = fminf(RD_PUSH_MAXMULT, 0.75f + 0.25f * weight + 0.025f * fminf(force, 10)) * RD_PUSH_SCALE;
    *hit = RD_PUSH_HIT * mult; *all = RD_PUSH_ALL * mult; *lift = RD_PUSH_LIFT * mult;
}

static int rdDead(ChrRecord *chr)
{
    if (!chr || !chr->prop || !chr->model) return 0;
    if (chr->prop->type == PROP_TYPE_VIEWER) {
        int p = getPlayerPointerIndex(chr->prop);
        return p >= 0 && p < getPlayerCount() && g_playerPointers[p] && g_playerPointers[p]->bonddead;
    }
    return chr->actiontype == ACT_DIE || chr->actiontype == ACT_DEAD || chr->actiontype == ACT_BONDDIE;
}

static RdSeen *rdSeen(ChrRecord *chr, int create)
{
    RdSeen *empty = NULL;
    for (int i=0;i<512;i++) {
        RdSeen *s=&s_seen[i];
        if (s->chr == chr) {
            if (s->model != chr->model) { memset(s,0,sizeof(*s)); s->chr=chr; s->model=chr->model; s->matrix=-1; }
            return s;
        }
        if (!s->chr && !empty) empty=s;
    }
    if (create && empty) { empty->chr=chr; empty->model=chr->model; empty->matrix=-1; }
    return create ? empty : NULL;
}

void *rdAlloc(RdBody *rd, size_t size)
{
    if (!size || size > 8*1024*1024) return NULL;
    RdMemory *m=calloc(1,sizeof(*m)+size);
    if (!m) return NULL;
    m->next=rd->memory;
    rd->memory=m;
    return m->bytes;
}

static void rdRelease(RdBody *rd, int retiringStage)
{
    for (int i = 0; i < 2; ++i)
        if (s_blades[i].body == rd) rdReleaseBlade(&s_blades[i], "body removed");
    if (rd->used && rd->handle >= 0) pdphysRagdollDestroy(rd->handle);
    if (rd->rig && (!retiringStage || !rdEnabled()))
        gevrEnemyRigRagdollReleased(rd->rig, rd->serial, !rdEnabled());
    gevrBloodyCombatForgetWound(&rd->model);
    gevrBeanForgetWounds(&rd->model);
    /* Display lists from an earlier view can still be in flight. The frame
     * arena uses double buffering too; keep private mesh storage for 3 frames. */
    while (rd->memory) {
        RdMemory *m=rd->memory;
        rd->memory=m->next;
        m->retired=s_frame;
        m->next=s_garbage;
        s_garbage=m;
    }
    memset(rd,0,sizeof(*rd));
}

void gevrRagdollReset(void)
{
    for (int i=0;i<RD_BODIES;i++) rdRelease(&s_bodies[i], TRUE);
    memset(s_blades,0,sizeof(s_blades));
    memset(s_seen,0,sizeof(s_seen));
    rdWorldReset();
    pdphysShutdown();
    gevrBeanSetCuts(NULL);
    s_failed=0;
    s_tacticalDeaths = gevrTacticalCampaignRules()
        && (sysArgCheck("--tactical-physical") || sysArgCheck("--tactical-bloody-combat"));
}

void gevrRagdollForgetChr(ChrRecord *chr)
{
    for (int i = 0; i < 2; ++i)
        if (s_blades[i].pending == chr) s_blades[i].pending = NULL;
    gevrBloodyCombatForgetWound(chr->model);
    RdSeen *s=rdSeen(chr,0);
    if (s) memset(s,0,sizeof(*s));
}

int gevrRagdollNeedsPose(ChrRecord *chr)
{
    if (!rdEnabled() || s_failed || !chr || !chr->model || !chr->model->obj) return 0;
    RdSeen *s=rdSeen(chr,1);
    if (!s) return 0;
    if (!rdDead(chr)) {
        s->suppressed = s->attempted = s->immediate = s->fatalHit = s->headBurst = s->stabbed = 0;
        s->coopId = s->coopCuts = 0;
        if (s_tacticalDeaths && chr->prop && chr->prop->type == PROP_TYPE_CHR) s->blastTick = 0;
        return 0;
    }
    if (!s->coopId && gevrCoopPuppets() && gevrCoopGuardGore(chr)) return 0;
    if (s_tacticalDeaths && chr->prop->type == PROP_TYPE_CHR
        && chr->actiontype == ACT_DIE && !s->immediate) return 0;
    return !s->attempted && !s->suppressed && !(chr->chrflags & CHRFLAG_HIDDEN)
        && chr->model->obj->Skeleton == &skeleton_guard;
}

int gevrRagdollSuppress(ChrRecord *chr)
{
    if (!rdEnabled() || !rdDead(chr)) return 0;
    RdSeen *s=rdSeen(chr,0);
    return s && s->suppressed;
}

/* A first-person player is deliberately left out of chrTick's visible models.
 * Capture its death before that early return, using fresh float matrices. */
void gevrRagdollCapturePlayer(ChrRecord *chr)
{
    if(!gevrRagdollNeedsPose(chr))return;
    ModelRenderData pose={0};
    pose.basemtx=camGetWorldToScreenMtxf();
    pose.mtxlist=dynAllocate(chr->model->obj->numMatrices*sizeof(Mtxf));
    if(!pose.mtxlist)return;
    subcalcmatrices(&pose,chr->model);
    gevrRagdollCapture(chr);
}

/* CE's shotgun volley (dismember.c:1478-1580): no pellet cuts by itself; a
 * whole volley on one limb of one body can. GoldenEye fires a volley as
 * NUMBER_SHOTGUN_BULLETS calls of the fire handler (chrprop.c). */
#define RD_VOLLEY_MAX 16
static struct {
    int active, pellets, hits, pellethit, parts[RD_VOLLEY_MAX];
    ChrRecord *chr; RdBody *body; int conflict;
    float dir[3];
} s_volley;

/* A volley that killed a living guard: decided once its body exists */
static struct { ChrRecord *chr; int hits, parts[RD_VOLLEY_MAX], tick; } s_pending;

static int rdIsShotgun(int weapon) { return weapon == ITEM_SHOTGUN || weapon == ITEM_AUTOSHOT; }

static void rdVolleyHit(ChrRecord *chr, RdBody *body, int skelpart, const float dir[3])
{
    if (!s_volley.active || s_volley.pellethit) return;
    s_volley.pellethit = 1;
    if (!s_volley.hits) { s_volley.chr = chr; s_volley.body = body; }
    else if (s_volley.chr != chr || s_volley.body != body) s_volley.conflict = 1;
    if (s_volley.hits < RD_VOLLEY_MAX) s_volley.parts[s_volley.hits] = skelpart;
    s_volley.hits++;
    memcpy(s_volley.dir, dir, sizeof(s_volley.dir));
}

void gevrRagdollNoteHit(ChrRecord *chr, int matrix, int hitpart, const float dir[3], int weapon)
{
    if (!rdEnabled() || !chr || !dir || gevrRagdollSuppress(chr)) return;
    if (s_tacticalDeaths && rdDead(chr) && chr->prop->type == PROP_TYPE_CHR) return;
    RdSeen *s=rdSeen(chr,1);
    if (!s) return;
    if(!rdDead(chr))s->suppressed=s->attempted=0;
    if(matrix<0 && s->matrix>=0 && s->hitpart==hitpart && s->hitTick==g_GlobalTimer)matrix=s->matrix;
    s->matrix=matrix; s->hitpart=hitpart; s->hitTick=g_GlobalTimer; s->weapon=weapon;
    memcpy(s->dir,dir,sizeof(s->dir));
    if (rdIsShotgun(weapon)) rdVolleyHit(chr, NULL, hitpart, dir); /* a hitpart until the body exists */
}

static int rdMilitaryGuard(const ChrRecord *chr)
{
    if (chr->chrflags & CHRFLAG_COUNT_DEATH_AS_CIVILIAN) return FALSE;
    switch (chr->bodynum) {
    case BODY_Jungle_Commando:
    case BODY_St_Petersburg_Guard:
    case BODY_Russian_Soldier:
    case BODY_Russian_Infantry:
    case BODY_Russian_Commandant:
    case BODY_Janus_Special_Forces:
    case BODY_Janus_Marine:
    case BODY_Siberian_Guard_1_Mishkin:
    case BODY_Siberian_Guard_2:
    case BODY_Siberian_Special_Forces:
    case BODY_Arctic_Commando:
    case BODY_Moonraker_Elite_1_Male:
    case BODY_Moonraker_Elite_2_Female:
        return TRUE;
    default:
        return FALSE;
    }
}

void gevrRagdollNoteShotDeath(ChrRecord *chr, int hitpart, int firearm)
{
    if (!s_tacticalDeaths || !rdEnabled() || chr->prop->type != PROP_TYPE_CHR) return;
    RdSeen *s = rdSeen(chr, 1);
    if (!s) return;
    s->blastTick = 0;
    s->fatalHit = firearm ? hitpart : HIT_NULL_PART;
    s->immediate = s->fatalHit == HIT_HEAD;
    if (s->immediate && rdDismember() && rdMilitaryGuard(chr) && gevrBloodyCombatEffects()
        && sysArgCheck("--tactical-head-bursts")) {
        const int force = sysArgCheck("--input-script") && sysArgCheck("--tactical-sim")
            && sysArgCheck("--tactical-test-head-burst");
        s->headBurst = force || rdChance(10.f);
    }
    if (sysArgCheck("--input-script"))
        sysLogPrintf(LOG_NOTE, "ragdoll: guard %d fatal hit part %d at tick %d, %s",
            chr->chrnum, hitpart, g_GlobalTimer, s->immediate ? "immediate physics" : "native death animation");
}

void gevrRagdollPrepareCoopCorpse(ChrRecord *chr, const GevrCoopCorpse *corpse)
{
    RdSeen *seen = rdSeen(chr, 1);
    if (!seen || seen->attempted || !rdDead(chr)) return;
    seen->coopId = corpse->id;
    seen->coopCuts = corpse->cuts;
    seen->matrix = -1;
    seen->hitpart = seen->fatalHit = corpse->part;
    seen->weapon = corpse->weapon;
    seen->hitTick = g_GlobalTimer;
    seen->immediate = corpse->immediate;
    seen->headBurst = corpse->headBurst;
    seen->stabbed = corpse->stabbed;
    memcpy(seen->dir, corpse->direction, sizeof(seen->dir));
}

int gevrBladeHitRegion(ChrRecord *chr, const float from[3], const float to[3], float reach, float contact[3])
{
    Model *model = chr->model;
    if (!model || !model->obj || !model->obj->RootNode) return HIT_NULL_PART;
    ModelRenderData pose = {.basemtx = camGetWorldToScreenMtxf()};
    pose.mtxlist = dynAllocate(model->obj->numMatrices * sizeof(Mtxf));
    if (!pose.mtxlist) return HIT_NULL_PART;
    RenderPosView *previous = model->render_pos;
    subcalcmatrices(&pose, model);
    int region = HIT_NULL_PART;
    float nearest = 1.0001f;
    int visited = 0;
    for (ModelNode *node = model->obj->RootNode; node && visited++ < 512; node = rdNext(model, node)) {
        if ((node->Opcode & 255) != MODELNODE_OPCODE_BBOX) continue;
        const ModelRoData_BoundingBoxRecord *part = &node->Data->BoundingBox;
        if (part->ModelNumber < HIT_LEFT_FOOT || part->ModelNumber > HIT_CHEST) continue;
        const Mtxf *m = modelFindNodeMtx(model, node, 0);
        if (!m) continue;
        const struct bbox *box = &part->Bounds;
        const float low[] = {box->xmin, box->ymin, box->zmin};
        const float high[] = {box->xmax, box->ymax, box->zmax};
        float entry = 0.f, exit = 1.f;
        for (int axis = 0; axis < 3 && entry <= exit; ++axis) {
            float start = 0.f, travel = 0.f, length2 = 0.f;
            for (int k = 0; k < 3; ++k) {
                const float basis = m->m[axis][k];
                start += (from[k] - m->m[3][k]) * basis;
                travel += (to[k] - from[k]) * basis;
                length2 += basis*basis;
            }
            if (!(length2 > 1e-8f)) { entry = 2.f; break; }
            const float margin = reach * sqrtf(length2);
            const float lo = low[axis] * length2 - margin, hi = high[axis] * length2 + margin;
            if (fabsf(travel) < 1e-6f) {
                if (start < lo || start > hi) entry = 2.f;
                continue;
            }
            const float a = (lo - start) / travel, b = (hi - start) / travel;
            entry = fmaxf(entry, fminf(a, b));
            exit = fminf(exit, fmaxf(a, b));
        }
        if (entry <= exit && entry < nearest) {
            nearest = entry;
            region = part->ModelNumber;
        }
    }
    model->render_pos = previous;
    if (region != HIT_NULL_PART)
        for (int k = 0; k < 3; ++k) contact[k] = from[k] + nearest * (to[k] - from[k]);
    return region;
}

static RdSeen *rdNoteBladeDeath(ChrRecord *chr, int weapon, int part, const float direction[3])
{
    if (!rdEnabled() || !rdDead(chr)) return NULL;
    RdSeen *seen = rdSeen(chr, 1);
    if (!seen || seen->suppressed) return NULL;
    seen->weapon = weapon;
    seen->matrix = -1;
    seen->hitpart = seen->fatalHit = part;
    seen->immediate = 1;
    seen->hitTick = g_GlobalTimer;
    seen->blastTick = 0;
    memcpy(seen->dir, direction, sizeof(seen->dir));
    return seen;
}

void gevrRagdollNoteKatanaDeath(ChrRecord *chr, int part, const float direction[3])
{
    rdNoteBladeDeath(chr, ITEM_TACTICAL_KATANA, part, direction);
}

void gevrRagdollNoteRemoteStab(ChrRecord *chr, int weapon, int part, const float direction[3])
{
    RdSeen *seen = rdNoteBladeDeath(chr, weapon, part, direction);
    if (seen) seen->stabbed = TRUE;
}

void gevrRagdollNoteBladeStab(ChrRecord *chr, int controller, int weapon, int part,
    const float direction[3], const float contact[3])
{
    if (chr->prop->type != PROP_TYPE_CHR) return;
    RdSeen *seen = rdNoteBladeDeath(chr, weapon, part, direction);
    if (!seen) return;
    seen->stabbed = TRUE;
    RdBladeHold *blade = &s_blades[controller];
    if (blade->body || blade->pending || blade->item != weapon
        || g_GlobalTimer - blade->poseTick > RD_BLADE_POSE_GRACE) return;
    float along = 0.f;
    for (int k = 0; k < 3; ++k) along += (contact[k] - blade->base[k]) * blade->axis[k];
    /* The first swept contact can catch the tip between samples. Give it
     * a short bite while keeping the anchor on the drawn blade. */
    const float bite = fminf(4.f, .25f * blade->length);
    blade->depth = fminf(blade->length, fmaxf(bite, blade->length - along));
    memcpy(blade->lastBase, blade->base, sizeof(blade->base));
    for (int k = 0; k < 3; ++k) blade->anchor[k] = contact[k];
    blade->pending = chr;
    blade->hitTick = g_GlobalTimer;
}

/* These relations live in shared definitions: relink for this instance before
 * following Child, especially HEAD (53f092b3/71b515e8). Never walk another head. */
ModelNode *rdNext(Model *model, ModelNode *node)
{
    int descend=1;
    switch (node->Opcode & 255) {
    case MODELNODE_OPCODE_HEAD: {
        ModelRwData_HeadPlaceholderRecord *h=&modelGetNodeRwData(model,node)->HeadPlaceholder;
        if (h->ModelFileHeader && h->RwDatas) modelApplyHeadRelations(model,node);
        else descend=0;
        break;
    }
    case MODELNODE_OPCODE_LOD: modelApplyDistanceRelations(model,node); break;
    case MODELNODE_OPCODE_SWITCH: modelApplyToggleRelations(model,node); break;
    case MODELNODE_OPCODE_BSP: modelApplyReorderRelations(model,node); break;
    }
    if (descend && node->Child) return node->Child;
    while (node) { if (node->Next) return node->Next; node=node->Parent; }
    return NULL;
}

static int rdParent(RdBody *rd, ModelNode *node)
{
    for (;node;node=node->Parent)
        for (int i=0;i<rd->count;i++) if (rd->nodes[i]==node) return i;
    return -1;
}

static int rdClone(RdBody *rd, ChrRecord *chr)
{
    Model *src=chr->model;
    int words=src->rwdatalen;
    if (words <= 0 || words>4096 || src->obj->numMatrices<=0 || src->obj->numMatrices>RD_MATRICES) return 0;
    rd->model=*src;
    rd->model.chr=NULL;
    rd->model.attachedto=NULL;
    rd->model.attachedto_objinst=NULL;
    rd->model.datas=rdAlloc(rd,(size_t)words*4);
    if (!rd->model.datas) return 0;
    memcpy(rd->model.datas,src->datas,(size_t)words*4);
    rd->matrices=src->obj->numMatrices;
    rd->origin=chr->prop->pos;
    rd->guardActor = gevrTacticalGuardCorpseEligible(chr) ? chr->chrnum : -1;
    rd->shade=chr->shadecol;
    memset(rd->rooms,255,sizeof(rd->rooms));
    for (int i=0;i<sizeof(chr->prop->rooms) && i<15 && chr->prop->rooms[i]!=255;i++) rd->rooms[i]=chr->prop->rooms[i];
    int visits=0, skelMatrix[RD_SKEL];
    for (int i=0;i<RD_SKEL;i++) skelMatrix[i]=rd->body[i]=-1;
    for (int i=0;i<20;i++) rd->hitskel[i]=-1;
    for (ModelNode *n=src->obj->RootNode;n;n=rdNext(&rd->model,n)) {
        if (++visits>RD_NODES) return 0;
        if ((n->Opcode&255)==MODELNODE_OPCODE_HEAD) {
            ModelRwData_HeadPlaceholderRecord *h=&modelGetNodeRwData(&rd->model,n)->HeadPlaceholder;
            if (h->RwDatas) {
                uintptr_t base=(uintptr_t)src->datas, ptr=(uintptr_t)h->RwDatas;
                if (ptr<base || ptr-base >= (size_t)words*4 || !h->ModelFileHeader
                    || (size_t)h->ModelFileHeader->numRecords*4 > (size_t)words*4-(ptr-base)) return 0;
                h->RwDatas=(u8 *)rd->model.datas+(ptr-base);
            }
        }
        /* CE ragdollCapturePose (create.c:17): skeleton parts by joint
         * number; hands and feet have no body and ride their parent's. */
        if ((n->Opcode&255)==MODELNODE_OPCODE_GROUP) {
            int part=n->Data->Group.JointID-1;
            if (part<0 || part>=RD_SKEL || skelMatrix[part]>=0) continue;
            skelMatrix[part]=n->Data->Group.MatrixID0;
            if (skelMatrix[part]<0 || skelMatrix[part]>=rd->matrices) return 0;
            if (!g_RdSkelHuman.hasbody[part]) continue;
            if (rd->count==RD_PARTS) return 0;
            int i=rd->count++;
            rd->nodes[i]=n;
            rd->primary[i]=skelMatrix[part];
            rd->joint[i]=part+1;
            rd->skel[i]=part;
            rd->body[part]=i;
            rd->parent[i]=rdParent(rd,n->Parent);
            if (rd->parent[i]<0) { if(i) return 0; rd->root=i; }
        }
        /* BBOX ModelNumber is the hitpart (chr.c chrTestHit) */
        if ((n->Opcode&255)==MODELNODE_OPCODE_BBOX && n->Data) {
            int hitpart=n->Data->BoundingBox.ModelNumber;
            for (ModelNode *a=n->Parent;a && hitpart>=0 && hitpart<20;a=a->Parent)
                if ((a->Opcode&255)==MODELNODE_OPCODE_GROUP && a->Data->Group.JointID>=1 && a->Data->Group.JointID<=RD_SKEL) {
                    rd->hitskel[hitpart]=a->Data->Group.JointID-1;
                    if (rd->nbbox<20) {
                        rd->bbox[rd->nbbox]=n; rd->bboxHit[rd->nbbox]=hitpart;
                        rd->bboxSkel[rd->nbbox++]=a->Data->Group.JointID-1;
                    }
                    break;
                }
        }
    }
    /* The same layout check as CE's: any other rig keeps its death animation */
    int ok=rd->count==11 && rd->root==0 && rd->skel[0]==0;
    for (int part=0;ok && part<RD_SKEL;part++) {
        ok=skelMatrix[part]>=0;
        if (ok && g_RdSkelHuman.hasbody[part] && part)
            ok=rd->body[part]>=0 && rd->parent[rd->body[part]]==rd->body[g_RdSkelHuman.parent[part]];
    }
    if (!ok) {
        if (!s_warned++) sysLogPrintf(LOG_WARNING,"ragdoll: chr %d has an unexpected skeleton layout, using its death animation",chr->chrnum);
        return 0;
    }
    for(int i=0;i<rd->matrices;i++) {
        rdMul(&src->render_pos[i].pos,currentPlayerGetViewToWorldMtxf(),&rd->bind[i]);
        for(int k=0;k<16;k++) if(!isfinite(((float *)&rd->bind[i])[k])) return 0;
        rd->owner[i]=rd->root;
    }
    visits=0;
    for(ModelNode *n=rd->model.obj->RootNode;n;n=rdNext(&rd->model,n)) {
        if(++visits>RD_NODES) return 0;
        int p=rdParent(rd,n);
        if(p<0)p=rd->root;
        int op=n->Opcode&255;
        if(op==MODELNODE_OPCODE_GROUP) {
            for(int k=0;k<3;k++) { int m=n->Data->Group.MatrixIDs[k]; if(m>=0 && m<rd->matrices)rd->owner[m]=p; }
        } else if(op==MODELNODE_OPCODE_GROUPSIMPLE || op==MODELNODE_OPCODE_HEADER) {
            int m=modelFindNodeMtxIndex(n,0); if(m>=0 && m<rd->matrices)rd->owner[m]=p;
        }
    }
    /* Normalized joint frames for Jolt; retain all model scaling and half-bend
     * matrices in offsets so native, HD and Bean all use the same pose. */
    for(int part=0;part<RD_SKEL;part++) {
        Mtxf m=rd->bind[skelMatrix[part]];
        for(int r=0;r<3;r++) {
            float l=sqrtf(m.m[r][0]*m.m[r][0]+m.m[r][1]*m.m[r][1]+m.m[r][2]*m.m[r][2]);
            if(!isfinite(l) || l<1e-6f)return 0;
            for(int k=0;k<3;k++)m.m[r][k]/=l;
        }
        m.m[0][3]=m.m[1][3]=m.m[2][3]=0; m.m[3][3]=1;
        rd->skelInitial[part]=m;
        if(rd->body[part]>=0)rd->initial[rd->body[part]]=rd->joints[rd->body[part]]=m;
    }
    for(int i=0;i<rd->matrices;i++) {
        Mtxf inv;
        if(!rdInverse(&rd->initial[rd->owner[i]],&inv))return 0;
        rdMul(&rd->bind[i],&inv,&rd->offset[i]);
    }
    return rdCopyMeshes(rd);
}

/* CE rdHitpartPart (rd_internal.h:683): the skeleton part with a physics
 * body behind a hitpart (hands -> forearms, feet -> shins), or -1 */
static int rdSkelForHit(const RdBody *rd, int hitpart)
{
    int sp=hitpart>=0 && hitpart<20 ? rd->hitskel[hitpart] : -1;
    while(sp>0 && rd->body[sp]<0)sp=g_RdSkelHuman.parent[sp];
    return sp;
}

/* The body part a recent hit landed on: the hit matrix when the shot found
 * one (BulletHit.room carries the matrix index for model hits), else its hitpart */
static int rdHitPart(RdBody *rd, RdSeen *s)
{
    if (!s || g_GlobalTimer-s->hitTick>120) return -1;
    if(s->matrix>=0 && s->matrix<rd->matrices)return rd->owner[s->matrix];
    int sp=rdSkelForHit(rd,s->hitpart);
    return sp>=0 ? rd->body[sp] : -1;
}

static int rdPartConnected(const RdBody *body, int part)
{
    if (part < 0) return FALSE;
    for (; part >= 0; part = body->parent[part])
        if (body->cuts & (1u << part)) return FALSE;
    return TRUE;
}

static void rdAttachBlade(ChrRecord *chr, RdBody *body, int part)
{
    for (int i = 0; i < 2; ++i) {
        RdBladeHold *blade = &s_blades[i];
        if (blade->pending != chr) continue;
        blade->pending = NULL;
        if (part < 0 || g_GlobalTimer - blade->poseTick > RD_BLADE_POSE_GRACE) return;
        Mtxf inverse;
        if (!rdInverse(&body->joints[part], &inverse)
            || !pdphysRagdollAttachPoint(body->handle, part, blade->anchor)) return;
        memcpy(blade->target, blade->anchor, sizeof(blade->target));
        rdTransform(&inverse, blade->anchor, TRUE);
        blade->body = body;
        blade->part = part;
        body->bladeHeld = TRUE;
        if (sysArgCheck("--input-script"))
            sysLogPrintf(LOG_NOTE, "ragdoll: guard %d body %u held on blade %d, depth %.1f at tick %d",
                chr->chrnum, body->serial, blade->item, blade->depth, g_GlobalTimer);
        return;
    }
}

static void rdUpdateBlades(float dt)
{
    for (int i = 0; i < 2; ++i) {
        RdBladeHold *blade = &s_blades[i];
        if (!blade->pending && !blade->body) continue;
        if (g_GlobalTimer - blade->poseTick > RD_BLADE_POSE_GRACE) {
            rdReleaseBlade(blade, "tracking lost");
            continue;
        }
        RdBody *body = blade->body;
        if (!body) {
            if (g_GlobalTimer - blade->hitTick > 30) blade->pending = NULL;
            continue;
        }
        if (!rdPartConnected(body, blade->part)) {
            rdReleaseBlade(blade, "body separated");
            continue;
        }

        /* Let the blade slide lengthwise through the contact. The body
         * follows sideways motion until the tip goes back past the wound. */
        float travel = 0.f;
        for (int k = 0; k < 3; ++k)
            travel += (blade->base[k] - blade->lastBase[k]) * blade->axis[k];
        blade->depth = fminf(blade->length, blade->depth + travel);
        memcpy(blade->lastBase, blade->base, sizeof(blade->base));
        if (blade->depth <= 0.f) {
            rdReleaseBlade(blade, "withdrawn");
            continue;
        }

        float point[3], target[3], velocity[3], error2 = 0.f, speed2 = 0.f;
        memcpy(point, blade->anchor, sizeof(point));
        rdTransform(&body->joints[blade->part], point, TRUE);
        for (int k = 0; k < 3; ++k) {
            target[k] = blade->base[k] + (blade->length - blade->depth) * blade->axis[k];
            const float error = target[k] - point[k];
            error2 += error * error;
            velocity[k] = (target[k] - blade->target[k]) / dt;
            speed2 += velocity[k] * velocity[k];
        }
        const float slack = fmaxf(65.f, 1.65f * body->torso);
        if (error2 > slack * slack) {
            rdReleaseBlade(blade, "out of reach");
            continue;
        }
        const float speed = sqrtf(speed2);
        if (speed > 520.f)
            for (int k = 0; k < 3; ++k) velocity[k] *= 520.f / speed;
        memcpy(blade->target, target, sizeof(target));
        pdphysRagdollMovePoint(body->handle, target, velocity);
        body->asleep = FALSE;
    }
}

/* CE ragdollCutPartForHit (dismember.c:1268) and the weapon gate of
 * ragdollTryDismemberShot (:1375): strong enough weapons, a chance, no
 * punches; the shotgun cuts only by whole volleys. Returns a body part. */
static int rdCutForHit(const RdBody *rd, int part, int weapon)
{
    if (weapon == ITEM_TACTICAL_KATANA && part >= 0 && part < rd->count)
        return rd->skel[part] <= 1 ? rd->body[1] : part;
    if (s_tacticalDeaths) return -1;
    float damage=rdDamage(weapon);
    if(!rdDismember() || part<0 || part>=rd->count || weapon==ITEM_FIST || rdIsShotgun(weapon)
        || damage<RD_DISMEMBER_DAMAGE) return -1;
    if (!rdChance(RD_DISMEMBER_CHANCE)) return -1;
    if(rd->skel[part]<=1)return damage>=RD_SPLIT_DAMAGE ? rd->body[1] : -1; /* the waist */
    return part;
}

/* CE ragdollShotgunVolleyEnd's limb rule over skeleton parts: every pellet
 * on one limb, the cut nearest the body. */
static int rdVolleyCut(const RdBody *rd, const int *parts, int hits)
{
    if (s_tacticalDeaths) return -1;
    int target=-1;
    for(int i=0;i<hits;i++) {
        int sp=parts[i],a;
        if(sp<=1)return -1;
        if(target<0){target=sp;continue;}
        for(a=sp;a>0 && a!=target;a=g_RdSkelHuman.parent[a]){}
        if(a==target)continue;
        for(a=target;a>0 && a!=sp;a=g_RdSkelHuman.parent[a]){}
        if(a!=sp)return -1;
        target=sp;
    }
    return target>1 && rdDismember() && rdChance(RD_DISMEMBER_CHANCE) ? rd->body[target] : -1;
}

static void rdLog(int level, const char *msg) { sysLogPrintf(level==PDPHYS_LOG_ERROR?LOG_ERROR:level==PDPHYS_LOG_WARNING?LOG_WARNING:LOG_NOTE,"%s",msg); }

static void rdRooms(RdBody *body)
{
    int rooms[64],count=0;
    unsigned char include[MAXROOMCOUNT]={0};
    for(int i=0;i<15 && body->rooms[i]!=255;i++)
        if(body->rooms[i]<MAXROOMCOUNT)include[body->rooms[i]]=1;
    if(g_BgPortals)for(int part=0;part<body->count;part++) {
        if(part!=body->root && !(body->cuts&(1u<<part)))continue;
        coord3d to; memcpy(to.f,body->joints[part].m[3],sizeof(to));
        u8 final[16]; s32 crossed[16],n=0;
        bgFindRoomsAlongSegment(&body->origin,&to,body->rooms,final,crossed,&n,15);
        for(int j=0;j<n;j++)if(crossed[j]>=0 && crossed[j]<MAXROOMCOUNT)include[crossed[j]]=1;
    }
    /* Include immediate neighbours so an attached arm can cross a doorway
     * before the pelvis does. Do not collide with unrelated stacked rooms. */
    unsigned char nearby[MAXROOMCOUNT]; memcpy(nearby,include,sizeof(nearby));
    if(g_BgPortals)for(int i=0;i<PORTMAX && g_BgPortals[i].offset_portal;i++) {
        int a=g_BgPortals[i].connectedRoom1,b=g_BgPortals[i].connectedRoom2;
        if(a<MAXROOMCOUNT && b<MAXROOMCOUNT) { if(include[a])nearby[b]=1; if(include[b])nearby[a]=1; }
    }
    for(int i=0;i<MAXROOMCOUNT && count<64;i++)if(nearby[i])rooms[count++]=i;
    if (body->handle >= 0) pdphysRagdollSetRooms(body->handle,rooms,count?count:-1);
    /* The rooms its pieces are in decide whether it is drawn */
    int nvis=0;
    memset(body->visrooms,255,sizeof(body->visrooms));
    for(int i=0;i<MAXROOMCOUNT && nvis<15;i++)if(include[i])body->visrooms[nvis++]=i;
}

/* CE ragdollIsAnyPieceOnScreen (rooms.c:460): drawn when one of its rooms
 * is being drawn and any part is inside the whole view (not the rooms'
 * screen boxes, which shrink to the gap of a moving door); the depth buffer
 * hides what is behind walls. posIsOnScreen's tests, per part. */
static int rdOnScreen(RdBody *b)
{
    int shown=0;
    for(int i=0;i<16 && b->visrooms[i]!=255 && !shown;i++)shown=getROOMID_isRendered(b->visrooms[i])!=0;
    if(!shown)return 0;
    const float size=getinstsize(&b->model)*0.5f;
    for(int i=0;i<b->count;i++) {
        coord3d pos; memcpy(pos.f,b->joints[i].m[3],sizeof(pos.f));
        if(fogPositionIsVisibleThroughFog(&pos,size) && camIsPosInScreen(&pos,size))return 1;
    }
    return 0;
}

/* CE ragdollStageLoad (ragdoll.c:97): the backend and the floors are made
 * when the stage starts (its first tick), not by the first death. */
static int rdStart(void)
{
    if(pdphysIsReady())return 1;
    if(s_failed || !rdEnabled() || lvlGetCurrentStageToLoad()==LEVELID_TITLE)return 0;
    if(!pdphysInit(rdLog)) { s_failed=1; return 0; }
    const GevrBeanCuts cuts={gevrRagdollModelCut,gevrRagdollPiece,gevrRagdollRemap,gevrRagdollBeanCap,gevrRagdollModelBody};
    gevrBeanSetCuts(&cuts);
    rdWorldInit();
    return 1;
}

/* CE rdTorsoLength (create.c:251): waist to neck; sizes and masses scale with it */
static float rdTorsoLength(const RdBody *rd)
{
    float l=0;
    for(int k=0;k<3;k++){float x=rd->skelInitial[2].m[3][k]-rd->skelInitial[1].m[3][k];l+=x*x;}
    l=sqrtf(l);
    return l<5 ? 40 : l;
}

static void rdBones(const RdBody *rd, float p[RD_SKEL][3], float bone[RD_SKEL][3])
{
    const RdSkel *sk=&g_RdSkelHuman;
    for(int i=0;i<RD_SKEL;i++)memcpy(p[i],rd->skelInitial[i].m[3],sizeof(p[i]));
    for(int i=0;i<RD_SKEL;i++) {
        const float *from=p[i],*to=p[i];
        if(sk->child[i]>=0) to=p[sk->child[i]];
        else if(i==2) { from=p[sk->parent[2]]; to=p[2]; } /* the head points along the spine */
        else if(i==0) to=p[2];
        else { from=p[sk->parent[i]]; to=p[i]; }
        float l=0;
        for(int k=0;k<3;k++){bone[i][k]=to[k]-from[k];l+=bone[i][k]*bone[i][k];}
        l=sqrtf(l);
        for(int k=0;k<3;k++)bone[i][k]=l>1e-6f?bone[i][k]/l:(k==1);
    }
}

/* The death-pose bend of a hinge, measured as CE does (create.c:617) */
static float rdHingeAngle(const RdBody *rd, int part)
{
    float p[RD_SKEL][3],bone[RD_SKEL][3],cross[3];
    rdBones(rd,p,bone);
    const float *n1=bone[g_RdSkelHuman.parent[part]],*n2=bone[part],*y=rd->skelInitial[part].m[1];
    cross[0]=n1[1]*n2[2]-n1[2]*n2[1]; cross[1]=n1[2]*n2[0]-n1[0]*n2[2]; cross[2]=n1[0]*n2[1]-n1[1]*n2[0];
    float angle=atan2f(cross[0]*y[0]+cross[1]*y[1]+cross[2]*y[2],n1[0]*n2[0]+n1[1]*n2[1]+n1[2]*n2[2]);
    /* GoldenEye's atan2f replaces libm's in the whole library and returns 0..2pi */
    return angle>(float)M_PI ? angle-2*(float)M_PI : angle;
}

/* CE's human hinges turn about each joint's local Y (create.c:593). PD's
 * animated frames put that axis across the limb; log and check GoldenEye's. */
static int rdHingeAlongBone(const RdBody *rd, int part, float bone[RD_SKEL][3])
{
    const float *y=rd->skelInitial[part].m[1];
    return fabsf(y[0]*bone[part][0]+y[1]*bone[part][1]+y[2]*bone[part][2])>0.7f;
}

static void rdCapsule(pdphysRagdollPart *pp, const float a[3], const float b[3], float radius)
{
    pp->shape=PDPHYS_SHAPE_CAPSULE; memcpy(pp->a,a,sizeof(pp->a)); memcpy(pp->b,b,sizeof(pp->b)); pp->radius=radius;
}
static void rdSwingTwist(pdphysRagdollPart *pp, const float twist[3], const float plane[3],
                         float normalcone, float planecone, float twistmin, float twistmax)
{
    pp->joint=PDPHYS_JOINT_SWINGTWIST; memcpy(pp->axis,twist,sizeof(pp->axis)); memcpy(pp->axis2,plane,sizeof(pp->axis2));
    pp->limits[0]=normalcone; pp->limits[1]=planecone; pp->limits[2]=twistmin; pp->limits[3]=twistmax;
}
static void rdMad(const float a[3], const float d[3], float s, float out[3]) { for(int k=0;k<3;k++)out[k]=a[k]+d[k]*s; }

/* CE ragdollCreatePhysics (create.c:455), human branch: shapes, masses and
 * joints from the death pose, scaled by the torso length */
static void rdBuildParts(RdBody *rd, pdphysRagdollPart *parts, const float vel[3])
{
    const RdSkel *sk=&g_RdSkelHuman;
    float p[RD_SKEL][3],bone[RD_SKEL][3],a[3],b[3];
    const float torso=rd->torso,sc=torso/RD_MASS_REFTORSO;
    rdBones(rd,p,bone);
    for(int i=0;i<rd->count;i++) {
        const int j=rd->skel[i];
        pdphysRagdollPart *pp=&parts[i];
        const float *localy=rd->skelInitial[j].m[1],*localz=rd->skelInitial[j].m[2];
        memset(pp,0,sizeof(*pp));
        pp->parent=i==rd->root ? -1 : rd->parent[i];
        memcpy(pp->jointmtx,&rd->skelInitial[j],sizeof(pp->jointmtx));
        memcpy(pp->linvel,vel,sizeof(pp->linvel));
        pp->mass=sk->mass[j]*sc*sc*sc;
        switch(j) {
        case 0: /* pelvis: across the hips */
            rdCapsule(pp,p[9],p[10],torso*0.25f);
            pp->joint=PDPHYS_JOINT_NONE;
            break;
        case 1: /* chest; the lower back bends ~35 degrees, sideways ~25, twists ~25 */
            rdMad(p[1],bone[1],torso*0.30f,a); rdMad(p[2],bone[1],-torso*0.15f,b);
            rdCapsule(pp,a,b,torso*0.25f);
            rdSwingTwist(pp,bone[1],localz,0.6f,0.45f,-0.45f,0.45f);
            break;
        case 2: /* head, up the spine from the neck joint */
            pp->shape=PDPHYS_SHAPE_SPHERE;
            rdMad(p[2],bone[1],torso*0.30f,pp->a);
            pp->radius=torso*0.26f;
            rdSwingTwist(pp,bone[1],localz,0.85f,0.8f,-1.0f,1.0f);
            break;
        case 3: case 4: /* upper arms */
            rdMad(p[j],bone[j],torso*0.11f,a);
            rdCapsule(pp,a,p[sk->child[j]],torso*0.12f);
            rdSwingTwist(pp,bone[j],localy,1.5f,1.3f,-0.9f,0.9f);
            break;
        case 5: case 6: /* forearms and hands */
            rdMad(p[sk->child[j]],bone[j],torso*0.18f,b);
            rdCapsule(pp,p[j],b,torso*0.10f);
            break;
        case 9: case 10: /* thighs: hips swing ~70 degrees forward and back, ~30 sideways */
            rdCapsule(pp,p[j],p[sk->child[j]],torso*0.17f);
            rdSwingTwist(pp,bone[j],localy,1.2f,0.5f,-0.5f,0.5f);
            break;
        case 11: case 12: { /* shins, with the foot from heel to toes */
            rdCapsule(pp,p[j],p[sk->child[j]],torso*0.14f);
            const int foot=sk->child[j];
            const float *fwd=rd->skelInitial[foot].m[2],*up=rd->skelInitial[foot].m[1];
            for(int k=0;k<3;k++) {
                pp->foota[k]=p[foot][k]-up[k]*torso*0.10f-fwd[k]*torso*0.04f;
                pp->footb[k]=p[foot][k]-up[k]*torso*0.12f+fwd[k]*torso*0.36f;
            }
            pp->footradius=torso*0.07f;
            break;
        }
        }
        rd->radius[i]=pp->radius;
        /* Elbows and knees: hinges about the joint's local Y, one way only,
         * widened to hold the death pose but never ~15 degrees backwards */
        if(sk->hingesign[j] && rdHingeAlongBone(rd,j,bone)) {
            /* The joint's Y runs along the bone, so it can't be the hinge:
             * CE's own rule for rigs without known hinge axes
             * (ragdollShapeFromHitboxes, create.c:409) - bend the way it is
             * bent now, about the axis it is bent around */
            const float *n1=bone[sk->parent[j]],*n2=bone[j];
            float cross[3]={n1[1]*n2[2]-n1[2]*n2[1],n1[2]*n2[0]-n1[0]*n2[2],n1[0]*n2[1]-n1[1]*n2[0]};
            const float sinang=sqrtf(cross[0]*cross[0]+cross[1]*cross[1]+cross[2]*cross[2]);
            pp->joint=PDPHYS_JOINT_HINGE;
            memcpy(pp->axis2,n1,sizeof(pp->axis2));
            memcpy(pp->axis3,n2,sizeof(pp->axis3));
            if(sinang>0.15f) {
                for(int k=0;k<3;k++)pp->axis[k]=cross[k]/sinang;
                const float angle=atan2f(sinang,n1[0]*n2[0]+n1[1]*n2[1]+n1[2]*n2[2]);
                pp->limits[0]=-0.1f;
                pp->limits[1]=angle+0.05f>2.4f ? angle+0.05f : 2.4f;
            } else {
                memcpy(pp->axis,localy,sizeof(pp->axis));
                pp->limits[0]=-1.2f; pp->limits[1]=1.2f;
            }
        } else if(sk->hingesign[j]) {
            const float maxbend=(j==5 || j==6) ? 2.55f : 2.45f;
            const float angle=rdHingeAngle(rd,j);
            pp->joint=PDPHYS_JOINT_HINGE;
            memcpy(pp->axis,localy,sizeof(pp->axis));
            memcpy(pp->axis2,bone[sk->parent[j]],sizeof(pp->axis2));
            memcpy(pp->axis3,bone[j],sizeof(pp->axis3));
            if(sk->hingesign[j]>0) { pp->limits[0]=-0.05f; pp->limits[1]=maxbend; }
            else { pp->limits[0]=-maxbend; pp->limits[1]=0.05f; }
            if(angle<pp->limits[0]) {
                pp->limits[0]=angle-0.05f;
                if(sk->hingesign[j]>0 && pp->limits[0]<-0.25f)pp->limits[0]=-0.25f;
            }
            if(angle>pp->limits[1]) {
                pp->limits[1]=angle+0.05f;
                if(sk->hingesign[j]<0 && pp->limits[1]>0.25f)pp->limits[1]=0.25f;
            }
        }
    }
    /* Cut joints stay cut */
    for(int i=0;i<rd->count;i++)if(rd->cuts&(1u<<i))parts[i].joint=PDPHYS_JOINT_NONE;
}

/* CE ragdollTryDismemberBlast (dismember.c:1593): up to two close joints. */
static void rdBlastCuts(RdBody *b, const float centre[3], float radius)
{
    if (b->coopId && gevrCoopPuppets()) return;
    const unsigned before = b->cuts;
    static const signed char joints[]={2,3,4,5,6,9,10,11,12,1};
    for(int i=0,cut=0;rdDismember() && i<(int)sizeof(joints) && cut<2;i++) {
        const int part=joints[i],body=b->body[part];
        if(body<0 || (b->cuts&(1u<<body)))continue;
        float dist=0,reach=part==1 ? radius*0.35f : radius*0.6f;
        for(int k=0;k<3;k++){float x=b->joints[body].m[3][k]-centre[k];dist+=x*x;}
        dist=sqrtf(dist);
        if(dist<reach && rdChance(RD_DISMEMBER_CHANCE*(1-dist/reach)) && rdCut(b,body))cut++;
    }
    if (b->cuts != before && b->coopId) gevrCoopSendCorpseCuts(b->coopId, b->cuts);
}

static void rdApplyCuts(RdBody *body, unsigned cuts)
{
    const unsigned added = cuts & ~body->cuts;
    for (int part = 0; part < body->count; ++part)
        if (added & (1u << part)) rdCut(body, part);
}

int gevrRagdollApplyCoopCuts(unsigned id, unsigned cuts)
{
    for (int i = 0; i < RD_BODIES; ++i) {
        RdBody *body = &s_bodies[i];
        if (!body->used || body->coopId != id) continue;
        rdApplyCuts(body, cuts);
        return TRUE;
    }
    return FALSE;
}

static void rdBlastBody(RdBody *b, const float centre[3], float radius, float damage)
{
    if (radius <= 0 || b->coopReplica) return;
    for (int i = 0; i < 2; ++i)
        if (s_blades[i].body == b) rdReleaseBlade(&s_blades[i], "blast");
    b->reaction.limb = b->reaction.hand = -1;
    rdBlastCuts(b, centre, radius);
    const float strength=fminf(2,fmaxf(0.5f,damage))*RD_PUSH_SCALE;
    for(int i=0;i<b->count;i++) {
        float dir[3],dist=0;
        for(int k=0;k<3;k++){dir[k]=b->joints[i].m[3][k]-centre[k];dist+=dir[k]*dir[k];}
        dist=sqrtf(dist);
        if(dist<1){dir[0]=dir[2]=0;dir[1]=1;dist=1;}
        else for(int k=0;k<3;k++)dir[k]/=dist;
        const float falloff=fmaxf(0.15f,1-dist/(radius*1.5f));
        const float speed=fminf(1100,700*strength*falloff);
        const float v[3]={dir[0]*speed,dir[1]*speed*0.5f+speed*0.45f,dir[2]*speed};
        pdphysRagdollAddVelocity(b->handle,i,v);
    }
    pdphysRagdollKeepAwake(b->handle,0.2f);
    b->asleep=0;
}

/* Remember a guard's fatal blast while the native explosion animation plays.
 * Viewers record impact here before their own damage handler runs. */
void gevrRagdollExplosionChr(ChrRecord *chr, const coord3d *centre, float radius, float damage)
{
    if(!rdEnabled() || !chr || !centre || gevrRagdollSuppress(chr))return;
    RdSeen *s=rdSeen(chr,1);
    if(!s)return;
    s->blastTick=g_GlobalTimer?g_GlobalTimer:1;
    memcpy(s->blast,centre->f,sizeof(s->blast));
    memcpy(s->blastOrigin,chr->prop->pos.f,sizeof(s->blastOrigin));
    s->blastRadius=radius; s->blastDamage=damage;
}

/* explosion.c, an explosion's first damage pulse: bodies already lying
 * within its reach are thrown and cut */
void gevrRagdollBlast(const coord3d *centre, float radius, float damage)
{
    if(!rdEnabled() || !pdphysIsReady() || !centre || radius<=0)return;
    for(int i=0;i<RD_BODIES;i++) {
        RdBody *b=&s_bodies[i];
        if(!b->used)continue;
        float dist=0;
        for(int k=0;k<3;k++){float x=b->joints[b->root].m[3][k]-centre->f[k];dist+=x*x;}
        if(dist<radius*radius*2.25f)rdBlastBody(b,centre->f,radius,damage);
    }
}

static void rdRigPose(RdBody *body)
{
    if (!body->rig) return;
    const int matrix = body->rigMatrix;
    Mtxf world;
    rdMul(&body->offset[matrix], &body->joints[body->owner[matrix]], &world);
    gevrEnemyRigRagdollPose(body->rig, body->serial, &world);
}

static void rdCoopPoseLog(const RdBody *body, const char *state)
{
    const float *at = body->joints[body->root].m[3];
    sysLogPrintf(LOG_NOTE, "tactical co-op: corpse %u pose %u %s at %.2f %.2f %.2f",
        body->coopId, body->coopSequence, state, at[0], at[1], at[2]);
}

static void rdSendCoopPose(RdBody *body, int anchor)
{
    ++body->coopSequence;
    body->coopPoseTime = body->age + RD_COOP_POSE_INTERVAL;
    netCoopSendCorpsePose(body->coopId, body->coopSequence, body->count,
        (const float (*)[16])body->joints, anchor);
    if (sysArgCheck("--input-script") && (body->coopSequence == 1 || anchor))
        rdCoopPoseLog(body, anchor ? "anchor host" : "sent");
}

int gevrRagdollReceiveCoopPose(unsigned id, unsigned sequence, int count,
    const float (*joints)[16], int final)
{
    for (int i = 0; i < RD_BODIES; ++i) {
        RdBody *body = &s_bodies[i];
        if (!body->used || !body->coopReplica || body->coopId != id) continue;
        if (body->count != count || sequence <= body->coopSequence) return TRUE;
        const int first = !body->coopSequence;
        body->coopSequence = sequence;
        if (first || final) {
            memcpy(body->joints, joints, count * sizeof(Mtxf));
            body->coopPoseTime = 0.f;
            rdRooms(body);
            rdRigPose(body);
            if (sysArgCheck("--input-script"))
                rdCoopPoseLog(body, final ? "anchor guest" : "received");
        } else {
            memcpy(body->coopFrom, body->joints, count * sizeof(Mtxf));
            memcpy(body->coopTo, joints, count * sizeof(Mtxf));
            body->coopPoseTime = RD_COOP_POSE_INTERVAL;
        }
        return TRUE;
    }
    return FALSE;
}

static void rdMoveCoopReplica(RdBody *body, float dt)
{
    if (body->coopPoseTime <= 0.f) return;
    body->coopPoseTime = fmaxf(0.f, body->coopPoseTime - dt);
    const float blend = 1.f - body->coopPoseTime / RD_COOP_POSE_INTERVAL;
    for (int i = 0; i < body->count; ++i) {
        if (blend >= 1.f) {
            body->joints[i] = body->coopTo[i];
            continue;
        }
        const Mtxf *from = &body->coopFrom[i], *to = &body->coopTo[i];
        float a[3][3], b[3][3], orientation[3][3], fromQ[4], toQ[4], q[4];
        for (int row = 0; row < 3; ++row) {
            memcpy(a[row], from->m[row], sizeof(a[row]));
            memcpy(b[row], to->m[row], sizeof(b[row]));
        }
        gevrPoseMatrixQuat(a, fromQ);
        gevrPoseMatrixQuat(b, toQ);
        gevrPoseQuatLerp(fromQ, toQ, blend, q);
        gevrPoseQuatMatrix(q, orientation);
        for (int row = 0; row < 3; ++row)
            memcpy(body->joints[i].m[row], orientation[row], sizeof(orientation[row]));
        for (int axis = 0; axis < 3; ++axis)
            body->joints[i].m[3][axis] = from->m[3][axis] + blend * (to->m[3][axis] - from->m[3][axis]);
    }
    if (blend >= 1.f) rdRooms(body);
    rdRigPose(body);
}

void gevrRagdollCapture(ChrRecord *chr)
{
    if(!gevrRagdollNeedsPose(chr) || !chr->model->render_pos)return;
    RdSeen *seen=rdSeen(chr,0);
    if(seen)seen->attempted=1;
    if(!rdStart())return;
    int limit=rdBodyLimit();
    int count=0; RdBody *slot=NULL,*oldest=NULL;
    for(int i=0;i<RD_BODIES;i++) {
        RdBody *b=&s_bodies[i];
        if(!b->used) { if(!slot)slot=b; continue; }
        count++;
        if(!b->bladeHeld && (!oldest || b->serial<oldest->serial))oldest=b;
    }
    if(count>=limit || !slot) {
        if(!oldest)return;
        slot=oldest;
        rdRelease(slot, FALSE);
    }
    if(!slot || !rdClone(slot,chr)) { if(slot)rdRelease(slot, FALSE); return; }
    const int fromHost = seen && seen->coopId;
    slot->coopReplica = fromHost;
    float velocity[3]={0};
    const int animated = s_tacticalDeaths && chr->prop->type == PROP_TYPE_CHR && chr->actiontype == ACT_DEAD;
    const int blasted = seen && seen->blastTick && (animated
        || (g_GlobalTimer - seen->blastTick <= 60
            && (g_GlobalTimer - seen->hitTick > 120 || seen->blastTick >= seen->hitTick)));
    /* An animation has already conveyed the hit and movement. Start physics
     * from its final pose without applying another fall or shot impulse. */
    float fall=0;
    for(int k=0;k<3;k++)fall+=chr->fallspeed.f[k]*chr->fallspeed.f[k];
    if (!animated && !blasted && fall > 1 && isfinite(fall)) {
        float factor=fminf(60,600/sqrtf(fall));
        for(int k=0;k<3;k++)velocity[k]=chr->fallspeed.f[k]*factor;
    }
    slot->torso=rdTorsoLength(slot);
    slot->handle = -1;
    if (!fromHost) {
        pdphysRagdollPart parts[RD_PARTS];
        rdBuildParts(slot,parts,velocity);
        slot->handle=pdphysRagdollCreate(parts,slot->count);
        if(slot->handle<0){rdRelease(slot, FALSE);return;}
        if (gevrCoopGuardGore(chr))
            pdphysRagdollGetJoints(slot->handle, (float (*)[16])slot->joints);
    }
    slot->used=1; slot->serial=++s_serial; slot->alpha=255;
    slot->fadeEnd = s_tacticalDeaths ? RD_REACTION_SECONDS + 1.5f : 5.5f;
    slot->rig = gevrEnemyRigAttachRagdoll(chr, slot->serial, &slot->rigMatrix);
    rdRigPose(slot);
    slot->woundMatrix = gevrBloodyCombatMoveWound(chr->model, &slot->model);
    if (slot->woundMatrix >= slot->matrices) {
        gevrBloodyCombatForgetWound(&slot->model);
        slot->woundMatrix = -1;
    }
    const int hit = blasted ? -1 : rdHitPart(slot, seen);
    const int weapon = !blasted && seen && g_GlobalTimer - seen->hitTick <= 120 ? seen->weapon : 0;
    /* CE ragdollOnChrDeath (ragdoll.c:1063): a strong push on the part that
     * was hit, a smaller push and a little lift on the whole body */
    const int stabbed = seen && seen->stabbed;
    if (!fromHost && stabbed) rdAttachBlade(chr, slot, hit);
    if (!fromHost && !animated && weapon && seen && !stabbed) {
        float l=sqrtf(seen->dir[0]*seen->dir[0]+seen->dir[1]*seen->dir[1]+seen->dir[2]*seen->dir[2]);
        float hitspeed,allspeed,liftspeed,push[3],all[3];
        rdPushSpeeds(weapon,&hitspeed,&allspeed,&liftspeed);
        if(l>1e-6f) {
            for(int k=0;k<3;k++){push[k]=seen->dir[k]/l*hitspeed;all[k]=seen->dir[k]/l*allspeed;}
            all[1]+=liftspeed;
            pdphysRagdollAddVelocity(slot->handle,-1,all);
            if(hit>=0)pdphysRagdollAddVelocity(slot->handle,hit,push);
        }
    }
    int cut = stabbed || fromHost ? -1 : rdCutForHit(slot,hit,weapon);
    if(!fromHost && s_pending.chr==chr && g_GlobalTimer-s_pending.tick<=120) {
        int volley[RD_VOLLEY_MAX];
        for(int i=0;i<s_pending.hits;i++)volley[i]=rdSkelForHit(slot,s_pending.parts[i]);
        cut=rdVolleyCut(slot,volley,s_pending.hits);
        s_pending.chr=NULL;
    }
    if (cut >= 0 && rdCut(slot, cut) && weapon == ITEM_TACTICAL_KATANA) {
        const float *point = slot->joints[cut].m[3];
        gevrBloodyCombatImpact(weapon, chr->prop->stan ? getTileRoom(chr->prop->stan) : -1, point, seen->dir);
        if (sysArgCheck("--input-script"))
            sysLogPrintf(LOG_NOTE, "tactical katana: separated hit region %d at joint %d", seen->hitpart, slot->skel[cut]);
    }
    if (fromHost) {
        slot->coopId = seen->coopId;
        rdApplyCuts(slot, seen->coopCuts);
        netCoopApplyPendingCorpsePose(slot->coopId);
        if (weapon == ITEM_TACTICAL_KATANA && hit >= 0 && (slot->cuts & (1u << hit)))
            gevrBloodyCombatImpact(weapon, chr->prop->stan ? getTileRoom(chr->prop->stan) : -1,
                slot->joints[hit].m[3], seen->dir);
    }
    if (seen && seen->headBurst && !blasted && slot->body[2] >= 0
        && (fromHost ? (slot->cuts & (1u << slot->body[2])) != 0 : rdCut(slot, slot->body[2]))) {
        slot->hiddenHead = TRUE;
        gevrBloodyCombatForgetWound(&slot->model);
        slot->woundMatrix = -1;
        const Mtxf *head = &slot->joints[slot->body[2]];
        float centre[3];
        for (int k = 0; k < 3; ++k) centre[k] = head->m[3][k] + 7.f*head->m[1][k];
        gevrBloodyCombatHeadBurst(chr->prop->stan ? getTileRoom(chr->prop->stan) : -1, centre, seen->dir);
        if (sysArgCheck("--input-script"))
            sysLogPrintf(LOG_NOTE, "tactical gore: guard %d fatal head burst", chr->chrnum);
    }
    if (blasted && animated) {
        float centre[3];
        for (int k = 0; k < 3; ++k)
            centre[k] = seen->blast[k] + chr->prop->pos.f[k] - seen->blastOrigin[k];
        rdBlastCuts(slot, centre, seen->blastRadius);
    } else if (blasted) {
        rdBlastBody(slot, seen->blast, seen->blastRadius, seen->blastDamage);
    }
    if (!fromHost && s_tacticalDeaths && chr->prop->type == PROP_TYPE_CHR) {
        slot->reaction.active = TRUE;
        slot->reaction.hit = seen && !blasted ? seen->fatalHit : HIT_NULL_PART;
        slot->reaction.limb = rdSkelForHit(slot, slot->reaction.hit);
        slot->reaction.hand = -1;
    }
    if (animated && sysArgCheck("--input-script"))
        sysLogPrintf(LOG_NOTE, "ragdoll: guard %d finished native animation at tick %d, body %u",
            chr->chrnum, g_GlobalTimer, slot->serial);
    rdRooms(slot);
    slot->onscreen=rdOnScreen(slot);
    if(seen)seen->suppressed=1;
    if (!fromHost && gevrCoopIsHost() && gevrCoopGuardGore(chr)) {
        slot->coopId = slot->serial;
        GevrCoopCorpse corpse = {.id = slot->coopId, .cuts = slot->cuts,
            .immediate = !animated, .headBurst = slot->hiddenHead, .stabbed = stabbed,
            .part = seen && !blasted ? seen->fatalHit : HIT_NULL_PART, .weapon = weapon};
        if (seen && !blasted) memcpy(corpse.direction, seen->dir, sizeof(corpse.direction));
        gevrCoopSendCorpse(chr, &corpse);
        rdSendCoopPose(slot, FALSE);
    }
    if (slot->coopId && sysArgCheck("--input-script"))
        sysLogPrintf(LOG_NOTE, "tactical co-op: corpse %u guard %d cuts 0x%x rig %d",
            slot->coopId, chr->chrnum, slot->cuts, slot->rig);
    float p[RD_SKEL][3],bone[RD_SKEL][3];
    rdBones(slot,p,bone);
    int walled,flooronly,tris,state;
    rdWorldStats(slot->rooms[0],&walled,&flooronly,&tris,&state);
    sysLogPrintf(LOG_NOTE,"ragdoll: stage walls so far: %d rooms (%d triangles), %d floor-only; body %u died in room %d (%s)",
        walled,tris,flooronly,s_serial,slot->rooms[0],state==1?"walls":state==2?"floor-only":"no walls yet");
    sysLogPrintf(LOG_NOTE,"ragdoll: body %u, %d parts, %d meshes, torso %.1f, hit part %d, weapon %d (damage %.1f), blast %d, cuts 0x%x,"
        " knees %.2f %.2f, elbows %.2f %.2f, hinge Y along bone %d%d%d%d",
        slot->serial,slot->count,slot->nmeshes,slot->torso,hit,weapon,rdDamage(weapon),blasted,slot->cuts,
        rdHingeAngle(slot,11),rdHingeAngle(slot,12),rdHingeAngle(slot,5),rdHingeAngle(slot,6),
        rdHingeAlongBone(slot,11,bone),rdHingeAlongBone(slot,12,bone),rdHingeAlongBone(slot,5,bone),rdHingeAlongBone(slot,6,bone));
}

/* Diagnostics for bodies seen in walls: the pelvis moved through level
 * geometry this step. In its own rooms it went through a wall it collides
 * with; only in other rooms, the room filter left that wall out. Once per
 * body. */
static void rdCrossCheck(RdBody *b, const float before[3], float dt)
{
    if(b->crossed || b->asleep)return;
    const float *after=b->joints[b->root].m[3];
    float moved=0;
    for(int k=0;k<3;k++){float x=after[k]-before[k];moved+=x*x;}
    if(moved<1)return;
    float hit[3],normal[3]; int room=-1;
    const int own=pdphysRagdollRaycast(b->handle,before,after,hit,normal,&room);
    if(!own && !pdphysRaycastAnyRoom(before,after,hit,normal,&room))return;
    if(fabsf(normal[1])>0.7f && !own)return; /* a floor of a stacked room */
    b->crossed=1;
    int walled,flooronly,tris,state;
    rdWorldStats(room,&walled,&flooronly,&tris,&state);
    sysLogPrintf(LOG_WARNING,"ragdoll: body %u's pelvis went through %s geometry (room %d, %s, normal y %.2f) at %.0f units/s; its rooms %d %d %d %d",
        b->serial,own?"its own":"another room's",room,state==1?"walls":state==2?"floor-only":"no walls",normal[1],
        sqrtf(moved)/dt,b->visrooms[0]==255?-1:b->visrooms[0],b->visrooms[1]==255?-1:b->visrooms[1],
        b->visrooms[2]==255?-1:b->visrooms[2],b->visrooms[3]==255?-1:b->visrooms[3]);
}

static float rdCameraDistance2(const float pos[3])
{
    const Mtxf *eye=currentPlayerGetViewToWorldMtxf();
    float d=0;
    for(int k=0;k<3;k++){float x=pos[k]-eye->m[3][k];d+=x*x;}
    return d;
}

static void rdJointPoint(const RdBody *body, int joint, float point[3])
{
    const int part = body->body[joint];
    if (part >= 0) {
        memcpy(point, body->joints[part].m[3], 3 * sizeof(float));
        return;
    }
    /* Hands and feet are rigidly attached to their parent capsules. */
    const int parent = g_RdSkelHuman.parent[joint];
    const Mtxf *bind = &body->skelInitial[parent], *now = &body->joints[body->body[parent]];
    const float *p = body->skelInitial[joint].m[3];
    memcpy(point, now->m[3], 3 * sizeof(float));
    for (int axis = 0; axis < 3; ++axis) {
        float offset = 0.f;
        for (int k = 0; k < 3; ++k) offset += (p[k] - bind->m[3][k]) * bind->m[axis][k];
        for (int k = 0; k < 3; ++k) point[k] += offset * now->m[axis][k];
    }
}

static int rdWoundHand(const RdBody *body, int limb)
{
    if (limb < 0 || !rdPartConnected(body, body->body[limb])) return -1;
    if (limb >= 3 && limb <= 6) {
        const int hand = 5 + (limb & 1);
        return rdPartConnected(body, body->body[hand]) ? hand : -1;
    }
    if (limb < 9 || limb > 12) return -1;
    if (!rdPartConnected(body, body->body[5])) return rdPartConnected(body, body->body[6]) ? 6 : -1;
    if (!rdPartConnected(body, body->body[6])) return 5;
    /* Pick the free hand that can reach the leg from this particular fall. */
    float wound[3], left[3], right[3];
    rdJointPoint(body, limb, wound);
    rdJointPoint(body, 7, left);
    rdJointPoint(body, 8, right);
    float leftScore = -35.f * left[1], rightScore = -35.f * right[1];
    for (int k = 0; k < 3; ++k) {
        leftScore += (wound[k] - left[k]) * (wound[k] - left[k]);
        rightScore += (wound[k] - right[k]) * (wound[k] - right[k]);
    }
    return leftScore < rightScore ? 5 : 6;
}

static void rdReachWound(RdBody *body, float blend, int limb, int forearm)
{
    const int part = body->body[forearm];
    const int shoulder = g_RdSkelHuman.parent[forearm];
    const int wrist = g_RdSkelHuman.child[forearm];
    const int end = g_RdSkelHuman.child[limb];
    float hand[3], wound[3], other[3], target[3];
    rdJointPoint(body, wrist, hand);
    rdJointPoint(body, limb, wound);
    rdJointPoint(body, end, other);
    const float *shoulderPoint = body->joints[body->body[shoulder]].m[3];
    float length2 = 0.f, upper2 = 0.f, lower2 = 0.f;
    const float along = limb >= 9 ? .20f : .40f;
    for (int k = 0; k < 3; ++k) {
        wound[k] += along * (other[k] - wound[k]);
        const float distance = wound[k] - shoulderPoint[k];
        const float upper = body->skelInitial[forearm].m[3][k] - body->skelInitial[shoulder].m[3][k];
        const float lower = body->skelInitial[wrist].m[3][k] - body->skelInitial[forearm].m[3][k];
        length2 += distance * distance;
        upper2 += upper * upper;
        lower2 += lower * lower;
    }
    const float palmLength = .15f * body->torso;
    const float *elbow = body->joints[part].m[3];
    const float lowerLength = sqrtf(lower2);
    if (lowerLength > 1e-3f)
        for (int k = 0; k < 3; ++k) hand[k] += palmLength * (hand[k] - elbow[k]) / lowerLength;
    const float reach = .985f * (sqrtf(upper2) + lowerLength + palmLength);
    /* A lower-leg wound is often out of reach. Draw the knee toward the
     * hip while the free hand reaches, with contact and joint limits intact. */
    if (limb >= 9 && length2 > reach * reach) {
        const int shin = limb >= 11 ? limb : g_RdSkelHuman.child[limb];
        const int knee = body->body[shin];
        pdphysRagdollGetPartPos(body->handle, knee, target);
        const float *hip = body->joints[body->body[g_RdSkelHuman.parent[shin]]].m[3];
        for (int k = 0; k < 3; ++k) target[k] += (hip[k] - target[k]) * .30f;
        target[1] += .21f * body->torso;
        pdphysRagdollPull(body->handle, knee, target, 84.f * blend, 3.2f);
    }
    const float ratio = length2 > reach * reach ? reach / sqrtf(length2) : 1.f;
    const float lift = .10f;
    float gap2 = 0.f;
    for (int k = 0; k < 3; ++k) {
        target[k] = shoulderPoint[k] + ratio * (wound[k] - shoulderPoint[k])
            + (k == 1 ? lift * body->torso : 0.f);
        gap2 += (wound[k] - hand[k]) * (wound[k] - hand[k]);
    }
    const float contact = body->radius[body->body[limb]] + .16f * body->torso;
    const int touching = gap2 < contact * contact;
    if (!body->reaction.contacted && touching) {
        body->reaction.contacted = TRUE;
        if (sysArgCheck("--input-script"))
            sysLogPrintf(LOG_NOTE, "ragdoll: body %u hand reached injured limb at tick %d",
                body->serial, g_GlobalTimer);
    }
    pdphysRagdollGuidePoint(body->handle, part, hand, target,
        (touching ? 76.f : 145.f) * blend, 4.5f);
}

static int rdFloorReaction(RdBody *body, int allowReach)
{
    if (!body->reaction.active) return FALSE;
    if (body->age >= RD_REACTION_SECONDS) {
        if (sysArgCheck("--input-script") && body->reaction.started)
            sysLogPrintf(LOG_NOTE, "ragdoll: body %u floor reaction ended at %.2fs after %d twitches",
                body->serial, body->age, body->reaction.pulses);
        body->reaction.active = FALSE;
        return FALSE;
    }

    const float elapsed = body->age - .20f;
    if (elapsed < 0.f || rdCameraDistance2(body->joints[body->root].m[3]) > 1350.f * 1350.f)
        return FALSE;
    if (!body->reaction.started) {
        body->reaction.started = TRUE;
        body->reaction.hand = rdWoundHand(body, body->reaction.limb);
        if (sysArgCheck("--input-script"))
            sysLogPrintf(LOG_NOTE, "ragdoll: body %u floor reaction for hit %d, limb %d at tick %d",
                body->serial, body->reaction.hit, body->reaction.limb, g_GlobalTimer);
    }

    const int limb = body->reaction.limb;
    const float fade = 1.f - .72f * body->age / RD_REACTION_SECONDS;
    const float phase = .035f * (body->serial & 3);
    static const float moments[] = {0.f, .26f, .64f, 1.14f, 1.78f, 2.58f, 3.54f, 4.62f, 5.83f, 6.91f, 7.59f};
    int next = body->reaction.nextPulse;
    int count = sizeof(moments) / sizeof(moments[0]);
    while (next < count && elapsed >= moments[next] + phase) ++next;
    const int leg = limb >= 9 && limb <= 12;
    if (next != body->reaction.nextPulse) {
        const int pulse = next - 1;
        body->reaction.nextPulse = next;
        if (elapsed - moments[pulse] - phase < .22f) {
            const int side = (pulse + body->serial) & 1;
            const int joint = leg && pulse % 3 == 0 ? 12 - (limb & 1) : (pulse % 3 == 1 ? 5 : 11) + side;
            int part = body->body[joint];
            if (!rdPartConnected(body, part)) part = body->body[joint + (joint & 1 ? 1 : -1)];
            if (rdPartConnected(body, part)) {
                const float size = (body->reaction.hit == HIT_HEAD ? 72.f : 115.f) * fade;
                const float velocity[3] = {side ? size * .38f : -size * .38f, size, (pulse & 1) ? .24f * size : -.24f * size};
                pdphysRagdollAddVelocity(body->handle, part, velocity);
                body->asleep = FALSE;
                body->reaction.pulses++;
            }
        }
    }

    const int hand = body->reaction.hand;
    const float reachPhase = fmodf(elapsed, 2.36f);
    const float reach = fminf(1.f, fminf(reachPhase * 5.f, (1.65f - reachPhase) * 3.5f));
    if (!allowReach || hand < 0 || reach <= 0.f
        || !rdPartConnected(body, body->body[limb]) || !rdPartConnected(body, body->body[hand])) return FALSE;
    rdReachWound(body, reach * (.45f + .55f * fade), limb, hand);
    return TRUE;
}

void gevrRagdollTick(int ticks60)
{
    s_frame++;
    RdMemory **m=&s_garbage;
    while(*m) {
        if(s_frame-(*m)->retired>=3) { RdMemory *old=*m; *m=old->next; free(old); }
        else m=&(*m)->next;
    }
    if(!rdEnabled()) {
        if(pdphysIsReady())gevrRagdollReset();
        return;
    }
    if(!rdStart())return;
    if(ticks60<=0)return;
    float dt=fminf(ticks60/60.0f,0.1f);
    int limit=rdBodyLimit();
    for(;;) {
        RdBody *old=NULL; int count=0;
        for(int i=0;i<RD_BODIES;i++)if(s_bodies[i].used) {
            count++;
            if(!s_bodies[i].bladeHeld && (!old || s_bodies[i].serial<old->serial))old=&s_bodies[i];
        }
        if(count<=limit || !old)break;
        rdRelease(old, FALSE);
    }
    rdWorldUpdate();
    rdMoversUpdate(dt,s_bodies,RD_BODIES);
    rdUpdateBlades(dt);
    /* CE ragdollTick (ragdoll.c:1645): collision rooms every tick near the
     * camera or after the pelvis moves 12 units, else every 4th tick (staggered) */
    int reactions = 0;
    for(int i=0;i<RD_BODIES;i++) {
        RdBody *b=&s_bodies[i];
        if (!b->used || b->coopReplica) continue;
        reactions += rdFloorReaction(b, reactions < 6);
        if (b->asleep && pdphysRagdollIsAsleep(b->handle)) continue;
        float moved=0;
        for(int k=0;k<3;k++){float x=b->joints[b->root].m[3][k]-b->colroompos[k];moved+=x*x;}
        if(rdCameraDistance2(b->joints[b->root].m[3])<RD_FAR*RD_FAR || moved>RD_COLROOM_MOVE*RD_COLROOM_MOVE
            || ((s_frame/2)+i)%4==0) {
            rdRooms(b);
            memcpy(b->colroompos,b->joints[b->root].m[3],sizeof(b->colroompos));
        }
    }
    pdphysStep(dt);
    for(int i=0;i<RD_BODIES;i++) {
        RdBody *b=&s_bodies[i]; if(!b->used)continue;
        b->age+=dt;
        if (b->coopReplica) {
            rdMoveCoopReplica(b, dt);
        } else {
            /* Still asleep after the step: the pose has not changed (CE :1691). */
            const int asleep=pdphysRagdollIsAsleep(b->handle);
            float before[3];memcpy(before,b->joints[b->root].m[3],sizeof(before));
            if (!(b->asleep && asleep)) {
                pdphysRagdollGetJoints(b->handle, (float (*)[16])b->joints);
                rdRigPose(b);
            }
            b->asleep=asleep;
            rdCrossCheck(b,before,dt);
            if (b->coopId && gevrCoopIsHost()) {
                const int settled = asleep && !b->reaction.active && !b->bladeHeld;
                /* Floor contacts can keep a corpse awake after twitching
                 * ends. Give the guest a reliable correction either way. */
                const int anchor = !b->reaction.active && !b->coopReactionAnchored;
                if (anchor || (settled && !b->coopSettled)) rdSendCoopPose(b, TRUE);
                else if ((!asleep || b->bladeHeld) && b->age >= b->coopPoseTime) rdSendCoopPose(b, FALSE);
                b->coopReactionAnchored |= anchor;
                b->coopSettled = settled;
            }
        }
        if (b->woundMatrix >= 0) {
            const int matrix = b->woundMatrix;
            Mtxf world;
            rdMul(&b->offset[matrix], &b->joints[b->owner[matrix]], &world);
            if (!gevrBloodyCombatFollowWound(&b->model, world.m, b->rooms[0])) b->woundMatrix = -1;
        }
        /* Fell out of the level (CE :1708) */
        if(b->joints[b->root].m[3][1]<RD_KILLFLOOR_Y) {
            sysLogPrintf(LOG_NOTE,"ragdoll: body %u fell out of the level, removing it",b->serial);
            if (b->coopId && !b->coopReplica && gevrCoopIsHost()) rdSendCoopPose(b, TRUE);
            rdRelease(b, FALSE);
            continue;
        }
        if(!VrBodiesStay && !b->coopId && !b->bladeHeld) {
            b->alpha=fmaxf(0,fminf(255,(b->fadeEnd-b->age)*170));
            if(b->alpha<=0)rdRelease(b, FALSE);
        }
    }
}

/* A shot body bleeds where it was hit, as a living guard does
 * (chrHandleBulletHit -> chrCreateBloodStain, chr.c:3876): the hit box of
 * that part nearest the hit, the point pushed half out from its joint and
 * half a joint into the body, in that box's matrix. The stain paints the
 * body's own vertex copies, and Bean's wound follows it. The game's random
 * sequence (the stain's shade) is put back: the body is cosmetic. */
static void rdStain(RdBody *b, int part, const float hit[3], const float dir[3])
{
    int best=-1; float bestd=1e30f;
    for(int i=0;i<b->nbbox;i++) {
        int sp=b->bboxSkel[i];
        while(sp>0 && b->body[sp]<0)sp=g_RdSkelHuman.parent[sp];
        if(sp<0 || b->body[sp]!=part)continue;
        const int m=modelFindNodeMtxIndex(b->bbox[i],0);
        if(m<0 || m>=b->matrices)continue;
        Mtxf w; rdMul(&b->offset[m],&b->joints[b->owner[m]],&w);
        float d=0; for(int k=0;k<3;k++){float x=w.m[3][k]-hit[k];d+=x*x;}
        if(d<bestd){bestd=d;best=i;}
    }
    if(best<0)return;
    ModelNode *node=b->bbox[best];
    const int m=modelFindNodeMtxIndex(node,0);
    Mtxf w,inverse; rdMul(&b->offset[m],&b->joints[b->owner[m]],&w);
    if(!rdInverse(&w,&inverse))return;
    const float size=getjointsize(&b->model,node)*0.5f;
    float p[3];
    for(int k=0;k<3;k++)p[k]=hit[k]+(hit[k]-w.m[3][k])*0.5f-size*dir[k];
    rdTransform(&inverse,p,1);
    coord3d local; memcpy(local.f,p,sizeof(local.f));
    const u64 seed=g_randomSeed;
    chrCreateBloodStain(&b->model,b->bboxHit[best],node,&local);
    g_randomSeed=seed;
}

void gevrRagdollBloodStain(int handle, int part, const float point[3], const float dir[3])
{
    for (int i = 0; i < RD_BODIES; ++i) {
        RdBody *body = &s_bodies[i];
        if (body->used && body->handle == handle && part >= 0 && part < body->count) {
            rdStain(body, part, point, dir);
            return;
        }
    }
}

/* The piece of body part 'part' and everything joined to it */
static void rdPushPiece(RdBody *b, int part, const float v[3])
{
    if(!b->cuts) { pdphysRagdollAddVelocity(b->handle,-1,v); return; }
    for(int i=0;i<b->count;i++)if(rdPiece(b,i)==rdPiece(b,part))pdphysRagdollAddVelocity(b->handle,i,v);
}

void gevrRagdollShot(ShotData *shot)
{
    if(s_volley.active){s_volley.pellets++;s_volley.pellethit=0;}
    if(!rdEnabled() || !pdphysIsReady() || !shot || shot->viewDir.z>=-1e-6f)return;
    float depth=shot->maxdist;
    for(int i=0;i<10;i++)if(shot->hits[i].prop && shot->hits[i].dist<depth)depth=shot->hits[i].dist;
    float distance=fminf(65535,(depth+shot->viewOrigin.z)/-shot->viewDir.z);
    if(distance<=0)return;
    float to[3],hit[3]; int handle,part;
    for(int k=0;k<3;k++)to[k]=shot->gunpos.f[k]+shot->dir.f[k]*distance;
    if(!pdphysRaycastRagdoll(shot->gunpos.f,to,0,hit,&handle,&part))return;
    for(int i=0;i<RD_BODIES;i++)if(s_bodies[i].used && s_bodies[i].handle==handle) {
        RdBody *b=&s_bodies[i];
        if(part<0 || part>=b->count)return;
        if(rdIsShotgun(shot->weapon))rdVolleyHit(NULL,b,b->skel[part],shot->dir.f);
        /* CE ragdollOnChrShot (ragdoll.c:858): automatic weapons push a body
         * at most every 4 ticks; nothing builds up over repeated shots */
        if(b->lastpush && g_GlobalTimer-b->lastpush<4)return;
        b->lastpush=g_GlobalTimer?g_GlobalTimer:1;
        float hitspeed,allspeed,liftspeed,push[3],all[3];
        rdPushSpeeds(shot->weapon,&hitspeed,&allspeed,&liftspeed);
        for(int k=0;k<3;k++){push[k]=shot->dir.f[k]*hitspeed*0.45f;all[k]=shot->dir.f[k]*allspeed*0.15f;}
        rdPushPiece(b,part,all);
        pdphysRagdollAddVelocity(handle,part,push);
        pdphysRagdollKeepAwake(handle,0.2f);
        b->asleep=0;
        if (shot->weapon != ITEM_FIST) {
            rdStain(b,part,hit,shot->dir.f);
            gevrBloodyCombatImpact(shot->weapon, b->rooms[0], hit, shot->dir.f);
        }
        const int cut=rdCutForHit(b,part,shot->weapon);
        if(cut>=0 && (!b->coopId || !gevrCoopPuppets()) && rdCut(b,cut) && b->coopId)
            gevrCoopSendCorpseCuts(b->coopId, b->cuts);
        return;
    }
}

/* chrprop.c's shotgun branch: a volley of pellets, each one call of the fire
 * handler (CE ragdollShotgunVolleyBegin/End) */
void gevrRagdollVolley(int begin)
{
    if(begin) { memset(&s_volley,0,sizeof(s_volley)); s_volley.active=rdEnabled(); return; }
    if(!s_volley.active)return;
    s_volley.active=0;
    if (s_tacticalDeaths) return;
    if(!rdDismember() || s_volley.conflict || s_volley.pellets<=0 || s_volley.hits!=s_volley.pellets
        || s_volley.hits>RD_VOLLEY_MAX)return;
    if(s_volley.body) {
        RdBody *b=s_volley.body;
        if(!b->used)return;
        const int cut=rdVolleyCut(b,s_volley.parts,s_volley.hits);
        if(cut>=0 && (!b->coopId || !gevrCoopPuppets()) && rdCut(b,cut) && b->coopId)
            gevrCoopSendCorpseCuts(b->coopId, b->cuts);
    } else if(s_volley.chr && rdDead(s_volley.chr)) {
        /* Its body is made by the next chrTick */
        s_pending.chr=s_volley.chr; s_pending.hits=s_volley.hits; s_pending.tick=g_GlobalTimer;
        memcpy(s_pending.parts,s_volley.parts,sizeof(s_pending.parts));
    }
}

/* A body's own model (&s_bodies[i].model): its slot by address. Bean asks for
 * every vertex of every cut body, and a search of all the slots each time was
 * 13% of the game thread at a pile of bodies (Bunker II, 2026-10-30). */
static RdBody *rdModel(const Model *model)
{
    const uintptr_t first=(uintptr_t)&s_bodies[0].model, at=(uintptr_t)model-first;
    if((uintptr_t)model<first || at%sizeof(RdBody) || at/sizeof(RdBody)>=RD_BODIES)return NULL;
    RdBody *b=&s_bodies[at/sizeof(RdBody)];
    return b->used ? b : NULL;
}
/* rdPiece of each matrix's owner, kept until the body or its cuts change: it
 * walks the parts up, and was asked for each influence of each vertex */
static const u8 *rdPieces(RdBody *b)
{
    if(b->pieceSerial!=b->serial || b->pieceCuts!=b->cuts) {
        for(int m=0;m<b->matrices;m++)b->piece[m]=(u8)rdPiece(b,b->owner[m]);
        b->pieceSerial=b->serial; b->pieceCuts=b->cuts;
    }
    return b->piece;
}
int gevrRagdollModelCut(const Model *model) { RdBody *b=rdModel(model); return b ? (int)b->cuts : 0; }
int gevrRagdollHidesHead(const Model *model)
{
    const RdBody *body = rdModel(model);
    return body && body->hiddenHead;
}
unsigned gevrRagdollModelBody(const Model *model) { RdBody *b=rdModel(model); return b ? b->serial : 0; }
/* Bean's skin found a hole's rim (gevr_bean_caps.inc.h): drawn after the body */
void gevrRagdollBeanCap(const Model *model,int matrix,const s16 (*rim)[2][3],int nedges,int piece,int other)
{
    RdBody *b=rdModel(model);
    if(!b || b->nbean==RD_MAXCAPS || nedges<3 || matrix<0 || matrix>=b->matrices)return;
    b->bean[b->nbean].matrix=matrix; b->bean[b->nbean].nedges=nedges; b->bean[b->nbean].rim=rim;
    b->bean[b->nbean].piece=piece; b->bean[b->nbean++].other=other;
}
int gevrRagdollPiece(const Model *model,int matrix)
{
    RdBody *b=rdModel(model);
    return b && matrix>=0 && matrix<b->matrices ? rdPieces(b)[matrix] : 0;
}
int gevrRagdollRemap(const Model *model,int owner,int matrix,float p[3],float n[3])
{
    RdBody *b=rdModel(model);
    if(!b || !b->cuts || owner<0 || owner>=b->matrices || matrix<0 || matrix>=b->matrices)return matrix;
    const u8 *piece=rdPieces(b);
    if(piece[owner]==piece[matrix])return matrix;
    Mtxf inv,map;
    if(!rdInverse(&b->bind[owner],&inv))return matrix;
    rdMul(&b->bind[matrix],&inv,&map);
    rdTransform(&map,p,1); rdTransform(&map,n,0);
    return owner;
}

Gfx *gevrRagdollRender(Gfx *gdl)
{
    if(!rdEnabled() || !pdphysIsReady())return gdl;
    gdl=bgScissorCurrentPlayerViewDefault(gdl);
    sub_GAME_7F073FC8(0); /* the native upright shadow does not follow a falling rig */
    for(int i=0;i<RD_BODIES;i++) {
        RdBody *b=&s_bodies[i]; if(!b->used)continue;
        /* Rooms and camera are now those of the view being drawn. */
        b->onscreen=rdOnScreen(b);
        if(!b->onscreen)continue;
        b->model.render_pos=dynAllocate(b->matrices*sizeof(RenderPosView));
        if(!b->model.render_pos)continue;
        for(int j=0;j<b->matrices;j++) {
            Mtxf world;
            rdMul(&b->offset[j],&b->joints[b->owner[j]],&world);
            rdMul(&world,camGetWorldToScreenMtxf(),&b->model.render_pos[j].pos);
        }
        b->nbean=0; /* this view's Bean rims arrive during subdraw */
        ModelRenderData render=D_8002CCBC;
        render.gdl=gdl; render.flags=3;
        /* Fogged as the living guard was (chr.c): the root's depth through the
         * game's prop fog. Unfogged, a body shot in fog showed its textures the
         * moment it fell (Surface 2, 2026-10-26). */
        const f32 depth=-b->model.render_pos[0].pos.m[3][2];   /* the chr's zDepth (sub_GAME_7F06C768) */
        rgba_f32 fog;
        const s32 fogged=gevrFogDepthColor(depth,&fog);
        if(!fogged)continue;
        /* And faded by distance as he was (chrRenderProp's chrobjFogVisRangeRelated,
         * which reads only the prop's zDepth): a guard faint in the distance came
         * in solid the moment he fell (player report, Jungle and Surface 2). The
         * corpse's own fade (b->alpha) on top. */
        PropRecord at; memset(&at,0,sizeof(at)); at.zDepth=depth;
        const f32 range=chrobjFogVisRangeRelated(&at,getinstsize(&b->model));
        const s32 alpha=(s32)(range*b->alpha);
        if(alpha<=0)continue;
        render.PropType=alpha<255?8:7;
        render.envcolour.word=0x5a000000u|(alpha<255?(u8)alpha:0);
        rgba_s32 shade={.r=b->shade.r,.g=b->shade.g,.b=b->shade.b,.a=b->shade.a};
        lerp_rgba_s32_with_rgba_f32(&shade,fogged,&fog);
        render.fogcolour.word=((u32)shade.r<<24)|((u32)shade.g<<16)|((u32)shade.b<<8)|(u32)shade.a;
        /* VR_FADE_DEPTH as chrRenderProp: fading, his solid pixels write depth */
        if(alpha<255)gDPNoOpTag(render.gdl++,0x56620000u|(u8)alpha);
        subdraw(&render,&b->model);
        if(alpha<255)gDPNoOpTag(render.gdl++,0x56620000u|0xFFu);
        {
            /* the cut caps fade with the body (gevr_ragdoll_mesh.c reads b->alpha) */
            const f32 own=b->alpha;
            b->alpha=(f32)alpha;
            gdl=rdDrawCaps(b,render.gdl);
            b->alpha=own;
        }
        if (b->rig)
            gdl = gevrEnemyRigDrawRagdoll(b->rig, b->serial, &b->model, &b->origin, &render, gdl);
        bondviewTransformManyPosToViewMatrix(b->model.render_pos,b->matrices);
    }
    return gdl;
}
