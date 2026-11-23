/* Tactical test effects baked from the HollywoodFX and BloodyCombat packs. */
#include <ultra64.h>
#include <bondconstants.h>
#include <bondtypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game/bondview.h"
#include "game/bg.h"
#include "game/image.h"
#include "game/lv.h"
#include "game/matrixmath.h"
#include "game/model.h"
#include "game/player.h"
#include "game/gevr_tactical_loadouts.h"
#include "game/stan.h"
#include "game/tex.h"
#include "dyn.h"
#include "gevr_bean.h"
#include "gevr_hollywood_fx.h"
#include "gevr_guard_cigarettes.h"
#include "gevr_ragdoll.h"
#include "pdphysics.h"
#include "gevr_tactical_gear.h"
#include "gevr_tactical_loot_net.h"
#include "gevr_tactical_net.h"
#include "gevr_tactical_mesh.h"
#include "gevr_tactical_stats.h"
#include "gevr_tactical_range.h"
#include "net_core.h"
#include "game/gevr_tactical_heavy_guards.h"
#include "pngread.h"
#include "system.h"

enum {
    FX_FLASH, FX_JET, FX_DIM_JET, FX_SMOKE, FX_CONCRETE, FX_DUST, FX_SPARK, FX_WOOD,
    FX_BLOOD_SPRAY, FX_BLOOD_STREAM, FX_BLOOD_SPLAT, FX_BLOOD_RIBBON,
    FX_BLOOD_MIST, FX_BLOOD_POOL, FX_BLOOD_DROP, FX_ATLASES
};
enum {
    FX_FRAMES = 8, FX_FRAME_SIZE = 128, FX_TILE_SIZE = 16, FX_PARTICLES = 96,
    FX_BLOOD_MARKS = 72, FX_BLOOD_MARK_LIFE = 2400, FX_BLEEDERS = 24, FX_BLEED_LIFE = 120,
    FX_BLOOD_FLIGHTS = 80, FX_BLOOD_FLIGHT_LIFE = 84,
    FX_BLOOD_TRACE_INTERVAL = 3, FX_BLOOD_SWEEPS_PER_TICK = 24
};

static const char *const s_names[FX_ATLASES] = {
    "flash", "jet", "dim-jet", "muzzle-smoke", "concrete", "dust", "metal-sparks", "wood-chips",
    "blood-spray", "blood-stream", "blood-splat", "blood-ribbon", "blood-mist", "blood-pool", "blood-drop"
};
static u16 s_keys[FX_ATLASES][FX_TILE_SIZE * FX_TILE_SIZE] __attribute__((aligned(8)));
static u8 *s_atlases[FX_ATLASES];
static int s_ready, s_bloodReady;

typedef struct HfxShot {
    unsigned int serial;
    s32 tick;
    int item, smokeSpawned;
} HfxShot;

enum { FX_GUARD_SHOTS = 128, FX_MUZZLE_TICKS = 4 };
static HfxShot s_shots[MAX_PLAYER_COUNT][2], s_tankShot;
static Mtxf s_tankMuzzle;
static struct {
    const PropRecord *prop;
    const WeaponObjRecord *weapon;
    HfxShot shot;
} s_guardShots[FX_GUARD_SHOTS];

typedef struct HfxParticle {
    coord3d origin, velocity;
    float radius, growth, roll, gravity, stretch, spin;
    s32 birth, life;
    u32 tint;
    u8 atlas, variant, alpha;
} HfxParticle;

static HfxParticle s_particles[FX_PARTICLES];
static unsigned int s_next;
typedef struct AccessoryBeam {
    coord3d origin, direction;
    int mode, room;
} AccessoryBeam;
enum { FX_ACCESSORY_BEAMS = 24 };
static AccessoryBeam s_accessories[FX_ACCESSORY_BEAMS];
static unsigned s_accessoryCount;
typedef struct BloodMark {
    coord3d center, across, down, normal;
    float radius;
    s32 birth, wetted;
    u8 frame, pool;
} BloodMark;
static BloodMark s_marks[FX_BLOOD_MARKS];
static unsigned int s_nextMark;

typedef struct BloodFlight {
    coord3d origin, velocity, landed;
    float width, fragmentRadius;
    s32 birth, traced, landedAt;
    int room;
    u8 active, variant;
} BloodFlight;
static BloodFlight s_flights[FX_BLOOD_FLIGHTS];
static unsigned s_nextFlight, s_traceFlight;
#define FX_BLOOD_GRAVITY 715.f

typedef struct BloodWound {
    const Model *model;
    coord3d local, out;
    s32 birth, next, poseTick;
    int matrix, drops;
} BloodWound;
static BloodWound s_wounds[FX_BLEEDERS];
static unsigned s_nextWound;
extern f32 D_800364CC;
extern int VrBloodPatch;

static int loadAtlases(int first, int end, const char *folder)
{
    for (int i = first; i < end; ++i) {
        char path[96];
        s32 width = 0, height = 0;
        snprintf(path, sizeof(path), "mods/tactical/%s/%s.png", folder, s_names[i]);
        s_atlases[i] = pngRead(path, &width, &height);
        if (s_atlases[i] && width == FX_FRAME_SIZE * FX_FRAMES && height == FX_FRAME_SIZE) continue;
        for (int j = first; j <= i; ++j) {
            free(s_atlases[j]);
            s_atlases[j] = NULL;
        }
        sysLogPrintf(LOG_WARNING, "Tactical FX: using original effects. Invalid or missing %s", path);
        return FALSE;
    }
    sysLogPrintf(LOG_NOTE, "Tactical FX: loaded %d %s sprite sheets", end - first, folder);
    return TRUE;
}

void gevrHollywoodReset(int stage)
{
    static int comparisonStarted;
    gevrGuardCigarettesReset();
    memset(s_shots, 0, sizeof(s_shots));
    memset(s_guardShots, 0, sizeof(s_guardShots));
    s_tankShot = (HfxShot){0};
    memset(s_particles, 0, sizeof(s_particles));
    s_next = s_nextMark = s_accessoryCount = 0;
    memset(s_marks, 0, sizeof(s_marks));
    memset(s_wounds, 0, sizeof(s_wounds));
    memset(s_flights, 0, sizeof(s_flights));
    s_nextWound = s_nextFlight = s_traceFlight = 0;
    if (sysArgCheck("--tactical-gear")) {
        if (!s_ready && sysArgCheck("--tactical-hollywood-fx"))
            s_ready = loadAtlases(FX_FLASH, FX_BLOOD_SPRAY, "hollywood-fx");
        if (!s_bloodReady && sysArgCheck("--tactical-bloody-combat")) {
            s_bloodReady = loadAtlases(FX_BLOOD_SPRAY, FX_BLOOD_MIST, "bloody-combat");
            if (s_bloodReady) {
                loadAtlases(FX_BLOOD_MIST, FX_BLOOD_DROP, "bloody-combat");
                loadAtlases(FX_BLOOD_DROP, FX_ATLASES, "bloody-combat");
            }
        }
    }
    if (comparisonStarted || stage != LEVELID_RUNWAY || !s_ready
        || !sysArgCheck("--tactical-hollywood-range") || netIsActive()) return;

    const int build = gevrTacticalGearKey(TACTICAL_GUN_M4A1);
    const char *preset = sysArgCheck("--tactical-hollywood-suppressed") ? "FX B - Suppressed" : "FX A - Unsuppressed";
    if (!gevrTacticalGearLoadNamedPreset(build, preset)) {
        sysLogPrintf(LOG_WARNING, "HollywoodFX: could not load the range preset %s", preset);
        return;
    }
    gevrTacticalGearCommit(stage, ITEM_TACTICAL_M4A1, ITEM_WPPKSIL, build, GEVR_GEAR_PPK_SUPPRESSED);
    gevrTacticalRangeRequest();
    comparisonStarted = TRUE;
}

int gevrHollywoodTexture(const void *key, const u8 **rgba, int *width, int *height)
{
    const uintptr_t offset = (uintptr_t)key - (uintptr_t)s_keys;
    if (offset >= sizeof(s_keys) || offset % sizeof(s_keys[0])) return FALSE;
    const u8 *atlas = s_atlases[offset / sizeof(s_keys[0])];
    if (!atlas) return FALSE;
    *rgba = atlas;
    *width = FX_FRAME_SIZE * FX_FRAMES;
    *height = FX_FRAME_SIZE;
    return TRUE;
}

int gevrHollywoodWeapon(int item)
{
    return s_ready && gevrTacticalCampaignRules() && (item == ITEM_TANKSHELLS || gevrCampaignGunIsFirearm(item));
}

static int pendingShot(const HfxShot *shot)
{
    const int age = g_GlobalTimer - shot->tick;
    return shot->serial && age >= 0 && age < FX_MUZZLE_TICKS;
}

static void recordShot(HfxShot *shot, int item)
{
    ++shot->serial;
    shot->tick = g_GlobalTimer;
    shot->item = item;
    shot->smokeSpawned = FALSE;
}

void gevrHollywoodFired(int hand, int item)
{
    if (item != ITEM_TANKSHELLS && gevrHollywoodWeapon(item))
        recordShot(&s_shots[get_cur_playernum()][hand], item);
}

unsigned gevrHollywoodShotSerial(int hand)
{
    return s_shots[get_cur_playernum()][hand].serial;
}

static HfxShot *guardShot(const PropRecord *prop, int create)
{
    const uintptr_t key = (uintptr_t)prop;
    const unsigned start = ((key >> 5) ^ (key >> 12)) & (FX_GUARD_SHOTS - 1);
    int freeSlot = -1;
    for (unsigned i = 0; i < 8; ++i) {
        const unsigned slot = (start + i) & (FX_GUARD_SHOTS - 1);
        if (s_guardShots[slot].prop == prop && s_guardShots[slot].weapon == prop->weapon)
            return &s_guardShots[slot].shot;
        if (freeSlot < 0 && !pendingShot(&s_guardShots[slot].shot)) freeSlot = slot;
    }
    if (!create || freeSlot < 0) return NULL;
    s_guardShots[freeSlot].prop = prop;
    s_guardShots[freeSlot].weapon = prop->weapon;
    s_guardShots[freeSlot].shot = (HfxShot){0};
    return &s_guardShots[freeSlot].shot;
}

int gevrHollywoodGuardFired(const PropRecord *prop)
{
    if (!s_ready || !prop || !prop->parent || !prop->weapon || !gevrHollywoodWeapon(prop->weapon->weaponnum))
        return FALSE;
    if (prop->parent->type == PROP_TYPE_VIEWER)
        return gevrTacticalRemoteWeapon(prop->weapon->model) != NULL;
    if (prop->parent->type != PROP_TYPE_CHR) return FALSE;
    HfxShot *shot = guardShot(prop, TRUE);
    if (!shot) return FALSE;
    /* Some native attacks set their firing flag on every attack tick.
     * Render discrete flashes without filling the particle pool with smoke. */
    if (!shot->serial || g_GlobalTimer - shot->tick >= 6 || shot->item != prop->weapon->weaponnum)
        recordShot(shot, prop->weapon->weaponnum);
    return TRUE;
}

static Gfx *beginSprites(Gfx *g)
{
    gDPPipeSync(g++);
    gSPClearGeometryMode(g++, G_LIGHTING | G_FOG | G_CULL_BOTH | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
    gSPSetGeometryMode(g++, G_SHADE | G_SHADING_SMOOTH | G_ZBUFFER);
    gDPSetCycleType(g++, G_CYC_1CYCLE);
    gDPSetAlphaCompare(g++, G_AC_NONE);
    gDPSetTextureLUT(g++, G_TT_NONE);
    gDPSetTextureLOD(g++, G_TL_TILE);
    gDPSetTexturePersp(g++, G_TP_PERSP);
    gDPSetTextureFilter(g++, G_TF_BILERP);
    gDPSetRenderMode(g++, G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2);
    gDPSetCombineMode(g++, G_CC_MODULATERGBA, G_CC_MODULATERGBA2);
    gSPTexture(g++, 0xffff, 0xffff, 0, G_TX_RENDERTILE, G_ON);
    return g;
}

static Gfx *endSprites(Gfx *g)
{
    gDPPipeSync(g++);
    gSPTexture(g++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetRenderMode(g++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
    return g;
}

static Gfx *quadTint(Gfx *g, int atlas, int frame, const coord3d *center, const coord3d *across,
    const coord3d *up, u8 alpha, u32 tint)
{
    Mtxf pose = {.m = {{0,0,0,0}, {0,0,0,0}, {0,0,1,0}, {0,0,0,1}}};
    for (int axis = 0; axis < 3; ++axis) {
        pose.m[0][axis] = across->f[axis] / 64.f;
        pose.m[1][axis] = up->f[axis] / 64.f;
        pose.m[3][axis] = center->f[axis];
    }
    Mtx *matrix = dynAllocateMatrix();
    guMtxF2L(pose.m, matrix);
    Vtx *v = dynAllocateVertices(4);
    const int cell = FX_TILE_SIZE * 32 / FX_FRAMES;
    /* Fast3D adds half a logical texel when filtering triangles. With the
     * small proxy tile that shifts the atlas by a quarter of a frame. */
    const int filterOffset = 16;
    for (int i = 0; i < 4; ++i) {
        v[i] = (Vtx){0};
        v[i].v.ob[0] = i & 1 ? 64 : -64;
        v[i].v.ob[1] = i & 2 ? -64 : 64;
        v[i].v.tc[0] = frame * cell + ((i & 1) ? cell - 2 : 2) - filterOffset;
        v[i].v.tc[1] = ((i & 2) ? FX_TILE_SIZE * 32 - 2 : 2) - filterOffset;
        v[i].v.cn[0] = (u8)(tint >> 16);
        v[i].v.cn[1] = (u8)(tint >> 8);
        v[i].v.cn[2] = (u8)tint;
        v[i].v.cn[3] = alpha;
    }
    gDPLoadTextureBlock(g++, s_keys[atlas], G_IM_FMT_RGBA, G_IM_SIZ_16b, FX_TILE_SIZE, FX_TILE_SIZE,
        0, G_TX_CLAMP, G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gSPMatrix(g++, osVirtualToPhysical(matrix), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_PUSH);
    gSPVertex(g++, osVirtualToPhysical(v), 4, 0);
    gSP2Triangles(g++, 0, 1, 2, 0, 2, 1, 3, 0);
    gSPPopMatrix(g++, G_MTX_MODELVIEW);
    return g;
}

static Gfx *quad(Gfx *g, int atlas, int frame, const coord3d *center, const coord3d *across,
    const coord3d *up, u8 alpha)
{
    return quadTint(g, atlas, frame, center, across, up, alpha, 0xffffff);
}

static void emitParticle(HfxParticle particle)
{
    particle.birth = g_GlobalTimer;
    s_particles[s_next++ % FX_PARTICLES] = particle;
}

void gevrHollywoodCigaretteSmoke(const coord3d *tip)
{
    if (!s_atlases[FX_SMOKE]) return;
    const float drift = .031f*g_GlobalTimer + .012f*tip->x;
    HfxParticle smoke = {.origin = *tip, .velocity = {.f = {9.f*cosf(drift), 22.f, 9.f*sinf(drift)}},
        .atlas = FX_SMOKE, .life = 44, .radius = .6f, .growth = 4.2f, .alpha = 82,
        .roll = drift, .spin = .7f, .stretch = 1.25f, .gravity = -7.f, .tint = 0xdde2e0};
    emitParticle(smoke);
}

static float vectorLength(const f32 f[3])
{
    return sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
}

static Gfx *drawMuzzle(Gfx *gdl, HfxShot *shot, const Mtxf *frame,
    const GevrGearAssembly *assembly, int held)
{
    const f32 (*pose)[4] = frame->m;
    const coord3d tip = {.f = {pose[3][0], pose[3][1], pose[3][2]}};
    coord3d forward, along, center;
    const float length = vectorLength(pose[2]);
    const float scale = D_800364CC;
    if (!(length > 1e-6f) || !(scale > 1e-6f) || !isfinite(tip.x)
        || !isfinite(tip.y) || !isfinite(tip.z) || tip.z > -1.f || tip.z < -9000.f) return gdl;
    for (int i = 0; i < 3; ++i) forward.f[i] = pose[2][i] / length;

    const int item = shot->item;
    const int suppressed = item == ITEM_WPPKSIL || item == ITEM_MP5KSIL
        || gevrTacticalGearAssemblySuppressed(assembly);
    const int kind = gevrTacticalItem(item);
    const int launcher = item == ITEM_ROCKETLAUNCH || item == ITEM_GRENADELAUNCH
        || item == ITEM_TACTICAL_M32A1 || item == ITEM_TANKSHELLS;
    const float base = item == ITEM_TANKSHELLS ? 2.65f : kind >= 0 ? gevrWeaponBaseFlash(gevrTacticalGearKey(kind))
        : launcher || item == ITEM_SHOTGUN || item == ITEM_AUTOSHOT ? 1.18f : .85f;
    const float size = fminf(3.f, fmaxf(.36f, base * (suppressed ? 1.f : gevrTacticalGearFlashScale(assembly))));
    const float units = held ? length * 10.f : scale;
    const unsigned int variant = (shot->serial * 5) % FX_FRAMES;
    if (!shot->smokeSpawned) {
        const float barrelScale = units * fminf(1.9f, size) / scale;
        HfxParticle smoke = {.atlas = FX_SMOKE, .life = launcher ? 32 : suppressed ? 26 : 19,
            .radius = 4.f * barrelScale, .growth = (suppressed ? 12.f : 18.f) * barrelScale,
            .alpha = suppressed ? 150 : 130, .roll = .24f * variant};
        smoke.origin = (coord3d){.f = {tip.x/scale, tip.y/scale, tip.z/scale}};
        smoke.velocity = forward;
        mtx4TransformVecInPlace(currentPlayerGetViewToWorldMtxf(), &smoke.origin);
        mtx4RotateVecInPlace(currentPlayerGetViewToWorldMtxf(), &smoke.velocity);
        for (int i = 0; i < 3; ++i) smoke.velocity.f[i] *= (suppressed ? 23.f : 52.f) * barrelScale;
        smoke.velocity.y += 21.f * barrelScale;
        emitParticle(smoke);
        shot->smokeSpawned = TRUE;
        if (sysArgCheck("--input-script"))
            sysLogPrintf(LOG_NOTE, "HollywoodFX: %s item %d %s muzzle size %.2f at view %.1f %.1f %.1f",
                held ? "player" : item == ITEM_TANKSHELLS ? "cannon" : "guard", item,
                suppressed ? "suppressed" : "unsuppressed", size, tip.x, tip.y, tip.z);
    }

    const int age = g_GlobalTimer - shot->tick;
    if (age > (suppressed ? 1 : 2)) return gdl;
    const float cm = units * size;
    const float jet = (suppressed ? 5.0f : 12.0f) * cm;
    const float width = (suppressed ? 2.5f : 4.25f) * cm;
    for (int i = 0; i < 3; ++i) {
        along.f[i] = forward.f[i] * jet;
        center.f[i] = tip.f[i] + along.f[i];
    }
    Gfx *list = dynAllocate(64 * sizeof(Gfx));
    Gfx *g = beginSprites(list);
    const u8 alpha = suppressed ? 112 : age == 2 ? 135 : 255;
    for (int strip = 0; strip < (suppressed ? 1 : 2); ++strip) {
        coord3d edge;
        for (int i = 0; i < 3; ++i) edge.f[i] = pose[strip][i] * width / length;
        g = quad(g, suppressed ? FX_DIM_JET : FX_JET, variant, &center, &along, &edge, alpha);
    }
    if (!suppressed) {
        /* A small core across the bore, just clear of the model's end face. */
        const float radius = (3.6f - age * .65f) * cm;
        coord3d across, up;
        for (int i = 0; i < 3; ++i) {
            center.f[i] = tip.f[i] + forward.f[i] * .8f * cm;
            across.f[i] = pose[0][i] * radius / length;
            up.f[i] = pose[1][i] * radius / length;
        }
        g = quad(g, FX_FLASH, variant, &center, &across, &up, alpha);
    }
    g = endSprites(g);
    gSPEndDisplayList(g++);
    gSPDisplayList(gdl++, list);
    return gdl;
}

static int nativeMuzzleFrame(const Model *model, int firstperson, Mtxf *out)
{
    if (!model || !model->obj || !model->render_pos || !model->obj->numMatrices) return FALSE;
    const ModelFileHeader *header = model->obj;
    const ModelNode *node = header->numSwitches > (firstperson ? 3 : 0)
        ? header->Switches[firstperson ? 3 : 0] : NULL;
    if (!node || !node->Data || (!firstperson && (node->Opcode & 255) != MODELNODE_OPCODE_GUNFIRE))
        return FALSE;
    const int index = firstperson ? 0 : modelFindNodeMtxIndex((ModelNode *)node, 0);
    if (index < 0 || index >= header->numMatrices) return FALSE;
    const Mtxf *pose = &model->render_pos[index].pos;
    const f32 *at = firstperson ? (const f32 *)node->Data : node->Data->Gunfire.Offset.f;
    *out = *pose;
    for (int k = 0; k < 3; ++k) {
        out->m[3][k] += at[0] * pose->m[0][k] + at[1] * pose->m[1][k] + at[2] * pose->m[2][k];
        if (!firstperson) {
            out->m[0][k] = -pose->m[1][k];
            out->m[1][k] = pose->m[2][k];
            out->m[2][k] = -pose->m[0][k];
        }
    }
    return TRUE;
}

Gfx *gevrHollywoodPlayerMuzzle(Gfx *gdl, int hand, int item, const Model *model)
{
    HfxShot *shot = &s_shots[get_cur_playernum()][hand];
    if (shot->item != item || !pendingShot(shot)) return gdl;
    const GevrGearAssembly *assembly = NULL;
    Mtxf frame;
    if (gevrTacticalMeshMuzzleFrame(model, item, frame.m, &assembly)
        || nativeMuzzleFrame(model, TRUE, &frame))
        return drawMuzzle(gdl, shot, &frame, assembly, TRUE);
    return gdl;
}

Gfx *gevrHollywoodGuardMuzzle(Gfx *gdl, const PropRecord *prop, const Model *detailed)
{
    if (!s_ready || !prop || !prop->weapon) return gdl;
    const Model *model = prop->weapon->model;
    const GevrVisualWeapon *remote = prop->parent && prop->parent->type == PROP_TYPE_VIEWER
        ? gevrTacticalRemoteWeapon(model) : NULL;
    HfxShot *shot = guardShot(prop, remote != NULL);
    if (!shot) return gdl;
    if (remote && (u16)shot->serial != remote->shots) {
        if (shot->item == remote->item) {
            recordShot(shot, remote->item);
        } else {
            /* Equipping and joining already carry the last shot serial. */
            shot->item = remote->item;
            shot->tick = g_GlobalTimer - FX_MUZZLE_TICKS;
        }
        shot->serial = remote->shots;
    } else if (remote && !shot->item) {
        shot->item = remote->item;
    }
    if (shot->item != prop->weapon->weaponnum || !pendingShot(shot)) return gdl;
    const GevrGearAssembly *assembly = NULL;
    Mtxf frame;
    if ((detailed && nativeMuzzleFrame(detailed, TRUE, &frame))
        || gevrTacticalMeshMuzzleFrame(model, shot->item, frame.m, &assembly)
        || nativeMuzzleFrame(model, FALSE, &frame))
        return drawMuzzle(gdl, shot, &frame, assembly, FALSE);
    return gdl;
}

void gevrHollywoodTankFired(const f32 origin[3], const f32 direction[3])
{
    if (!gevrHollywoodWeapon(ITEM_TANKSHELLS)) return;
    const float length = vectorLength(direction);
    if (!(length > 1e-6f)) return;
    Mtxf frame = {.m = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}}};
    const float horizontal = sqrtf(direction[0] * direction[0] + direction[2] * direction[2]);
    for (int k = 0; k < 3; ++k) {
        frame.m[2][k] = direction[k] / length;
        frame.m[3][k] = origin[k];
    }
    if (horizontal > 1e-6f) {
        frame.m[0][0] = direction[2] / horizontal;
        frame.m[0][2] = -direction[0] / horizontal;
    }
    frame.m[1][0] = frame.m[2][1] * frame.m[0][2];
    frame.m[1][1] = frame.m[2][2] * frame.m[0][0] - frame.m[2][0] * frame.m[0][2];
    frame.m[1][2] = -frame.m[2][1] * frame.m[0][0];
    s_tankMuzzle = frame;
    recordShot(&s_tankShot, ITEM_TANKSHELLS);
}

static void groundDust(const f32 point[3], const coord3d *normal, const f32 direction[3], int material)
{
    const int loose = material == HIT_DIRT || material == HIT_MUD || material == HIT_SNOW;
    const u32 tint = material == HIT_DIRT ? 0xd1c7b1 : material == HIT_MUD ? 0xb6a38c
        : material == HIT_SNOW ? 0xe9f0f3 : 0xd9d7cd;
    const float dot = normal->x * direction[0] + normal->y * direction[1] + normal->z * direction[2];
    coord3d tangent = {.f = {direction[0] - normal->x * dot, direction[1] - normal->y * dot,
        direction[2] - normal->z * dot}};
    const coord3d side = {.f = {normal->y * tangent.z - normal->z * tangent.y,
        normal->z * tangent.x - normal->x * tangent.z, normal->x * tangent.y - normal->y * tangent.x}};
    const int phase = s_next & 7;
    HfxParticle plume = {.atlas = FX_SMOKE, .life = loose ? 59 : 52, .radius = loose ? 22.f : 18.f,
        .growth = loose ? 44.f : 36.f, .alpha = loose ? 245 : 223, .stretch = .85f, .tint = tint,
        .roll = .27f * (phase - 3), .spin = phase & 1 ? .55f : -.55f, .gravity = 19.f};
    for (int k = 0; k < 3; ++k) {
        plume.origin.f[k] = point[k] + normal->f[k] * 8.f;
        plume.velocity.f[k] = normal->f[k] * 53.f + tangent.f[k] * 18.f;
    }
    emitParticle(plume);
    for (int i = -1; i <= 1; i += 2) {
        HfxParticle skirt = plume;
        skirt.atlas = FX_DUST;
        skirt.life = 41 + i * 4;
        skirt.radius = 21.f + i * 2.f;
        skirt.growth = loose ? 46.f : 39.f;
        skirt.stretch = .52f;
        skirt.alpha = 211;
        skirt.gravity = 0.f;
        skirt.roll = i * (.22f + phase * .027f);
        skirt.spin = -.35f * i;
        for (int k = 0; k < 3; ++k) {
            skirt.origin.f[k] = point[k] + normal->f[k] * 5.7f + side.f[k] * 9.f * i;
            skirt.velocity.f[k] = normal->f[k] * 18.f + tangent.f[k] * (32.f + 10.f * i)
                + side.f[k] * 52.f * i;
        }
        emitParticle(skirt);
    }
    for (int i = 0; i < 4; ++i) {
        HfxParticle grit = plume;
        grit.atlas = FX_WOOD;
        grit.variant = (phase + i * 3) % FX_FRAMES;
        grit.radius = 1.15f + .28f * i;
        grit.growth = 0.f;
        grit.stretch = .65f;
        grit.alpha = 177;
        grit.life = 16 + i * 2;
        grit.roll = i * 1.1f + phase * .6f;
        const float spread = (i - 1.5f) * (29.f + phase * 3.f);
        grit.velocity.x = tangent.x * 65.f + side.x * spread + (i & 1 ? 18.f : -18.f);
        grit.velocity.y = 65.f + i * 20.f;
        grit.velocity.z = tangent.z * 65.f + side.z * spread;
        emitParticle(grit);
    }
    if (sysArgCheck("--input-script"))
        sysLogPrintf(LOG_NOTE, "HollywoodFX: layered ground dust (material %d) at %.1f %.1f %.1f",
            material, point[0], point[1], point[2]);
}

int gevrHollywoodImpact(int item, int texture, const f32 point[3], const f32 normal[3], const f32 direction[3])
{
    if (!gevrHollywoodWeapon(item) || !isfinite(point[0]) || !isfinite(point[1]) || !isfinite(point[2])) return FALSE;
    const int material = texture >= 0 && texture < gevrTextureCount() ? g_Textures[texture].hitTexture : HIT_DEFAULT;
    if (material == HIT_WATER || material == HIT_CHR) return FALSE;
    const int metal = material == HIT_METAL || material == HIT_METALOBJ;
    const int wood = material == HIT_WOOD;
    const int glass = material == HIT_GLASS || material == HIT_GLASS_XLU;
    const int soil = material == HIT_DIRT || material == HIT_MUD || material == HIT_SNOW;
    const f32 *source = normal ? normal : direction;
    const float len = vectorLength(source);
    if (!isfinite(len) || len < 1e-6f) return FALSE;
    const float dot = source[0]*direction[0] + source[1]*direction[1] + source[2]*direction[2];
    const float side = (dot > 0.f ? -1.f : 1.f) / len;
    if (normal && !metal && !glass && !wood && normal[1] * side > .61f) {
        const coord3d face = {.f = {normal[0] * side, normal[1] * side, normal[2] * side}};
        groundDust(point, &face, direction, material);
        return TRUE;
    }
    /* Wall and prop replacements still use the individual material effects. */
    if (item != ITEM_TACTICAL_M4A1) return FALSE;
    HfxParticle puff = {.atlas = metal || glass ? FX_SPARK : wood || soil ? FX_DUST : FX_CONCRETE,
        .life = metal || glass ? 10 : 27, .radius = metal ? 12.f : glass ? 8.f : 10.f,
        .growth = metal || glass ? 11.f : 19.f, .alpha = glass ? 145 : 235};
    for (int i = 0; i < 3; ++i) {
        puff.origin.f[i] = point[i] + 2.8f * side * source[i];
        puff.velocity.f[i] = (metal || glass ? 31.f : 43.f) * side * source[i];
    }
    puff.velocity.y += 9.f;
    emitParticle(puff);
    if (wood) {
        const unsigned int variant = s_next;
        for (int i = 0; i < 3; ++i) {
            HfxParticle chip = puff;
            chip.atlas = FX_WOOD;
            chip.variant = (variant + i * 3) % FX_FRAMES;
            chip.radius = 1.8f + .42f * i;
            chip.growth = 0.f;
            chip.life = 13 + 2 * i;
            chip.velocity.x += 13.f * (i - 1);
            chip.velocity.y += 26.f + 7.f * i;
            chip.velocity.z -= 10.f * (i - 1);
            chip.roll = .83f * (i - 1);
            emitParticle(chip);
        }
    }
    if (sysArgCheck("--input-script")) {
        sysLogPrintf(LOG_NOTE, "HollywoodFX: %s impact (material %d) at %.1f %.1f %.1f",
            s_names[puff.atlas], material, point[0], point[1], point[2]);
    }
    return TRUE;
}

int gevrBloodyCombatEffects(void)
{
    return s_bloodReady && (gevrTacticalGearEnabled() || gevrTacticalLootEnabled() || (VrBloodPatch && !netIsActive()));
}

/* Hit points returned by the BSP tracer are in room scale. Test the hit
 * room first, plus the rooms the ray actually crosses, and choose the
 * closest surface. That keeps sprays out of the next room through a wall. */
static int bloodSurface(coord3d from, coord3d to, int room, HitThing *nearest)
{
    if (room <= 0 || room >= MAXROOMCOUNT) return FALSE;
    s32 candidates[8] = {room};
    s32 count = 0;
    if (g_BgPortals) {
        u8 rooms[16], finalRooms[16];
        memset(rooms, 255, sizeof(rooms));
        rooms[0] = room;
        bgFindRoomsAlongSegment(&from, &to, rooms, finalRooms, candidates + 1, &count, 7);
    }
    const float roomScale = get_room_data_float1();
    const coord3d ray = {.f = {to.x - from.x, to.y - from.y, to.z - from.z}};
    const float range2 = ray.x*ray.x + ray.y*ray.y + ray.z*ray.z;
    if (roomScale <= 0.f || range2 < 1e-4f) return FALSE;
    const float invScale = 1.f / roomScale;
    float best = INFINITY;
    for (int i = 0; i <= count; ++i) {
        const int candidate = candidates[i];
        if (candidate <= 0 || candidate >= MAXROOMCOUNT || (i && candidate == room)) continue;
        HitThing hit;
        if (!bgTestBulletHitBackground(&from, &to, candidate, &hit)) continue;
        float distance = 0.f, along = 0.f;
        for (int axis = 0; axis < 3; ++axis) {
            hit.hitpos.f[axis] *= invScale;
            const float delta = hit.hitpos.f[axis] - from.f[axis];
            distance += delta * delta;
            along += delta * ray.f[axis];
        }
        /* The gun tracer can report hits beyond its requested endpoint. */
        if (along >= 0.f && along <= range2 * 1.002f && distance < best) {
            best = distance;
            *nearest = hit;
        }
    }
    return isfinite(best);
}

static int bloodMistAtlas(void)
{
    return s_atlases[FX_BLOOD_MIST] ? FX_BLOOD_MIST : FX_BLOOD_SPRAY;
}

static void bloodMark(coord3d center, coord3d normal, const coord3d *direction, float radius)
{
    const float length = vectorLength(normal.f);
    if (!isfinite(length) || length < 1e-6f) return;
    const float facing = normal.x * direction->x + normal.y * direction->y + normal.z * direction->z;
    const float scale = (facing > 0.f ? -1.f : 1.f) / length;
    for (int i = 0; i < 3; ++i) {
        normal.f[i] *= scale;
    }
    const int pool = normal.y > .66f && direction->y < 0.f;
    if (pool) {
        const unsigned count = s_nextMark < FX_BLOOD_MARKS ? s_nextMark : FX_BLOOD_MARKS;
        for (unsigned j = 0; j < count; ++j) {
            BloodMark *mark = &s_marks[(s_nextMark - 1 - j) % FX_BLOOD_MARKS];
            if (!mark->pool || g_GlobalTimer - mark->wetted > FX_BLOOD_MARK_LIFE
                || fabsf(mark->center.y - center.y) > 3.f) continue;
            const float dx = mark->center.x - center.x, dz = mark->center.z - center.z;
            if (dx*dx + dz*dz > 21.f * 21.f) continue;
            mark->radius = fminf(57.f, sqrtf(mark->radius * mark->radius + radius * radius * .68f));
            mark->wetted = g_GlobalTimer;
            if (sysArgCheck("--input-script"))
                sysLogPrintf(LOG_NOTE, "BloodyCombat: floor pool grew to %.1f cm radius", mark->radius);
            return;
        }
    }

    coord3d across = {.f = {normal.z, 0.f, -normal.x}};
    if (fabsf(normal.y) > .93f) across = (coord3d){.f = {normal.y, -normal.x, 0.f}};
    const float axisLength = vectorLength(across.f);
    for (int i = 0; i < 3; ++i) across.f[i] /= axisLength;
    coord3d down = {.f = {normal.y * across.z - normal.z * across.y,
        normal.z * across.x - normal.x * across.z, normal.x * across.y - normal.y * across.x}};
    const float angle = .96f * (s_nextMark % 13);
    const float cosine = cosf(angle), sine = sinf(angle);
    for (int i = 0; i < 3; ++i) {
        const float x = across.f[i], y = down.f[i];
        across.f[i] = x * cosine + y * sine;
        down.f[i] = (y * cosine - x * sine) * (pool ? .77f : .9f);
    }
    const unsigned index = s_nextMark++;
    s_marks[index % FX_BLOOD_MARKS] = (BloodMark){
        .center = center, .across = across, .down = down, .normal = normal, .radius = radius,
        .birth = g_GlobalTimer, .wetted = g_GlobalTimer, .frame = index % FX_FRAMES, .pool = pool
    };
    if (sysArgCheck("--input-script"))
        sysLogPrintf(LOG_NOTE, "BloodyCombat: %s landing at %.1f %.1f %.1f, radius %.1f",
            pool ? "floor pool" : "surface spatter", center.x, center.y, center.z, radius);
}

static BloodFlight *launchBlood(coord3d origin, coord3d velocity, float width, int room)
{
    const unsigned id = s_nextFlight++;
    s_flights[id % FX_BLOOD_FLIGHTS] = (BloodFlight){
        .origin = origin, .velocity = velocity, .width = width, .room = room,
        .birth = g_GlobalTimer, .active = TRUE, .variant = (id * 5) % FX_FRAMES
    };
    return &s_flights[id % FX_BLOOD_FLIGHTS];
}

static coord3d bloodPosition(const BloodFlight *flight, float time)
{
    coord3d p = flight->origin;
    for (int k = 0; k < 3; ++k) p.f[k] += flight->velocity.f[k] * time;
    p.y -= .5f * FX_BLOOD_GRAVITY * time * time;
    return p;
}

static float bloodDistance2(const coord3d *a, const coord3d *b)
{
    const float x = a->x - b->x, y = a->y - b->y, z = a->z - b->z;
    return x*x + y*y + z*z;
}

static int bloodDecalSurface(const HitThing *collision, int room, HitThing *surface)
{
    const float length = vectorLength(collision->normal.f);
    if (!isfinite(length) || length < 1e-6f) return FALSE;
    const float reach = 4.f;
    coord3d from, to, along;
    for (int axis = 0; axis < 3; ++axis) {
        along.f[axis] = -collision->normal.f[axis] / length;
        from.f[axis] = collision->hitpos.f[axis] - reach * along.f[axis];
        to.f[axis] = collision->hitpos.f[axis] + reach * along.f[axis];
    }
    if (!bloodSurface(from, to, room, surface)) return FALSE;
    const float alignment = along.x * surface->normal.x + along.y * surface->normal.y + along.z * surface->normal.z;
    return fabsf(alignment) >= .8f * vectorLength(surface->normal.f);
}

static int bloodWorldContact(const BloodFlight *flight, coord3d from, coord3d to, HitThing *hit, int *canStamp)
{
    int room = flight->room;
    if (room <= 0 || room >= MAXROOMCOUNT) {
        StandTile *tile = stanFindTileBelowPos(&from, NULL, NULL);
        room = tile ? getTileRoom(tile) : -1;
    }
    *canStamp = FALSE;
    hit->texturenum = -1;
    int atPhysicsSurface = FALSE;
    if (pdphysIsReady()) {
        int hitRoom = -1;
        atPhysicsSurface = pdphysRaycastAnyRoom(from.f, to.f, hit->hitpos.f, hit->normal.f, &hitRoom);
        if (atPhysicsSurface && hitRoom > 0) room = hitRoom;
    }
    HitThing visible;
    if ((!pdphysIsReady() || atPhysicsSurface) && bloodSurface(from, to, room, &visible)
        && (!atPhysicsSurface || bloodDistance2(&from, &visible.hitpos) <= bloodDistance2(&from, &hit->hitpos))) {
        *hit = visible;
        *canStamp = TRUE;
        return TRUE;
    }
    if (atPhysicsSurface) {
        if (bloodDecalSurface(hit, room, &visible)) {
            *hit = visible;
            *canStamp = TRUE;
        }
        return TRUE;
    }

    const float drop = from.y - to.y;
    if (drop <= 0.f || room <= 0 || room >= MAXROOMCOUNT) return FALSE;
    coord3d probe = {.f = {to.x, from.y + 1.f, to.z}};
    u8 rooms[2] = {(u8)room, 255};
    f32 floor;
    if (!stanFindTileBelowPos(&probe, rooms, &floor) || floor > from.y || floor < to.y) return FALSE;
    const float t = (from.y - floor) / drop;
    hit->hitpos = (coord3d){.f = {from.x + (to.x - from.x) * t, floor, from.z + (to.z - from.z) * t}};
    hit->normal = (coord3d){.f = {0, 1, 0}};
    if (bloodDecalSurface(hit, room, &visible)) {
        *hit = visible;
        *canStamp = TRUE;
    }
    return TRUE;
}

static void bloodLandingMist(coord3d point, int body)
{
    HfxParticle puff = {.origin = point, .atlas = bloodMistAtlas(), .radius = body ? 9.f : 7.f,
        .growth = 9.f, .life = 13, .velocity = {.f = {0, 24.f, 0}},
        .alpha = body ? 156 : 112, .spin = .45f};
    puff.origin.y += 1.5f;
    emitParticle(puff);
}

static int traceBloodFlight(BloodFlight *flight, coord3d from, coord3d to)
{
    HitThing world;
    int canStamp;
    const int hitWorld = bloodWorldContact(flight, from, to, &world, &canStamp);
    const coord3d delta = {.f = {to.x - from.x, to.y - from.y, to.z - from.z}};
    coord3d bodyHit;
    int handle, part;
    if (flight->traced >= 8 && pdphysIsReady()
        && pdphysRaycastRagdoll(from.f, to.f, .35f, bodyHit.f, &handle, &part)
        && (!hitWorld || bloodDistance2(&from, &bodyHit) < bloodDistance2(&from, &world.hitpos))) {
        const float length = vectorLength(delta.f);
        if (length > 1e-4f) {
            const float direction[3] = {delta.x / length, delta.y / length, delta.z / length};
            gevrRagdollBloodStain(handle, part, bodyHit.f, direction);
        }
        bloodLandingMist(bodyHit, TRUE);
        if (sysArgCheck("--input-script"))
            sysLogPrintf(LOG_NOTE, "BloodyCombat: spray struck a ragdoll at %.1f %.1f %.1f", bodyHit.x, bodyHit.y, bodyHit.z);
        return TRUE;
    }
    if (!hitWorld) return FALSE;
    if (flight->fragmentRadius > 0.f) {
        flight->landed = world.hitpos;
        flight->landed.y += flight->fragmentRadius*.65f;
        flight->landedAt = g_GlobalTimer;
    }
    if (gevrBeanIsEnabled()) {
        const float length = vectorLength(delta.f);
        if (length > 1e-4f) {
            const float dir[3] = {delta.x / length, delta.y / length, delta.z / length};
            const float at = dir[0] * (world.hitpos.x - from.x) + dir[1] * (world.hitpos.y - from.y)
                + dir[2] * (world.hitpos.z - from.z);
            if (gevrBeanWorldShotSurface(from.f, dir, at - 4.f, at + 4.f, world.hitpos.f, world.normal.f))
                canStamp = TRUE;
        }
    }
    if (!canStamp) return TRUE;
    if (world.texturenum >= 0 && world.texturenum < gevrTextureCount()
        && g_Textures[world.texturenum].hitTexture == HIT_WATER) return TRUE;

    const float normalLength = vectorLength(world.normal.f);
    const int wall = normalLength > 0.f && fabsf(world.normal.y) < normalLength * .66f;
    const float radius = wall ? 19.f + 9.f * flight->width : 9.f + 7.f * flight->width;
    bloodMark(world.hitpos, world.normal, &delta, radius);
    if ((flight->variant & 1) == 0) bloodLandingMist(world.hitpos, FALSE);
    return TRUE;
}

void gevrHollywoodTick(void)
{
    if (!s_nextFlight || !gevrBloodyCombatEffects()) return;
    /* Sweep a capped number each simulation tick. Both eyes only read results. */
    int sweeps = 0;
    for (int visit = 0; visit < FX_BLOOD_FLIGHTS && sweeps < FX_BLOOD_SWEEPS_PER_TICK; ++visit) {
        BloodFlight *flight = &s_flights[s_traceFlight++ % FX_BLOOD_FLIGHTS];
        if (!flight->active) continue;
        const int age = g_GlobalTimer - flight->birth;
        if (age - flight->traced < FX_BLOOD_TRACE_INTERVAL) continue;
        const int next = age < FX_BLOOD_FLIGHT_LIFE ? age : FX_BLOOD_FLIGHT_LIFE;
        if (traceBloodFlight(flight, bloodPosition(flight, flight->traced / 60.f),
            bloodPosition(flight, next / 60.f)) || age >= FX_BLOOD_FLIGHT_LIFE) flight->active = FALSE;
        flight->traced = next;
        ++sweeps;
    }
}

int gevrBloodyCombatImpact(int item, int room, const f32 point[3], const f32 direction[3])
{
    const int severed = item == ITEM_TACTICAL_KATANA;
    if (!gevrBloodyCombatEffects() || (!severed && !gevrTacticalIsArmorShot(item))) return FALSE;
    const float length = vectorLength(direction);
    if (!isfinite(length) || length < 1e-6f) return FALSE;
    for (int i = 0; i < 3; ++i)
        if (!isfinite(point[i])) return FALSE;

    const unsigned variant = (s_next * 7) % FX_FRAMES;
    const float side = variant & 1 ? -1.f : 1.f;
    const coord3d shot = {.f = {direction[0] / length, direction[1] / length, direction[2] / length}};
    const coord3d from = {.f = {point[0] - 4.f * shot.x, point[1] - 4.f * shot.y, point[2] - 4.f * shot.z}};
    HfxParticle cloud = {.origin = from, .atlas = bloodMistAtlas(), .life = 26, .radius = 22.f,
        .growth = 33.f, .alpha = 239, .roll = side * (.10f + .055f * variant), .spin = -.68f * side};
    cloud.velocity = (coord3d){.f = {-41.f * shot.x - 24.f * shot.z * side,
        26.f - 31.f * shot.y, -41.f * shot.z + 24.f * shot.x * side}};
    emitParticle(cloud);
    HfxParticle spray = cloud;
    spray.atlas = FX_BLOOD_SPRAY;
    spray.radius = 43.f;
    spray.growth = 18.f;
    spray.life = 18;
    spray.alpha = 245;
    spray.gravity = 37.f;
    spray.velocity.x += -shot.z * 53.f * side;
    spray.velocity.z += shot.x * 53.f * side;
    emitParticle(spray);
    spray.atlas = FX_BLOOD_STREAM;
    spray.radius = 31.f;
    spray.life = 13;
    spray.alpha = 212;
    spray.roll = -.36f * side;
    spray.velocity.x += shot.z * 92.f * side;
    spray.velocity.z -= shot.x * 92.f * side;
    spray.velocity.y -= 18.f;
    emitParticle(spray);

    for (int i = 0; i < 3; ++i) {
        const float lateral = (i - 1) * 67.f + (variant - 3.f) * 8.f;
        const float out = 77.f + i * 36.f;
        const coord3d velocity = {.f = {-shot.x * out - shot.z * lateral, 168.f + i * 17.f,
            -shot.z * out + shot.x * lateral}};
        launchBlood(from, velocity, 1.5f + .35f * i, room);
    }
    /* Give nearby walls behind the impact a separate fan of droplets. */
    const coord3d exit = {.f = {point[0], point[1], point[2]}};
    for (int fan = -1; fan <= 1; fan += 2) {
        const float spread = fan * (40.f + 6.f * variant);
        const coord3d velocity = {.f = {shot.x * 248.f - shot.z * spread,
            108.f + shot.y * 115.f + 17.f * fan, shot.z * 248.f + shot.x * spread}};
        launchBlood(exit, velocity, fan > 0 ? 2.5f : 1.7f, room);
    }
    if (severed) {
        for (int i = 0; i < 8; ++i) {
            const float angle = .80f * i;
            const float speed = 110.f + 19.f * i;
            const coord3d velocity = {.f = {cosf(angle) * speed, 140.f + 13.f*i, sinf(angle) * speed}};
            launchBlood(exit, velocity, 2.5f + .13f*i, room);
        }
    }
    if (sysArgCheck("--input-script"))
        sysLogPrintf(LOG_NOTE, "BloodyCombat: cloud and spray, item %d variant %u at %.1f %.1f %.1f",
            item, variant, point[0], point[1], point[2]);
    return TRUE;
}

void gevrBloodyCombatHeadBurst(int room, const f32 point[3], const f32 direction[3])
{
    if (!gevrBloodyCombatEffects()) return;
    const float length = vectorLength(direction);
    if (!isfinite(length) || length < 1e-6f) return;
    const coord3d at = {.f={point[0], point[1], point[2]}};
    const float angle = atan2f(direction[2], direction[0]);
    for (int i = 0; i < 5; ++i) {
        const float heading = angle + .78f*(i-2);
        const float speed = 145.f+23.f*i;
        const coord3d velocity = {.f={speed*cosf(heading), 102.f+17.f*i, speed*sinf(heading)}};
        BloodFlight *gib = launchBlood(at, velocity, 1.7f+.17f*i, room);
        gib->fragmentRadius = 1.9f + .41f*((i*3)%5);
    }
    HfxParticle mist = {.origin=at, .atlas=bloodMistAtlas(), .life=15, .radius=14.f, .growth=14.f,
        .alpha=190, .velocity={.f={direction[0]*38.f/length,23.f,direction[2]*38.f/length}}, .spin=.6f};
    emitParticle(mist);
}

static BloodWound *bloodWound(const Model *model)
{
    if (!s_nextWound || !model) return NULL;
    for (int i = 0; i < FX_BLEEDERS; ++i) {
        BloodWound *wound = &s_wounds[i];
        if (wound->model != model) continue;
        if (g_GlobalTimer - wound->birth < FX_BLEED_LIFE) return wound;
        wound->model = NULL;
        break;
    }
    return NULL;
}

void gevrBloodyCombatFatalHit(Model *model, int matrix, const f32 point[3], const f32 direction[3])
{
    if (!model || !model->obj || !model->render_pos || matrix < 0 || matrix >= model->obj->numMatrices) return;
    Mtxf toLocal;
    matrix_4x4_invert_affine(&model->render_pos[matrix].pos, &toLocal);
    BloodWound *wound = bloodWound(model);
    if (!wound) wound = &s_wounds[s_nextWound++ % FX_BLEEDERS];
    *wound = (BloodWound){.model = model, .matrix = matrix, .birth = g_GlobalTimer,
        .next = g_GlobalTimer + 4, .local = {.f = {point[0], point[1], point[2]}},
        .out = {.f = {-direction[0], -direction[1], -direction[2]}}};
    Mtxf *view = camGetWorldToScreenMtxf();
    mtx4TransformVecInPlace(view, &wound->local);
    mtx4RotateVecInPlace(view, &wound->out);
    mtx4TransformVecInPlace(&toLocal, &wound->local);
    mtx4RotateVecInPlace(&toLocal, &wound->out);
}

int gevrBloodyCombatMoveWound(const Model *from, const Model *to)
{
    BloodWound *wound = bloodWound(from);
    if (!wound) return -1;
    wound->model = to;
    return wound->matrix;
}

void gevrBloodyCombatForgetWound(const Model *model)
{
    BloodWound *wound = bloodWound(model);
    if (wound) wound->model = NULL;
}

int gevrBloodyCombatFollowWound(const Model *model, const f32 joint[4][4], int room)
{
    BloodWound *wound = bloodWound(model);
    if (!wound) return FALSE;
    if (!gevrBloodyCombatEffects() || wound->poseTick == g_GlobalTimer) return TRUE;
    const int age = g_GlobalTimer - wound->birth;
    const float strength = 1.f - age / (float)FX_BLEED_LIFE;
    coord3d outlet = wound->local, out = wound->out;
    for (int i = 0; i < 3; ++i) {
        outlet.f[i] = joint[3][i] + wound->local.x * joint[0][i] + wound->local.y * joint[1][i] + wound->local.z * joint[2][i];
        out.f[i] = wound->out.x * joint[0][i] + wound->out.y * joint[1][i] + wound->out.z * joint[2][i];
    }
    const float length = vectorLength(out.f);
    if (!isfinite(length) || length < 1e-6f) return TRUE;
    for (int i = 0; i < 3; ++i) {
        out.f[i] /= length;
        outlet.f[i] += out.f[i] * 1.7f;
    }
    wound->poseTick = g_GlobalTimer;
    if (g_GlobalTimer < wound->next) return TRUE;
    wound->next = g_GlobalTimer + (age < 58 ? 4 : 6);
    const float horizontal = sqrtf(out.x * out.x + out.z * out.z);
    const float phase = wound->drops * .82f + wound->matrix * 1.17f;
    const float heading = horizontal > .025f ? atan2f(out.z, out.x) + .69f * sinf(phase) : phase;
    const float pressure = .5f + .5f * cosf(age * .29f);
    const float speed = 77.f + 65.f * strength + pressure * 24.f;
    const coord3d velocity = {.f = {cosf(heading) * speed, 124.f + strength * 80.f + pressure * 15.f,
        sinf(heading) * speed}};
    launchBlood(outlet, velocity, 1.5f + 1.55f * strength, room);
    if (age >= 20 && (wound->drops & 3) == 1) {
        const coord3d runoff = {.f = {-sinf(heading) * 14.f, velocity.y * .65f, cosf(heading) * 14.f}};
        launchBlood(outlet, runoff, 1.2f, room);
    }
    if (sysArgCheck("--input-script") && (wound->drops == 0 || wound->drops % 12 == 0))
        sysLogPrintf(LOG_NOTE, "BloodyCombat: wound arc at tick %d, rise %.1f cm from %.1f %.1f %.1f",
            age, velocity.y * velocity.y / (2.f * FX_BLOOD_GRAVITY), outlet.x, outlet.y, outlet.z);
    ++wound->drops;
    return TRUE;
}

void gevrBloodyCombatAnimatedWound(const Model *model, int room)
{
    BloodWound *wound = bloodWound(model);
    if (!wound || !model->render_pos) return;
    Mtxf world;
    matrix_4x4_multiply(currentPlayerGetViewToWorldMtxf(), &model->render_pos[wound->matrix].pos, &world);
    gevrBloodyCombatFollowWound(model, world.m, room);
}

static Gfx *beginWorldSprites(Gfx *g, int *begun)
{
    if (*begun) return g;
    *begun = TRUE;
    gSPMatrix(g++, osVirtualToPhysical(currentPlayerGetProjectionMatrix()), G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    return beginSprites(g);
}

void gevrHollywoodAccessory(int mode, const float origin[3], const float direction[3], int room)
{
    if (!mode || room <= 0) return;
    const float distance2 = origin[0]*origin[0] + origin[1]*origin[1] + origin[2]*origin[2];
    if (!isfinite(distance2) || distance2 > 2500.f*2500.f*D_800364CC*D_800364CC) return;
    unsigned slot = s_accessoryCount;
    if (slot == FX_ACCESSORY_BEAMS) {
        float farthest = distance2;
        for (unsigned i = 0; i < FX_ACCESSORY_BEAMS; ++i) {
            const coord3d *at = &s_accessories[i].origin;
            const float d = at->x*at->x + at->y*at->y + at->z*at->z;
            if (d > farthest) { farthest = d; slot = i; }
        }
        if (slot == FX_ACCESSORY_BEAMS) return;
    } else ++s_accessoryCount;
    AccessoryBeam *beam = &s_accessories[slot];
    memcpy(beam->origin.f, origin, sizeof(beam->origin.f));
    memcpy(beam->direction.f, direction, sizeof(beam->direction.f));
    beam->mode = mode;
    beam->room = room;
}

static coord3d accessoryCross(coord3d a, coord3d b)
{
    return (coord3d){.f = {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x}};
}

static int accessoryUnit(coord3d *axis)
{
    const float length = vectorLength(axis->f);
    if (!isfinite(length) || length < 1e-6f) return FALSE;
    for (int i = 0; i < 3; ++i) axis->f[i] /= length;
    return TRUE;
}

static Gfx *accessoryFrame(Gfx *g, coord3d centre, coord3d x, coord3d y, float width, float length)
{
    Mtxf pose = {.m = {{1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {0,0,0,1}}};
    for (int i = 0; i < 3; ++i) {
        pose.m[0][i] = x.f[i] * width / 256.f;
        pose.m[1][i] = y.f[i] * length / 256.f;
        pose.m[3][i] = centre.f[i];
    }
    Mtx *matrix = dynAllocateMatrix();
    guMtxF2L(pose.m, matrix);
    gSPMatrix(g++, osVirtualToPhysical(matrix), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_PUSH);
    return g;
}

static Vtx accessoryVertex(int x, int y, int light, int alpha)
{
    Vtx v = {0};
    v.v.ob[0] = x;
    v.v.ob[1] = y;
    v.v.cn[0] = 255;
    v.v.cn[1] = light ? 244 : 27;
    v.v.cn[2] = light ? 216 : 22;
    v.v.cn[3] = alpha;
    return v;
}

static Gfx *drawAccessorySpot(Gfx *g, coord3d at, coord3d normal, float radius, int light)
{
    const coord3d vertical = {.f = {0.f, 1.f, 0.f}};
    coord3d across = accessoryCross(normal, vertical);
    if (!accessoryUnit(&across)) across = (coord3d){.f = {1.f, 0.f, 0.f}};
    const coord3d down = accessoryCross(across, normal);
    g = accessoryFrame(g, at, across, down, radius, radius);
    enum { SIDES = 10 };
    Vtx *vertices = dynAllocateVertices(1 + 2*SIDES);
    vertices[0] = accessoryVertex(0, 0, light, light ? 166 : 254);
    for (int i = 0; i < SIDES; ++i) {
        const float angle = (M_TAU_F / SIDES) * i;
        const float x = cosf(angle), y = sinf(angle);
        vertices[1+i] = accessoryVertex(lroundf(103.f*x), lroundf(103.f*y), light, light ? 87 : 180);
        vertices[1+SIDES+i] = accessoryVertex(lroundf(256.f*x), lroundf(256.f*y), light, 0);
    }
    gSPVertex(g++, osVirtualToPhysical(vertices), 1 + 2*SIDES, 0);
    for (int i = 1; i <= SIDES; ++i) {
        const int next = i == SIDES ? 1 : i + 1;
        gSP1Triangle(g++, 0, i, next, 0);
        gSP2Triangles(g++, i, i+SIDES, next, 0, next, i+SIDES, next+SIDES, 0);
    }
    gSPPopMatrix(g++, G_MTX_MODELVIEW);
    return g;
}

static Gfx *drawAccessories(Gfx *g, Mtxf *view, float scale)
{
    Mtxf *toWorld = currentPlayerGetViewToWorldMtxf();
    if (!toWorld || scale <= 1e-6f) return g;
    int begun = FALSE;
    for (unsigned i = 0; i < s_accessoryCount; ++i) {
        const AccessoryBeam *beam = &s_accessories[i];
        coord3d start = beam->origin, end, direction = beam->direction;
        for (int k = 0; k < 3; ++k) start.f[k] /= scale;
        mtx4TransformVecInPlace(toWorld, &start);
        mtx4RotateVecInPlace(toWorld, &direction);
        if (!accessoryUnit(&direction)) continue;
        for (int k = 0; k < 3; ++k) end.f[k] = start.f[k] + direction.f[k] * 1800.f;
        HitThing wall;
        const int hit = bloodSurface(start, end, beam->room, &wall);
        if (!hit && !(beam->mode & GEVR_DEVICE_LASER)) continue;
        coord3d tip = hit ? wall.hitpos : end;
        const float distance = sqrtf(bloodDistance2(&tip, &start));
        if (distance < .4f) continue;
        mtx4TransformVecInPlace(view, &tip);
        for (int k = 0; k < 3; ++k) tip.f[k] *= scale;
        if (!begun) {
            g = beginWorldSprites(g, &begun);
            gDPPipeSync(g++);
            gSPTexture(g++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
            gDPSetCombineMode(g++, G_CC_SHADE, G_CC_SHADE);
            gSPSetGeometryMode(g++, G_SHADE | G_SHADING_SMOOTH);
        }
        if (beam->mode & GEVR_DEVICE_LASER) {
            coord3d line, middle;
            for (int k = 0; k < 3; ++k) {
                line.f[k] = tip.f[k] - beam->origin.f[k];
                middle.f[k] = .5f * (tip.f[k] + beam->origin.f[k]);
            }
            const float length = vectorLength(line.f);
            coord3d side = accessoryCross(middle, line);
            if (accessoryUnit(&side) && accessoryUnit(&line)) {
                g = accessoryFrame(g, beam->origin, side, line, .15f * scale, length);
                Vtx *v = dynAllocateVertices(4);
                v[0] = accessoryVertex(-256, 0, 0, 132);
                v[1] = accessoryVertex(256, 0, 0, 132);
                v[2] = accessoryVertex(-256, 256, 0, 66);
                v[3] = accessoryVertex(256, 256, 0, 66);
                gSPVertex(g++, osVirtualToPhysical(v), 4, 0);
                gSP2Triangles(g++, 0, 1, 2, 0, 2, 1, 3, 0);
                gSPPopMatrix(g++, G_MTX_MODELVIEW);
            }
        }
        if (hit && accessoryUnit(&wall.normal)) {
            const float dot = direction.x*wall.normal.x + direction.y*wall.normal.y + direction.z*wall.normal.z;
            mtx4RotateVecInPlace(view, &wall.normal);
            for (int k = 0; k < 3; ++k) tip.f[k] += scale * (dot > 0.f ? -.06f : .06f) * wall.normal.f[k];
            gDPNoOpTag(g++, 0x56610000);
            gDPSetRenderMode(g++, G_RM_AA_ZB_XLU_DECAL, G_RM_AA_ZB_XLU_DECAL2);
            if (beam->mode & GEVR_DEVICE_LIGHT) {
                const float radius = fminf(250.f, fmaxf(8.f, distance * .21f));
                g = drawAccessorySpot(g, tip, wall.normal, radius * scale, TRUE);
            }
            if (beam->mode & GEVR_DEVICE_LASER)
                g = drawAccessorySpot(g, tip, wall.normal, fmaxf(.32f, distance * .0011f)*scale, FALSE);
            gDPNoOpTag(g++, 0x56610001);
            gDPSetRenderMode(g++, G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2);
        }
    }
    return begun ? endSprites(g) : g;
}

static Gfx *drawBloodFragments(Gfx *g, Mtxf *view, float scale)
{
    int begun = FALSE;
    for (int i = 0; i < FX_BLOOD_FLIGHTS; ++i) {
        const BloodFlight *flight = &s_flights[i];
        if (flight->fragmentRadius <= 0.f) continue;
        const int age = g_GlobalTimer - flight->birth;
        const int settled = !flight->active && flight->landedAt > 0 && g_GlobalTimer - flight->landedAt < 130;
        if (age < 0 || (!settled && !flight->active)) continue;
        coord3d centre = settled ? flight->landed : bloodPosition(flight, age/60.f);
        mtx4TransformVecInPlace(view, &centre);
        if (centre.z > -4.f || centre.z < -1500.f) continue;
        if (!begun) {
            begun = TRUE;
            gDPPipeSync(g++);
            gSPMatrix(g++, osVirtualToPhysical(currentPlayerGetProjectionMatrix()),
                G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
            gSPClearGeometryMode(g++, G_LIGHTING | G_FOG | G_CULL_BOTH | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
            gSPSetGeometryMode(g++, G_SHADE | G_SHADING_SMOOTH | G_ZBUFFER);
            gSPTexture(g++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
            gDPSetCycleType(g++, G_CYC_1CYCLE);
            gDPSetCombineMode(g++, G_CC_SHADE, G_CC_SHADE);
            gDPSetRenderMode(g++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
        }
        const float radius = flight->fragmentRadius*scale*.01f;
        const float theta = .9f*flight->variant + .19f*(settled ? flight->landedAt - flight->birth : age);
        const float c = cosf(theta)*radius, s = sinf(theta)*radius;
        Mtxf pose = {.m={{c,s,0,0},{-s,c,0,0},{0,0,radius,0},
            {centre.x*scale,centre.y*scale,centre.z*scale,1}}};
        Mtx *fixed = dynAllocateMatrix();
        guMtxF2L(pose.m, fixed);
        gSPMatrix(g++, osVirtualToPhysical(fixed), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_PUSH);
        static const s16 shape[6][3] = {{-96,12,6},{9,16,71},{78,0,12},{-14,-19,-52},{10,92,8},{-23,-71,-3}};
        Vtx *v = dynAllocateVertices(6);
        for (int k = 0; k < 6; ++k) {
            v[k] = (Vtx){0};
            memcpy(v[k].v.ob, shape[k], sizeof(shape[k]));
            v[k].v.cn[0] = 83 + 12*k;
            v[k].v.cn[1] = k == 4 ? 40 : 14;
            v[k].v.cn[2] = k == 4 ? 31 : 11;
            v[k].v.cn[3] = 255;
        }
        gSPVertex(g++, osVirtualToPhysical(v), 6, 0);
        for (int k = 0; k < 4; ++k) {
            const int next = (k+1)%4;
            gSP2Triangles(g++, k, next, 4, 0, next, k, 5, 0);
        }
        gSPPopMatrix(g++, G_MTX_MODELVIEW);
    }
    return g;
}

static Gfx *drawBloodFlights(Gfx *g, Mtxf *view, float scale, int *begun)
{
    if (!s_nextFlight || !gevrBloodyCombatEffects()) return g;
    for (int i = 0; i < FX_BLOOD_FLIGHTS; ++i) {
        const BloodFlight *flight = &s_flights[i];
        const int ticks = g_GlobalTimer - flight->birth;
        if (!flight->active || ticks <= 0 || ticks >= FX_BLOOD_FLIGHT_LIFE) continue;
        const float age = ticks / 60.f;
        const int broken = s_atlases[FX_BLOOD_DROP] && age > flight->velocity.y / FX_BLOOD_GRAVITY + .075f;
        /* Parcels curve over the crest, then split into shorter beads as they fall. */
        const float tail = fmaxf(0.f, age - (broken ? .073f : .157f));
        coord3d previous = bloodPosition(flight, tail);
        mtx4TransformVecInPlace(view, &previous);
        for (int n = 1; n <= 2; ++n) {
            coord3d point = bloodPosition(flight, tail + (age - tail) * (.5f * n));
            mtx4TransformVecInPlace(view, &point);
            coord3d center, along;
            for (int k = 0; k < 3; ++k) {
                center.f[k] = (previous.f[k] + point.f[k]) * .5f * scale;
                along.f[k] = (point.f[k] - previous.f[k]) * .55f * scale;
            }
            previous = point;
            if (point.z > -7.f || point.z < -4000.f) continue;
            const float flatLength = sqrtf(along.x*along.x + along.y*along.y);
            if (flatLength < .06f) continue;
            const float thickness = flight->width * scale * fmaxf(.38f, 1.f - age * .57f);
            const float width = broken ? thickness * .74f : thickness;
            const coord3d up = {.f = {-along.y * width / flatLength, along.x * width / flatLength, 0.f}};
            if (broken) {
                const float streak = fminf(1.f, thickness * (1.f + .25f * n) / flatLength);
                for (int k = 0; k < 3; ++k) along.f[k] *= streak;
            }
            g = beginWorldSprites(g, begun);
            g = quad(g, broken ? FX_BLOOD_DROP : FX_BLOOD_RIBBON, (flight->variant + ticks / 4 + n) % FX_FRAMES,
                &center, &along, &up, (u8)(n == 2 ? 246 : 207));
        }
    }
    return g;
}

Gfx *gevrHollywoodDrawWorld(Gfx *g)
{
    const int tankFlash = pendingShot(&s_tankShot);
    const float scale = D_800364CC;
    Mtxf *view = camGetWorldToScreenMtxf();
    if (s_accessoryCount) {
        if (gevrTacticalCampaignRules()) g = drawAccessories(g, view, scale);
        s_accessoryCount = 0;
    }
    if ((!s_next && !s_nextMark && !s_nextFlight && !tankFlash) || !gevrTacticalCampaignRules()) return g;
    if (tankFlash) {
        Mtxf frame;
        matrix_4x4_multiply(view, &s_tankMuzzle, &frame);
        for (int k = 0; k < 3; ++k) frame.m[3][k] *= scale;
        g = drawMuzzle(g, &s_tankShot, &frame, NULL, FALSE);
    }
    const int blood = gevrBloodyCombatEffects();
    if (blood) g = drawBloodFragments(g, view, scale);
    int begun = FALSE;
    if (blood) {
        int decals = FALSE;
        const unsigned int count = s_nextMark < FX_BLOOD_MARKS ? s_nextMark : FX_BLOOD_MARKS;
        for (unsigned int i = s_nextMark - count; i < s_nextMark; ++i) {
            const BloodMark *mark = &s_marks[i % FX_BLOOD_MARKS];
            const int age = g_GlobalTimer - mark->birth;
            const int sinceWet = g_GlobalTimer - mark->wetted;
            if (age < 0 || sinceWet >= FX_BLOOD_MARK_LIFE) continue;
            const float size = mark->radius * (mark->pool ? .60f + .40f * fminf(1.f, age / 48.f) : 1.f);
            coord3d center = mark->center, across = mark->across, down = mark->down, normal = mark->normal;
            mtx4TransformVecInPlace(view, &center);
            if (center.z > -10.f || center.z < -4000.f) continue;
            mtx4RotateVecInPlace(view, &normal);
            const float facing = -(normal.x * center.x + normal.y * center.y + normal.z * center.z)
                / vectorLength(center.f);
            if (facing <= .025f) continue;
            /* Near parallel, tiny depth variations can make a whole stain
             * switch between adjacent pixels. Fade through that narrow angle. */
            const float angle = fminf(1.f, (facing - .025f) / .085f);
            const float visibility = angle * angle * (3.f - 2.f * angle);
            mtx4RotateVecInPlace(view, &across);
            mtx4RotateVecInPlace(view, &down);
            for (int j = 0; j < 3; ++j) {
                /* A sub-millimetre offset clears the quantized room plane.
                 * The depth band still clips the sprite to that surface. */
                center.f[j] = (center.f[j] + normal.f[j] * .065f) * scale;
                across.f[j] *= scale * size;
                down.f[j] *= scale * size;
            }
            const int fade = FX_BLOOD_MARK_LIFE - sinceWet;
            const u8 alpha = (u8)(243.f * visibility * fminf(1.f, fade / 210.f));
            if (!alpha) continue;
            const int atlas = mark->pool && s_atlases[FX_BLOOD_POOL] ? FX_BLOOD_POOL : FX_BLOOD_SPLAT;
            g = beginWorldSprites(g, &begun);
            if (!decals) {
                /* Use the bullet-hole depth band. Its stencil rejects the
                 * part of this quad across an edge or in front of a surface. */
                gDPNoOpTag(g++, 0x56610000); /* VR_IMPACT_DL_BEGIN */
                gDPSetRenderMode(g++, G_RM_AA_ZB_XLU_DECAL, G_RM_AA_ZB_XLU_DECAL2);
                decals = TRUE;
            }
            g = quad(g, atlas, mark->frame, &center, &across, &down, alpha);
        }
        if (decals) {
            gDPNoOpTag(g++, 0x56610001); /* VR_IMPACT_DL_END */
            gDPSetRenderMode(g++, G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2);
        }
    }
    for (unsigned int i = 0; i < FX_PARTICLES; ++i) {
        const HfxParticle *p = &s_particles[(s_next + i) % FX_PARTICLES];
        const int ticks = g_GlobalTimer - p->birth;
        if (!p->life || ticks < 0 || ticks >= p->life) continue;
        const int directionalBlood = p->atlas == FX_BLOOD_SPRAY || p->atlas == FX_BLOOD_STREAM;
        if ((directionalBlood || p->atlas == FX_BLOOD_MIST) && !blood) continue;
        const float at = (float)ticks / p->life;
        const float time = ticks / 60.f;
        coord3d center = p->origin;
        for (int axis = 0; axis < 3; ++axis) center.f[axis] += p->velocity.f[axis] * time;
        center.y -= (p->atlas == FX_WOOD ? 185.f : p->gravity) * time * time;
        mtx4TransformVecInPlace(view, &center);
        if (center.z > -1.f || center.z < -9000.f) continue;
        for (int axis = 0; axis < 3; ++axis) center.f[axis] *= scale;
        const float size = (p->radius + p->growth * at) * scale;
        float angle = p->roll + (p->atlas == FX_WOOD ? 6.f : p->spin) * time;
        if (directionalBlood) {
            coord3d heading = p->velocity;
            heading.y -= p->gravity * time * 2.f;
            mtx4RotateVecInPlace(view, &heading);
            angle += atan2f(heading.y, heading.x);
        }
        const float c = cosf(angle) * size, s = sinf(angle) * size;
        const float width = p->stretch > 0 ? p->stretch : 1.f;
        const coord3d x = {.f = {c, s, 0}}, y = {.f = {-s * width, c * width, 0}};
        if (directionalBlood) {
            /* The animated fluid starts at 22% of the image width. */
            center.x += .56f * c;
            center.y += .56f * s;
        }
        const int frame = p->atlas == FX_WOOD ? p->variant : (int)(FX_FRAMES * at);
        g = beginWorldSprites(g, &begun);
        g = quadTint(g, p->atlas, frame, &center, &x, &y, (u8)(p->alpha * (1.f - .79f * at)),
            p->tint ? p->tint : 0xffffff);
    }
    g = drawBloodFlights(g, view, scale, &begun);
    return begun ? endSprites(g) : g;
}
