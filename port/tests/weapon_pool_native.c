/* Live weapon records are protected from recycling while held. The fixture
 * supplies the small surrounding world for the actual allocator. */
#include "net_core.h"
#include "game/chrai.h"
#include "game/initunk_005520.h"
#include "game/loadobjectmodel.h"
#include "game/model.h"
#include "game/objecthandler.h"
#include "game/propobj.h"
#include "memp.h"
#include "system.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

WeaponObjRecord g_WeaponSlots[MAX_WEAPON_SLOTS];
AmmoCrateRecord g_AmmoCrates[MAX_AMMO_CRATES];
stagesetup g_CurrentSetup;
s32 g_NextWeaponSlot;
struct ModelSlot *g_ModelSlots;
s32 g_MaxModelSlots, g_MaxAnimModelSlots, g_NumSetupModelSlots;
s32 g_ModelIsLvResetting;

static NetState s_state;
static struct { NetMatchConfig config; } s_round;
static int players = 1;
static bool physical, tacticalCoop;
static PropRecord guard, props[MAX_WEAPON_SLOTS];
static Model reusableModel;
static u32 modelData[4];
static ObjectRecord *freed;
static _Alignas(16) u8 stageMemory[256 * 1024];
static size_t stageBytes;
static struct {
    WeaponObjRecord weapon;
    PropDefHeaderRecord end;
} setup;

s32 getPlayerCount(void) { return players; }

void *mempAllocBytesInBank(u32 bytes, u8 bank)
{
    assert(bank == MEMPOOL_STAGE);
    const size_t start = stageBytes;
    stageBytes += (bytes + 15u) & ~15u;
    assert(stageBytes <= sizeof(stageMemory));
    return stageMemory + start;
}

void gevrBeanForgetWounds(Model *model) { (void)model; }

void modelInitRwData(Model *model, ModelNode *node)
{
    assert(model && !node);
}

void sysLogPrintf(s32 level, const char *format, ...)
{
    (void)level;
    (void)format;
}

s32 sysArgCheck(const char *key)
{
    if (!strcmp(key, "--tactical-physical")) return physical;
    if (!strcmp(key, "--tactical-coop")) return tacticalCoop;
    assert(!"unexpected game-mode flag");
    return FALSE;
}

s32 sizepropdef(PropDefHeaderRecord *def)
{
    assert(def->type == PROPDEF_COLLECTABLE);
    return sizeof(WeaponObjRecord) / 4;
}

ObjectRecord *setupGetPtrToCommandByIndex(s32 index)
{
    return g_CurrentSetup.propDefs && index == 0 ? (ObjectRecord *)&setup.weapon : NULL;
}

void objFreePermanently(ObjectRecord *object, bool freeprop)
{
    assert(freeprop && object->prop);
    assert(!freed);
    freed = object;
    object->prop = NULL;
}

/* PRODUCTION */

static void fillWhileHeld(int capacity)
{
    memset(g_WeaponSlots, 0, sizeof(g_WeaponSlots));
    memset(props, 0, sizeof(props));
    g_NextWeaponSlot = 0;
    g_CurrentSetup.propDefs = NULL;
    freed = NULL;
    for (int i = 0; i < capacity; i++) {
        WeaponObjRecord *slot = weaponCreate(FALSE, FALSE, NULL);
        assert(slot == &g_WeaponSlots[i]);
        slot->prop = &props[i];
        slot->type = PROPDEF_COLLECTABLE;
        slot->weaponnum = ITEM_AK47;
        slot->model = &reusableModel;
        props[i].parent = &guard;
    }
    assert(!weaponCreate(FALSE, FALSE, NULL));
    assert(!freed && g_NextWeaponSlot == 0);
}

static void checkModeCapacity(void)
{
    assert(weaponGetSlotCapacity() == 30);
    fillWhileHeld(30);
    physical = TRUE;
    assert(weaponGetSlotCapacity() == 256);
    fillWhileHeld(256);

    players = 2;
    assert(weaponGetSlotCapacity() == 30);
    players = 1;
    s_state = NET_STATE_HOSTING_LOBBY;
    s_round.config.mode = NET_MODE_COOP;
    assert(weaponGetSlotCapacity() == 30);
    s_state = NET_STATE_INGAME;
    assert(weaponGetSlotCapacity() == 30);
    tacticalCoop = TRUE;
    players = 4;
    assert(weaponGetSlotCapacity() == 256);
    fillWhileHeld(256);
    s_state = NET_STATE_MIGRATING;
    assert(weaponGetSlotCapacity() == 256);

    s_state = NET_STATE_INGAME;
    s_round.config.mode = NET_MODE_DEATHMATCH;
    assert(weaponGetSlotCapacity() == 30);
    s_round.config.mode = NET_MODE_HORDE;
    assert(weaponGetSlotCapacity() == 30);
    g_NextWeaponSlot = 255;
    assert(!weaponCreate(FALSE, FALSE, NULL));
    assert(g_NextWeaponSlot == 0);
    s_round.config.mode = NET_MODE_COOP;
    physical = FALSE;
    assert(weaponGetSlotCapacity() == 30);
    physical = TRUE;
}

static void checkRecyclingAndIndices(void)
{
    assert(weaponGetSlotCapacity() == 256);
    fillWhileHeld(256);
    const int last = MAX_WEAPON_SLOTS - 1;
    assert(netObjectIndex((ObjectRecord *)&g_WeaponSlots[last]) == -1);
    assert(!netObjectByIndex(0x8000, PROPDEF_COLLECTABLE));
    assert(!netObjectByIndex(0x80ff, PROPDEF_COLLECTABLE));
    g_AmmoCrates[0].type = PROPDEF_MAGAZINE;
    assert(netObjectIndex((ObjectRecord *)&g_AmmoCrates[0]) == -1);
    assert(!netObjectByIndex(0x9000, PROPDEF_MAGAZINE));

    props[20].parent = props[21].parent = props[22].parent = props[last].parent = NULL;
    props[20].flags = PROPFLAG_ONSCREEN;
    g_WeaponSlots[21].runtime_bitflags = RUNTIMEBITFLAG_HASPROJECTILE;
    g_WeaponSlots[22].state = PROPSTATE_RESPAWN;
    ModelFileHeader request = {.numRecords = 5};
    reusableModel.datas = modelData;
    reusableModel.rwdatalen = 4;
    assert(!weaponCreate(TRUE, TRUE, &request));
    assert(!freed);

    request.numRecords = 4;
    assert(weaponCreate(TRUE, TRUE, &request) == &g_WeaponSlots[last]);
    assert(freed == (ObjectRecord *)&g_WeaponSlots[last]);
    freed = NULL;
    assert(weaponCreate(TRUE, TRUE, &request) == &g_WeaponSlots[20]);
    assert(freed == (ObjectRecord *)&g_WeaponSlots[20]);
    assert(g_WeaponSlots[21].prop && g_WeaponSlots[22].prop);

    fillWhileHeld(256);
    setup.weapon.type = PROPDEF_COLLECTABLE;
    setup.end.type = PROPDEF_END;
    g_CurrentSetup.propDefs = (PropDefHeaderRecord *)&setup;
    assert(netObjectIndex((ObjectRecord *)&setup.weapon) == 0);
    assert(netObjectByIndex(0, PROPDEF_COLLECTABLE) == (ObjectRecord *)&setup.weapon);
    assert(weaponCreate(FALSE, FALSE, NULL) == &setup.weapon);
    assert(!freed);
}

static void checkModelReserve(bool tactical, int capacity)
{
    physical = tactical;
    g_CurrentSetup.propDefs = NULL;
    fillWhileHeld(capacity);

    const int setupSlots = 3;
    ModelFileHeader header = {.numRecords = 2};
    stageBytes = 0;
    modelmgrResetSlotCounts();
    modelmgrAllocateModelSlots(setupSlots, weaponGetSlotCapacity());
    assert(g_MaxModelSlots == setupSlots + capacity);
    assert(g_NumSetupModelSlots == setupSlots);

    g_ModelIsLvResetting = TRUE;
    for (int i = 0; i < setupSlots; i++)
        assert(modelmgrInstantiateModel(&header) == (Model *)&g_ModelSlots[i]);
    assert(modelmgrInstantiateModel(&header) != (Model *)&g_ModelSlots[setupSlots]);

    g_ModelIsLvResetting = FALSE;
    const size_t initialBytes = stageBytes;
    for (int i = 0; i < capacity; i++) {
        Model *model = modelmgrInstantiateModel(&header);
        assert(model == (Model *)&g_ModelSlots[setupSlots + i]);
        assert(model->obj == &header && model->datas && model->rwdatalen >= header.numRecords);
        g_WeaponSlots[i].model = model;
    }
    Model *released = g_WeaponSlots[capacity - 1].model;
    for (int i = 0; i < 500; i++) {
        clear_model_obj(released);
        assert(modelmgrInstantiateModel(&header) == released);
    }
    assert(stageBytes == initialBytes);
}

int main(void)
{
    guard.type = PROP_TYPE_CHR;
    checkModeCapacity();
    checkRecyclingAndIndices();
    checkModelReserve(TRUE, 256);
    checkModelReserve(FALSE, 30);
    puts("PASS: 256 held Tactical guns offline and in co-op, 30 in stock modes, protected slots, high-slot recycling and private pool indices");
    puts("PASS: setup cannot consume the dynamic model reserve, and 500 reuse cycles consume no further stage memory");
    return 0;
}
