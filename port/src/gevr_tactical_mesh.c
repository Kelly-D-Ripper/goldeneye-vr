/* Private, opt-in experimental renderer for the tactical weapon slice.
 * The asset file is user-provided and is never included in the source tree. */
#include <ultra64.h>
#include <PR/gbi.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system.h"
#include "gevr_tactical_scale.h"
#include "gevr_static_mesh.h"
#include "gevr_tactical_fit.h"
#include "gevr_tactical_mesh.h"
#include "gevr_tactical_asset.h"
#include "gevr_tactical_facility.h"
#include "gevr_tactical_heavy_guards.h"
#include "gevr_tactical_gear.h"
#include "gevr_tactical_net.h"
#include "gevr_tactical_optics.h"
#include "gevr_hollywood_fx.h"
#include "gevr_gexmodel.h"
#include "dyn.h"
#include "player.h"

#define TACT_TEX_MAX 32
/* Each gun can have a world header and held headers, alongside mission items. */
#define TACT_MODEL_MAX (2 * TACTICAL_GUN_COUNT + 16)
#define TACT_TRI_MAX 50000
enum {
    TACT_ASSET_SKORPION = 23, TACT_ASSET_SVD, TACT_ASSET_RPG, TACT_ASSET_RPG_ROUND,
    TACT_ASSET_MINE_PROXIMITY, TACT_ASSET_MINE_REMOTE, TACT_ASSET_MINE_TIMED,
    TACT_ASSET_KNIFE, TACT_ASSET_KNIFE_SHEATH, TACT_ASSET_KATANA, TACT_ASSET_KATANA_SCABBARD
};
enum { TACT_VIEW_SKORPION = 5, TACT_VIEW_SVD, TACT_VIEW_RPG, TACT_VIEW_MINE, TACT_VIEW_KNIFE };
enum { TACT_RPG_VIEW = 17, TACT_RPG_PROP, TACT_MINE_VIEW, TACT_MINE_PROP, TACT_KNIFE_VIEW, TACT_KNIFE_PROP,
       TACT_KATANA_VIEW, TACT_KATANA_PROP };
#define TACT_GLOVE_BASE (TACT_ASSET_KATANA_SCABBARD + 1)
#define TACT_GUN_GLOVE_POSES TACTICAL_GLOVE_OPEN
#define TACT_SHARED_GLOVE_BASE (TACT_GLOVE_BASE + TACTICAL_GUN_UZI * TACT_GUN_GLOVE_POSES)
#define TACT_ASSET_KLMK (TACT_SHARED_GLOVE_BASE + TACTICAL_GLOVE_POSE_COUNT - TACT_GUN_GLOVE_POSES)
#define TACT_ASSET_HELMET (TACT_ASSET_KLMK + GEVR_VEST_COUNT - 1)
#define TACT_ASSET_VEST_ARM (TACT_ASSET_HELMET + GEVR_HELMET_COUNT)
#define TACT_VEST_ARM_SETS (GEVR_VEST_REDUT_T5 - GEVR_VEST_ZABRALO + 1)
#define TACT_ASSET_VEST_FIT (TACT_ASSET_VEST_ARM + TACT_VEST_ARM_SETS * 2)
static const struct { int body; GevrTacticalVest vest; const char *path; } s_vestFits[] = {
#define TACTICAL_VEST_FIT(body, id, path) {body, GEVR_VEST_##id, "mods/tactical/" path},
#include "gevr_tactical_vest_fits.inc.h"
#undef TACTICAL_VEST_FIT
};
#define TACT_ASSET_MAX (TACT_ASSET_VEST_FIT + ARRAYCOUNT(s_vestFits))

typedef struct TacticalCorner {
    float p[3], st[2];
    u8 rgba[4], tex;
} TacticalCorner;
typedef struct TacticalPicture { int w, h; u8 *rgba; } TacticalPicture;
typedef struct TacticalAsset {
    int status, ntex, ntri;
    int watchMounted;
    GevrTacticalWatchMount watch;
    TacticalPicture tex[TACT_TEX_MAX];
    TacticalCorner *corners;
} TacticalAsset;
static TacticalAsset s_assets[TACT_ASSET_MAX];
static u16 s_texkey[TACT_ASSET_MAX][TACT_TEX_MAX][16 * 16] __attribute__((aligned(8)));
typedef struct TacticalGpuList {
    GevrStaticMesh *mesh;
    Gfx draw[2][2];
    int attempted;
    unsigned warmed;
} TacticalGpuList;

enum { TACT_CACHE_BODY, TACT_CACHE_GUARD, TACT_CACHE_MAG,
       TACT_CACHE_CORE, TACT_CACHE_GEAR_BODY, TACT_CACHE_INSTALLED_MAG,
       TACT_CACHE_MOUNTED_BODY, TACT_CACHE_MOUNTED_EMPTY, TACT_CACHE_REMOTE_CORE,
       TACT_CACHE_REMOTE_BOLT, TACT_CACHE_REMOTE_HANDLE, TACT_CACHE_COUNT };

typedef struct TacticalModel {
    ModelFileHeader *header;
    ModelNode *nodes[32], *drawNode;
    ModelNode *handNodes[64], *handDrawNode;
    int handCount;
    Gfx pistolHands[8][4];
    int pistolHandBuilt[8];
    int magRemovedLogged;
    int matrix, count, asset, firstperson, expanded;
    Vtx *vertices, *heldVertices, *otherMagVertices, *npcVertices;
    Gfx *list, *noMag, *heldMag, *otherMag, *withOtherMag, *npcList;
    Gfx *gearCore, *gearBody, *gearMagazine;
    Gfx *remoteCore, *remoteBolt, *remoteHandle;
    Gfx *mountedBody[2];
    TacticalGpuList gpuList[TACT_CACHE_COUNT];
    int gearLogged;
    int built, logged;
    float lastAction, lastManualAction;
    int magBoundsReady;
    float magLow[3], magHigh[3];
    int mineNormal;
    float mineLow[3], mineExtent[3];
    int boundsReady, muzzleReady;
    float low[3], high[3], muzzle[3];
    float rigFront[3];
    unsigned used;
} TacticalModel;
static TacticalModel s_models[TACT_MODEL_MAX];
static int s_modelCount;
static TacticalModel s_rpgRound;
static TacticalModel s_knifeSheath;
static int buildTacticalWorldMesh(TacticalModel *model, int asset, const char *path);
static void copyMeshBounds(TacticalModel *model, int count, float low[3], float high[3]);
static TacticalModel s_vests[GEVR_VEST_COUNT];
static TacticalModel s_fittedVests[ARRAYCOUNT(s_vestFits)];
static TacticalModel s_vestArms[TACT_VEST_ARM_SETS][2];
static TacticalModel s_helmets[GEVR_HELMET_COUNT];
static struct HelmetAttachment {
    const Model *model;
    const ModelFileHeader *header;
    const ObjectRecord *hat;
    ModelNode *node;
    int helmet;
} s_helmetAttachments[512];
static int s_helmetAttachmentCount;
static Gfx s_helmetAttachmentLists[GEVR_HELMET_COUNT][2][3];
static struct {
    GevrTacticalHelmet kind;
    const ModelFileHeader *header;
    ModelNode *node;
    Gfx draw[2][3];
} s_helmetShells[] = {
    {GEVR_HELMET_SSH68}, {GEVR_HELMET_SSH68}, {GEVR_HELMET_ALTYN}, {GEVR_HELMET_FAST}
};
typedef struct TacticalGearMesh {
    TacticalAsset mesh;
    TacticalModel model;
} TacticalGearMesh;
static TacticalGearMesh s_gearMeshes[GEVR_GEAR_PARTS];
static u16 s_gearTexKeys[GEVR_GEAR_PARTS][TACT_TEX_MAX][16 * 16] __attribute__((aligned(8)));
static TacticalGearMesh *s_gearOpticMeshes[2][GEVR_GEAR_PARTS];
static u16 s_gearOpticKeys[2][GEVR_GEAR_PARTS][16 * 16] __attribute__((aligned(8)));
static unsigned s_modelClock;
static unsigned s_warmPass, s_warmMeshes, s_warmTextures;
static int s_warmingGear;
extern int gfx_tactical_texture_prepare(const void *address);

extern const TacticalMagazine *gevrTacticalGearModelMagazine(const Model *model, int kind, int *build);
extern bool netIsActive(void);
extern int gevrTacticalInstalledMag(int asset, s32 hand);
extern int gevrTacticalLoadedMagAsset(int asset, s32 hand);
extern float gevrTacticalActionMotion(int asset, s32 hand);
extern float gevrTacticalManualMotion(int asset, s32 hand);
extern int gevrTacticalChamberState(int asset, s32 hand);
extern int gevrTacticalGunTrigger(Model *model);
extern int gevrTacticalGunGloveMirrored(Model *model);
extern int gevrTacticalAk74ModelHeader(ModelFileHeader *header);
extern s32 gevrStereoShot(s32 hand, coord2d *spread, coord3d *origin, coord3d *direction);
extern s32 g_gevrStereo;

static Gfx *buildGearGun(TacticalModel *m, int kind, const GevrGearAssembly *assembly,
                        const TacticalMagazine *magazine, int loadMatrix, float action, float manual,
                        const Mtxf *heldPose, int hand, int lowDetail, const GevrVisualWeapon *remote);
static void alignOpticTestView(Model *model, int kind, int matrix, const GevrGearAssembly *assembly);
static Gfx *expandedMagazinePose(int kind, unsigned part, Mtxf *pose, float low[3], float high[3]);
static int expandedGunBounds(int kind, float low[3], float high[3]);

int gevrTacticalGearCacheEnabled(void)
{
    static int enabled = -1;
    if (enabled < 0)
        enabled = sysArgCheck("--tactical-gear-cache") && sysArgCheck("--tactical-mesh");
    return enabled;
}

static void warmGearTextures(const Gfx *list)
{
    for (const Gfx *g = list; g; ++g) {
        const unsigned op = (unsigned)(g->words.w0 >> 24) & 255;
        if (op == (G_ENDDL & 255)) break;
        if (op == (G_SETTIMG & 255)) s_warmTextures += gfx_tactical_texture_prepare((const void *)g->words.w1);
    }
}

void gevrTacticalCacheBegin(void)
{
    ++s_warmPass;
    s_warmMeshes = s_warmTextures = 0;
    s_warmingGear = TRUE;
}

void gevrTacticalCacheEnd(int stage, u64 begin, int actors)
{
    s_warmingGear = FALSE;
    sysLogPrintf(LOG_NOTE, "tactical gear cache: stage %d, %d actors, %u new GPU meshes, %u texture uploads in %llu ms",
        stage, actors, s_warmMeshes, s_warmTextures, (unsigned long long)((sysGetMicroseconds() - begin) / 1000));
}

static Gfx *cachedRigidList(TacticalGpuList *cache, Gfx *list, int lowDetail)
{
    if (!list || !gevrTacticalGearCacheEnabled()) return list;
    if (!cache->attempted) {
        cache->attempted = 1;
        cache->mesh = gevrStaticMeshBuildGear(list, TRUE);
        if (cache->mesh) {
            for (int lod = 0; lod < 2; ++lod) {
                cache->draw[lod][0].words.w0 = ((uintptr_t)G_GEVR_MESH << 24) | lod;
                cache->draw[lod][0].words.w1 = (uintptr_t)cache->mesh;
                gSPBranchList(&cache->draw[lod][1], list);
            }
        }
    }
    if (s_warmingGear && cache->warmed != s_warmPass) {
        cache->warmed = s_warmPass;
        if (cache->mesh && !cache->mesh->gpu) {
            gfx_static_mesh_prepare(cache->mesh);
            s_warmMeshes += cache->mesh->gpu != NULL;
        }
        warmGearTextures(list);
    }
    return cache->mesh ? cache->draw[!!lowDetail] : list;
}

static void clearTacticalModel(TacticalModel *model)
{
    for (int i = 0; i < TACT_CACHE_COUNT; ++i) gevrStaticMeshFree(model->gpuList[i].mesh);
    free(model->vertices);
    free(model->heldVertices);
    free(model->list);
    free(model->noMag);
    free(model->heldMag);
    free(model->otherMagVertices);
    free(model->otherMag);
    free(model->withOtherMag);
    free(model->npcVertices);
    free(model->npcList);
    free(model->gearCore);
    free(model->gearBody);
    free(model->gearMagazine);
    free(model->remoteCore);
    free(model->remoteBolt);
    free(model->remoteHandle);
    free(model->mountedBody[0]);
    free(model->mountedBody[1]);
    memset(model, 0, sizeof(*model));
}

static int tacticalModelFar(const Model *model, int matrix)
{
    if (!gevrTacticalGearCacheEnabled() || !model->render_pos || matrix < 0 || matrix >= model->obj->numMatrices)
        return FALSE;
    const float *position = model->render_pos[matrix].pos.m[3];
    return position[0]*position[0] + position[1]*position[1] + position[2]*position[2] > 800.f*800.f;
}

static void helmetDrawList(Gfx draw[2][3], GevrTacticalHelmet helmet)
{
    TacticalModel *model = &s_helmets[helmet];
    for (int lod = 0; lod < 2; ++lod) {
        Gfx *g = draw[lod];
        gSPMatrix(g++, (void *)(uintptr_t)0x03000000u, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
        gSPDisplayList(g++, cachedRigidList(&model->gpuList[TACT_CACHE_BODY], model->list, lod));
        gSPEndDisplayList(g++);
    }
}

static int isPlayerGunModel(const Model *model)
{
    return g_CurrentPlayer && (model == &g_CurrentPlayer->hands[GUNRIGHT].weaponModel
        || model == &g_CurrentPlayer->hands[GUNLEFT].weaponModel);
}

static int gevrTacticalMeshGunKind(int asset)
{
    if (asset == 4) return TACTICAL_GUN_MP5;
    if (asset == 5) return TACTICAL_GUN_MP5K;
    if (asset == 21) return TACTICAL_GUN_AK74N;
    if (asset == 22) return TACTICAL_GUN_MAKAROV;
    if (asset == TACT_ASSET_SKORPION) return TACTICAL_GUN_SKORPION;
    if (asset == TACT_ASSET_SVD) return TACTICAL_GUN_SVD;
    if (asset == TACT_ASSET_RPG) return TACTICAL_GUN_RPG;
    if (asset == 10 || asset == 11) return TACTICAL_GUN_USP;
    return asset;
}

static u32 read32(const u8 *p) { u32 v; memcpy(&v, p, 4); return v; }
static float readfloat(const u8 *p) { float v; memcpy(&v, p, 4); return v; }
static int readMeshFile(TacticalAsset *mesh, const char *path, int hasGloveMount, int requiredTextures);

static int loadMesh(int asset)
{
    TacticalAsset *mesh = &s_assets[asset];
    static const char *const files[TACT_GLOVE_BASE] = {
        "mods/tactical/ak74un.bin", "mods/tactical/usp45.bin", "mods/tactical/mp5.bin",
        "mods/tactical/mp5k.bin", "mods/tactical/mp5-world.bin", "mods/tactical/mp5k-world.bin",
        "mods/tactical/hand-open.bin", "mods/tactical/hand-grip.bin",
        "mods/tactical/hand-pinch.bin", "mods/tactical/hand-trigger.bin",
        "mods/tactical/ppk-black.bin", "mods/tactical/ppk-black-suppressed.bin",
        "mods/tactical/hand-support.bin", "mods/tactical/hand-ppk-grip.bin",
        "mods/tactical/hand-ppk-trigger.bin", "mods/tactical/hand-ppk-magazine.bin",
        "mods/tactical/hand-ak-grip.bin", "mods/tactical/hand-ak-trigger.bin",
        "mods/tactical/hand-ak-support.bin", "mods/tactical/hand-ak-magazine.bin",
        "mods/tactical/hand-ak-open.bin", "mods/tactical/ak74n.bin", "mods/tactical/makarov.bin",
        [TACT_ASSET_SKORPION] = "mods/tactical/scorpio61.bin",
        [TACT_ASSET_SVD] = "mods/tactical/svd-wood.bin",
        [TACT_ASSET_RPG] = "mods/tactical/rpg-7v2.bin",
        [TACT_ASSET_RPG_ROUND] = "mods/tactical/rpg-round.bin",
        [TACT_ASSET_MINE_PROXIMITY] = "mods/tactical/source-mine-proximity.bin",
        [TACT_ASSET_MINE_REMOTE] = "mods/tactical/source-mine-remote.bin",
        [TACT_ASSET_MINE_TIMED] = "mods/tactical/source-mine-timed.bin",
        [TACT_ASSET_KNIFE] = "mods/tactical/huntsman.bin",
        [TACT_ASSET_KNIFE_SHEATH] = "mods/tactical/huntsman-sheath.bin",
        [TACT_ASSET_KATANA] = "mods/tactical/katana.bin",
        [TACT_ASSET_KATANA_SCABBARD] = "mods/tactical/katana-scabbard.bin"
    };
    static const char *const gloveGuns[TACTICAL_GUN_COUNT] = {
        "ak74un", "ppk", "mp5", "mp5k", "ak74n", "makarov", "skorpion", "ak74n"
    };
    static const char *const glovePoses[TACTICAL_GLOVE_POSE_COUNT] = {
        "ready", "trigger", "support", "open", "pinch"
    };
    char gloveFile[80];
    const char *path;
    if (asset < TACT_GLOVE_BASE) {
        path = files[asset];
    } else if (asset < TACT_SHARED_GLOVE_BASE) {
        const int index = asset - TACT_GLOVE_BASE;
        snprintf(gloveFile, sizeof(gloveFile), "mods/tactical/%s-glove-%s.bin",
            gloveGuns[index / TACT_GUN_GLOVE_POSES], glovePoses[index % TACT_GUN_GLOVE_POSES]);
        path = gloveFile;
    } else {
        const int pose = TACTICAL_GLOVE_OPEN + asset - TACT_SHARED_GLOVE_BASE;
        snprintf(gloveFile, sizeof(gloveFile), "mods/tactical/ppk-glove-%s.bin", glovePoses[pose]);
        path = gloveFile;
    }
    return readMeshFile(mesh, path, asset >= TACT_GLOVE_BASE, asset == TACT_ASSET_SKORPION ? 3 : 0);
}

static int readMeshFile(TacticalAsset *mesh, const char *path, int hasGloveMount, int requiredTextures)
{
    FILE *f;
    long size;
    u8 *bytes = NULL, *p, *end;
    if (mesh->status) return mesh->status > 0;
    mesh->status = -1;
    f = gevrTacticalOpenAsset(path, "rb");
    if (!f) { sysLogPrintf(LOG_WARNING, "tactical mesh: missing %s", path); return 0; }
    if (fseek(f, 0, SEEK_END) || (size = ftell(f)) < 16 || size > 64 * 1024 * 1024 || fseek(f, 0, SEEK_SET)) goto fail;
    bytes = malloc((size_t)size);
    if (!bytes || fread(bytes, 1, (size_t)size, f) != (size_t)size) goto fail;
    fclose(f); f = NULL;
    p = bytes; end = bytes + size;
    const int watchMounted = hasGloveMount && !memcmp(p, "GEVRWPN2", 8);
    if (!watchMounted && memcmp(p, "GEVRWPN1", 8)) goto fail;
    p += 8; mesh->ntex = (int)read32(p); p += 4;
    if (mesh->ntex < 1 || mesh->ntex > TACT_TEX_MAX) goto fail;
    if (requiredTextures && mesh->ntex != requiredTextures) goto fail;
    for (int t = 0; t < mesh->ntex; t++) {
        size_t n;
        if ((size_t)(end - p) < 4) goto fail;
        mesh->tex[t].w = p[0] | p[1] << 8;
        mesh->tex[t].h = p[2] | p[3] << 8; p += 4;
        if (mesh->tex[t].w < 1 || mesh->tex[t].h < 1 || mesh->tex[t].w > 512 || mesh->tex[t].h > 512) goto fail;
        n = (size_t)mesh->tex[t].w * mesh->tex[t].h * 4;
        if ((size_t)(end - p) < n || !(mesh->tex[t].rgba = malloc(n))) goto fail;
        memcpy(mesh->tex[t].rgba, p, n); p += n;
    }
    if ((size_t)(end - p) < 4) goto fail;
    mesh->ntri = (int)read32(p); p += 4;
    const size_t mountSize = watchMounted ? 9 * 4 : 0;
    if (mesh->ntri < 1 || mesh->ntri > TACT_TRI_MAX || (size_t)(end - p) != (size_t)mesh->ntri * 3 * 28 + mountSize) goto fail;
    mesh->corners = calloc((size_t)mesh->ntri * 3, sizeof(*mesh->corners));
    if (!mesh->corners) goto fail;
    for (int i = 0; i < mesh->ntri * 3; i++, p += 28) {
        TacticalCorner *c = &mesh->corners[i];
        for (int j = 0; j < 3; j++) { c->p[j] = readfloat(p + j * 4); if (!isfinite(c->p[j])) goto fail; }
        for (int j = 0; j < 2; j++) { c->st[j] = readfloat(p + 12 + j * 4); if (!isfinite(c->st[j])) goto fail; }
        memcpy(c->rgba, p + 20, 4); c->tex = p[24];
        if (c->tex >= mesh->ntex || (i % 3 && c->tex != mesh->corners[i - 1].tex)) goto fail;
    }
    if (watchMounted) {
        for (int j = 0; j < 3; ++j) {
            mesh->watch.along[j] = readfloat(p + j * 4);
            mesh->watch.normal[j] = readfloat(p + 12 + j * 4);
            mesh->watch.centre[j] = readfloat(p + 24 + j * 4);
            if (!isfinite(mesh->watch.along[j]) || !isfinite(mesh->watch.normal[j])
                || !isfinite(mesh->watch.centre[j])) goto fail;
        }
        mesh->watchMounted = 1;
    }
    free(bytes); mesh->status = 1;
    sysLogPrintf(LOG_NOTE, "tactical mesh: loaded %s, %d textures, %d triangles", path, mesh->ntex, mesh->ntri);
    return 1;
fail:
    if (f) fclose(f);
    free(bytes); free(mesh->corners); mesh->corners = NULL;
    for (int t = 0; t < TACT_TEX_MAX; t++) { free(mesh->tex[t].rgba); mesh->tex[t].rgba = NULL; }
    sysLogPrintf(LOG_WARNING, "tactical mesh: malformed or unreadable %s", path);
    return 0;
}

int gevrTacticalSkorpionEnabled(void)
{
    return sysArgCheck("--tactical-skorpion") && sysArgCheck("--tactical-mesh")
        && gevrTacticalCampaignRules() && loadMesh(TACT_ASSET_SKORPION);
}

int gevrTacticalSvdEnabled(void)
{
    return sysArgCheck("--tactical-svd") && sysArgCheck("--tactical-mesh")
        && gevrTacticalCampaignRules() && loadMesh(TACT_ASSET_SVD);
}

int gevrTacticalRpgEnabled(void)
{
    return gevrTacticalCampaignRules() && sysArgCheck("--tactical-rpg") && sysArgCheck("--tactical-mesh")
        && loadMesh(TACT_ASSET_RPG) && loadMesh(TACT_ASSET_RPG_ROUND);
}

int gevrTacticalKnifeEnabled(void)
{
    return gevrTacticalCampaignRules() && sysArgCheck("--tactical-huntsman") && sysArgCheck("--tactical-physical")
        && sysArgCheck("--tactical-mesh") && loadMesh(TACT_ASSET_KNIFE) && loadMesh(TACT_ASSET_KNIFE_SHEATH);
}

int gevrTacticalKatanaEnabled(void)
{
    return gevrTacticalCampaignRules() && sysArgCheck("--tactical-katana") && sysArgCheck("--tactical-physical")
        && sysArgCheck("--tactical-mesh") && loadMesh(TACT_ASSET_KATANA)
        && loadMesh(TACT_ASSET_KATANA_SCABBARD);
}

void *gevrTacticalKatanaScabbard(void)
{
    static TacticalModel scabbard;
    if (!gevrTacticalKatanaEnabled() || !buildTacticalWorldMesh(&scabbard, TACT_ASSET_KATANA_SCABBARD,
        "mods/tactical/katana-scabbard.bin")) return NULL;
    return cachedRigidList(&scabbard.gpuList[TACT_CACHE_BODY], scabbard.list, FALSE);
}

void *gevrTacticalKnifeSheathList(void)
{
    if (!gevrTacticalKnifeEnabled()
        || !buildTacticalWorldMesh(&s_knifeSheath, TACT_ASSET_KNIFE_SHEATH, "mods/tactical/huntsman-sheath.bin"))
        return NULL;
    return cachedRigidList(&s_knifeSheath.gpuList[TACT_CACHE_BODY], s_knifeSheath.list, FALSE);
}

static void collect(ModelNode *node, ModelNode **found, int *count, int depth)
{
    for (; node && depth < 64; node = node->Next) {
        int op = node->Opcode & 0xff;
        if (op == MODELNODE_OPCODE_DL && node->Data) {
            int seen = 0;
            for (int i = 0; i < *count; i++) if (found[i] == node) seen = 1;
            if (!seen && *count < 128) found[(*count)++] = node;
        }
        if (node->Data) {
            ModelNode *a = op == MODELNODE_OPCODE_SWITCH ? node->Data->Switch.Controls
                : op == MODELNODE_OPCODE_LOD ? node->Data->LOD.Affects
                : op == MODELNODE_OPCODE_BSP ? node->Data->BSP.leftChild : NULL;
            ModelNode *b = op == MODELNODE_OPCODE_BSP ? node->Data->BSP.rightChild : NULL;
            if (a && a != node->Child) collect(a, found, count, depth + 1);
            if (b && b != node->Child) collect(b, found, count, depth + 1);
        }
        if (node->Child) collect(node->Child, found, count, depth + 1);
    }
}
static int expandedHeaderKind(ModelFileHeader *header, int *firstperson)
{
    if (!gevrTacticalExpandedEnabled()) return -1;
    if (g_CurrentPlayer) {
        for (int hand = 0; hand < 2; ++hand) {
            if (header != &g_CurrentPlayer->copy_of_body_obj_header[hand]) continue;
            const int kind = gevrTacticalItem(g_CurrentPlayer->field_2A44[hand]);
            if (tacticalGunIsExpanded(kind)) {
                *firstperson = TRUE;
                return kind;
            }
        }
    }
#define TACTICAL_EXPANDED(id, group, native, gun, prop, capacity, cadence, name) \
    extern ModelFileHeader tactical_##id##_header; \
    if (header == &tactical_##id##_header) return TACTICAL_GUN_##id;
#include "gevr_tactical_expanded.inc.h"
#undef TACTICAL_EXPANDED
    return -1;
}

static int isMineAsset(int asset)
{
    return asset >= TACT_ASSET_MINE_PROXIMITY && asset <= TACT_ASSET_MINE_TIMED;
}

static int sourceMineAsset(const char *name)
{
    const char *stem = name[0] == 'G' ? name + 1
        : !strncmp(name, "Pchr", 4) ? name + 4 : "";
    if (!strcmp(stem, "proximitymineZ")) return TACT_ASSET_MINE_PROXIMITY;
    if (!strcmp(stem, "remotemineZ")) return TACT_ASSET_MINE_REMOTE;
    if (!strcmp(stem, "timedmineZ")) return TACT_ASSET_MINE_TIMED;
    return -1;
}

static int fitSourceMine(TacticalModel *model, ModelNode *node)
{
    const ModelRoData_DisplayListRecord *dl = &node->Data->DisplayList;
    if (!dl->Vertices || !dl->numVertices) return FALSE;
    float low[3] = {INFINITY, INFINITY, INFINITY};
    float high[3] = {-INFINITY, -INFINITY, -INFINITY};
    for (int i = 0; i < dl->numVertices; ++i) {
        const float p[3] = {dl->Vertices[i].coord.x, dl->Vertices[i].coord.y, dl->Vertices[i].coord.z};
        for (int axis = 0; axis < 3; ++axis) {
            low[axis] = fminf(low[axis], p[axis]);
            high[axis] = fmaxf(high[axis], p[axis]);
        }
    }
    model->mineNormal = 0;
    for (int axis = 0; axis < 3; ++axis) {
        model->mineLow[axis] = low[axis];
        model->mineExtent[axis] = high[axis] - low[axis];
        if (model->mineExtent[axis] <= 0.f) return FALSE;
        if (model->mineExtent[axis] < model->mineExtent[model->mineNormal])
            model->mineNormal = axis;
    }
    return TRUE;
}

void gevrTacticalMeshLoaded(ModelFileHeader *header, const char *name)
{
    TacticalModel *m = NULL;
    ModelNode *nodes[128], *node = NULL;
    int count = 0, gun = 0;
    if (!header || !name) return;
    for (unsigned i = 0; i < ARRAYCOUNT(s_helmetShells); ++i)
        if (s_helmetShells[i].header == header) s_helmetShells[i].header = NULL;
    extern ModelFileHeader tacticalaltyn_header, tacticalfasttrooper_header;
    const int helmet = header == &tacticalaltyn_header ? 2 : header == &tacticalfasttrooper_header ? 3
        : !strcmp(name, "PhathelmetZ") ? 0 : !strcmp(name, "PhathelmetgreyZ") ? 1 : -1;
    if (helmet >= 0) {
        const GevrTacticalHelmet kind = s_helmetShells[helmet].kind;
        const int enabled = kind == GEVR_HELMET_SSH68
            ? sysArgCheck("--tactical-facility-armor") || sysArgCheck("--tactical-campaign-gear")
            : sysArgCheck("--tactical-heavy-guards");
        if (enabled && gevrTacticalHelmetAvailable(kind)) {
            collect(header->RootNode, nodes, &count, 0);
            if (count == 1) {
                s_helmetShells[helmet].header = header;
                s_helmetShells[helmet].node = nodes[0];
                helmetDrawList(s_helmetShells[helmet].draw, kind);
                sysLogPrintf(LOG_NOTE, "tactical helmet: fitted shell %d to %s", kind, name);
            }
        }
        return;
    }
    for (int i = 0; i < s_modelCount; i++) {
        if (s_models[i].header == header) {
            m = &s_models[i];
            clearTacticalModel(m);
            break;
        }
        if (!m && !s_models[i].header) m = &s_models[i];
    }
    if (sysArgCheck("--tactical-inspect-mesh") && (!strcmp(name,"Gm16Z") || !strcmp(name,"Gtt33Z") || !strcmp(name,"Pchrm16Z") || !strcmp(name,"Pchrtt33Z"))) {
        collect(header->RootNode,nodes,&count,0);
        sysLogPrintf(LOG_NOTE,"tactical inspect: %s nodes %d switches %d",name,count,header->numSwitches);
        for(int j=0;j<count;j++) sysLogPrintf(LOG_NOTE,"tactical inspect: %s node %d verts %d type %d secondary %d",name,j,
            nodes[j]->Data->DisplayList.numVertices,nodes[j]->Data->DisplayList.ModelType,
            nodes[j]->Data->DisplayList.Secondary != NULL);
        count=0;
    }
    if (!sysArgCheck("--tactical-mesh") || gevrGexPendingFile) return;
    int expandedView = FALSE;
    const int expanded = expandedHeaderKind(header, &expandedView);
    const int mine = sysArgCheck("--tactical-source-mines") && gevrTacticalCampaignRules() ? sourceMineAsset(name) : -1;
    extern ModelFileHeader tacticalkatana_header;
    if (tacticalGunIsExpanded(expanded)) {
        gun = expandedView && tacticalGunIsPistol(expanded) ? 1 : 16;
    } else if (!strcmp(name, "GwppkZ") || !strcmp(name, "GwppksilZ")) gun = 1;
    else if (!strcmp(name, "Gak47Z")) gun = 2;
    else if (!strcmp(name, "PchrwppkZ") || !strcmp(name, "PchrwppksilZ")) gun = 3;
    else if (!strcmp(name, "GspectreZ")) gun = 4;
    else if (!strcmp(name, "Gmp5kZ")) gun = 5;
    else if (!strcmp(name, "PchrspectreZ")) gun = 6;
    else if (!strcmp(name, "Pchrmp5kZ")) gun = 7;
    else if (sysArgCheck("--tactical-variety") && gevrTacticalAk74ModelHeader(header)) gun = name[0] == 'G' ? 8 : 10;
    else if (sysArgCheck("--tactical-variety") && !strcmp(name,"Gtt33Z")) gun=9;
    else if (sysArgCheck("--tactical-variety") && !strcmp(name,"Pchrtt33Z")) gun=11;
    else if (!strcmp(name, "GskorpionZ") && gevrTacticalSkorpionEnabled()) gun = 12;
    else if (!strcmp(name, "PchrskorpionZ") && gevrTacticalSkorpionEnabled()) gun = 13;
    else if (!strcmp(name, "GsniperrifleZ") && gevrTacticalSvdEnabled()) gun = 14;
    else if (!strcmp(name, "PchrsniperrifleZ") && gevrTacticalSvdEnabled()) gun = 15;
    else if (!strcmp(name, "GrocketlaunchZ") && gevrTacticalRpgEnabled()) gun = TACT_RPG_VIEW;
    else if (!strcmp(name, "PchrrocketlaunchZ") && gevrTacticalRpgEnabled()) gun = TACT_RPG_PROP;
    else if (mine >= 0 && loadMesh(mine)) gun = name[0] == 'G' ? TACT_MINE_VIEW : TACT_MINE_PROP;
    else if (header == &tacticalkatana_header && gevrTacticalKatanaEnabled()) gun = TACT_KATANA_PROP;
    else if (!strcmp(name, "GknifeZ") || !strcmp(name, "GthrowknifeZ")) {
        const int sword = g_CurrentPlayer
            && ((header == &g_CurrentPlayer->copy_of_body_obj_header[GUNRIGHT]
                    && g_CurrentPlayer->field_2A44[GUNRIGHT] == ITEM_TACTICAL_KATANA)
                || (header == &g_CurrentPlayer->copy_of_body_obj_header[GUNLEFT]
                    && g_CurrentPlayer->field_2A44[GUNLEFT] == ITEM_TACTICAL_KATANA));
        if (sword && gevrTacticalKatanaEnabled()) gun = TACT_KATANA_VIEW;
        else if (gevrTacticalKnifeEnabled()) gun = TACT_KNIFE_VIEW;
        else return;
    }
    else if ((!strcmp(name, "PchrknifeZ") || !strcmp(name, "PchrthrowknifeZ"))
        && gevrTacticalKnifeEnabled()) gun = TACT_KNIFE_PROP;
    else if (strcmp(name, "PchrkalashZ")) return;
    if (!m && s_modelCount < TACT_MODEL_MAX) m = &s_models[s_modelCount];
    if (!m) {
        /* World headers survive for the stage and might never load again. */
        for (int i = 0; i < s_modelCount; i++)
            if (s_models[i].firstperson && (!m || s_models[i].used < m->used)) m = &s_models[i];
        if (!m) {
            sysLogPrintf(LOG_WARNING, "tactical mesh: no registration available for %s", name);
            return;
        }
        clearTacticalModel(m);
    }
    collect(header->RootNode, nodes, &count, 0);
    if ((tacticalGunIsExpanded(expanded) && gun != 1) || gun == TACT_RPG_PROP
        || gun == TACT_MINE_PROP || gun == TACT_KNIFE_PROP || gun == TACT_KATANA_PROP) {
        for (int n = 0; n < count && m->count < 32; ++n) {
            if (!node || nodes[n]->Data->DisplayList.numVertices > node->Data->DisplayList.numVertices)
                node = nodes[n];
            m->nodes[m->count++] = nodes[n];
        }
        if (!node) return;
    } else if (!gun || gun == 3 || gun == 6 || gun == 7 || gun == 10 || gun == 11 || gun == 13) {
        const int expected = !gun ? 100 : gun == 6 ? 99 : gun == 7 ? 96 : gun == 13 ? 92
            : !strcmp(name, "PchrwppkZ") ? 50 : 74;
        if (count != 1 || (gun != 10 && gun != 11 && nodes[0]->Data->DisplayList.numVertices != expected)) {
            sysLogPrintf(LOG_WARNING, "tactical mesh: unsupported %s (%d nodes)", name, count); return;
        }
        node = nodes[0]; m->nodes[m->count++] = node;
    } else {
        /* Native PP7 geometry has no switch, or switch 7 (magazine) / 35
         * (silencer). Bond's hands use other switches and remain untouched. */
        for (int i = 0; i < count && m->count < 32; i++) {
            ModelNode *n = nodes[i], *p; int skip = 0;
            for (p = n; p; p = p->Parent) {
                for (int s = 0; s < header->numSwitches; s++)
                if (header->Switches[s] == p && s != 7 && s != 35) skip = 1;
            }
            if (!skip) {
                m->nodes[m->count++] = n;
                const int anchor = gun == 4 ? 63 : gun == 5 ? 22 : 35;
                if (gun == 8 || gun == 9 || gun == 12 || gun >= 14) {
                    if (!node || n->Data->DisplayList.numVertices > node->Data->DisplayList.numVertices) node=n;
                } else if (!node && n->Data->DisplayList.numVertices == anchor) node = n;
            } else if ((gun == 1 || gun == 9) && m->handCount < 64) {
                m->handNodes[m->handCount++] = n;
            }
        }
        if (!node || (gun < 14 && m->count < 5)) { memset(m, 0, sizeof(*m));
            sysLogPrintf(LOG_WARNING, "tactical mesh: unsupported %s (%d nodes)", name, count); return; }
    }
    if (mine >= 0 && !fitSourceMine(m, node)) {
        memset(m, 0, sizeof(*m));
        return;
    }
    ModelNode *parent = node;
    while (parent && (!parent->Data || ((parent->Opcode & 0xff) != MODELNODE_OPCODE_GROUPSIMPLE
           && (parent->Opcode & 0xff) != MODELNODE_OPCODE_GROUP))) parent = parent->Parent;
    if (!parent) { memset(m, 0, sizeof(*m)); return; }
    m->matrix = (parent->Opcode & 0xff) == MODELNODE_OPCODE_GROUPSIMPLE
        ? parent->Data->GroupSimple.Group1 : parent->Data->Group.MatrixIDs[0];
    m->header = header; m->drawNode = node;
    m->asset = gun == 8 || gun == 10 ? 21 : gun == 9 || gun == 11 ? 22 : gun == 1 || gun == 3 ? 1 : gun == 4 ? 2 : gun == 5 ? 3 : gun == 6 ? 4 : gun == 7 ? 5 : 0;
    if (gun == 12 || gun == 13) m->asset = TACT_ASSET_SKORPION;
    if (gun == 14 || gun == 15) m->asset = TACT_ASSET_SVD;
    if (gun == TACT_RPG_VIEW || gun == TACT_RPG_PROP) m->asset = TACT_ASSET_RPG;
    if ((gun == 1 || gun == 3) && sysArgCheck("--tactical-ppk"))
        m->asset = (!strcmp(name, "GwppksilZ") || !strcmp(name, "PchrwppksilZ")) ? 11 : 10;
    m->firstperson = gun == 1 || gun == 9 ? 1 : gun == 2 || gun == 8 ? 2
        : gun == 4 ? 3 : gun == 5 ? 4 : gun == 12 ? TACT_VIEW_SKORPION : gun == 14 ? TACT_VIEW_SVD : 0;
    if (gun == TACT_RPG_VIEW) m->firstperson = TACT_VIEW_RPG;
    if (gun == TACT_KNIFE_VIEW || gun == TACT_KNIFE_PROP || gun == TACT_KATANA_VIEW || gun == TACT_KATANA_PROP) {
        m->asset = gun >= TACT_KATANA_VIEW ? TACT_ASSET_KATANA : TACT_ASSET_KNIFE;
        m->firstperson = gun == TACT_KNIFE_VIEW || gun == TACT_KATANA_VIEW ? TACT_VIEW_KNIFE : 0;
    }
    if (mine >= 0) {
        m->asset = mine;
        m->firstperson = gun == TACT_MINE_VIEW ? TACT_VIEW_MINE : 0;
        if (sysArgCheck("--tactical-inspect-mesh"))
            sysLogPrintf(LOG_NOTE, "tactical mine fit: %s axis %d low %.0f %.0f %.0f size %.0f %.0f %.0f",
                name, m->mineNormal, m->mineLow[0], m->mineLow[1], m->mineLow[2],
                m->mineExtent[0], m->mineExtent[1], m->mineExtent[2]);
    }
    if (tacticalGunIsExpanded(expanded)) {
        m->expanded = expanded;
        m->firstperson = expandedView;
    }
    if (m == &s_models[s_modelCount]) ++s_modelCount;
    m->used = ++s_modelClock;
    sysLogPrintf(LOG_NOTE, "tactical mesh: registered %s matrix %d, replacing %d nodes", name, m->matrix, m->count);
}
static s16 s16clamp(float v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : (s16)lrintf(v); }
static int mask(int n) { int r = 0; while ((1 << r) < n) ++r; return r; }

static int isMagazineTexture(int asset, int tex)
{
    if (asset == TACT_ASSET_RPG) return tex == 2; /* Optional loaded round. */
    if (asset == 10 || asset == 11) return tex == 3 || tex == 4;
    if (asset == 0 || asset == 21) return tex == 2;
    if (asset == 22 || asset == TACT_ASSET_SKORPION || asset == TACT_ASSET_SVD) return tex == 0;
    return tex == (asset == 1 ? 3 : 1);
}

static const float s_viewForwardOffset[] = {
    [1] = -70.f, [2] = 16.f, [3] = -140.f, [4] = 100.f, [TACT_VIEW_KNIFE] = 0.f
};

static Gfx *emitMeshTexture(Gfx *g, const TacticalAsset *mesh, Vtx *vertices, int texture, const u16 *key)
{
    const int w = mesh->tex[texture].w, h = mesh->tex[texture].h;
    const int ms = mask(w), mt = mask(h);
    gSPTexture(g++, 0xffff, 0xffff, 0, G_TX_RENDERTILE, G_ON);
    gDPLoadTextureBlock(g++, key, G_IM_FMT_RGBA, G_IM_SIZ_16b, 16, 16, 0, G_TX_WRAP, G_TX_WRAP, 4, 4, G_TX_NOLOD, G_TX_NOLOD);
    for (int tile = G_TX_RENDERTILE; tile <= G_TX_RENDERTILE + 1; tile++) {
        gDPSetTile(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b, ((w * 2) + 7) >> 3, 0, tile, 0, G_TX_WRAP, mt, G_TX_NOLOD, G_TX_WRAP, ms, G_TX_NOLOD);
        gDPSetTileSize(g++, tile, 0, 0, (w - 1) << G_TEXTURE_IMAGE_FRAC, (h - 1) << G_TEXTURE_IMAGE_FRAC);
    }
    int first = -1, nb = 0;
    for (int tri = 0; tri <= mesh->ntri; tri++) {
        const int match = tri < mesh->ntri && mesh->corners[tri * 3].tex == texture;
        if (nb && (!match || nb == 5)) {
            gSPVertex(g++, osVirtualToPhysical(&vertices[first * 3]), nb * 3, 0);
            for (int j = 0; j < nb; j++) gSP1Triangle(g++, j * 3, j * 3 + 1, j * 3 + 2, 0);
            nb = 0;
        }
        if (match) {
            const TacticalCorner *c = &mesh->corners[tri * 3];
            const float bs = floorf(c[0].st[0]), bt = floorf(c[0].st[1]);
            if (!nb) first = tri;
            for (int j = 0; j < 3; j++) {
                vertices[tri * 3 + j].v.tc[0] = s16clamp((c[j].st[0] - bs) * w * 32);
                vertices[tri * 3 + j].v.tc[1] = s16clamp((c[j].st[1] - bt) * h * 32);
            }
            nb++;
        }
    }
    return g;
}

enum { TACT_LIST_GEAR_CORE = 6, TACT_LIST_INSTALLED_MAG, TACT_LIST_WEARABLE,
    TACT_LIST_REMOTE_CORE, TACT_LIST_REMOTE_BOLT, TACT_LIST_REMOTE_HANDLE };

enum { TACT_ACTION_STATIC, TACT_ACTION_BOLT, TACT_ACTION_HANDLE };

static int legacyActionMaterial(int asset, int texture)
{
    switch (asset) {
    case 0: return texture == 8 || texture == 9 ? TACT_ACTION_BOLT : TACT_ACTION_STATIC;
    case 1: return texture >= 4 && texture <= 6 ? TACT_ACTION_BOLT : TACT_ACTION_STATIC;
    case 2: case 3: case 4: case 5:
        if (texture == 0) return TACT_ACTION_HANDLE;
        return texture == (asset == 2 || asset == 4 ? 8 : 7) ? TACT_ACTION_BOLT : TACT_ACTION_STATIC;
    case 10: case 11: return texture == 6 || texture == 7 ? TACT_ACTION_BOLT : TACT_ACTION_STATIC;
    case 21: return texture == 9 ? TACT_ACTION_BOLT : TACT_ACTION_STATIC;
    case 22: return texture == 2 || texture == 3 ? TACT_ACTION_BOLT : TACT_ACTION_STATIC;
    case TACT_ASSET_SKORPION: case TACT_ASSET_SVD:
        return texture == 1 ? TACT_ACTION_BOLT : TACT_ACTION_STATIC;
    default: return TACT_ACTION_STATIC;
    }
}

static int gearCoreTexture(int asset, int texture)
{
    switch (asset) {
    case 2: case 4: return texture >= 7; /* MP5 lower and bolt */
    case 3: case 5: return texture >= 6; /* MP5K lower and bolt */
    case 10: case 11: return texture != 1 && texture != 3 && texture != 4 && texture != 9;
    case 21: return texture >= 8;
    case 22: return texture == 4; /* PM frame */
    case TACT_ASSET_SKORPION: return texture == 1; /* Original animated bolt */
    case TACT_ASSET_SVD: return texture >= 1 && texture <= 8;
    default: return texture >= 7;
    }
}

/* filter: 0 whole gun, 1 gun without magazine, 2 detached magazine,
 * 3 matrix-free full mesh, 4 matrix-free without magazine, 5 glove without watch.
 * Gear lists use the existing animated geometry for the fixed receiver and action. */
static void emitList(TacticalModel *m, Gfx *g, int filter)
{
    TacticalAsset *mesh = &s_assets[m->asset];
    Vtx *vertices = filter == 2 ? m->heldVertices : m->vertices;
    if (filter == 0 || filter == 1)
        gSPMatrix(g++, (void *)(uintptr_t)(0x03000000u + m->matrix * sizeof(Mtx)), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gDPPipeSync(g++); gDPSetTexturePersp(g++, G_TP_PERSP); gDPSetTextureLUT(g++, G_TT_NONE);
    gSPClearGeometryMode(g++, G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR | G_LIGHTING | G_CULL_BOTH);
    gSPSetGeometryMode(g++, G_SHADE | G_SHADING_SMOOTH);
    /* Hand meshes have baked per-vertex lighting. Do not inherit a preceding
     * weapon combiner which can ignore that shading and flatten the hand. */
    if (filter == 3 || filter == 5 || filter >= TACT_LIST_GEAR_CORE)
        gDPSetCombineMode(g++, G_CC_MODULATERGBA, G_CC_MODULATERGBA2);
    u32 translucent = 0;
    if (filter == TACT_LIST_WEARABLE) {
        for (int i = 0; i < mesh->ntri * 3; ++i) {
            const TacticalCorner *corner = &mesh->corners[i];
            if (corner->rgba[3] < 255) translucent |= 1u << corner->tex;
        }
    }
    for (int t = 0; t < mesh->ntex; t++) {
        if (translucent & (1u << t)) continue;
        const int isMagazine = isMagazineTexture(m->asset, t);
        if (((filter == 1 || filter == 4) && isMagazine) || (filter == 2 && !isMagazine)) continue;
        if (filter == 5 && t != 0) continue; /* texture 1 is the watch */
        if (filter == TACT_LIST_GEAR_CORE && !gearCoreTexture(m->asset, t)) continue;
        if (filter == TACT_LIST_INSTALLED_MAG && !isMagazine) continue;
        if (filter >= TACT_LIST_REMOTE_CORE) {
            const int moving = legacyActionMaterial(m->asset, t);
            if (isMagazine || !gearCoreTexture(m->asset, t)
                || moving != filter - TACT_LIST_REMOTE_CORE) continue;
        }
        g = emitMeshTexture(g, mesh, vertices, t, s_texkey[m->asset][t]);
    }
    if (translucent) {
        /* The shield is blended after its helmet, hinges and frame have
         * written depth. Other wearables use the opaque material path. */
        gDPPipeSync(g++);
        gDPSetRenderMode(g++, G_RM_FOG_PRIM_A, G_RM_AA_ZB_XLU_SURF2);
        for (int t = 0; t < mesh->ntex; ++t) {
            if (translucent & (1u << t)) g = emitMeshTexture(g, mesh, vertices, t, s_texkey[m->asset][t]);
        }
        gDPPipeSync(g++);
        gDPSetRenderMode(g++, G_RM_FOG_PRIM_A, G_RM_AA_ZB_OPA_SURF2);
    }
    gSPEndDisplayList(g++);
}
static void build(TacticalModel *m)
{
    TacticalAsset *mesh = &s_assets[m->asset];
    const int nv = mesh->ntri * 3;
    const size_t commands = (size_t)mesh->ntex * 16 + (size_t)mesh->ntri * 2 + 16;
    m->vertices = calloc((size_t)nv, sizeof(Vtx));
    m->list = calloc(commands, sizeof(Gfx));
    const int mine = isMineAsset(m->asset);
    const int hasEmptyModel = (m->firstperson && !mine && m->asset != TACT_ASSET_KNIFE && m->asset != TACT_ASSET_KATANA)
        || m->asset == TACT_ASSET_RPG;
    const int hasDetachedMag = hasEmptyModel && m->asset != TACT_ASSET_RPG;
    if (hasEmptyModel) m->noMag = calloc(commands, sizeof(Gfx));
    if (hasDetachedMag) {
        m->heldVertices = calloc((size_t)nv, sizeof(Vtx));
        m->heldMag = calloc(commands, sizeof(Gfx));
    }
    if (!m->vertices || !m->list || (hasEmptyModel && !m->noMag)
        || (hasDetachedMag && (!m->heldVertices || !m->heldMag))) {
        free(m->vertices); free(m->heldVertices); free(m->list); free(m->noMag); free(m->heldMag);
        m->vertices = m->heldVertices = NULL; m->list = m->noMag = m->heldMag = NULL; return;
    }
    const int scaledAk = m->asset == 0 && m->firstperson == 2 && sysArgCheck("--tactical-physical");
    for (int i = 0; i < nv; i++) {
        TacticalCorner *c = &mesh->corners[i]; Vtx *v = &m->vertices[i];
        float point[3];
        if (mine) {
            /* The replacement occupies the original disc's local bounds. The
             * held model and the world prop use different native bases. */
            const int normal = m->mineNormal;
            const int x = (normal + 1) % 3, y = (normal + 2) % 3;
            point[x] = m->mineLow[x] + (.5f + .5f * c->p[0]) * m->mineExtent[x];
            point[y] = m->mineLow[y] + (.5f + .5f * c->p[1]) * m->mineExtent[y];
            point[normal] = m->mineLow[normal] + c->p[2] * m->mineExtent[normal];
        } else if (m->firstperson) {
            gevrTacticalViewAxes(c->p, point);
            point[0] *= 1000.f;
            point[1] = 1000.f * point[1] + (m->firstperson == 1 ? 7.f : 20.f);
            point[2] = 1000.f * point[2] + s_viewForwardOffset[m->firstperson];
            if (scaledAk) gevrTacticalScaleAkPoint(&point[0], &point[1], &point[2]);
        } else {
            const int pistol = m->asset == 1 || m->asset == 10 || m->asset == 11 || m->asset == 22;
            const float offset = pistol ? 155.f : m->asset == 4 ? 122.f : m->asset == 5 ? -51.f
                : m->asset == TACT_ASSET_SKORPION ? 35.f : -25.f;
            gevrTacticalPropAxes(c->p, point);
            point[0] = 1000.f * point[0] + offset;
            point[1] *= 1000.f;
            point[2] = 1000.f * point[2] + (pistol ? 8.f : m->asset == TACT_ASSET_SKORPION ? 20.f : -12.f);
        }
        if (m->asset == TACT_ASSET_KATANA && m->firstperson && m->header)
            gevrTacticalKatanaGripPoint(point, point);
        if (m->asset == TACT_ASSET_KNIFE) {
            /* Keep the handle through the palm and lengthen the blade about it. */
            point[0] *= 1.24f;
            point[1] = -29.3f + (point[1] + 29.3f)*1.24f;
            point[2] = m->firstperson ? -60.f + (point[2] + 60.f)*1.24f : point[2]*1.24f;
        }
        for (int axis = 0; axis < 3; ++axis) v->v.ob[axis] = s16clamp(point[axis]);
        memcpy(v->v.cn, c->rgba, 4);
        if (hasDetachedMag) {
            m->heldVertices[i] = *v;
            const int centreY = m->asset == TACT_ASSET_SKORPION ? (int)GEVR_SKORPION_MAG_Y
                : m->asset == TACT_ASSET_SVD ? (int)GEVR_SVD_MAG_Y
                : m->asset == 22 ? -35 : m->asset == 21 ? -93 : (m->asset == 10 || m->asset == 11) ? -22 : m->asset == 0 ? (scaledAk ? -93 : -89) : m->asset == 1 ? -38 : m->asset == 2 ? -104 : -63;
            const int centreZ = m->asset == TACT_ASSET_SKORPION ? (int)GEVR_SKORPION_MAG_Z
                : m->asset == TACT_ASSET_SVD ? (int)GEVR_SVD_MAG_Z
                : m->asset == 22 ? -60 : m->asset == 21 ? 146 : (m->asset == 10 || m->asset == 11) ? 34 : m->asset == 0 ? (scaledAk ? 183 : 149) : m->asset == 1 ? 33 : m->asset == 2 ? 143 : 130;
            m->heldVertices[i].v.ob[1] -= centreY;
            m->heldVertices[i].v.ob[2] -= centreZ;
        }
    }
    emitList(m, m->list, 0);
    if (hasEmptyModel) emitList(m, m->noMag, 1);
    if (hasDetachedMag) emitList(m, m->heldMag, 2);
    m->built = 1;
}

/* Both MP5s accept the same magazines. Fit the other variant's magazine to
 * the current well so a 20- or 30-round magazine keeps its own silhouette. */
static void buildOtherMagazine(TacticalModel *m)
{
    int other = m->asset == 2 ? 3 : 2;
    TacticalAsset *mesh;
    TacticalModel temp = {0};
    Gfx *g;
    size_t nv, commands;
    if (m->withOtherMag || (m->asset != 2 && m->asset != 3) || !loadMesh(other)) return;
    mesh = &s_assets[other]; nv = (size_t)mesh->ntri * 3;
    commands = (size_t)mesh->ntex * 16 + (size_t)mesh->ntri * 2 + 16;
    m->otherMagVertices = calloc(nv, sizeof(Vtx));
    m->otherMag = calloc(commands, sizeof(Gfx));
    m->withOtherMag = calloc(4, sizeof(Gfx));
    if (!m->otherMagVertices || !m->otherMag || !m->withOtherMag) {
        free(m->otherMagVertices); free(m->otherMag); free(m->withOtherMag);
        m->otherMagVertices = NULL; m->otherMag = m->withOtherMag = NULL; return;
    }
    for (size_t i = 0; i < nv; i++) {
        const TacticalCorner *c = &mesh->corners[i]; Vtx *v = &m->otherMagVertices[i];
        v->v.ob[0] = s16clamp(-1000 * c->p[0]);
        v->v.ob[1] = s16clamp(1000 * c->p[2] + 20);
        v->v.ob[2] = s16clamp(1000 * (c->p[1] + (m->asset == 2 ? .113f : -.153f)));
        memcpy(v->v.cn, c->rgba, 4);
    }
    temp.asset = other; temp.heldVertices = m->otherMagVertices;
    emitList(&temp, m->otherMag, 2);
    g = m->withOtherMag;
    gSPDisplayList(g++, m->noMag);
    gSPDisplayList(g++, m->otherMag);
    gSPEndDisplayList(g++);
    sysLogPrintf(LOG_NOTE, "tactical mesh: fitted interchangeable magazine asset %d to %d", other, m->asset);
}
/* Guard AK: align the original Pchr attachment's wrist with the replacement
 * model's pistol grip. Keep the floor model's placement unchanged. */
static void buildGuardAkFit(TacticalModel *m)
{
    TacticalAsset *mesh = &s_assets[m->asset];
    TacticalModel temp;
    size_t nv = (size_t)mesh->ntri * 3;
    size_t commands = (size_t)mesh->ntex * 16 + (size_t)mesh->ntri * 2 + 16;
    if (m->npcList || (m->asset != 0 && m->asset != 21) || m->firstperson || !m->built) return;
    m->npcVertices = calloc(nv, sizeof(Vtx)); m->npcList = calloc(commands, sizeof(Gfx));
    if (!m->npcVertices || !m->npcList) {
        free(m->npcVertices); free(m->npcList); m->npcVertices=NULL; m->npcList=NULL; return;
    }
    for (size_t i=0;i<nv;i++) {
        m->npcVertices[i]=m->vertices[i];
        m->npcVertices[i].v.ob[0]+=133;
        m->npcVertices[i].v.ob[1]-=8;
        m->npcVertices[i].v.ob[2]+=45;
    }
    temp=*m; temp.vertices=m->npcVertices;
    emitList(&temp,m->npcList,0);
}

typedef struct TacticalChamberFit {
    float x, y, front, radius, depth;
    float portMotion;
} TacticalChamberFit;

static Gfx *gevrChamberBand(Gfx *g, float x, float y, float z0, float z1,
                           float r0, float r1, int red, int green, int blue)
{
    static const float circle[8][2] = {
        {1,0}, {.707f,.707f}, {0,1}, {-.707f,.707f},
        {-1,0}, {-.707f,-.707f}, {0,-1}, {.707f,-.707f}
    };
    Vtx *v = dynAllocateVertices(16);
    for (int i = 0; i < 16; ++i) {
        const float *n = circle[i & 7];
        const float radius = i < 8 ? r0 : r1;
        const float shade = .73f + .23f * n[1] + .12f * fabsf(n[0]);
        v[i].v.ob[0] = s16clamp(x + radius * n[0]);
        v[i].v.ob[1] = s16clamp(y + radius * n[1]);
        v[i].v.ob[2] = s16clamp(i < 8 ? z0 : z1);
        v[i].v.flag = 0;
        v[i].v.tc[0] = v[i].v.tc[1] = 0;
        v[i].v.cn[0] = (u8)fminf(255, red * shade);
        v[i].v.cn[1] = (u8)fminf(255, green * shade);
        v[i].v.cn[2] = (u8)fminf(255, blue * shade);
        v[i].v.cn[3] = 255;
    }
    gSPVertex(g++, osVirtualToPhysical(v), 16, 0);
    for (int i = 0; i < 8; ++i) {
        const int next = (i + 1) & 7;
        gSP2Triangles(g++, i,next,next+8,0, i,next+8,i+8,0);
    }
    return g;
}

/* Detail at the opening in each imported receiver. A slightly open action
 * exposes a sliver first. The cartridge is drawn from the well's chamber bit,
 * independently of the magazine and its number of rounds. */
static Gfx *gevrTacticalChamberList(Gfx *gun, int kind, float pull, int chambered)
{
    static const TacticalChamberFit fits[TACTICAL_GUN_COUNT] = {
        {-8.5f, 35.f, 181.f, 4.2f, 33.f, 0.f}, {-4.5f, 30.f, 81.f, 3.f, 17.f, .94f},
        {-5.4f, 27.f, 107.f, 3.6f, 23.f, 0.f}, {-5.2f, 27.f, 113.f, 3.6f, 23.f, 0.f},
        {-8.5f, 35.f, 180.f, 4.2f, 33.f, 0.f}, {-4.7f, 25.f, -20.f, 3.1f, 16.f, .94f},
        {0.f, 42.f, 109.f, 3.5f, 24.f, 0.f}, {-14.f, 36.f, 255.f, 4.f, 38.f, 0.f}
    };
    const int physical = sysArgCheck("--tactical-physical");
    TacticalChamberFit fit = fits[kind];
    const GevrTacticalExpandedFit *expanded = gevrTacticalExpandedFit(kind);
    if (expanded) {
        const int beltBox = kind == TACTICAL_GUN_PKM;
        fit = (TacticalChamberFit){expanded->ejection[0], expanded->ejection[1], expanded->ejection[2],
            beltBox ? 5.f : 3.5f, beltBox ? 34.f : 22.f, tacticalGunIsPistol(kind) ? .94f : 0.f};
    }
    if (kind == TACTICAL_GUN_USP && !sysArgCheck("--tactical-ppk"))
        fit = (TacticalChamberFit){-5.f, 28.f, 90.f, 4.f, 22.f, .96f};
    if (kind == TACTICAL_GUN_AK && physical) {
        gevrTacticalScaleAkPoint(&fit.x, &fit.y, &fit.front);
        fit.radius *= 1.2f;
        fit.depth *= 1.2f;
    }
    const float shift = gevrTacticalActionShift(kind, pull, physical);
    const float opening = fminf(fit.depth, shift - 4.f);
    if (opening < 2.f || chambered < 0) return gun;
    const float front = fit.front - fit.portMotion * shift;
    const float back = front - opening;
    const float x = fit.x, y = fit.y, radius = fit.radius;
    if (sysArgCheck("--input-script")) {
        static unsigned char covered[TACTICAL_GUN_COUNT][2];
        if (!covered[kind][!!chambered]) {
            sysLogPrintf(LOG_NOTE, "tactical render: chamber detail item %d loaded %d opening %.1f",
                kind, chambered, (double)opening);
            covered[kind][!!chambered] = TRUE;
        }
    }
    Gfx *draw = dynAllocate(60 * sizeof(Gfx));
    Gfx *g = draw;
    gSPDisplayList(g++, gun);
    gDPPipeSync(g++);
    gSPTexture(g++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gSPClearGeometryMode(g++, G_LIGHTING | G_CULL_BOTH | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
    gSPSetGeometryMode(g++, G_SHADE | G_SHADING_SMOOTH | G_ZBUFFER);
    gDPSetCombineMode(g++, G_CC_SHADE, G_CC_SHADE);

    /* Recess wall and forward rim, with an open back for inspection. */
    const float inward = x < 0.f ? 1.f : -1.f;
    g = gevrChamberBand(g, x + inward*radius*.65f, y - radius*.38f, back, front,
        radius*.65f, radius*.65f, 32, 35, 38);
    g = gevrChamberBand(g, x, y, front, front, radius*.70f, radius*1.14f, 107, 113, 117);
    if (chambered) {
        const float shoulder = front - opening*.27f;
        g = gevrChamberBand(g, x, y, back, shoulder, radius*.72f, radius*.72f, 228, 183, 77);
        g = gevrChamberBand(g, x, y, shoulder, front, radius*.64f, radius*.28f, 169, 99, 55);
        g = gevrChamberBand(g, x, y, back, back, 0.f, radius*.72f, 194, 154, 75);
    }
    gSPEndDisplayList(g++);
    return draw;
}

static int fittedPistolModel(const TacticalModel *m)
{
    return m->firstperson == 1 && (tacticalGunIsPistol(m->expanded) || m->asset == 10 || m->asset == 11 || m->asset == 22);
}

static int pistolWatchMount(const TacticalModel *m, Model *model, TacticalGlovePose pose, int mirrored,
    GevrTacticalWatchMount *out)
{
    return mirrored && g_gevrStereo && isPlayerGunModel(model)
        && gevrTacticalGloveWatchMount(m->expanded ? m->expanded : gevrTacticalMeshGunKind(m->asset), pose, mirrored, out)
        && gevrTacticalOriginalWatchReady();
}

/* The drawn pistol hand inherits the actual gun node matrix, including recoil. */
int gevrTacticalPistolWatchMount(Model *model, GevrTacticalWatchMount *out)
{
    if (!g_gevrStereo || !model->render_pos || !gevrTacticalGunGloveMirrored(model)
        || !sysArgCheck("--tactical-watch")) return -1;
    const TacticalGlovePose pose = gevrTacticalGunTrigger(model) ? TACTICAL_GLOVE_TRIGGER : TACTICAL_GLOVE_READY;
    for (int i = 0; i < s_modelCount; ++i) {
        const TacticalModel *m = &s_models[i];
        if (m->header == model->obj && m->handDrawNode && fittedPistolModel(m)
            && pistolWatchMount(m, model, pose, 1, out)) return m->matrix;
    }
    return -1;
}

static const struct HelmetAttachment *helmetAttachment(const Model *model)
{
    if (!model) return NULL;
    for (int i = 0; i < s_helmetAttachmentCount; ++i) {
        const struct HelmetAttachment *a = &s_helmetAttachments[i];
        if (a->model == model && a->header == model->obj && a->hat->model == model
            && a->hat->prop && a->hat->prop->type == PROP_TYPE_OBJ && a->hat->prop->obj == a->hat) return a;
    }
    return NULL;
}

void gevrTacticalHelmetResetAttachments(void)
{
    s_helmetAttachmentCount = 0;
}

void gevrTacticalHelmetForget(const void *hat)
{
    for (int i = 0; i < s_helmetAttachmentCount; ++i)
        if (s_helmetAttachments[i].hat == hat) {
            s_helmetAttachments[i] = s_helmetAttachments[--s_helmetAttachmentCount];
            return;
        }
}

int gevrTacticalHelmetAttach(Model *model, const void *hat, GevrTacticalHelmet helmet)
{
    if (!model || !model->obj || !hat || !gevrTacticalHelmetAvailable(helmet)) return 0;
    ModelNode *nodes[128];
    int count = 0;
    collect(model->obj->RootNode, nodes, &count, 0);
    if (count != 1) return 0;
    struct HelmetAttachment *slot = NULL;
    for (int i = 0; i < s_helmetAttachmentCount; ++i) {
        struct HelmetAttachment *a = &s_helmetAttachments[i];
        if (a->model == model) { slot = a; break; }
        if (!slot && (!a->hat->prop || a->hat->prop->type != PROP_TYPE_OBJ || a->hat->prop->obj != a->hat))
            slot = a;
    }
    if (!slot) {
        if (s_helmetAttachmentCount == (int)ARRAYCOUNT(s_helmetAttachments)) return 0;
        slot = &s_helmetAttachments[s_helmetAttachmentCount++];
    }
    *slot = (struct HelmetAttachment){model, model->obj, hat, nodes[0], helmet};
    helmetDrawList(s_helmetAttachmentLists[helmet], helmet);
    return 1;
}

static int tacticalHelmetVariant(const Model *model)
{
    if (!model || !model->obj) return -1;
    for (unsigned i = 0; i < ARRAYCOUNT(s_helmetShells); ++i) {
        if (s_helmetShells[i].header != model->obj) continue;
        if (s_helmetShells[i].kind == GEVR_HELMET_SSH68 && !gevrTacticalFacilityArmor()
            && (!sysArgCheck("--tactical-campaign-gear") || !gevrTacticalCampaignRules()))
            return -1;
        return i;
    }
    return -1;
}

int gevrTacticalHelmetKind(const Model *model)
{
    const struct HelmetAttachment *attachment = helmetAttachment(model);
    if (attachment) return attachment->helmet;
    const int shell = tacticalHelmetVariant(model);
    return shell < 0 ? -1 : (int)s_helmetShells[shell].kind;
}

int gevrTacticalHelmetModel(const Model *model)
{
    return gevrTacticalHelmetKind(model) >= 0;
}

int gevrTacticalMeshNode(ModelNode *node, Model *model, void **gdl)
{
    if (!sysArgCheck("--tactical-mesh") || !model || !model->obj || !gevrTacticalCampaignRules()) return 0;
    const struct HelmetAttachment *attachment = helmetAttachment(model);
    if (attachment && attachment->node == node) {
        *gdl = s_helmetAttachmentLists[attachment->helmet][tacticalModelFar(model, 0)];
        return 1;
    }
    const int helmet = tacticalHelmetVariant(model);
    if (helmet >= 0 && s_helmetShells[helmet].node == node) {
        *gdl = s_helmetShells[helmet].draw[tacticalModelFar(model, 0)];
        return 1;
    }
    for (int i = 0; i < s_modelCount; i++) {
        TacticalModel *m = &s_models[i];
        int found = 0;
        if (m->header != model->obj) continue;
        if (fittedPistolModel(m) && sysArgCheck("--tactical-pavlov-hands")) {
            for (int j = 0; j < m->handCount; j++) if (m->handNodes[j] == node) {
                int trigger = gevrTacticalGunTrigger(model);
                const int mirrored = gevrTacticalGunGloveMirrored(model);
                if (sysArgCheck("--input-script") && sysArgCheck("--tactical-ppk-grip-preview")) trigger = 1;
                const TacticalGlovePose pose = trigger ? TACTICAL_GLOVE_TRIGGER : TACTICAL_GLOVE_READY;
                GevrTacticalWatchMount mount;
                const int liveWatch = pistolWatchMount(m, model, pose, mirrored, &mount);
                const int kind = m->expanded ? m->expanded : gevrTacticalMeshGunKind(m->asset);
                Gfx *hand = liveWatch ? gevrTacticalFittedGloveList(kind, pose, mirrored, 0)
                    : gevrTacticalPistolFiringHandList(kind, trigger, mirrored);
                if (!hand) break; /* Missing mesh: keep the original hand. */
                if (!m->handDrawNode) {
                    m->handDrawNode = node;
                    sysLogPrintf(LOG_NOTE, "tactical PPK: selected replacement hand node (%d candidates)", m->handCount);
                }
                if (node != m->handDrawNode) { *gdl = NULL; return 1; }
                const int k = !!trigger + 2 * !!mirrored + 4 * liveWatch;
                if (!m->pistolHandBuilt[k]) {
                    Gfx *g = m->pistolHands[k];
                    gSPMatrix(g++, (void *)(uintptr_t)(0x03000000u + m->matrix * sizeof(Mtx)), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
                    gSPDisplayList(g++, hand);
                    if (sysArgCheck("--input-script") && sysArgCheck("--tactical-ppk-twohand-preview")) {
                        Gfx *support = (Gfx *)gevrTacticalPistolSupportList();
                        if (support) gSPDisplayList(g++, support);
                    }
                    gSPEndDisplayList(g++);
                    m->pistolHandBuilt[k] = 1;
                }
                *gdl = m->pistolHands[k]; return 1;
            }
        }
        for (int j = 0; j < m->count; j++) if (m->nodes[j] == node) found = 1;
        const int lowDetail = found && !m->firstperson && tacticalModelFar(model, m->matrix);
        if (found && tacticalGunIsExpanded(m->expanded)) {
            if (node != m->drawNode) {
                *gdl = NULL;
                return 1;
            }
            const int kind = m->expanded;
            int build = -1;
            const int player = m->firstperson && isPlayerGunModel(model);
            const int hand = player && model == &g_CurrentPlayer->hands[GUNLEFT].weaponModel ? GUNLEFT : GUNRIGHT;
            const TacticalMagazine fallback = {.id = !player || gevrTacticalInstalledMag(kind, hand)};
            const TacticalMagazine *magazine = gevrTacticalGearModelMagazine(model, kind, &build);
            const GevrGearAssembly *assembly = gevrTacticalGearDeployedAssembly(build);
            if (!assembly) assembly = gevrTacticalGearStockAssembly(gevrTacticalGearKey(kind));
            const Mtxf *held = player && model->render_pos && m->matrix < model->obj->numMatrices
                ? &model->render_pos[m->matrix].pos : NULL;
            if (held && assembly && !g_gevrStereo && sysArgCheck("--input-script")
                && sysArgCheck("--tactical-optic-preview"))
                alignOpticTestView(model, kind, m->matrix, assembly);
            const GevrVisualWeapon *remote = player ? NULL : gevrTacticalRemoteWeapon(model);
            const float manual = remote ? .1f * remote->manual : player ? gevrTacticalManualMotion(kind, hand) : 0.f;
            const float action = remote ? .1f * remote->action : player ? gevrTacticalActionMotion(kind, hand) : 0.f;
            Gfx *draw = buildGearGun(m, kind, assembly, magazine ? magazine : &fallback, TRUE,
                action, manual, held, hand, lowDetail, remote);
            if (!draw) return 0;
            if (player && manual > 0.f)
                draw = gevrTacticalChamberList(draw, kind, manual, gevrTacticalChamberState(kind, hand));
            *gdl = draw;
            m->used = ++s_modelClock;
            return 1;
        }
        if (!found || !loadMesh(m->asset)) continue;
        if (!m->built) build(m);
        if (!m->built) return 0;
        m->used = ++s_modelClock;
        if (isMineAsset(m->asset) || m->asset == TACT_ASSET_KNIFE || m->asset == TACT_ASSET_KATANA) {
            *gdl = node == m->drawNode ? cachedRigidList(&m->gpuList[TACT_CACHE_BODY], m->list, lowDetail) : NULL;
            const int remote = node == m->drawNode && m->asset == TACT_ASSET_KATANA
                && gevrTacticalRemoteWeapon(model) != NULL;
            const int logMask = remote ? 2 : 1;
            if (node == m->drawNode && !(m->logged & logMask)) {
                m->logged |= logMask;
                sysLogPrintf(LOG_NOTE, "tactical %s: drawing asset %d (%s)",
                    m->asset == TACT_ASSET_KATANA ? "katana" : m->asset == TACT_ASSET_KNIFE ? "Huntsman" : "mine", m->asset,
                    remote ? "teammate hand" : m->firstperson ? "held" : "world");
            }
            return 1;
        }
        if (m->asset == TACT_ASSET_RPG) {
            *gdl = NULL;
            if (node == m->drawNode) {
                const int loaded = gevrTacticalRpgHasRound(model);
                Gfx *list = loaded ? m->list : m->noMag;
                *gdl = m->firstperson ? list : cachedRigidList(
                    &m->gpuList[loaded ? TACT_CACHE_BODY : TACT_CACHE_MOUNTED_EMPTY], list, lowDetail);
            }
            return 1;
        }
        const int kind = gevrTacticalMeshGunKind(m->asset);
        const int playerAction = m->firstperson && ((m->asset != TACT_ASSET_SKORPION && m->asset != TACT_ASSET_SVD)
            || isPlayerGunModel(model));
        const int hand = playerAction && g_CurrentPlayer
            && model == &g_CurrentPlayer->hands[GUNLEFT].weaponModel ? GUNLEFT : GUNRIGHT;
        const float manual = playerAction ? gevrTacticalManualMotion(kind, hand) : 0.f;
        const float action = playerAction ? gevrTacticalActionMotion(kind, hand) : 0.f;
        if (playerAction && node == m->drawNode && (manual != m->lastManualAction || action != m->lastAction)) {
            TacticalAsset *mesh = &s_assets[m->asset];
            const int physical = sysArgCheck("--tactical-physical");
            int displaced = 0;
            for (int j = 0; j < mesh->ntri * 3; j++) {
                TacticalCorner *c = &mesh->corners[j];
                int moving = (m->asset == TACT_ASSET_SKORPION || m->asset == TACT_ASSET_SVD) ? c->tex == 1
                    : m->asset == 22 ? c->tex == 2 || c->tex == 3 : m->asset == 21 ? c->tex == 9 : (m->asset == 10 || m->asset == 11) ? c->tex == 6 || c->tex == 7
                    : m->asset == 0 ? c->tex == 8 || c->tex == 9
                    : m->asset == 1 ? c->tex >= 4 && c->tex <= 6
                    : c->tex == 0 || c->tex == (m->asset == 2 ? 8 : 7);
                float travel = (m->asset == 2 || m->asset == 3) && c->tex == 0 ? manual : action;
                if (moving) {
                    float z = 1000.f * c->p[1] + s_viewForwardOffset[m->firstperson];
                    if (m->asset == 0 && m->firstperson == 2 && physical) {
                        float x = 0, y = 0;
                        gevrTacticalScaleAkPoint(&x, &y, &z);
                    }
                    m->vertices[j].v.ob[2] = s16clamp(z - gevrTacticalActionShift(kind, travel, physical));
                    displaced += travel > 0.f;
                }
            }
            if (displaced && sysArgCheck("--input-script") && sysArgCheck("--tactical-cycle-audit"))
                sysLogPrintf(LOG_NOTE, "tactical cycle: kind %d legacy action, %d vertices, %.2f cm",
                    kind, displaced, (double)action);
            m->lastAction = action;
            m->lastManualAction = manual;
        }
        if (!m->logged) { m->logged = 1; sysLogPrintf(LOG_NOTE, "tactical mesh: drawing asset %d (%s)", m->asset, m->firstperson ? "first person" : "world"); }
        if (node != m->drawNode) *gdl = NULL;
        else if (m->asset == 0 && m->firstperson == 2 && sysArgCheck("--input-script")
                 && sysArgCheck("--tactical-ak-hands-preview")) {
            static Gfx preview[4];
            Gfx *gunHand = gevrTacticalAkHandList(3), *support = gevrTacticalAkSupportHandList();
            if (gunHand && support) {
                Gfx *g = preview;
                gSPDisplayList(g++, m->list); gSPDisplayList(g++, gunHand);
                gSPDisplayList(g++, support); gSPEndDisplayList(g++);
                *gdl = preview;
            } else *gdl = m->list;
        }
        else if (!m->firstperson && (m->asset == 0 || m->asset == 21) && model->attachedto && model->attachedto_objinst) {
            buildGuardAkFit(m);
            *gdl = m->npcList ? cachedRigidList(&m->gpuList[TACT_CACHE_GUARD], m->npcList, lowDetail)
                : cachedRigidList(&m->gpuList[TACT_CACHE_BODY], m->list, lowDetail);
        }
        else if (playerAction && !gevrTacticalInstalledMag(kind, hand)) {
            if ((m->asset == 10 || m->asset == 11 || m->asset == 22) && !m->magRemovedLogged) {
                m->magRemovedLogged = 1;
                sysLogPrintf(LOG_NOTE, "tactical PPK: drawing with magazine removed");
            }
            if (m->asset == TACT_ASSET_SKORPION && !m->magRemovedLogged) {
                m->magRemovedLogged = 1;
                sysLogPrintf(LOG_NOTE, "tactical Skorpion: drawing with magazine removed");
            }
            *gdl = m->noMag;
        }
        else if (m->firstperson && (m->asset == 2 || m->asset == 3) && gevrTacticalLoadedMagAsset(m->asset, hand) != m->asset) {
            buildOtherMagazine(m);
            *gdl = m->withOtherMag ? m->withOtherMag : m->list;
        } else *gdl = m->firstperson ? m->list : cachedRigidList(&m->gpuList[TACT_CACHE_BODY], m->list, lowDetail);
        if (node == m->drawNode && kind >= 0) {
            int build = -1;
            const TacticalMagazine installed = {.id = !playerAction || gevrTacticalInstalledMag(kind, hand)};
            const TacticalMagazine *mag = gevrTacticalGearModelMagazine(model, kind, &build);
            const GevrGearAssembly *assembly = gevrTacticalGearDeployedAssembly(build);
            if (!assembly && kind == TACTICAL_GUN_SVD) assembly = gevrTacticalGearStockAssembly(kind);
            if (assembly || (mag && mag->gear_part)) {
                const Mtxf *held = m->firstperson && isPlayerGunModel(model) && model->render_pos
                    && m->matrix < model->obj->numMatrices ? &model->render_pos[m->matrix].pos : NULL;
                if (held && assembly && !g_gevrStereo && sysArgCheck("--input-script")
                    && sysArgCheck("--tactical-optic-preview"))
                    alignOpticTestView(model, kind, m->matrix, assembly);
                const GevrVisualWeapon *remote = m->firstperson ? NULL : gevrTacticalRemoteWeapon(model);
                Gfx *gear = buildGearGun(m, kind, assembly, mag ? mag : &installed, TRUE,
                    remote ? .1f * remote->action : action, remote ? .1f * remote->manual : manual,
                    held, hand, lowDetail, remote);
                if (gear) *gdl = gear;
            }
        }
        if (m->firstperson && node == m->drawNode && *gdl && manual > 0.f)
            *gdl = gevrTacticalChamberList(*gdl, kind, manual, gevrTacticalChamberState(kind, hand));
        return 1;
    }
    return 0;
}
static TacticalModel *gevrTacticalHeldMagModel(int asset)
{
    static TacticalModel standalone[TACT_GLOVE_BASE];
    if (asset < 0 || asset >= TACTICAL_GUN_COUNT || asset == TACTICAL_GUN_AK74N) return NULL;
    if (asset == TACTICAL_GUN_SKORPION) asset = TACT_ASSET_SKORPION;
    else if (asset == TACTICAL_GUN_SVD) asset = TACT_ASSET_SVD;
    else if (asset == TACTICAL_GUN_MAKAROV) asset = 22;
    else if (asset == 1 && sysArgCheck("--tactical-ppk")) asset = 10;
    for (int i = 0; i < s_modelCount; i++)
        if (s_models[i].header && s_models[i].firstperson && s_models[i].asset == asset) {
            if (!s_models[i].built && loadMesh(asset)) build(&s_models[i]);
            if (s_models[i].built) return &s_models[i];
        }
    /* An enemy may carry a gun Bond has not equipped. Build its detached
     * magazine independently, so its texture/geometry survives the transfer. */
    if (!standalone[asset].built && loadMesh(asset)) {
        standalone[asset].asset = asset;
        standalone[asset].firstperson = asset == TACT_ASSET_SKORPION ? TACT_VIEW_SKORPION
            : asset == TACT_ASSET_SVD ? TACT_VIEW_SVD
            : asset == 0 ? 2 : (asset == 1 || asset == 10 || asset == 11 || asset == 22) ? 1 : asset + 1;
        build(&standalone[asset]);
    }
    return standalone[asset].built ? &standalone[asset] : NULL;
}
static int buildRpgRound(void)
{
    return gevrTacticalRpgEnabled()
        && buildTacticalWorldMesh(&s_rpgRound, TACT_ASSET_RPG_ROUND, "mods/tactical/rpg-round.bin");
}

void *gevrTacticalHeldMagList(int asset)
{
    if (asset == TACTICAL_GUN_RPG)
        return buildRpgRound() ? cachedRigidList(&s_rpgRound.gpuList[TACT_CACHE_BODY], s_rpgRound.list, FALSE) : NULL;
    if (tacticalGunIsExpanded(asset)) return gevrTacticalGearLooseMagList(0, asset);
    TacticalModel *m = gevrTacticalHeldMagModel(asset);
    return m ? cachedRigidList(&m->gpuList[TACT_CACHE_MAG], m->heldMag, FALSE) : NULL;
}
int gevrTacticalHeldMagBounds(int asset, float low[3], float high[3])
{
    if (asset == TACTICAL_GUN_RPG) {
        if (!low || !high || !buildRpgRound()) return 0;
        copyMeshBounds(&s_rpgRound, s_assets[TACT_ASSET_RPG_ROUND].ntri * 3, low, high);
        return 1;
    }
    if (tacticalGunIsExpanded(asset)) {
        Mtxf pose;
        return low && high && expandedMagazinePose(asset, 0, &pose, low, high) != NULL;
    }
    TacticalModel *m = gevrTacticalHeldMagModel(asset);
    if (!m || !low || !high || !m->heldVertices) return 0;
    if (!m->magBoundsReady) {
        TacticalAsset *mesh = &s_assets[m->asset];
        int found = 0;
        for (int i = 0; i < mesh->ntri * 3; i++) if (isMagazineTexture(m->asset, mesh->corners[i].tex)) {
            for (int j = 0; j < 3; j++) {
                float v = m->heldVertices[i].v.ob[j];
                if (!found || v < m->magLow[j]) m->magLow[j] = v;
                if (!found || v > m->magHigh[j]) m->magHigh[j] = v;
            }
            found = 1;
        }
        if (!found) return 0;
        m->magBoundsReady = 1;
    }
    memcpy(low, m->magLow, sizeof(m->magLow));
    memcpy(high, m->magHigh, sizeof(m->magHigh));
    return 1;
}

/* The pistol magazine held by the controller gets its own visual fit. The
 * rig and world keep the original upright mesh and physical item identity. */
void *gevrTacticalHandMagList(int asset)
{
    static TacticalModel held[2];
    int index = asset == 5 && sysArgCheck("--tactical-variety") ? 1 : asset == 1 && sysArgCheck("--tactical-ppk") ? 0 : -1;
    TacticalModel *m;
    if (index < 0 || !sysArgCheck("--tactical-ppk-hand-refit")) return gevrTacticalHeldMagList(asset);
    m=&held[index];
    if (!m->built && loadMesh(index ? 22 : 10)) {
        m->asset=index ? 22 : 10; m->firstperson=1; build(m);
        if (m->built) {
            TacticalAsset *mesh = &s_assets[m->asset];
            for (int i=0;i<mesh->ntri*3;i++) {
                Vtx *v=&m->heldVertices[i]; int y=v->v.ob[1],z=v->v.ob[2];
                v->v.ob[0]+=10;v->v.ob[1]=-z-45;v->v.ob[2]=y+75;
            }
            emitList(m,m->heldMag,2);
            sysLogPrintf(LOG_NOTE,"tactical: fitted pistol magazine visual asset %d",m->asset);
        }
    }
    return m->built ? m->heldMag : NULL;
}

static void magazineDetailVertex(Vtx *vertex, float x, float y, float z, u32 rgb, int rotated)
{
    *vertex = (Vtx){0};
    vertex->v.ob[0] = (s16)lroundf(x + (rotated ? 10.f : 0.f));
    vertex->v.ob[1] = (s16)lroundf(rotated ? -z - 45.f : y);
    vertex->v.ob[2] = (s16)lroundf(rotated ? y + 75.f : z);
    vertex->v.cn[0] = rgb >> 16;
    vertex->v.cn[1] = rgb >> 8;
    vertex->v.cn[2] = rgb;
    vertex->v.cn[3] = 255;
}

static Gfx *magazineCartridge(Gfx *gdl, float x, float y, float z, float radius, float length, int rotated)
{
    static const f32 ring[6][2] = {{1,0},{.5f,.866f},{-.5f,.866f},{-1,0},{-.5f,-.866f},{.5f,-.866f}};
    static const u32 brass[] = {0x9e6926,0xc99a40,0xecc468,0xe3b753,0xc4913c,0xa8762b};
    static const u32 copper[] = {0x75472b,0x9d633a,0xc18456,0xc48d5b,0xa57143,0x895333};
    Vtx *v = dynAllocateVertices(19);
    for (int i = 0; i < 6; ++i) {
        const float dx = ring[i][0]*radius, dy = ring[i][1]*radius;
        magazineDetailVertex(&v[i], x+dx, y+dy, z-length*.49f, brass[i], rotated);
        magazineDetailVertex(&v[i+6], x+dx, y+dy, z+length*.19f, brass[i], rotated);
        magazineDetailVertex(&v[i+12], x+.68f*dx, y+.68f*dy, z+length*.39f, copper[i], rotated);
    }
    magazineDetailVertex(&v[18], x, y, z+length*.54f, 0xc28751, rotated);
    gSPVertex(gdl++, osVirtualToPhysical(v), 19, 0);
    for (int i = 0; i < 6; ++i) {
        const int next = (i+1)%6;
        gSP2Triangles(gdl++, i,next,next+6,0, i,next+6,i+6,0);
        gSP2Triangles(gdl++, i+6,next+6,next+12,0, i+6,next+12,i+12,0);
        gSP1Triangle(gdl++, i+12,next+12,18,0);
    }
    gSP2Triangles(gdl++, 0,2,1,0, 0,3,2,0);
    gSP2Triangles(gdl++, 0,4,3,0, 0,5,4,0);
    return gdl;
}

/* Five small witness marks on the sides of a magazine inspected in the hand.
 * Reserve the fifth mark for a completely full magazine. */
static Gfx *magazineFill(Gfx *gdl, const float low[3], const float high[3],
    unsigned rounds, unsigned capacity, int rotated)
{
    const unsigned lit = rounds >= capacity ? 5u : (rounds*4u + capacity-1u)/capacity;
    const float z = (low[2]+high[2])*.5f, span = fminf(7.f, (high[2]-low[2])*.16f);
    const float step = (high[1]-low[1])*.115f;
    for (int side = 0; side < 2; ++side) {
        const float x = side ? high[0]+1.f : low[0]-1.f;
        Vtx *v = dynAllocateVertices(20);
        for (unsigned i = 0; i < 5; ++i) {
            const float y = low[1] + step*(1.f+i);
            const u32 color = i >= lit ? 0x484b45 : lit <= 1 ? 0xe5a44f : 0xcbdf9a;
            magazineDetailVertex(&v[4*i], x, y, z-span, color, rotated);
            magazineDetailVertex(&v[4*i+1], x, y+step*.58f, z-span, color, rotated);
            magazineDetailVertex(&v[4*i+2], x, y+step*.58f, z+span, color, rotated);
            magazineDetailVertex(&v[4*i+3], x, y, z+span, color, rotated);
        }
        gSPVertex(gdl++, osVirtualToPhysical(v), 20, 0);
        for (int i = 0; i < 20; i += 4)
            gSP2Triangles(gdl++, i,i+1,i+2,0, i,i+2,i+3,0);
    }
    return gdl;
}

void *gevrTacticalDrawMagazineRounds(void *drawList, int asset, unsigned rounds, unsigned displayCapacity,
    int inHand, unsigned part)
{
    Gfx *gdl = drawList;
    if ((!rounds && !displayCapacity) || asset < 0 || asset >= TACTICAL_GUN_COUNT
        || asset == TACTICAL_GUN_RPG) return gdl;
    const int pistol = tacticalGunIsPistol(asset);
    const int skorpion = asset == TACTICAL_GUN_SKORPION;
    const int smg = asset == TACTICAL_GUN_MP5 || asset == TACTICAL_GUN_MP5K || skorpion;
    const int expanded = tacticalGunIsExpanded(asset);
    float low[3], high[3];
    if (expanded) {
        Mtxf pose;
        if (!expandedMagazinePose(asset, part, &pose, low, high)) return gdl;
    } else if (displayCapacity && !gevrTacticalHeldMagBounds(asset, low, high)) {
        displayCapacity = 0;
    }
    const int looseRounds = asset != TACTICAL_GUN_PKM && asset != TACTICAL_GUN_M32A1;
    const int count = !rounds || !looseRounds ? 0 : !pistol && !smg && !expanded && rounds > 1 ? 2 : 1;
    const int rotated = pistol && !expanded && inHand && sysArgCheck("--tactical-ppk-hand-refit");
    gDPPipeSync(gdl++); gSPTexture(gdl++,0,0,0,G_TX_RENDERTILE,G_OFF);
    gSPClearGeometryMode(gdl++,G_LIGHTING | G_CULL_BOTH | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
    gSPSetGeometryMode(gdl++,G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);
    gDPSetCombineMode(gdl++,G_CC_SHADE,G_CC_SHADE);
    for (int c = 0; c < count; ++c) {
        int x = count == 2 ? (c ? 6 : -6) : 0;
        int y = skorpion ? 64 : asset == 5 ? 53 : pistol ? 46 : asset == TACTICAL_GUN_MP5 ? 106 : smg ? 65 : 120;
        int z = skorpion ? -14 : pistol ? 7 : smg ? -6 : -32;
        int radius = pistol || smg ? 3 : 4, len = pistol || smg ? 18 : 34;
        if (expanded) {
            y = (int)high[1] - 2;
            z = -4;
            radius = 3;
            len = 20;
        }
        gdl = magazineCartridge(gdl, x, y, z, radius, len, rotated);
    }
    if (displayCapacity) gdl = magazineFill(gdl, low, high, rounds, displayCapacity, rotated);
    return gdl;
}

/* A standalone, matrix-free weapon for the body mount. It keeps its own
 * geometry, so animating the weapon in the hand cannot move the mounted gun. */
static TacticalModel *mountedGunModel(int asset, int suppressed, int *index)
{
    static TacticalModel mounted[TACTICAL_GUN_COUNT + 1];
    static const int modelKind[TACTICAL_GUN_COUNT] = {
        2, 1, 3, 4, 2, 1, TACT_VIEW_SKORPION, TACT_VIEW_SVD, [TACTICAL_GUN_RPG] = TACT_VIEW_RPG
    };
    if (asset < 0 || asset >= TACTICAL_GUN_COUNT) return NULL;
    const int ppk = asset == TACTICAL_GUN_USP && sysArgCheck("--tactical-ppk");
    *index = ppk && suppressed ? TACTICAL_GUN_COUNT : asset;
    TacticalModel *m = &mounted[*index];
    if (!m->built) {
        m->asset = ppk ? (suppressed ? 11 : 10)
            : asset == TACTICAL_GUN_AK74N ? 21 : asset == TACTICAL_GUN_MAKAROV ? 22
            : asset == TACTICAL_GUN_SKORPION ? TACT_ASSET_SKORPION : asset == TACTICAL_GUN_SVD ? TACT_ASSET_SVD
            : asset == TACTICAL_GUN_RPG ? TACT_ASSET_RPG : asset;
        m->firstperson = modelKind[asset];
        if (!loadMesh(m->asset)) return NULL;
        build(m);
        if (!m->built) return NULL;
    }
    return m;
}

static Gfx *cachedGunList(TacticalModel *model, Gfx **list, int filter, int lowDetail);

static Gfx *mountedGunBody(TacticalModel *model, int withMagazine)
{
    Gfx *body = cachedGunList(model, &model->mountedBody[!!withMagazine], withMagazine ? 3 : 4, FALSE);
    return cachedRigidList(&model->gpuList[withMagazine ? TACT_CACHE_MOUNTED_BODY : TACT_CACHE_MOUNTED_EMPTY], body, FALSE);
}

void *gevrTacticalBladePreview(int item, float low[3], float high[3])
{
    static TacticalModel preview[2];
    const int katana = item == ITEM_TACTICAL_KATANA;
    if (!(katana ? gevrTacticalKatanaEnabled() : gevrTacticalKnifeEnabled())) return NULL;
    TacticalModel *model = &preview[katana];
    if (!model->built) {
        model->asset = katana ? TACT_ASSET_KATANA : TACT_ASSET_KNIFE;
        model->firstperson = TACT_VIEW_KNIFE;
        build(model);
    }
    if (!model->built) return NULL;
    copyMeshBounds(model, s_assets[model->asset].ntri * 3, low, high);
    return mountedGunBody(model, TRUE);
}

void *gevrTacticalMountedGunList(int asset, int suppressed, int withMagazine)
{
    if (tacticalGunIsExpanded(asset)) {
        const TacticalMagazine magazine = {.id = !!withMagazine};
        return gevrTacticalMountedGearList(asset, -1, suppressed, &magazine);
    }
    int index;
    TacticalModel *m = mountedGunModel(asset, suppressed, &index);
    if (!m) return NULL;
    if (asset == TACTICAL_GUN_SVD) {
        const TacticalMagazine magazine = {.id = !!withMagazine};
        Gfx *stock = buildGearGun(m, asset, gevrTacticalGearStockAssembly(asset), &magazine, FALSE, 0.f, 0.f, NULL, GUNRIGHT, FALSE, NULL);
        if (stock) return stock;
    }
    return mountedGunBody(m, withMagazine);
}

static void copyMeshBounds(TacticalModel *m, int vertices, float low[3], float high[3])
{
    if (!m->boundsReady) {
        for (int j = 0; j < 3; ++j) m->low[j] = m->high[j] = m->vertices[0].v.ob[j];
        for (int i = 1; i < vertices; ++i) for (int j = 0; j < 3; ++j) {
            const float coordinate = m->vertices[i].v.ob[j];
            if (coordinate < m->low[j]) m->low[j] = coordinate;
            if (coordinate > m->high[j]) m->high[j] = coordinate;
        }
        m->boundsReady = 1;
    }
    memcpy(low, m->low, sizeof(m->low));
    memcpy(high, m->high, sizeof(m->high));
}

int gevrTacticalMountedGunBounds(int asset, int suppressed, float low[3], float high[3])
{
    if (tacticalGunIsExpanded(asset)) return expandedGunBounds(asset, low, high);
    int index;
    TacticalModel *m = mountedGunModel(asset, suppressed, &index);
    if (!m) return 0;
    copyMeshBounds(m, s_assets[m->asset].ntri * 3, low, high);
    return 1;
}

static Gfx *gearPartList(int part, int housing, float low[3], float high[3], int lowDetail)
{
    const GevrGearPart *info = gevrTacticalGearPart(part);
    if (!info || !(info->art & GEVR_GEAR_ART_MESH) || part >= GEVR_GEAR_PARTS) return NULL;
    TacticalGearMesh *entry = &s_gearMeshes[part];
    if (housing) {
        TacticalGearMesh **slot = &s_gearOpticMeshes[housing-1][part];
        if (!*slot) *slot = calloc(1, sizeof(*entry));
        entry = *slot;
        if (!entry) return NULL;
    }
    TacticalAsset *mesh = &entry->mesh;
    TacticalModel *model = &entry->model;
    if (!model->built) {
        char path[80];
        snprintf(path, sizeof(path), "mods/tactical/gear/%s/%s%s.bin", housing ? "optics" : "meshes",
            info->id, housing == 2 ? "-folded" : "");
        if (!readMeshFile(mesh, path, 0, housing ? 1 : 0)) return NULL;
        const size_t vertices = (size_t)mesh->ntri * 3;
        model->vertices = calloc(vertices, sizeof(*model->vertices));
        model->list = calloc(16 + (size_t)mesh->ntex * 16 + (size_t)mesh->ntri * 2, sizeof(*model->list));
        if (!model->vertices || !model->list) {
            free(model->vertices);
            free(model->list);
            model->vertices = NULL;
            model->list = NULL;
            return NULL;
        }
        for (size_t i = 0; i < vertices; ++i) {
            Vtx *v = &model->vertices[i];
            const TacticalCorner *corner = &mesh->corners[i];
            for (int j = 0; j < 3; ++j) v->v.ob[j] = s16clamp(1000.f * corner->p[j]);
            memcpy(v->v.cn, corner->rgba, 4);
        }
        Gfx *g = model->list;
        gDPPipeSync(g++);
        gDPSetTexturePersp(g++, G_TP_PERSP);
        gDPSetTextureLUT(g++, G_TT_NONE);
        gSPClearGeometryMode(g++, G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR | G_LIGHTING | G_CULL_BOTH);
        gSPSetGeometryMode(g++, G_SHADE | G_SHADING_SMOOTH);
        gDPSetCombineMode(g++, G_CC_MODULATERGBA, G_CC_MODULATERGBA2);
        for (int tex = 0; tex < mesh->ntex; ++tex)
            g = emitMeshTexture(g, mesh, model->vertices, tex,
                housing ? s_gearOpticKeys[housing-1][part] : s_gearTexKeys[part][tex]);
        gSPEndDisplayList(g++);
        model->built = 1;
    }
    if (low && high) copyMeshBounds(model, mesh->ntri * 3, low, high);
    return cachedRigidList(&model->gpuList[TACT_CACHE_BODY], model->list, lowDetail);
}

void *gevrTacticalGearPartList(int part, float low[3], float high[3])
{
    return gearPartList(part, FALSE, low, high, FALSE);
}

/* Keep the same authoring scale, axis conversion and grip correction as the
 * original AK viewmodels. The workbench files store the unscaled pack axes. */
static Mtxf gearGunAxes(int kind, int firstperson)
{
    const GevrTacticalExpandedFit *fit = gevrTacticalExpandedFit(kind);
    if (fit) {
        const float s = fit->scale;
        if (firstperson)
            return (Mtxf){.m = {{fit->lateral*s,0,0,0}, {0,0,s,0}, {0,-s,0,0},
                {0,fit->lift,fit->forward,1}}};
        return (Mtxf){.m = {{0,-fit->lateral*s,0,0}, {-s,0,0,0}, {0,0,-s,0},
            {tacticalGunIsPistol(kind) ? 48.f : 125.f,0,-12.f,1}}};
    }
    if (kind == TACTICAL_GUN_SVD || kind == TACTICAL_GUN_SKORPION || kind == TACTICAL_GUN_USP) {
        const int pistol = kind == TACTICAL_GUN_USP;
        const int skorpion = kind == TACTICAL_GUN_SKORPION;
        if (firstperson) return (Mtxf){.m = {{-1,0,0,0}, {0,0,1,0}, {0,-1,0,0},
            {0,pistol ? 7 : 20,pistol ? -70 : 0,1}}};
        return (Mtxf){.m = {{0,1,0,0}, {-1,0,0,0}, {0,0,-1,0},
            {pistol ? 155 : skorpion ? 35 : -25,0,pistol ? 8 : skorpion ? 20 : -12,1}}};
    }
    const int mp5 = kind == TACTICAL_GUN_MP5, mp5k = kind == TACTICAL_GUN_MP5K, pm = kind == TACTICAL_GUN_MAKAROV;
    const float scale = mp5 ? 2.018f : mp5k ? 2.0532f : pm ? 1.660014f
        : kind == TACTICAL_GUN_AK74N ? 1.7318101f : 1.7450793f;
    if (!firstperson)
        return (Mtxf){.m = {{0,-scale,0,0}, {-scale,0,0,0}, {0,0,-scale,0},
            {mp5 ? 122 : mp5k ? -51 : pm ? 155 : -25,0,pm ? 8 : -12,1}}};
    float s = scale, x = 0, y = pm ? 7.f : 20.f, z = s_viewForwardOffset[mp5 ? 3 : mp5k ? 4 : pm ? 1 : 2];
    if (kind == TACTICAL_GUN_AK && sysArgCheck("--tactical-physical")) {
        s *= 1.2f;
        gevrTacticalScaleAkPoint(&x, &y, &z);
    }
    return (Mtxf){.m = {{s,0,0,0}, {0,0,s,0}, {0,-s,0,0}, {x,y,z,1}}};
}

static Mtxf gearComposeFrame(const float frame[4][4], const Mtxf *gun)
{
    Mtxf pose;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            pose.m[row][col] = 0;
            for (int k = 0; k < 4; ++k) pose.m[row][col] += frame[row][k] * gun->m[k][col];
        }
    }
    return pose;
}

static void transformBounds(const Mtxf *pose, float low[3], float high[3])
{
    float nextLow[3], nextHigh[3];
    for (int col = 0; col < 3; ++col) {
        nextLow[col] = nextHigh[col] = pose->m[3][col];
        for (int row = 0; row < 3; ++row) {
            const float a = low[row] * pose->m[row][col], b = high[row] * pose->m[row][col];
            nextLow[col] += fminf(a, b);
            nextHigh[col] += fmaxf(a, b);
        }
    }
    memcpy(low, nextLow, sizeof(nextLow));
    memcpy(high, nextHigh, sizeof(nextHigh));
}

static Gfx *expandedMagazinePose(int kind, unsigned part, Mtxf *pose, float low[3], float high[3])
{
    const GevrGearAssembly *stock = gevrTacticalGearStockAssembly(gevrTacticalGearKey(kind));
    const GevrGearMount *well = gevrTacticalGearFindMount(stock, "mod_magazine");
    if (!well) return NULL;
    Gfx *list = gevrTacticalGearPartList(part ? (int)part - 1 : well->part, low, high);
    if (!list) return NULL;
    const Mtxf root = gearGunAxes(kind, TRUE);
    *pose = gearComposeFrame(well->frame, &root);
    transformBounds(pose, low, high);
    for (int i = 0; i < 3; ++i) {
        const float center = .5f * (high[i] + low[i]);
        pose->m[3][i] -= center;
        low[i] -= center;
        high[i] -= center;
    }
    return list;
}

int gevrTacticalGearMagazineContact(int kind, const GevrGearAssembly *assembly, unsigned int part, float out[3])
{
    const GevrGearMount *well = gevrTacticalGearFindMount(assembly, "mod_magazine");
    if (!well) return 0;
    float low[3], high[3];
    if (!gevrTacticalGearPartList(part ? (int)part - 1 : well->part, low, high)) return 0;
    const Mtxf root = gearGunAxes(kind, TRUE);
    const Mtxf pose = gearComposeFrame(well->frame, &root);
    transformBounds(&pose, low, high);
    for (int i = 0; i < 3; ++i) out[i] = .5f * (low[i] + high[i]);
    return 1;
}

static int gearMuzzlePoint(int kind, const GevrGearAssembly *assembly, int held, float out[3])
{
    if (!assembly) return 0;
    const Mtxf root = gearGunAxes(kind, held);
    const int lengthAxis = held ? 2 : 0;
    int found = 0;
    for (int i = 1; i < assembly->count; ++i) {
        const GevrGearMount *mount = &assembly->mounts[i];
        const char *key = mount->socket ? mount->socket->key : "";
        if (strncmp(key, "mod_muzzle", 10) && strcmp(key, "mod_barrel")) continue;
        float low[3], high[3];
        if (!gevrTacticalGearPartList(mount->part, low, high)) continue;
        const Mtxf pose = gearComposeFrame(mount->frame, &root);
        transformBounds(&pose, low, high);
        const float end = held ? high[lengthAxis] : low[lengthAxis];
        if (found && (held ? end <= out[lengthAxis] : end >= out[lengthAxis])) continue;
        memcpy(out, pose.m[3], 3 * sizeof(float));
        out[lengthAxis] = end;
        found = 1;
    }
    return found;
}

int gevrTacticalGearMuzzlePoint(int kind, const GevrGearAssembly *assembly, float out[3])
{
    return gearMuzzlePoint(kind, assembly, TRUE, out);
}

static const GevrGearAssembly *guardAssembly(const Model *model, int kind)
{
    int build = -1;
    gevrTacticalGearModelMagazine(model, kind, &build);
    const GevrGearAssembly *assembly = gevrTacticalGearDeployedAssembly(build);
    return assembly ? assembly : gevrTacticalGearStockAssembly(gevrTacticalGearKey(kind));
}

static int guardGripBounds(const GevrGearAssembly *assembly, const Mtxf *root,
    const char *key, const char *alternate, float low[3], float high[3])
{
    if (!assembly) return FALSE;
    for (int i = 1; i < assembly->count; ++i) {
        const GevrGearMount *mount = &assembly->mounts[i];
        const char *slot = mount->socket->key;
        if (strcmp(slot, key) && (!alternate || strcmp(slot, alternate))) continue;
        if (!gevrTacticalGearPartList(mount->part, low, high)) continue;
        const Mtxf pose = gearComposeFrame(mount->frame, root);
        transformBounds(&pose, low, high);
        return TRUE;
    }
    return FALSE;
}

void gevrTacticalFitGuardWeapon(Model *model, int item)
{
    const int kind = gevrTacticalItem(item);
    if (!model || !model->obj || !model->render_pos || !tacticalGunIsExpanded(kind)) return;
    const Mtxf root = gearGunAxes(kind, FALSE);
    const Mtxf player = gearGunAxes(kind, TRUE);
    float grip[3] = {root.m[3][0] + player.m[3][2], 0.f, root.m[3][2] - player.m[3][1]};
    float low[3], high[3];
    if (guardGripBounds(guardAssembly(model, kind), &root, "mod_pistol_grip", "mod_pistolgrip", low, high)) {
        grip[0] = .5f * (low[0] + high[0]);
        grip[1] = .5f * (low[1] + high[1]);
        grip[2] = .37f * low[2] + .63f * high[2];
    }
    /* The held model is already in the wrist's frame. Move all weapon
     * joints together so attachments and the muzzle follow the grip. */
    const Mtxf *pose = &model->render_pos[0].pos;
    float shift[3];
    for (int k = 0; k < 3; ++k)
        shift[k] = -grip[0] * pose->m[0][k] - grip[1] * pose->m[1][k] - grip[2] * pose->m[2][k];
    for (int m = 0; m < model->obj->numMatrices; ++m)
        for (int k = 0; k < 3; ++k) model->render_pos[m].pos.m[3][k] += shift[k];
}

int gevrTacticalGuardSupportContact(const Model *model, int item, float out[3])
{
    const int kind = gevrTacticalItem(item);
    if (kind < 0 || tacticalGunIsPistol(kind) || kind == TACTICAL_GUN_RPG) return FALSE;
    const GevrTacticalExpandedFit *fit = gevrTacticalExpandedFit(kind);
    if (fit) {
        const Mtxf root = gearGunAxes(kind, FALSE);
        const GevrGearAssembly *assembly = guardAssembly(model, kind);
        float low[3], high[3];
        out[0] = root.m[3][0] - (fit->support[2] - fit->forward);
        out[1] = -fit->support[0] - 9.f;
        out[2] = root.m[3][2] + fit->support[1] - fit->lift;
        if (guardGripBounds(assembly, &root, "mod_foregrip", NULL, low, high)) {
            out[0] = .5f * (low[0] + high[0]);
            out[1] = .5f * (low[1] + high[1]) - 9.f;
            out[2] = .45f * low[2] + .55f * high[2];
        } else if (guardGripBounds(assembly, &root, "mod_handguard", NULL, low, high)) {
            const float inset = (high[0] - low[0]) * .19f;
            out[0] = fminf(high[0] - inset, fmaxf(low[0] + inset, out[0]));
            out[2] = low[2] + (high[2] - low[2]) * .19f;
        }
        return TRUE;
    }
    /* The original replacement meshes were fitted directly in Pchr units. */
    static const float legacy[TACTICAL_GUN_COUNT][3] = {
        [TACTICAL_GUN_AK] = {-137.f, -8.f, 38.f},
        [TACTICAL_GUN_AK74N] = {-243.f, -8.f, 38.f},
        [TACTICAL_GUN_MP5] = {-146.f, -8.f, -24.f},
        [TACTICAL_GUN_MP5K] = {-104.f, -8.f, -53.f},
        [TACTICAL_GUN_SKORPION] = {-61.f, -28.f, -34.f},
        [TACTICAL_GUN_SVD] = {-270.f, -8.f, -42.f},
    };
    if (legacy[kind][0] == 0.f) return FALSE;
    memcpy(out, legacy[kind], sizeof(legacy[kind]));
    return TRUE;
}

static void meshMuzzlePoint(TacticalModel *m)
{
    /* The rim at the very front of the installed mesh, including integral cans. */
    const int axis = m->firstperson ? 2 : 0;
    const float sign = m->firstperson ? 1.f : -1.f;
    const int count = s_assets[m->asset].ntri * 3;
    float front = -32768.f;
    for (int i = 0; i < count; ++i)
        if (!isMagazineTexture(m->asset, s_assets[m->asset].corners[i].tex))
            front = fmaxf(front, sign * m->vertices[i].v.ob[axis]);
    float low[3] = {32768,32768,32768}, high[3] = {-32768,-32768,-32768};
    for (int i = 0; i < count; ++i) {
        const Vtx *v = &m->vertices[i];
        if (isMagazineTexture(m->asset, s_assets[m->asset].corners[i].tex)
            || sign * v->v.ob[axis] < front - 4.f) continue;
        for (int k = 0; k < 3; ++k) {
            low[k] = fminf(low[k], v->v.ob[k]);
            high[k] = fmaxf(high[k], v->v.ob[k]);
        }
    }
    for (int k = 0; k < 3; ++k) m->muzzle[k] = .5f * (low[k] + high[k]);
    m->muzzle[axis] = sign * front;
    m->muzzleReady = TRUE;
}

int gevrTacticalMeshMuzzleFrame(const Model *model, int item, float out[4][4],
    const GevrGearAssembly **assembly)
{
    const int kind = gevrTacticalItem(item);
    if (kind < 0 || !model || !model->obj || !model->render_pos) return FALSE;
    for (int i = 0; i < s_modelCount; ++i) {
        TacticalModel *m = &s_models[i];
        if (m->header != model->obj || m->matrix >= model->obj->numMatrices) continue;
        int build = -1;
        gevrTacticalGearModelMagazine(model, kind, &build);
        *assembly = gevrTacticalGearDeployedAssembly(build);
        const int expanded = tacticalGunIsExpanded(kind);
        if (!*assembly && (expanded || kind == TACTICAL_GUN_SVD))
            *assembly = gevrTacticalGearStockAssembly(gevrTacticalGearKey(kind));

        float at[3];
        const int held = m->firstperson != 0;
        if (kind == TACTICAL_GUN_RPG) {
            at[0] = held ? 0.f : -GEVR_RPG_MUZZLE_Z - 25.f;
            at[1] = held ? GEVR_RPG_MUZZLE_Y : 0.f;
            at[2] = held ? GEVR_RPG_MUZZLE_Z : GEVR_RPG_MUZZLE_Y - 32.f;
        } else if (!gearMuzzlePoint(kind, *assembly, held, at)) {
            if (expanded) {
                const GevrTacticalExpandedFit *fit = gevrTacticalExpandedFit(kind);
                if (!fit || !*assembly) return FALSE;
                const Mtxf view = gearGunAxes(kind, TRUE), world = gearGunAxes(kind, FALSE);
                at[0] = held ? fit->muzzle[0] : world.m[3][0] - (fit->muzzle[2] - view.m[3][2]);
                at[1] = held ? fit->muzzle[1] : -fit->muzzle[0];
                at[2] = held ? fit->muzzle[2] : world.m[3][2] + fit->muzzle[1] - view.m[3][1];
            } else {
                if (!m->built) return FALSE;
                if (!m->muzzleReady) meshMuzzlePoint(m);
                memcpy(at, m->muzzle, sizeof(at));
                if (!held && m->npcList && model->attachedto && model->attachedto_objinst) {
                    at[0] += 133.f; at[1] -= 8.f; at[2] += 45.f;
                }
            }
        }

        const Mtxf *pose = &model->render_pos[m->matrix].pos;
        memcpy(out, pose->m, sizeof(pose->m));
        for (int k = 0; k < 3; ++k) {
            out[3][k] += at[0] * pose->m[0][k] + at[1] * pose->m[1][k] + at[2] * pose->m[2][k];
            if (!held) {
                out[0][k] = -pose->m[1][k];
                out[1][k] = pose->m[2][k];
                out[2][k] = -pose->m[0][k];
            }
        }
        return TRUE;
    }
    return FALSE;
}

static int expandedGunBounds(int kind, float low[3], float high[3])
{
    const GevrGearAssembly *stock = gevrTacticalGearStockAssembly(gevrTacticalGearKey(kind));
    if (!stock) return 0;
    const Mtxf root = gearGunAxes(kind, TRUE);
    int haveBounds = 0;
    for (int i = 0; i < stock->count; ++i) {
        const GevrGearMount *mount = &stock->mounts[i];
        float a[3], b[3];
        if (!gevrTacticalGearPartList(mount->part, a, b)) continue;
        const Mtxf pose = gearComposeFrame(mount->frame, &root);
        transformBounds(&pose, a, b);
        for (int axis = 0; axis < 3; ++axis) {
            low[axis] = haveBounds ? fminf(low[axis], a[axis]) : a[axis];
            high[axis] = haveBounds ? fmaxf(high[axis], b[axis]) : b[axis];
        }
        haveBounds = 1;
    }
    return haveBounds;
}

int gevrTacticalMagnifierFlick(int kind, const float gun[4][4], const float tip[3], float cm, unsigned tick)
{
    const GevrGearAssembly *assembly = gevrTacticalGearDeployedAssembly(kind);
    if (!assembly) return FALSE;
    for (int i = 1; i < assembly->count; ++i) {
        const GevrGearMount *mount = &assembly->mounts[i];
        if (!gevrTacticalOpticCanFlip(mount->part)) continue;
        const GevrOpticMode *mode = gevrTacticalOpticMode(mount->part, NULL);
        Mtxf root;
        memcpy(root.m, gun, sizeof(root.m));
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col) root.m[row][col] *= .1f;
        const int key = gevrTacticalGearBaseBuild(kind);
        const int gunKind = key == GEVR_GEAR_PPK_SUPPRESSED ? TACTICAL_GUN_USP
            : key > GEVR_GEAR_PPK_SUPPRESSED ? key - 1 : key;
        const Mtxf axes = gearGunAxes(gunKind, TRUE);
        const Mtxf view = gearComposeFrame(axes.m, &root);
        const Mtxf pose = gearComposeFrame(mount->frame, &view);
        /* Contact centres on the HHS-1 moving housing, measured in the two
         * imported mesh states. The tan version shares this geometry. */
        static const float contact[2][3] = {{-2, -44, -22}, {-25, -44, -14}};
        const float *from = contact[mode->folded];
        const float *to = contact[!mode->folded];
        float along[3] = {0}, delta[3];
        float travelSquared = 0.f, distanceSquared = 0.f, projection = 0.f;
        for (int col = 0; col < 3; ++col) {
            delta[col] = tip[col] - pose.m[3][col];
            for (int row = 0; row < 3; ++row) {
                delta[col] -= from[row] * pose.m[row][col];
                along[col] += (to[row] - from[row]) * pose.m[row][col];
            }
            delta[col] /= cm;
            travelSquared += along[col] * along[col];
            distanceSquared += delta[col] * delta[col];
            projection += delta[col] * along[col];
        }
        if (travelSquared <= 1e-6f) return FALSE;
        const float projectedCm = projection / sqrtf(travelSquared);
        if (!gevrTacticalOpticFlick(mount->part, tick, projectedCm,
                fmaxf(0.f, distanceSquared - projectedCm * projectedCm))) return FALSE;
        const GevrGearPart *part = gevrTacticalGearPart(mount->part);
        sysLogPrintf(LOG_NOTE, "tactical optics: hand flick %s %s", part->name,
            mode->folded ? "deployed" : "folded");
        return TRUE;
    }
    gevrTacticalOpticFlickReset();
    return FALSE;
}

static void gearPartMatrix(Mtx *matrix, const float frame[4][4], const Mtxf *gun, float slide)
{
    Mtxf pose = gearComposeFrame(frame, gun);
    pose.m[3][2] -= slide;
    guMtxF2L(pose.m, matrix);
}

/* Cylinders retain their spent shells. Index the drum around its authored
 * forward axis as each in-game chamber is used. */
static void gearCylinderMatrix(Mtx *matrix, const GevrGearMount *well, const Mtxf *gun,
    const float low[3], const float high[3], float turn)
{
    Mtxf pose = gearComposeFrame(well->frame, gun);
    Mtxf pivot, rotated;
    const coord3d centre = {.f = { .5f*(low[0]+high[0]), 0.f, .5f*(low[2]+high[2]) }};
    coord3d offset = centre;
    matrix_4x4_set_rotation_around_y(turn, &pivot);
    mtx4RotateVecInPlace(&pivot, &offset);
    for (int k = 0; k < 3; ++k) pivot.m[3][k] = centre.f[k] - offset.f[k];
    matrix_4x4_multiply_homogeneous(&pose, &pivot, &rotated);
    guMtxF2L(rotated.m, matrix);
}

static int gearUnderSocket(const char *path, const char *socket)
{
    const size_t size = strlen(socket);
    return !strncmp(path, socket, size) && (!path[size] || path[size] == '/');
}

static Gfx *gearDrawPart(Gfx *g, Gfx *part, Mtx *matrix)
{
    gSPMatrix(g++, osVirtualToPhysical(matrix), G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_PUSH);
    gSPDisplayList(g++, part);
    gSPPopMatrix(g++, G_MTX_MODELVIEW);
    return g;
}

/* The bounds and the detached display list use the same magazine transform,
 * including the selected Workshop magazine rather than the stock outline. */
static Gfx *looseGearMagazinePose(unsigned part, int kind, Mtxf *pose, float low[3], float high[3])
{
    if (part > GEVR_GEAR_PARTS || kind < 0 || kind >= TACTICAL_GUN_COUNT) return NULL;
    if (tacticalGunIsExpanded(kind)) return expandedMagazinePose(kind, part, pose, low, high);
    if (!part) return NULL;

    const GevrGearMount *well = gevrTacticalGearFindMount(gevrTacticalGearStockAssembly(kind), "mod_magazine");
    Gfx *list = gevrTacticalGearPartList((int)part - 1, low, high);
    if (!list || !well) return NULL;
    static const float centre[TACTICAL_GUN_COUNT][2] = {
        [TACTICAL_GUN_AK] = {-93, 183}, [TACTICAL_GUN_AK74N] = {-93, 146},
        [TACTICAL_GUN_USP] = {-22, 34}, [TACTICAL_GUN_MAKAROV] = {-35, -60},
        [TACTICAL_GUN_MP5] = {-104, 143}, [TACTICAL_GUN_MP5K] = {-63, 130},
        [TACTICAL_GUN_SKORPION] = {GEVR_SKORPION_MAG_Y, GEVR_SKORPION_MAG_Z},
        [TACTICAL_GUN_SVD] = {GEVR_SVD_MAG_Y, GEVR_SVD_MAG_Z}
    };
    Mtxf root = gearGunAxes(kind, TRUE);
    root.m[3][1] -= centre[kind][0];
    root.m[3][2] -= centre[kind][1];
    *pose = gearComposeFrame(well->frame, &root);
    if (low && high) transformBounds(pose, low, high);
    return list;
}

int gevrTacticalLooseMagBounds(unsigned int part, int kind, float low[3], float high[3])
{
    Mtxf pose;
    return looseGearMagazinePose(part, kind, &pose, low, high) != NULL
        || gevrTacticalHeldMagBounds(kind, low, high);
}

void *gevrTacticalGearLooseMagList(unsigned int part, int kind)
{
    Mtxf pose;
    /* Expanded magazines need their bounds to compute the detached centre. */
    float low[3], high[3];
    const int expanded = tacticalGunIsExpanded(kind);
    Gfx *list = looseGearMagazinePose(part, kind, &pose, expanded ? low : NULL, expanded ? high : NULL);
    if (!list) return NULL;

    Mtx *matrix = dynAllocateMatrix();
    guMtxF2L(pose.m, matrix);
    Gfx *draw = dynAllocate(4 * sizeof(Gfx));
    Gfx *g = gearDrawPart(draw, list, matrix);
    gSPEndDisplayList(g++);
    return draw;
}

static Gfx *cachedGunList(TacticalModel *model, Gfx **list, int filter, int lowDetail)
{
    if (!*list) {
        const TacticalAsset *mesh = &s_assets[model->asset];
        const size_t size = (size_t)mesh->ntex * 16 + (size_t)mesh->ntri * 2 + 16;
        *list = calloc(size, sizeof(Gfx));
        if (*list) emitList(model, *list, filter);
    }
    if (model->firstperson) return *list;
    const int cache = filter == TACT_LIST_GEAR_CORE ? TACT_CACHE_CORE
        : filter == TACT_LIST_INSTALLED_MAG ? TACT_CACHE_INSTALLED_MAG
        : filter >= TACT_LIST_REMOTE_CORE ? TACT_CACHE_REMOTE_CORE + filter - TACT_LIST_REMOTE_CORE
        : TACT_CACHE_GEAR_BODY;
    return cachedRigidList(&model->gpuList[cache], *list, lowDetail);
}

static float opticAxis(const Mtxf *pose, const float axis[3], float out[3])
{
    float length = 0.f;
    for (int i = 0; i < 3; ++i) {
        out[i] = axis[0]*pose->m[0][i] + axis[1]*pose->m[1][i] + axis[2]*pose->m[2][i];
        length += out[i]*out[i];
    }
    length = sqrtf(length);
    if (length > 1e-6f)
        for (int i = 0; i < 3; ++i) out[i] /= length;
    return length;
}

static void opticSurface(GevrOpticSurface *surface, const GevrOpticMode *mode, float zoom, const Mtxf *pose)
{
    *surface = (GevrOpticSurface){.mode = mode, .zoom = zoom};
    surface->half[0] = 1000.f * mode->half[0] * opticAxis(pose, mode->right, surface->right);
    surface->half[1] = 1000.f * mode->half[1] * opticAxis(pose, mode->up, surface->up);
    opticAxis(pose, mode->forward, surface->forward);
    memcpy(surface->aim, surface->forward, sizeof(surface->aim));
    for (int i = 0; i < 3; ++i) {
        surface->centre[i] = pose->m[3][i] + 1000.f * (mode->centre[0]*pose->m[0][i]
            + mode->centre[1]*pose->m[1][i] + mode->centre[2]*pose->m[2][i]);
    }
}

static float opticDot(const float a[3], const float b[3])
{
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}

typedef struct PlayerDevices {
    const GevrGearAssembly *assembly;
    unsigned revision;
    unsigned char modes[GEVR_GEAR_MOUNTS];
} PlayerDevices;

static PlayerDevices s_playerDevices[2];

static PlayerDevices *playerDevices(int hand, const GevrGearAssembly *assembly)
{
    PlayerDevices *state = &s_playerDevices[hand];
    const unsigned revision = assembly ? assembly->revision : 0;
    if (state->assembly != assembly || state->revision != revision)
        *state = (PlayerDevices){.assembly = assembly, .revision = revision};
    return state;
}

static int accessoryMode(const GevrGearMount *mount)
{
    const GevrGearPart *part = gevrTacticalGearPart(mount->part);
    if (!part || strncmp(mount->socket->key, "mod_tactical", 12)) return 0;
    return ((part->art & GEVR_GEAR_ART_LASER) ? GEVR_DEVICE_LASER : 0)
        | ((part->art & GEVR_GEAR_ART_LIGHT) ? GEVR_DEVICE_LIGHT : 0);
}

static int accessoryBounds(const GevrGearMount *mount, const Mtxf *gun, float low[3], float high[3])
{
    if (!gevrTacticalGearPartList(mount->part, low, high)) return FALSE;
    const Mtxf pose = gearComposeFrame(mount->frame, gun);
    transformBounds(&pose, low, high);
    return TRUE;
}

int gevrTacticalAccessoryTap(const GevrGearAssembly *assembly, int hand, int kind,
    const float weapon[4][4], const float finger[3], float reach2)
{
    if (!assembly) return FALSE;
    Mtxf model;
    memcpy(model.m, weapon, sizeof(model.m));
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col) model.m[row][col] *= .1f;
    const Mtxf axes = gearGunAxes(kind, TRUE);
    const Mtxf gun = gearComposeFrame(axes.m, &model);
    int selected = -1, modes = 0;
    for (int i = 1; i < assembly->count; ++i) {
        const GevrGearMount *mount = &assembly->mounts[i];
        const int options = accessoryMode(mount);
        float low[3], high[3];
        if (!options || !accessoryBounds(mount, &gun, low, high)) continue;
        float distance2 = 0.f;
        for (int k = 0; k < 3; ++k) {
            const float d = finger[k] - .5f * (low[k] + high[k]);
            distance2 += d*d;
        }
        if (distance2 < reach2) {
            reach2 = distance2;
            selected = i;
            modes = options;
        }
    }
    if (selected < 0) return FALSE;
    unsigned char *mode = &playerDevices(hand, assembly)->modes[selected];
    if (modes == (GEVR_DEVICE_LIGHT | GEVR_DEVICE_LASER)) *mode = (*mode + 1) % 3;
    else *mode = *mode ? 0 : modes;
    if (sysArgCheck("--input-script"))
        sysLogPrintf(LOG_NOTE, "tactical accessory: %s mode %u",
            gevrTacticalGearPart(assembly->mounts[selected].part)->name, *mode);
    return TRUE;
}

void gevrTacticalCollectMountVisual(const GevrGearAssembly *assembly, int hand,
    uint8_t mounts[GEVR_VIS_MOUNT_BYTES])
{
    if (!assembly) return;
    const PlayerDevices *devices = playerDevices(hand, assembly);
    for (int i = 1; i < assembly->count; ++i) {
        const GevrGearMount *mount = &assembly->mounts[i];
        const GevrOpticMode *optic = gevrTacticalOpticMode(mount->part, NULL);
        const int state = (optic && optic->folded ? 4 : 0) | devices->modes[i];
        mounts[i / 2] |= state << (4 * (i % 2));
    }
}

static void emitWeaponAccessories(const GevrGearAssembly *assembly, const Mtxf *gun,
    const PlayerDevices *state, const GevrVisualWeapon *remote, int room)
{
    if (!assembly || room <= 0) return;
    const float y[3] = {0.f, 1.f, 0.f};
    float forward[3];
    if (opticAxis(gun, y, forward) <= 1e-6f) return;
    for (int i = 1; i < assembly->count; ++i) {
        const GevrGearMount *mount = &assembly->mounts[i];
        const int modes = accessoryMode(mount);
        const int mode = remote ? (remote->mounts[i / 2] >> (4 * (i % 2))) & modes
            : state ? state->modes[i] : modes;
        float low[3], high[3], origin[3], front = 0.f;
        if (!mode || !accessoryBounds(mount, gun, low, high)) continue;
        for (int k = 0; k < 3; ++k) {
            origin[k] = .5f * (low[k] + high[k]);
            front += .5f * fabsf(forward[k]) * (high[k] - low[k]);
        }
        for (int k = 0; k < 3; ++k) origin[k] += forward[k] * front;
        gevrHollywoodAccessory(mode, origin, forward, room);
    }
}

void gevrTacticalGuardAccessories(const Model *model, int item, int room)
{
    const int kind = gevrTacticalItem(item);
    if (!model || !model->render_pos || room <= 0 || kind < 0 || !sysArgCheck("--tactical-gear")) return;
    const GevrGearAssembly *assembly = guardAssembly(model, kind);
    const Mtxf axes = gearGunAxes(kind, FALSE);
    const Mtxf gun = gearComposeFrame(axes.m, &model->render_pos[0].pos);
    emitWeaponAccessories(assembly, &gun, NULL, gevrTacticalRemoteWeapon(model), room);
}

void gevrTacticalPlayerAccessories(const GevrGearAssembly *assembly, int hand, int kind,
    const float weapon[4][4], int room)
{
    const PlayerDevices *state = playerDevices(hand, assembly);
    if (!assembly || !weapon) return;
    Mtxf model;
    memcpy(model.m, weapon, sizeof(model.m));
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col) model.m[row][col] *= .1f;
    const Mtxf axes = gearGunAxes(kind, TRUE);
    const Mtxf gun = gearComposeFrame(axes.m, &model);
    emitWeaponAccessories(assembly, &gun, state, NULL, room);
}

static void opticCross(const float a[3], const float b[3], float out[3])
{
    out[0] = a[1]*b[2] - a[2]*b[1];
    out[1] = a[2]*b[0] - a[0]*b[2];
    out[2] = a[0]*b[1] - a[1]*b[0];
}

/* Match the physical sight line in a headset-free capture. */
static void alignOpticTestView(Model *model, int kind, int matrix, const GevrGearAssembly *assembly)
{
    const Mtxf raw = gearGunAxes(kind, TRUE);
    const Mtxf view = gearComposeFrame(raw.m, &model->render_pos[matrix].pos);
    for (int i = 1; i < assembly->count; ++i) {
        const GevrGearMount *mount = &assembly->mounts[i];
        const GevrOpticMode *mode = gevrTacticalOpticMode(mount->part, NULL);
        if (!mode) continue;
        const Mtxf pose = gearComposeFrame(mount->frame, &view);
        GevrOpticSurface s;
        float right[3], up[3];
        opticSurface(&s, mode, 1.f, &pose);
        opticCross(s.forward, s.up, right);
        const float r = sqrtf(opticDot(right, right));
        if (r <= 1e-6f) return;
        for (int n = 0; n < 3; ++n) right[n] /= r;
        opticCross(right, s.forward, up);
        for (int m = 0; m < model->obj->numMatrices; ++m) {
            Mtxf *joint = &model->render_pos[m].pos;
            for (int row = 0; row < 4; ++row) {
                float p[3] = {joint->m[row][0], joint->m[row][1], joint->m[row][2]};
                if (row == 3)
                    for (int n = 0; n < 3; ++n) p[n] -= s.centre[n];
                joint->m[row][0] = opticDot(p, right);
                joint->m[row][1] = opticDot(p, up);
                joint->m[row][2] = -opticDot(p, s.forward) - (row == 3 ? 16.f : 0.f);
            }
        }
        return;
    }
}

static Gfx *drawRifleOptics(Gfx *g, const GevrGearAssembly *assembly, const Mtxf *view,
    const Mtxf *held, int kind, int hand)
{
    GevrOpticSurface *surfaces[GEVR_GEAR_MOUNTS];
    GevrOpticSurface *scope = NULL;
    float best = 0.f, bestFocus = .966f;
    int count = 0, focus = -1;
    coord3d origin, aim;
    int validAim = g_gevrStereo && gevrStereoShot(hand, NULL, &origin, &aim);
    if (!validAim && sysArgCheck("--input-script") && sysArgCheck("--tactical-optic-preview")
        && gearMuzzlePoint(kind, assembly, TRUE, origin.f)) {
        mtx4TransformVecInPlace((Mtxf *)held, &origin);
        const float barrel[3] = {0.f, 0.f, 1.f};
        validAim = opticAxis(held, barrel, aim.f) > 1e-6f;
    }
    /* The same reference distance for every sight on the assembly. */
    const float zeroDistance = 2500.f * D_800364CC;
    for (int i = 1; i < assembly->count; ++i) {
        const GevrGearMount *mount = &assembly->mounts[i];
        float zoom;
        const GevrOpticMode *mode = gevrTacticalOpticMode(mount->part, &zoom);
        if (!mode || !gearPartList(mount->part, 1 + mode->folded, NULL, NULL, FALSE)) continue;
        GevrOpticSurface *surface = dynAllocate(sizeof(*surface));
        const Mtxf pose = gearComposeFrame(mount->frame, view);
        opticSurface(surface, mode, zoom, &pose);
        if (validAim) gevrTacticalOpticZero(surface, origin.f, aim.f, zeroDistance);
        else gevrTacticalOpticZero(surface, surface->centre, surface->forward, zeroDistance);
        surfaces[count++] = surface;
        const float *c = surface->centre;
        const float length = sqrtf(opticDot(c, c));
        const float dot = opticDot(c, surface->forward);
        const float alignment = length > 1e-6f ? dot / length : 0.f;
        if (c[2] < 0.f && alignment > bestFocus) {
            focus = mount->part;
            bestFocus = alignment;
        }
        // Match the wider off-axis fade of ordinary magnified glass.
        const float minAlignment = mode->kind == GEVR_OPTIC_SCOPE ? .85f : .90f;
        if (mode->kind != GEVR_OPTIC_COLLIMATOR && c[2] < 0.f
            && alignment > minAlignment && alignment > best) {
            scope = surface;
            best = alignment;
        }
    }
    gevrTacticalOpticFocus(focus);
    if (scope) scope->live = TRUE;
    /* First draw the live scope so the scene image is ready before other lenses. */
    if (scope) gDPParam(g++, G_GEVR_OPTIC_SURFACE, osVirtualToPhysical(scope));
    for (int i = 0; i < count; ++i)
        if (surfaces[i] != scope) gDPParam(g++, G_GEVR_OPTIC_SURFACE, osVirtualToPhysical(surfaces[i]));
    return g;
}

static Gfx *buildGearGun(TacticalModel *model, int kind, const GevrGearAssembly *assembly,
    const TacticalMagazine *magazine, int loadMatrix, float action, float manual, const Mtxf *heldPose, int hand, int lowDetail, const GevrVisualWeapon *remote)
{
    if (!model || (!assembly && !magazine->gear_part)) return NULL;
    const int expanded = tacticalGunIsExpanded(kind);
    const GevrGearMount *well = gevrTacticalGearFindMount(assembly, "mod_magazine");
    const int legacySvd = kind == TACTICAL_GUN_SVD && !well;
    const int modular = expanded || (kind == TACTICAL_GUN_SVD && well);
    Gfx *body = NULL;
    const int splitAction = remote && assembly && !legacySvd && !modular && !lowDetail;
    if (modular) {
        if (!assembly) return NULL;
    } else if (legacySvd) {
        const int withMagazine = magazine->id || !model->firstperson;
        /* Mounted and carousel callers supply their own matrix. */
        body = withMagazine ? model->list : model->noMag;
        if (!loadMatrix) body = mountedGunBody(model, withMagazine);
        else if (!model->firstperson) body = cachedRigidList(&model->gpuList[TACT_CACHE_BODY], body, lowDetail);
    } else if (splitAction) {
        body = cachedGunList(model, &model->remoteCore, TACT_LIST_REMOTE_CORE, lowDetail);
    } else {
        body = assembly ? cachedGunList(model, &model->gearCore, TACT_LIST_GEAR_CORE, lowDetail)
            : cachedGunList(model, &model->gearBody, 4, lowDetail);
    }
    if (!body && !modular) return NULL;
    if (remote && assembly && sysArgCheck("--tactical-coop-test")) {
        static unsigned drawn[256];
        const unsigned index = (unsigned)remote->build % ARRAYCOUNT(drawn);
        if (drawn[index] != assembly->revision) {
            drawn[index] = assembly->revision;
            sysLogPrintf(LOG_NOTE, "tactical co-op: rendering teammate item %d build %d (%d fittings)",
                remote->item, remote->build, assembly->count - 1);
        }
    }
    const int count = assembly ? assembly->count : 1;
    const int audit = heldPose && (action > 0.f || manual > 0.f)
        && sysArgCheck("--input-script") && sysArgCheck("--tactical-cycle-audit");
    Gfx *draw = dynAllocate((16 + count * 4) * sizeof(Gfx));
    Gfx *g = draw;
    if (loadMatrix)
        gSPMatrix(g++, (void *)(uintptr_t)(0x03000000u + model->matrix * sizeof(Mtx)),
            G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    if (body) gSPDisplayList(g++, body);
    if (splitAction) {
        const TacticalAsset *asset = &s_assets[model->asset];
        for (int part = TACT_ACTION_BOLT; part <= TACT_ACTION_HANDLE; ++part) {
            int hasPart = FALSE;
            for (int tex = 0; tex < asset->ntex; ++tex)
                if (gearCoreTexture(model->asset, tex) && legacyActionMaterial(model->asset, tex) == part) {
                    hasPart = TRUE;
                    break;
                }
            if (!hasPart) continue;
            Gfx **cache = part == TACT_ACTION_BOLT ? &model->remoteBolt : &model->remoteHandle;
            Gfx *list = cachedGunList(model, cache, TACT_LIST_REMOTE_CORE + part, FALSE);
            if (!list) continue;
            Mtxf shift;
            matrix_4x4_set_identity(&shift);
            const float travel = part == TACT_ACTION_BOLT ? action : manual;
            shift.m[3][0] = gevrTacticalActionShift(kind, travel, TRUE);
            Mtx *matrix = dynAllocateMatrix();
            guMtxF2L(shift.m, matrix);
            gSPMatrix(g++, osVirtualToPhysical(matrix), G_MTX_MUL | G_MTX_PUSH | G_MTX_MODELVIEW);
            gSPDisplayList(g++, list);
            gSPPopMatrix(g++, G_MTX_MODELVIEW);
        }
    }
    const Mtxf pose = gearGunAxes(kind, model->firstperson);
    const int first = modular || (assembly && kind == TACTICAL_GUN_SKORPION) ? 0 : 1;
    for (int i = first; i < count; ++i) {
        const GevrGearMount *mount = &assembly->mounts[i];
        if (!strcmp(mount->path, "mod_magazine") || gevrTacticalGearIsRootChoice(mount->path)) continue;
        const GevrOpticMode *optic = gevrTacticalOpticMode(mount->part, NULL);
        const int folded = remote ? (remote->mounts[i / 2] >> (4 * (i % 2))) & 4 : optic && optic->folded;
        Gfx *part = optic ? gearPartList(mount->part, 1 + (folded != 0), NULL, NULL, lowDetail) : NULL;
        if (!part) part = gearPartList(mount->part, FALSE, NULL, NULL, lowDetail);
        if (!part) continue;
        Mtx *matrix = dynAllocateMatrix();
        const char *slot = mount->socket ? mount->socket->key : "";
        float travel = 0.f;
        if (!strcmp(slot, "mod_charge") || !strncmp(slot, "mod_charge_", sizeof("mod_charge_") - 1))
            travel = gevrTacticalChargeTravel(kind, action, manual);
        else if (!strcmp(slot, "mod_bolt")) travel = action;
        else if (tacticalGunIsPistol(kind) && (kind == TACTICAL_GUN_MAKAROV || expanded)
            && (gearUnderSocket(mount->path, "mod_reciever")
                || gearUnderSocket(mount->path, "mod_slide")
                || gearUnderSocket(mount->path, "mod_charge")))
            travel = action;
        const float slide = model->firstperson || remote
            ? gevrTacticalActionShift(kind, travel, sysArgCheck("--tactical-physical")) : 0.f;
        if (model->firstperson || slide == 0.f) {
            gearPartMatrix(matrix, mount->frame, &pose, slide);
        } else {
            Mtxf sliding = gearComposeFrame(mount->frame, &pose);
            sliding.m[3][0] += slide;
            guMtxF2L(sliding.m, matrix);
        }
        if (audit && (slide > 0.f || !strcmp(slot, "mod_charge")))
            sysLogPrintf(LOG_NOTE, "tactical cycle: kind %d %s, shift %.1f mm, manual %.2f cm",
                kind, mount->path, (double)slide, (double)manual);
        g = gearDrawPart(g, part, matrix);
    }
    if (assembly && heldPose) {
        const Mtxf view = gearComposeFrame(pose.m, heldPose);
        g = drawRifleOptics(g, assembly, &view, heldPose, kind, hand);
    }
    if (!legacySvd && magazine->id) {
        int magazinePart = (int)magazine->gear_part - 1;
        if (expanded && magazinePart < 0 && well) {
            const GevrGearMount *stock = gevrTacticalGearFindMount(
                gevrTacticalGearStockAssembly(gevrTacticalGearKey(kind)), "mod_magazine");
            if (stock) magazinePart = stock->part;
        }
        const int other = tacticalMagazineGun(magazine);
        if (magazinePart < 0 && well && other != kind
            && (kind == TACTICAL_GUN_MP5 || kind == TACTICAL_GUN_MP5K)) {
            const GevrGearMount *stock = gevrTacticalGearFindMount(gevrTacticalGearStockAssembly(other), "mod_magazine");
            if (stock) magazinePart = stock->part;
        }
        if (!well && magazinePart >= 0)
            well = gevrTacticalGearFindMount(gevrTacticalGearStockAssembly(kind), "mod_magazine");
        float low[3], high[3];
        int chamber = -1;
        if (kind == TACTICAL_GUN_M32A1) {
            if (remote) chamber = (remote->flags & GEVR_VIS_CHAMBER) != 0;
            else if (heldPose) chamber = gevrTacticalChamberState(kind, hand);
        }
        Gfx *part = well ? gearPartList(magazinePart, FALSE, chamber >= 0 ? low : NULL,
            chamber >= 0 ? high : NULL, lowDetail) : NULL;
        if (part) {
            Mtx *matrix = dynAllocateMatrix();
            if (chamber >= 0 && magazine->capacity > 0) {
                const int spent = (int)magazine->capacity - (int)magazine->rounds - chamber;
                const float step = spent > 0 ? spent - fminf(1.f, action/3.2f) : 0.f;
                gearCylinderMatrix(matrix, well, &pose, low, high, step*6.2831853f/magazine->capacity);
                if (audit) sysLogPrintf(LOG_NOTE, "tactical cycle: kind %d cylinder, step %.3f", kind, (double)step);
            } else {
                gearPartMatrix(matrix, well->frame, &pose, 0.f);
            }
            g = gearDrawPart(g, part, matrix);
        } else if (!expanded) {
            Gfx *stock = cachedGunList(model, &model->gearMagazine, TACT_LIST_INSTALLED_MAG, lowDetail);
            if (stock) gSPDisplayList(g++, stock);
        }
    }
    gSPEndDisplayList(g++);
    if (!model->gearLogged) {
        model->gearLogged = 1;
        sysLogPrintf(LOG_NOTE, "tactical gear: drew %s weapon %d, assembly %d, magazine part %u (%u/%u)",
            !loadMatrix ? "slung" : model->firstperson ? "held" : "dropped",
            kind, assembly != NULL, magazine->gear_part, magazine->rounds, magazine->capacity);
    }
    return draw;
}

void *gevrTacticalMountedGearList(int kind, int build, int suppressed, const TacticalMagazine *magazine)
{
    if (tacticalGunIsExpanded(kind)) {
        static TacticalModel mounted[TACTICAL_GUN_COUNT - TACTICAL_GUN_UZI];
        TacticalModel *model = &mounted[kind - TACTICAL_GUN_UZI];
        model->firstperson = TRUE;
        const GevrGearAssembly *assembly = gevrTacticalGearDeployedAssembly(build);
        if (!assembly) assembly = gevrTacticalGearStockAssembly(gevrTacticalGearKey(kind));
        return buildGearGun(model, kind, assembly, magazine, FALSE, 0.f, 0.f, NULL, GUNRIGHT, FALSE, NULL);
    }
    if (build >= 0 || magazine->gear_part || kind == TACTICAL_GUN_SVD) {
        const GevrGearAssembly *assembly = gevrTacticalGearDeployedAssembly(build);
        if (!assembly && kind == TACTICAL_GUN_SVD) assembly = gevrTacticalGearStockAssembly(kind);
        int index;
        TacticalModel *model = mountedGunModel(kind, suppressed, &index);
        Gfx *gear = buildGearGun(model, kind, assembly, magazine, FALSE, 0.f, 0.f, NULL, GUNRIGHT, FALSE, NULL);
        if (gear) return gear;
    }
    return gevrTacticalMountedGunList(kind, suppressed, magazine->id != 0);
}

static int allocateTacticalStaticList(TacticalModel *m, int asset)
{
    TacticalAsset *mesh = &s_assets[asset];
    const size_t commands = (size_t)mesh->ntex * 16 + (size_t)mesh->ntri * 2 + 16;
    m->vertices = calloc((size_t)mesh->ntri * 3, sizeof(Vtx));
    m->list = calloc(commands, sizeof(Gfx));
    if (!m->vertices || !m->list) {
        free(m->vertices);
        free(m->list);
        m->vertices = NULL;
        m->list = NULL;
        return 0;
    }
    m->asset = asset;
    return 1;
}

static int buildTacticalWorldMesh(TacticalModel *model, int asset, const char *path)
{
    if (model->built) return 1;
    TacticalAsset *mesh = &s_assets[asset];
    if (!readMeshFile(mesh, path, FALSE, 0) || !allocateTacticalStaticList(model, asset)) return 0;
    for (int i = 0; i < mesh->ntri * 3; ++i) {
        Vtx *v = &model->vertices[i];
        const TacticalCorner *corner = &mesh->corners[i];
        for (int axis = 0; axis < 3; ++axis) v->v.ob[axis] = s16clamp(1000.f * corner->p[axis]);
        memcpy(v->v.cn, corner->rgba, 4);
    }
    emitList(model, model->list, TACT_LIST_WEARABLE);
    model->built = TRUE;
    return 1;
}

static const struct { const char *path, *name; } s_vestFiles[GEVR_VEST_COUNT] = {
#define TACTICAL_VEST(id, file, label) [GEVR_VEST_##id] = {"mods/tactical/" file, label},
#include "gevr_tactical_vests.inc.h"
#undef TACTICAL_VEST
};

static TacticalModel *tacticalVestModel(GevrTacticalVest vest, int body)
{
    if (vest <= GEVR_VEST_NONE || vest >= GEVR_VEST_COUNT) return NULL;
    for (int i = 0; i < (int)ARRAYCOUNT(s_vestFits); ++i)
        if (s_vestFits[i].body == body && s_vestFits[i].vest == vest
            && buildTacticalWorldMesh(&s_fittedVests[i], TACT_ASSET_VEST_FIT + i, s_vestFits[i].path))
            return &s_fittedVests[i];
    TacticalModel *model = &s_vests[vest];
    return buildTacticalWorldMesh(model, TACT_ASSET_KLMK + vest - 1, s_vestFiles[vest].path) ? model : NULL;
}

void *gevrTacticalVestRenderList(GevrTacticalVest vest, int body, int lowDetail)
{
    TacticalModel *model = tacticalVestModel(vest, body);
    return model ? cachedRigidList(&model->gpuList[TACT_CACHE_BODY], model->list, lowDetail) : NULL;
}

const char *gevrTacticalVestName(GevrTacticalVest vest)
{
    return vest > GEVR_VEST_NONE && vest < GEVR_VEST_COUNT ? s_vestFiles[vest].name : "Canvas";
}

void *gevrTacticalVestArmList(GevrTacticalVest vest, int side, int lowDetail)
{
    const int index = (int)vest - GEVR_VEST_ZABRALO;
    if (index < 0 || index >= TACT_VEST_ARM_SETS || side < 0 || side > 1) return NULL;
    TacticalModel *arm = &s_vestArms[index][side];
    const char *file = s_vestFiles[vest].path;
    char path[128];
    snprintf(path, sizeof(path), "%.*s-%s-arm.bin", (int)strlen(file) - 4, file, side ? "right" : "left");
    if (!buildTacticalWorldMesh(arm, TACT_ASSET_VEST_ARM + index * 2 + side, path)) return NULL;
    return cachedRigidList(&arm->gpuList[TACT_CACHE_BODY], arm->list, lowDetail);
}

int gevrTacticalHelmetAvailable(GevrTacticalHelmet helmet)
{
    static const char *const paths[GEVR_HELMET_COUNT] = {
#define TACTICAL_HELMET(id, file, label) [GEVR_HELMET_##id] = "mods/tactical/" file,
#include "gevr_tactical_helmets.inc.h"
#undef TACTICAL_HELMET
    };
    return sysArgCheck("--tactical-mesh") && helmet >= 0 && helmet < GEVR_HELMET_COUNT
        && buildTacticalWorldMesh(&s_helmets[helmet], TACT_ASSET_HELMET + helmet, paths[helmet]);
}

static float tacticalWorldMeshRaycast(TacticalModel *model, const float origin[3],
    const float direction[3], float limit)
{
    if (!(limit > 0.f) || !model->built) return -1.f;
    const int corners = s_assets[model->asset].ntri * 3;
    float low[3], high[3], near = 0.f, far = limit;
    int largestAxis = 0;
    copyMeshBounds(model, corners, low, high);
    for (int axis = 0; axis < 3; ++axis) {
        const float d = direction[axis];
        if (fabsf(d) > fabsf(direction[largestAxis])) largestAxis = axis;
        if (fabsf(d) < 1e-6f) {
            if (origin[axis] < low[axis] || origin[axis] > high[axis]) return -1.f;
            continue;
        }
        const float a = (low[axis] - origin[axis]) / d;
        const float b = (high[axis] - origin[axis]) / d;
        near = fmaxf(near, fminf(a, b));
        far = fminf(far, fmaxf(a, b));
        if (near > far) return -1.f;
    }
    if (fabsf(direction[largestAxis]) < 1e-6f) return -1.f;

    coord3d zero = {.f = {0, 0, 0}}, start, dir;
    memcpy(start.f, origin, sizeof(start.f));
    memcpy(dir.f, direction, sizeof(dir.f));
    float best = limit;
    for (int i = 0; i < corners; i += 3) {
        Vertex triangle[3];
        for (int corner = 0; corner < 3; ++corner) {
            const s16 *point = model->vertices[i + corner].v.ob;
            triangle[corner].coord.x = point[0];
            triangle[corner].coord.y = point[1];
            triangle[corner].coord.z = point[2];
        }
        HitThing hit;
        if (intersectRayTriangle(&triangle[0], &triangle[1], &triangle[2], &zero, &start, &start, &dir, &hit)) {
            const float t = (hit.hitpos.f[largestAxis] - origin[largestAxis]) / direction[largestAxis];
            if (t >= 0.f && t < best) best = t;
        }
    }
    return best < limit ? best : -1.f;
}

float gevrTacticalVestRigFront(GevrTacticalVest vest, int body, int pouch)
{
    if (pouch < 0 || pouch >= 3) return .47f;
    TacticalModel *model = tacticalVestModel(vest, body);
    if (!model) return .47f;
    if (!model->rigFront[pouch]) {
        static const float direction[3] = {0.f, 0.f, -1.f};
        const float origin[3] = {(pouch - 1) * 220.f, 45.f, 800.f};
        const float at = tacticalWorldMeshRaycast(model, origin, direction, 790.f);
        model->rigFront[pouch] = at >= 0.f ? (800.f - at) * .001f : .47f;
    }
    return model->rigFront[pouch];
}

/* The bounded torso volume closes seams in the mesh while leaving the
 * face and limbs outside the plate's coverage. */
static float tacticalVestTorsoRaycast(TacticalModel *model, const float origin[3], const float direction[3], float limit)
{
    float low[3], high[3];
    copyMeshBounds(model, s_assets[model->asset].ntri * 3, low, high);
    const float bottom = fmaxf(-632.f, low[1]), top = fminf(235.f, high[1]);
    const float width = fminf(448.f, (high[0] - low[0]) * .5f);
    const float depth = fminf(427.f, (high[2] - low[2]) * .5f);
    if (!(bottom < top && width > 0.f && depth > 0.f)) return -1.f;
    const float x = (origin[0] - (low[0] + high[0]) * .5f) / width;
    const float z = (origin[2] - (low[2] + high[2]) * .5f) / depth;
    const float dx = direction[0] / width, dz = direction[2] / depth;
    const float a = dx * dx + dz * dz, b = x * dx + z * dz;
    const float c = x * x + z * z - 1.f;
    float near = 0.f, far = limit;
    if (a > 1e-12f) {
        const float discriminant = b * b - a * c;
        if (discriminant < 0.f) return -1.f;
        const float root = sqrtf(discriminant);
        near = fmaxf(near, (-b - root) / a);
        far = fminf(far, (-b + root) / a);
    } else if (c > 0.f) {
        return -1.f;
    }

    if (fabsf(direction[1]) > 1e-6f) {
        const float enter = (bottom - origin[1]) / direction[1];
        const float exit = (top - origin[1]) / direction[1];
        near = fmaxf(near, fminf(enter, exit));
        far = fminf(far, fmaxf(enter, exit));
    } else if (origin[1] < bottom || origin[1] > top) {
        return -1.f;
    }
    return near <= far && near < limit ? near : -1.f;
}

float gevrTacticalVestRaycast(GevrTacticalVest vest, int body,
    const float origin[3], const float direction[3], float limit)
{
    if (!(limit > 0.f)) return -1.f;
    TacticalModel *model = tacticalVestModel(vest, body);
    if (!model) return -1.f;
    const float hit = tacticalWorldMeshRaycast(model, origin, direction, limit);
    return hit >= 0.f ? hit : tacticalVestTorsoRaycast(model, origin, direction, limit);
}

float gevrTacticalVestArmRaycast(GevrTacticalVest vest, int side,
    const float origin[3], const float direction[3], float limit)
{
    if (!(limit > 0.f) || !gevrTacticalVestArmList(vest, side, FALSE)) return -1.f;
    return tacticalWorldMeshRaycast(&s_vestArms[vest - GEVR_VEST_ZABRALO][side], origin, direction, limit);
}

float gevrTacticalHelmetRaycast(const Model *model, const float origin[3], const float direction[3], float limit)
{
    const int kind = gevrTacticalHelmetKind(model);
    return kind >= 0 ? tacticalWorldMeshRaycast(&s_helmets[kind], origin, direction, limit) : -1.f;
}

typedef struct TacticalGloveFit {
    int firing, support;
    float firingOffset[2], supportOffset[2];
} TacticalGloveFit;

static const TacticalGloveFit s_expandedGloves[] = {
    {TACTICAL_GUN_MP5,   TACTICAL_GUN_MP5,    {-9, 0},   {-2, -65}},  /* Uzi */
    {TACTICAL_GUN_MP5,   TACTICAL_GUN_MP5K,   {-2, -4},  {21, -70}},  /* SR-2M */
    {TACTICAL_GUN_MP5K,  TACTICAL_GUN_MP5K,   {0, 2},    {13, -40}},  /* MP9 */
    {TACTICAL_GUN_MP5,   TACTICAL_GUN_MP5,    {12, -15}, {-24, -20}}, /* EVO 3 */
    {TACTICAL_GUN_MP5K,  TACTICAL_GUN_MP5K,   {-4, -12}, {13, -85}},  /* PM-9 */
    {TACTICAL_GUN_USP,   TACTICAL_GUN_USP,    {9, 20},   {9, 20}},    /* Mk 23 */
    {TACTICAL_GUN_AK74N, TACTICAL_GUN_AK74N,  {0, 2},    {0, 79}},    /* PKM */
    {TACTICAL_GUN_AK74N, TACTICAL_GUN_AK74N,  {2, -8},   {-4, 8}},    /* M4A1 */
    {TACTICAL_GUN_AK74N, TACTICAL_GUN_AK74N,  {1, -8},   {-5, 32}},   /* HK416A5 */
    {TACTICAL_GUN_AK74N, TACTICAL_GUN_AK74N,  {0, 0},    {0, 0}},     /* AS VAL */
    {TACTICAL_GUN_USP, TACTICAL_GUN_USP,      {0, 0},    {0, 0}},     /* M1911A1 */
    {TACTICAL_GUN_USP, TACTICAL_GUN_USP,      {0, 0},    {0, 0}},     /* CZ 75B */
    {TACTICAL_GUN_MP5K, TACTICAL_GUN_MP5K,    {0, 0},    {5, -6}},    /* P90 */
    {TACTICAL_GUN_USP, TACTICAL_GUN_USP,      {0, 0},    {0, 0}},     /* Glock 17 */
    {TACTICAL_GUN_USP, TACTICAL_GUN_USP,      {0, 0},    {0, 0}},     /* Glock 18C */
    {TACTICAL_GUN_USP, TACTICAL_GUN_USP,      {0, 0},    {0, 0}},     /* Glock 19X */
    {TACTICAL_GUN_USP, TACTICAL_GUN_USP,      {-1, 2},   {-3, 8}},    /* Desert Eagle */
    {TACTICAL_GUN_AK74N, TACTICAL_GUN_AK74N,  {0, 0},    {-4, 4}},    /* M32A1 */
    {TACTICAL_GUN_AK74N, TACTICAL_GUN_AK74N,  {0, 0},    {0, 0}},     /* Saiga-12K */
    [TACTICAL_GUN_FN57 - TACTICAL_GUN_UZI] = {TACTICAL_GUN_USP, TACTICAL_GUN_USP, {0, 0}, {0, 0}},
    [TACTICAL_GUN_MP7A2 - TACTICAL_GUN_UZI] = {TACTICAL_GUN_MP5, TACTICAL_GUN_MP5, {0, 0}, {0, -30}},
    [TACTICAL_GUN_MP7A1 - TACTICAL_GUN_UZI] = {TACTICAL_GUN_MP5, TACTICAL_GUN_MP5, {0, 0}, {0, -30}},
    [TACTICAL_GUN_KACPDW - TACTICAL_GUN_UZI] = {TACTICAL_GUN_AK74N, TACTICAL_GUN_AK74N, {0, 0}, {0, 0}},
    [TACTICAL_GUN_VITYAZ - TACTICAL_GUN_UZI] = {TACTICAL_GUN_AK74N, TACTICAL_GUN_AK74N, {0, 0}, {0, 30}},
    [TACTICAL_GUN_MPX - TACTICAL_GUN_UZI] = {TACTICAL_GUN_MP5, TACTICAL_GUN_MP5, {0, 0}, {0, 0}},
    [TACTICAL_GUN_M1A - TACTICAL_GUN_UZI] = {TACTICAL_GUN_AK74N, TACTICAL_GUN_AK74N, {0, 0}, {0, 30}},
    [TACTICAL_GUN_SR3M - TACTICAL_GUN_UZI] = {TACTICAL_GUN_AK74N, TACTICAL_GUN_AK74N, {0, 0}, {0, 0}},
    [TACTICAL_GUN_AUGA3 - TACTICAL_GUN_UZI] = {TACTICAL_GUN_AK74N, TACTICAL_GUN_AK74N, {0, 0}, {0, 0}},
    [TACTICAL_GUN_TOKAREV - TACTICAL_GUN_UZI] = {TACTICAL_GUN_USP, TACTICAL_GUN_USP, {0, 0}, {0, 0}},
    [TACTICAL_GUN_STM9 - TACTICAL_GUN_UZI] = {TACTICAL_GUN_AK74N, TACTICAL_GUN_AK74N, {0, 0}, {0, 0}},
    [TACTICAL_GUN_L85A2 - TACTICAL_GUN_UZI] = {TACTICAL_GUN_AK74N, TACTICAL_GUN_AK74N, {0, 0}, {0, 0}},
    [TACTICAL_GUN_FAMAS - TACTICAL_GUN_UZI] = {TACTICAL_GUN_AK74N, TACTICAL_GUN_AK74N, {0, 0}, {0, 0}},
    [TACTICAL_GUN_SKS - TACTICAL_GUN_UZI] = {TACTICAL_GUN_AK74N, TACTICAL_GUN_AK74N, {0, 0}, {0, 20}},
};

static const float *expandedGloveOffset(int gun, TacticalGlovePose pose)
{
    if (!tacticalGunIsExpanded(gun) || pose >= TACTICAL_GLOVE_OPEN) return NULL;
    const TacticalGloveFit *fit = &s_expandedGloves[gun - TACTICAL_GUN_UZI];
    return pose == TACTICAL_GLOVE_SUPPORT ? fit->supportOffset : fit->firingOffset;
}

static int fittedGloveAsset(int gun, TacticalGlovePose pose)
{
    if (pose < 0 || pose >= TACTICAL_GLOVE_POSE_COUNT || gun >= TACTICAL_GUN_COUNT
        || (gun < 0 && pose < TACTICAL_GLOVE_OPEN) || !gevrTacticalCampaignRules()
        || !sysArgCheck("--tactical-pavlov-hands")) return -1;
    if (gun == TACTICAL_GUN_RPG && pose < TACTICAL_GLOVE_OPEN) return -1;
    if (tacticalGunIsExpanded(gun) && pose < TACTICAL_GLOVE_OPEN) {
        const TacticalGloveFit *fit = &s_expandedGloves[gun - TACTICAL_GUN_UZI];
        gun = pose == TACTICAL_GLOVE_SUPPORT ? fit->support : fit->firing;
    }
    const int ppk = gun == TACTICAL_GUN_USP && sysArgCheck("--tactical-ppk");
    if ((gun == TACTICAL_GUN_USP && !ppk) || (!sysArgCheck("--tactical-grip-fit")
        && !(ppk && sysArgCheck("--tactical-ppk-hand-fit")))) return -1;
    return pose < TACTICAL_GLOVE_OPEN ? TACT_GLOVE_BASE + gun * TACT_GUN_GLOVE_POSES + pose
        : TACT_SHARED_GLOVE_BASE + pose - TACTICAL_GLOVE_OPEN;
}

int gevrTacticalGloveWatchMount(int gun, TacticalGlovePose pose, int mirrored, GevrTacticalWatchMount *out)
{
    if (!sysArgCheck("--tactical-watch")) return 0;
    const int asset = fittedGloveAsset(gun, pose);
    if (asset < 0 || !loadMesh(asset) || !s_assets[asset].watchMounted) return 0;
    *out = s_assets[asset].watch;
    /* The baked watch is rolled 24 degrees around the cuff. Bring the
     * detachable case back over the wrist and rotate about the cuff centre. */
    const float *a = out->along, *n = s_assets[asset].watch.normal;
    const float length = sqrtf(a[0]*a[0] + a[1]*a[1] + a[2]*a[2]);
    if (length <= 1e-6f) return 0;
    const float roll = (pose == TACTICAL_GLOVE_SUPPORT ? -.3420201f : .3420201f) / length;
    const float side[3] = {a[1]*n[2] - a[2]*n[1], a[2]*n[0] - a[0]*n[2], a[0]*n[1] - a[1]*n[0]};
    for (int axis = 0; axis < 3; ++axis) {
        out->normal[axis] = .9396926f*n[axis] + roll*side[axis];
        out->centre[axis] += 41.5f*(out->normal[axis] - n[axis]);
    }
    const float *offset = expandedGloveOffset(gun, pose);
    if (offset) {
        out->centre[1] += offset[0];
        out->centre[2] += offset[1];
    }
    if (mirrored) {
        out->along[0] = -out->along[0];
        out->normal[0] = -out->normal[0];
        out->centre[0] = -out->centre[0];
    }
    return 1;
}

void *gevrTacticalFittedGloveList(int gun, TacticalGlovePose pose, int mirrored, int watch)
{
    static TacticalModel gloves[TACT_ASSET_MAX - TACT_GLOVE_BASE][2][2];
    static TacticalModel expanded[TACTICAL_GUN_COUNT - TACTICAL_GUN_UZI][TACT_GUN_GLOVE_POSES][2][2];
    const int asset = fittedGloveAsset(gun, pose);
    if (asset < 0) return NULL;
    const float *offset = expandedGloveOffset(gun, pose);
    TacticalModel *m = offset ? &expanded[gun - TACTICAL_GUN_UZI][pose][!!mirrored][!!watch]
        : &gloves[asset - TACT_GLOVE_BASE][!!mirrored][!!watch];
    if (m->built) return cachedRigidList(&m->gpuList[TACT_CACHE_BODY], m->list, FALSE);
    TacticalAsset *mesh = &s_assets[asset];
    if (!loadMesh(asset) || mesh->ntex != 2 || !allocateTacticalStaticList(m, asset)) return NULL;
    for (size_t i = 0; i < (size_t)mesh->ntri * 3; i++) {
        const TacticalCorner *corner = &mesh->corners[i];
        Vtx *v = &m->vertices[i];
        v->v.ob[0] = s16clamp((mirrored ? -1000.f : 1000.f) * corner->p[0]);
        v->v.ob[1] = s16clamp(1000.f * corner->p[1] + (offset ? offset[0] : 0.f));
        v->v.ob[2] = s16clamp(1000.f * corner->p[2] + (offset ? offset[1] : 0.f));
        memcpy(v->v.cn, corner->rgba, 4);
    }
    emitList(m, m->list, watch ? 3 : 5);
    m->built = 1;
    if (sysArgCheck("--input-script"))
        sysLogPrintf(LOG_NOTE, "tactical hands: fitted gun %d pose %d mirrored %d watch %d",
            gun, pose, !!mirrored, !!watch);
    return cachedRigidList(&m->gpuList[TACT_CACHE_BODY], m->list, FALSE);
}

/* Private hand snapshots use the caller's controller matrix, like held mags.
 * Kept opt-in: controller offsets and actual weapon grips need VR fitting. */
static void *buildTacticalHandList(int pose, int pistol, int mirrored)
{
    static TacticalModel hands[11][5][2];
    TacticalModel *m;
    TacticalAsset *mesh;
    size_t nv;
    int asset;
    if (pose < 0 || pose >= 5 || pistol < 0 || pistol > 10 || !sysArgCheck("--tactical-pavlov-hands")
        || !gevrTacticalCampaignRules()) return NULL;
    m = &hands[pistol][pose][!!mirrored];
    asset = pistol == 10 ? 12 : pistol == 9 ? (pose == 1 ? 13 : 14) : pistol == 8 ? 6 + pose : pistol == 7 ? 20 : pistol == 6 ? 19 : pistol == 5 ? 18 : pistol == 4 ? (pose == 1 ? 17 : 16)
        : pistol == 3 ? 15 : pistol == 1 ? (pose == 1 ? 13 : 14) : pose == 4 ? 12 : 6 + pose;
    if (m->built) return cachedRigidList(&m->gpuList[TACT_CACHE_BODY], m->list, FALSE);
    if (!loadMesh(asset) || !allocateTacticalStaticList(m, asset)) return NULL;
    const int physical = sysArgCheck("--tactical-physical");
    const int refitPpk = sysArgCheck("--tactical-ppk-hand-refit");
    mesh = &s_assets[asset]; nv = (size_t)mesh->ntri * 3;
    for (size_t i = 0; i < nv; i++) {
        TacticalCorner *c = &mesh->corners[i]; Vtx *v = &m->vertices[i];
        if (pistol == 4 || pistol == 5) {
            float x = 1000*c->p[0], y = 1000*c->p[1], z = 1000*c->p[2];
            v->v.ob[0] = s16clamp((pistol == 5 ? -.82f : .82f)*y);
            v->v.ob[1] = s16clamp(.82f*x-(pistol == 5 ? 10 : 65));
            v->v.ob[2] = s16clamp(.82f*z+(pistol == 5 ? 190 : -110));
            if (pistol == 4 && physical) {
                /* Tighten around the receiver without moving the palm off
                 * its grip anchor on the AK and the two SMGs. */
                v->v.ob[0] = s16clamp(.91f * v->v.ob[0]);
                v->v.ob[2] = s16clamp(-20.f + .95f * (v->v.ob[2] + 20.f));
            }
        } else if (pistol == 1 || pistol == 2 || pistol == 9 || pistol == 10) {
            float x = 1000*c->p[0], y = 1000*c->p[1], z = 1000*c->p[2];
            /* Rotate the M9 reference hand into the pistol grip: index above
             * the other fingers, thumb along the frame, fingers around grip.
             * The support is mirrored to wrap from the opposite side. */
            const int support = pistol == 2 || pistol == 10;
            const int makarov = pistol == 9 || pistol == 10;
            const int refit = support && refitPpk;
            v->v.ob[0] = s16clamp((support ? -.82f : .82f)*y - (refit ? 10 : 0));
            v->v.ob[1] = s16clamp(.82f*x-(support ? (refit ? 40 : 50) : 30) - (makarov ? 10 : 0));
            v->v.ob[2] = s16clamp(.82f*z-(support ? (refit ? 65 : 75) : 80) - (makarov ? 90 : 0));
            if (refit) {
                /* Turn the cupped support palm toward the firing hand instead
                 * of leaving its fingers splayed sideways. Pivot at the grip. */
                int xfit = v->v.ob[0], yfit = v->v.ob[1] + 40;
                v->v.ob[0] = -yfit; v->v.ob[1] = xfit - 40;
            }
            if (!support && physical) {
                /* Bring the knuckles in around the narrower pistol grips.
                 * Shorten toward the trigger guard, keeping the wrist fit. */
                const float grip = makarov ? -66.f : 24.f;
                v->v.ob[0] = s16clamp(.86f * v->v.ob[0]);
                v->v.ob[2] = s16clamp(grip + .92f * (v->v.ob[2] - grip) + 3.f);
            }
        } else if (pistol == 8 || (pistol == 3 && refitPpk)) {
            v->v.ob[0] = s16clamp(820*c->p[0]);
            v->v.ob[1] = s16clamp(820*c->p[1]);
            v->v.ob[2] = s16clamp(820*c->p[2] - 65);
        } else for (int j = 0; j < 3; j++) v->v.ob[j] = s16clamp(1000 * c->p[j]);
        if (mirrored) v->v.ob[0] = -v->v.ob[0];
        memcpy(v->v.cn, c->rgba, 4);
    }
    emitList(m, m->list, 3); m->built = 1;
    return cachedRigidList(&m->gpuList[TACT_CACHE_BODY], m->list, FALSE);
}
void *gevrTacticalHandList(int pose) { return buildTacticalHandList(pose, 0, 0); }
void *gevrTacticalPistolFiringHandList(int gun, int trigger, int mirrored)
{
    void *fit = gevrTacticalFittedGloveList(gun, trigger ? TACTICAL_GLOVE_TRIGGER : TACTICAL_GLOVE_READY,
        mirrored, mirrored);
    if (fit) return fit;
    return buildTacticalHandList(trigger ? 1 : 3, gun == TACTICAL_GUN_MAKAROV ? 9 : 1, mirrored);
}
void *gevrTacticalMakarovSupportList(void) { return buildTacticalHandList(4, 10, 0); }
void *gevrTacticalPistolSupportList(void) { return buildTacticalHandList(4, 2, 0); }
void *gevrTacticalPistolMagazineHandList(void) { return buildTacticalHandList(2, 3, 0); }
void *gevrTacticalPistolOpenHandList(void) { return buildTacticalHandList(0, 8, 0); }
void *gevrTacticalPinchHandList(void) { return buildTacticalHandList(2, 8, 0); }
void *gevrTacticalAkHandList(int pose) { return buildTacticalHandList(pose, 4, 0); }
void *gevrTacticalAkSupportHandList(void) { return buildTacticalHandList(4, 5, 0); }
void *gevrTacticalAkFreeHandList(void) { return buildTacticalHandList(2, 6, 0); }
void *gevrTacticalAkOpenHandList(void) { return buildTacticalHandList(0, 7, 0); }
/* Prepare the installed parts and the housing used by each optic in play. */
static void prewarmAssembly(int kind, int buildId)
{
    const GevrGearAssembly *assembly = gevrTacticalGearDeployedAssembly(buildId);
    if (!assembly && (tacticalGunIsExpanded(kind) || kind == TACTICAL_GUN_SVD))
        assembly = gevrTacticalGearStockAssembly(gevrTacticalGearKey(kind));
    if (!assembly) return;
    for (int i = 0; i < assembly->count; ++i) {
        const GevrGearMount *mount = &assembly->mounts[i];
        if (mount->part < 0 || gevrTacticalGearIsRootChoice(mount->path)) continue;
        const GevrOpticMode *optic = gevrTacticalOpticMode(mount->part, NULL);
        if (!optic || !gearPartList(mount->part, 1 + optic->folded, NULL, NULL, FALSE))
            gevrTacticalGearPartList(mount->part, NULL, NULL);
        if (optic && gevrTacticalOpticCanFlip(mount->part))
            gearPartList(mount->part, 1 + !optic->folded, NULL, NULL, FALSE);
    }
}

void gevrTacticalCacheVest(GevrTacticalVest vest, int body)
{
    if (!gevrTacticalVestRenderList(vest, body, FALSE)) return;
    gevrTacticalVestArmList(vest, 0, FALSE);
    gevrTacticalVestArmList(vest, 1, FALSE);
}

void gevrTacticalCacheModel(const Model *model, int kind)
{
    if (!model || !model->obj) return;
    const struct HelmetAttachment *hat = helmetAttachment(model);
    const int shell = tacticalHelmetVariant(model);
    if (hat || shell >= 0) {
        const int helmet = hat ? hat->helmet : s_helmetShells[shell].kind;
        TacticalModel *m = &s_helmets[helmet];
        cachedRigidList(&m->gpuList[TACT_CACHE_BODY], m->list, FALSE);
        return;
    }
    if (kind < 0 || kind >= TACTICAL_GUN_COUNT) return;
    int buildId = -1;
    const TacticalMagazine *magazine = gevrTacticalGearModelMagazine(model, kind, &buildId);
    if (tacticalGunIsExpanded(kind) || (magazine && magazine->gear_part)) {
        float low[3], high[3];
        gevrTacticalLooseMagBounds(magazine ? magazine->gear_part : 0, kind, low, high);
    } else {
        gevrTacticalHeldMagList(kind);
    }
    prewarmAssembly(kind, buildId);
    for (int i = 0; i < s_modelCount; ++i) {
        TacticalModel *m = &s_models[i];
        if (m->header != model->obj || tacticalGunIsExpanded(m->expanded) || !loadMesh(m->asset)) continue;
        if (!m->built) build(m);
        if (!m->built) return;
        if (m->firstperson) {
            warmGearTextures(m->list);
        } else {
            cachedRigidList(&m->gpuList[TACT_CACHE_BODY], m->list, FALSE);
            buildGuardAkFit(m);
            cachedRigidList(&m->gpuList[TACT_CACHE_GUARD], m->npcList, FALSE);
            const GevrGearAssembly *assembly = gevrTacticalGearDeployedAssembly(buildId);
            if (assembly || (magazine && magazine->gear_part)) {
                if (assembly) cachedGunList(m, &m->gearCore, TACT_LIST_GEAR_CORE, FALSE);
                else cachedGunList(m, &m->gearBody, 4, FALSE);
                cachedGunList(m, &m->gearMagazine, TACT_LIST_INSTALLED_MAG, FALSE);
            }
        }
        return;
    }
}

void gevrTacticalCachePlayerWeapon(int kind, int buildId, int suppressed)
{
    if (kind < 0 || kind >= TACTICAL_GUN_COUNT) return;
    prewarmAssembly(kind, buildId);
    if (!tacticalGunIsExpanded(kind)) {
        int slot;
        TacticalModel *m = mountedGunModel(kind, suppressed, &slot);
        if (m) {
            mountedGunBody(m, TRUE);
            mountedGunBody(m, FALSE);
            gevrTacticalHeldMagList(kind);
        }
    }
    for (int pose = TACTICAL_GLOVE_READY; pose <= TACTICAL_GLOVE_SUPPORT; ++pose)
        for (int mirrored = 0; mirrored < 2; ++mirrored)
            gevrTacticalFittedGloveList(kind, pose, mirrored, FALSE);
}

void gevrTacticalCachePlayerHands(void)
{
    for (int pose = TACTICAL_GLOVE_OPEN; pose <= TACTICAL_GLOVE_PINCH; ++pose)
        for (int mirrored = 0; mirrored < 2; ++mirrored)
            gevrTacticalFittedGloveList(-1, pose, mirrored, FALSE);
}

int gevrTacticalMeshTexture(const void *addr, const u8 **rgba, int *w, int *h)
{
    const uintptr_t address = (uintptr_t)addr, base = (uintptr_t)s_gearTexKeys;
    const uintptr_t opticBase = (uintptr_t)s_gearOpticKeys;
    const size_t stride = sizeof(s_gearTexKeys[0][0]);
    if (address >= opticBase && address - opticBase < sizeof(s_gearOpticKeys)
        && (address - opticBase) % stride == 0) {
        const size_t key = (address - opticBase) / stride;
        const TacticalGearMesh *entry = s_gearOpticMeshes[key / GEVR_GEAR_PARTS][key % GEVR_GEAR_PARTS];
        if (entry && entry->mesh.status == 1) {
            *rgba = entry->mesh.tex[0].rgba;
            *w = entry->mesh.tex[0].w;
            *h = entry->mesh.tex[0].h;
            return 1;
        }
    }
    if (address >= base && address - base < sizeof(s_gearTexKeys) && (address - base) % stride == 0) {
        const size_t key = (address - base) / stride, tex = key % TACT_TEX_MAX;
        const TacticalAsset *part = &s_gearMeshes[key / TACT_TEX_MAX].mesh;
        if (part->status == 1 && tex < (size_t)part->ntex) {
            *rgba = part->tex[tex].rgba;
            *w = part->tex[tex].w;
            *h = part->tex[tex].h;
            return 1;
        }
    }
    const uintptr_t meshBase = (uintptr_t)s_texkey;
    if (address >= meshBase && address - meshBase < sizeof(s_texkey) && (address - meshBase) % stride == 0) {
        const size_t key = (address - meshBase) / stride, tex = key % TACT_TEX_MAX;
        const TacticalAsset *asset = &s_assets[key / TACT_TEX_MAX];
        if (asset->status == 1 && tex < (size_t)asset->ntex) {
            *rgba = asset->tex[tex].rgba; *w = asset->tex[tex].w; *h = asset->tex[tex].h; return 1;
        }
    }
    return 0;
}
