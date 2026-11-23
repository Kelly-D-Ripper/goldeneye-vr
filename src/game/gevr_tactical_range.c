/* A private practice session over the airstrip's floor and collision. */
#include <ultra64.h>
#include <bondgame.h>
#include <bondconstants.h>
#include <boss.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "system.h"
#include "bondview.h"
#include "chraction.h"
#include "chr.h"
#include "dyn.h"
#include "gevr_tactical_range.h"
#include "gevr_tactical_gear.h"
#include "lv.h"
#include "matrixmath.h"
#include "mp_music.h"
#include "player.h"
#include "stan.h"

extern f32 D_800364CC;
extern PropRecord *chrSpawnAtCoord(s32 body, s32 head, coord3d *pos, StandTile *tile,
    f32 angle, AIRecord *ailist, s32 flags);
extern void gevrRangeChrWalk(ChrRecord *chr, coord3d *point, StandTile *tile);
extern void gevrRangeChrStop(ChrRecord *chr);

static int s_requested, s_active, s_returning, s_moving, s_ready;
static PadRecord s_start;
static const int s_distance[GEVR_RANGE_TARGETS] = {1000, 2500, 4000, 5500};
static struct {
    int chr, direction;
    u32 replaceAt;
    coord3d origin;
} s_targets[GEVR_RANGE_TARGETS];
static const int s_lateral[GEVR_RANGE_TARGETS] = {-360, -155, 140, 370};
static const int s_bodies[GEVR_RANGE_TARGETS] = {BODY_Russian_Soldier, BODY_Russian_Infantry,
    BODY_Arctic_Commando, BODY_St_Petersburg_Guard};
static const int s_benchItems[] = {ITEM_SNIPERRIFLE, ITEM_TACTICAL_AK74N, ITEM_AK47, ITEM_SPECTRE,
    ITEM_MP5K, ITEM_SKORPION, ITEM_TT33, ITEM_WPPKSIL, ITEM_ROCKETLAUNCH,
#define TACTICAL_EXPANDED(id, group, native, gun, prop, capacity, cadence, name) ITEM_TACTICAL_##id,
#include "gevr_tactical_expanded.inc.h"
#undef TACTICAL_EXPANDED
};
static const struct { int item; const char *label; } s_magazineBins[] = {
    {ITEM_AK47, "AK"}, {ITEM_SPECTRE, "MP5"}, {ITEM_MP5K, "MP5K"}, {ITEM_SKORPION, "SKORPION"},
    {ITEM_SNIPERRIFLE, "SVD"}, {ITEM_WPPKSIL, "PPK"}, {ITEM_TT33, "PM"},
    {ITEM_TACTICAL_UZI, "UZI"}, {ITEM_TACTICAL_SR2M, "SR2M"}, {ITEM_TACTICAL_MP9, "MP9"},
    {ITEM_TACTICAL_EVO3, "EVO3"}, {ITEM_TACTICAL_PM9, "PM9"}, {ITEM_TACTICAL_MK23, "MK23"},
    {ITEM_TACTICAL_PKM, "PKM BOX"}, {ITEM_TACTICAL_M4A1, "5.56 AR"},
    {ITEM_TACTICAL_ASVAL, "VAL"}, {ITEM_TACTICAL_M1911A1, "1911"}, {ITEM_TACTICAL_CZ75B, "CZ 75"},
    {ITEM_TACTICAL_P90, "P90"}, {ITEM_TACTICAL_GLOCK17, "GLOCK"}, {ITEM_TACTICAL_DEAGLE, "DE"},
    {ITEM_TACTICAL_M32A1, "M32"}, {ITEM_TACTICAL_SAIGA12K, "SAIGA"}, {ITEM_ROCKETLAUNCH, "RPG"},
    {ITEM_TACTICAL_FN57, "FN57"}, {ITEM_TACTICAL_MP7A1, "MP7"},
    {ITEM_TACTICAL_KACPDW, "KAC PDW"}, {ITEM_TACTICAL_VITYAZ, "PP-19"},
    {ITEM_TACTICAL_MPX, "MPX"}, {ITEM_TACTICAL_M1A, "M1A"},
    {ITEM_TACTICAL_SR3M, "SR3M"}, {ITEM_TACTICAL_AUGA3, "AUG"},
    {ITEM_TACTICAL_TOKAREV, "TT-33"}, {ITEM_TACTICAL_STM9, "STM-9"},
    {ITEM_TACTICAL_FAMAS, "FAMAS"}, {ITEM_TACTICAL_SKS, "SKS"}
};
static WeaponObjRecord s_benchModels[ARRAYCOUNT(s_benchItems)];

enum {
    RANGE_MESH_QUADS = 3072, RANGE_QUADS_PER_BATCH = 4, RANGE_TABLE_HEIGHT = 96,
    RANGE_GUN_TABLE_X = -1540, RANGE_GUN_TABLE_Z = -198, RANGE_GUN_TABLE_DEPTH = 166,
    RANGE_GUN_SPACING = 61, RANGE_BIN_SIZE = 54
};
static Vtx s_vertices[RANGE_MESH_QUADS * 4];
/* Two triangle commands per quad, one vertex load per batch, and the end. */
static Gfx s_scene[RANGE_MESH_QUADS * 2 + (RANGE_MESH_QUADS + RANGE_QUADS_PER_BATCH - 1) / RANGE_QUADS_PER_BATCH + 1];
static unsigned s_quads;
static int s_gunTableLength, s_magTableWidth;

void gevrTacticalRangeRequest(void) { s_requested = TRUE; }
int gevrTacticalRangeActive(void) { return s_active; }
int gevrTacticalRangeReturning(void) { return s_returning; }
void gevrTacticalRangeFinishReturn(void) { s_returning = FALSE; }
int gevrTacticalRangeMoving(void) { return s_moving; }

void gevrTacticalRangeStageLoad(int stage)
{
    s_active = s_requested && stage == LEVELID_RUNWAY;
    s_requested = s_moving = s_ready = FALSE;
    s_quads = 0;
    memset(&s_start, 0, sizeof(s_start));
    memset(s_targets, 0, sizeof(s_targets));
    for (int i = 0; i < GEVR_RANGE_TARGETS; ++i) s_targets[i].chr = -1;
}

void gevrTacticalRangeLeave(void)
{
    if (!s_active) return;
    s_returning = TRUE;
    set_missionstate(MISSION_STATE_0);
    bossRunTitleStage();
    sysLogPrintf(LOG_NOTE, "workshop: leaving firing range");
}

static coord3d rangePoint(float x, float y, float z)
{
    return (coord3d){.f = {s_start.pos.x + x, s_start.pos.y + y, s_start.pos.z - z}};
}

float gevrTacticalRangeSupport(const coord3d *at, float ceiling, float floor)
{
    if (!s_ready) return floor;
    const float top = s_start.pos.y + RANGE_TABLE_HEIGHT;
    if (top <= floor || top > ceiling) return floor;
    const float x = at->x - s_start.pos.x, z = s_start.pos.z - at->z;
    const bool gunTable = x >= RANGE_GUN_TABLE_X && x <= RANGE_GUN_TABLE_X + RANGE_GUN_TABLE_DEPTH
        && z >= RANGE_GUN_TABLE_Z && z <= RANGE_GUN_TABLE_Z + s_gunTableLength;
    const bool magTable = x >= 127.f && x <= 127.f + s_magTableWidth && z >= -93.f && z <= 61.f;
    return gunTable || magTable ? top : floor;
}

static StandTile *rangeFloor(coord3d *point, float tolerance)
{
    float floor;
    coord3d probe = *point;
    probe.y += 160.f;
    StandTile *tile = stanFindTileBelowPos(&probe, NULL, &floor);
    if (!tile || fabsf(floor - point->y) > tolerance) return NULL;
    point->y = floor;
    return tile;
}

static int rangeFits(int depth, int halfWidth)
{
    for (int side = -1; side <= 1; ++side) {
        coord3d from = rangePoint(side * halfWidth, 0, 0);
        StandTile *tile = rangeFloor(&from, 45.f);
        if (!tile) return FALSE;
        for (int d = 0; d <= 5; ++d) {
            coord3d point = rangePoint(side * halfWidth, 0, depth * d / 5);
            if (!rangeFloor(&point, 45.f) || !walkTilesBetweenPoints_NoCallback(&tile, from.x, from.z, point.x, point.z))
                return FALSE;
            from = point;
        }
    }
    return TRUE;
}

static void rangeQuad(const float corners[4][3], unsigned rgb)
{
    if (s_quads == RANGE_MESH_QUADS) return;
    Vtx *out = s_vertices + 4 * s_quads++;
    for (int i = 0; i < 4; ++i) {
        const coord3d p = rangePoint(corners[i][0], corners[i][1], corners[i][2]);
        out[i] = (Vtx){0};
        for (int j = 0; j < 3; ++j) out[i].v.ob[j] = (s16)lroundf(p.f[j]);
        out[i].v.cn[0] = rgb >> 16;
        out[i].v.cn[1] = rgb >> 8;
        out[i].v.cn[2] = rgb;
        out[i].v.cn[3] = 255;
    }
}

static void rangeBox(float x, float y, float z, float w, float h, float d, unsigned color)
{
    const float p[8][3] = {{x,y,z}, {x+w,y,z}, {x,y+h,z}, {x+w,y+h,z},
        {x,y,z+d}, {x+w,y,z+d}, {x,y+h,z+d}, {x+w,y+h,z+d}};
    static const unsigned char faces[6][4] = {{0,1,3,2},{4,6,7,5},{0,2,6,4},{1,5,7,3},{2,3,7,6},{0,4,5,1}};
    static const float shade[6] = {.90f, .70f, .74f, .84f, 1.f, .60f};
    for (int f = 0; f < 6; ++f) {
        float corners[4][3];
        for (int i = 0; i < 4; ++i) memcpy(corners[i], p[faces[f][i]], sizeof(corners[i]));
        unsigned rgb = 0;
        for (int c = 0; c < 3; ++c) rgb |= (unsigned)(((color >> (8*c)) & 255) * shade[f]) << (8*c);
        rangeQuad(corners, rgb);
    }
}

static void rangeStencil(float x, float y, float z, float size, const char *text, unsigned rgb)
{
    /* Five columns, seven rows. Only the training bay sign and lane markers. */
    static const struct { char letter; unsigned char rows[7]; } glyphs[] = {
        {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}}, {'2',{14,17,1,2,4,8,31}},
        {'3',{30,1,1,14,1,1,30}}, {'4',{2,6,10,18,31,2,2}}, {'5',{31,16,16,30,1,1,30}},
        {'7',{31,1,2,4,8,8,8}}, {'8',{14,17,17,14,17,17,14}}, {'9',{14,17,17,15,1,2,12}},
        {'A',{14,17,17,31,17,17,17}}, {'E',{31,16,16,30,16,16,31}},
        {'F',{31,16,16,30,16,16,16}}, {'G',{14,17,16,23,17,17,15}}, {'I',{14,4,4,4,4,4,14}},
        {'C',{14,17,16,16,16,17,14}}, {'L',{16,16,16,16,16,16,31}},
        {'M',{17,27,21,21,17,17,17}}, {'N',{17,25,25,21,19,19,17}}, {'R',{30,17,17,30,20,18,17}},
        {'D',{28,18,17,17,17,18,28}}, {'K',{17,18,20,24,20,18,17}}, {'O',{14,17,17,17,17,17,14}},
        {'P',{30,17,17,30,16,16,16}}, {'S',{15,16,16,14,1,1,30}}, {'V',{17,17,17,17,17,10,4}},
        {'W',{17,17,17,21,21,27,17}}, {'Z',{31,1,2,4,8,16,31}},
    };
    for (; *text; ++text, x += 6 * size) {
        for (unsigned i = 0; i < sizeof(glyphs)/sizeof(glyphs[0]); ++i) {
            if (*text != glyphs[i].letter) continue;
            for (int row = 0; row < 7; ++row) {
                for (int col = 0; col < 5; ++col) {
                    if (!(glyphs[i].rows[row] & (16 >> col))) continue;
                    const float a = x + col * size, b = y - row * size;
                    const float corners[4][3] = {{a,b,z},{a+size,b,z},{a+size,b-size,z},{a,b-size,z}};
                    rangeQuad(corners, rgb);
                }
            }
            break;
        }
    }
}

static void rangeTable(int x, int z, int width, int depth, const char *name)
{
    const unsigned steel = 0x384344, trim = 0x657374, wood = 0x665641;
    rangeBox(x, RANGE_TABLE_HEIGHT - 8, z, width, 8, depth, wood);
    for (int side = 0; side <= 1; ++side) {
        const int leg = x + (side ? width - 25 : 12);
        rangeBox(leg, 0, z + 13, 13, RANGE_TABLE_HEIGHT - 8, 13, steel);
        rangeBox(leg, 0, z + depth - 26, 13, RANGE_TABLE_HEIGHT - 8, 13, steel);
        rangeBox(leg, 24, z + 25, 13, 8, depth - 50, trim);
    }
    rangeBox(x + 13, 67, z + 5, width - 26, 24, 9, steel);
    rangeStencil(x + 26, 85, z + 4, 1.8f, name, 0xe7ead8);
}

static int rangeBenchCount(void)
{
    int count = 0;
    for (unsigned i = 0; i < ARRAYCOUNT(s_benchItems); ++i)
        count += gevrTacticalItem(s_benchItems[i]) >= 0;
    return count;
}

static int rangeBinCount(void)
{
    int count = 0;
    for (unsigned i = 0; i < ARRAYCOUNT(s_magazineBins); ++i)
        count += gevrTacticalItem(s_magazineBins[i].item) >= 0;
    return count;
}

static int rangeBinX(int index) { return 142 + RANGE_BIN_SIZE * index; }

void gevrTacticalRangeStockTables(void)
{
    if (!s_ready) return;
    int guns = 0, magazines = 0;
    int position = 0;
    for (unsigned i = 0; i < ARRAYCOUNT(s_benchItems); ++i) {
        if (gevrTacticalItem(s_benchItems[i]) < 0) continue;
        coord3d pos = rangePoint(RANGE_GUN_TABLE_X + 122.f, 0.f,
            RANGE_GUN_TABLE_Z + RANGE_GUN_SPACING * (position++ + .5f));
        StandTile *tile = rangeFloor(&pos, 45.f);
        pos.y += RANGE_TABLE_HEIGHT + 4.f;
        guns += gevrTacticalRangePlaceGun(&s_benchModels[i], s_benchItems[i], &pos, tile, 0.f);
    }
    position = 0;
    for (unsigned bin = 0; bin < ARRAYCOUNT(s_magazineBins); ++bin) {
        const int item = s_magazineBins[bin].item;
        if (gevrTacticalItem(item) < 0) continue;
        const int x = rangeBinX(position++);
        const int spares = item == ITEM_TACTICAL_PKM || item == ITEM_TACTICAL_M32A1 ? 1 : 3;
        for (int spare = 0; spare < spares; ++spare) {
            const coord3d surface = rangePoint(x + 20.f, RANGE_TABLE_HEIGHT + 2.f, -61.f + 34.f * spare);
            magazines += gevrTacticalRangePlaceMagazine(item, &surface);
        }
    }
    sysLogPrintf(LOG_NOTE, "workshop: stocked range tables with %d weapons and %d magazines", guns, magazines);
}

static void rangeBuildScene(void)
{
    const unsigned steel = 0x365360, frame = 0x586c72, hazard = 0xd5aa56, ivory = 0xe7ead8;
    rangeBox(-590, 298, -125, 1180, 16, 546, steel);
    rangeBox(-590, 243, 417, 1180, 63, 15, steel);
    rangeBox(-590, 244, 414, 1180, 4, 4, hazard);
    rangeStencil(-305, 286, 416, 5.f, "FIRING RANGE", ivory);
    for (int x = -570; x <= 570; x += 95) rangeBox(x, 289, -125, 11, 9, 528, frame);
    for (int side = -1; side <= 1; side += 2) {
        const int x = side * 560;
        rangeBox(x - 18, 0, 392, 36, 299, 35, steel);
        rangeBox(x - 22, 0, 377, 44, 48, 64, frame);
        rangeBox(x - 64, 0, 235, 128, 91, 139, steel);
        rangeBox(x - 70, 88, 225, 140, 11, 158, frame);
        rangeBox(x - 62, 59, 233, 124, 8, 9, hazard);
        for (int z = 640; z < s_distance[3]; z += 315)
            rangeBox(x - 8, 4, z, 16, 2, 125, 0xabb49e);
    }
    for (int i = 0; i < 20; ++i)
        rangeBox(-498 + 50*i, 4, 436, 50, 2, 18, i & 1 ? 0x463f31 : hazard);
    const int weaponLength = s_gunTableLength = rangeBenchCount() * RANGE_GUN_SPACING;
    const int magazineWidth = s_magTableWidth = 24 + RANGE_BIN_SIZE * rangeBinCount();
    rangeTable(RANGE_GUN_TABLE_X, RANGE_GUN_TABLE_Z, RANGE_GUN_TABLE_DEPTH, weaponLength, "WEAPONS");
    rangeBox(RANGE_GUN_TABLE_X + 9, RANGE_TABLE_HEIGHT, RANGE_GUN_TABLE_Z + 11,
        RANGE_GUN_TABLE_DEPTH - 18, 1, weaponLength - 22, 0x414744);
    rangeTable(127, -93, magazineWidth, 154, "MAGAZINES");
    int bin = 0;
    for (unsigned i = 0; i < ARRAYCOUNT(s_magazineBins); ++i) {
        if (gevrTacticalItem(s_magazineBins[i].item) < 0) continue;
        const int x = rangeBinX(bin++);
        rangeBox(x, RANGE_TABLE_HEIGHT, -80, 44, 2, 117, 0x393e37);
        rangeBox(x, RANGE_TABLE_HEIGHT + 1, 34, 44, 20, 4, steel);
        rangeStencil(x + 3, RANGE_TABLE_HEIGHT + 16, 33, .8f, s_magazineBins[i].label, ivory);
    }
    for (int i = 0; i < GEVR_RANGE_TARGETS; ++i) {
        const int x = s_lateral[i], z = s_distance[i];
        rangeBox(x - 191, 5, z + 44, 382, 5, 19, frame);
        const int edge = x + (x < 0 ? -250 : 222);
        rangeBox(edge, 0, z + 37, 11, 129, 11, steel);
        rangeBox(edge - 13, 111, z + 35, 70, 34, 7, steel);
        char meters[12];
        snprintf(meters, sizeof(meters), "%dM", z / 100);
        rangeStencil(edge - 7, 137, z + 34, 3.f, meters, ivory);
    }
    Gfx *g = s_scene;
    for (unsigned first = 0; first < s_quads; first += RANGE_QUADS_PER_BATCH) {
        unsigned count = s_quads - first;
        if (count > RANGE_QUADS_PER_BATCH) count = RANGE_QUADS_PER_BATCH;
        gSPVertex(g++, osVirtualToPhysical(&s_vertices[4*first]), count * 4, 0);
        for (unsigned i = 0; i < count; ++i) {
            const unsigned v = i*4;
            gSP1Triangle(g++, v, v+1, v+2, 0);
            gSP1Triangle(g++, v, v+2, v+3, 0);
        }
    }
    gSPEndDisplayList(g++);
}

struct PadRecord *gevrTacticalRangeStartPad(void)
{
    if (!s_active) return NULL;
    /* Start at the painted centerline, looking along the runway toward the far cliff. */
    s_start.pos = (coord3d){.f = {9225.f, -659.f, -11555.f}};
    s_start.look = (coord3d){.f = {0.f, 0.f, -1.f}};
    s_start.up = (coord3d){.f = {0.f, 1.f, 0.f}};
    s_start.stan = rangeFloor(&s_start.pos, 45.f);
    if (!s_start.stan || !rangeFits(s_distance[GEVR_RANGE_TARGETS - 1] + 230, 690)) {
        sysLogPrintf(LOG_WARNING, "workshop: no open firing range floor");
        return NULL;
    }
    for (int i = 0; i < GEVR_RANGE_TARGETS; ++i) s_targets[i].origin = rangePoint(s_lateral[i], 0, s_distance[i]);
    rangeBuildScene();
    s_ready = TRUE;
    sysLogPrintf(LOG_NOTE, "workshop: range at %.0f %.0f %.0f, facing %.0f %.0f, lanes at %d %d %d %d metres",
        (double)s_start.pos.x, (double)s_start.pos.y, (double)s_start.pos.z,
        (double)s_start.look.x, (double)s_start.look.z,
        s_distance[0]/100, s_distance[1]/100, s_distance[2]/100, s_distance[3]/100);
    return &s_start;
}

void gevrTacticalRangeToggleMoving(void)
{
    s_moving = !s_moving;
    if (!s_moving) {
        for (int i = 0; i < GEVR_RANGE_TARGETS; ++i) {
            ChrRecord *chr = chrFindByLiteralId(s_targets[i].chr);
            if (chr && chr->actiontype == ACT_GOPOS) gevrRangeChrStop(chr);
        }
    }
    sysLogPrintf(LOG_NOTE, "workshop: moving targets %d", s_moving);
}

void gevrTacticalRangeTick(void)
{
    if (!s_ready || g_ClockTimer == 0) return;
    for (int i = 0; i < GEVR_RANGE_TARGETS; ++i) {
        ChrRecord *chr = chrFindByLiteralId(s_targets[i].chr);
        if (chr && chr->actiontype == ACT_DEAD) {
            if (!s_targets[i].replaceAt) s_targets[i].replaceAt = g_GlobalTimer + 140;
            if (g_GlobalTimer >= s_targets[i].replaceAt) {
                chr->hidden |= CHRHIDDEN_REMOVE;
                s_targets[i].chr = -1;
                chr = NULL;
            }
        }
        if (!chr) {
            coord3d pos = s_targets[i].origin;
            StandTile *floor = rangeFloor(&pos, 55.f);
            PropRecord *prop = floor ? chrSpawnAtCoord(s_bodies[i], -1, &pos, floor,
                atan2f(-s_start.look.x, -s_start.look.z), NULL, 0x10) : NULL;
            if (!prop) continue;
            chr = prop->chr;
            s_targets[i].chr = chr->chrnum;
            s_targets[i].direction = (i & 1) ? -1 : 1;
            s_targets[i].replaceAt = 0;
            sysLogPrintf(LOG_NOTE, "workshop: target %d ready at %d m (chr %d)", i + 1, s_distance[i]/100, chr->chrnum);
        }
        if (s_moving && chr->actiontype == ACT_STAND) {
            const float side = 185.f*s_targets[i].direction;
            coord3d dest = rangePoint(s_lateral[i] + side, 0, s_distance[i]);
            StandTile *floor = rangeFloor(&dest, 60.f);
            if (!floor) continue;
            gevrRangeChrWalk(chr, &dest, floor);
            s_targets[i].direction *= -1;
        }
    }
}

Gfx *gevrTacticalRangeRender(Gfx *g)
{
    if (!s_ready || !s_quads) return g;
    Mtxf *camera = currentPlayerGetViewToWorldMtxf();
    if (!camera) return g;
    Mtxf view;
    matrix_4x4_invert_affine(camera, &view);
    for (int row = 0; row < 4; ++row)
        for (int axis = 0; axis < 3; ++axis) view.m[row][axis] *= D_800364CC;
    Mtx *matrix = dynAllocateMatrix();
    guMtxF2L(view.m, matrix);
    gDPPipeSync(g++);
    gSPMatrix(g++, osVirtualToPhysical(matrix), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPClearGeometryMode(g++, G_LIGHTING | G_CULL_BOTH | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR | G_FOG);
    gSPSetGeometryMode(g++, G_SHADE | G_SHADING_SMOOTH | G_ZBUFFER);
    gSPTexture(g++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetCycleType(g++, G_CYC_1CYCLE);
    gDPSetCombineMode(g++, G_CC_SHADE, G_CC_SHADE);
    gDPSetRenderMode(g++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
    gSPDisplayList(g++, osVirtualToPhysical(s_scene));
    return g;
}
