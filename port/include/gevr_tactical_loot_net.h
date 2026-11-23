#ifndef GEVR_TACTICAL_LOOT_NET_H
#define GEVR_TACTICAL_LOOT_NET_H

#include "gevr_tactical_inventory.h"
#include <stdint.h>

struct netbuf;
struct PropRecord;

/* Rig magazines have host-assigned IDs in the high half of the ID range. */
#define GEVR_RIG_LOOT_ID UINT32_C(0x80000000)
#define GEVR_WORLD_MAGAZINE_CAPACITY 128
enum { GEVR_LOOT_GUN = 1, GEVR_LOOT_MAG, GEVR_LOOT_RIG };

typedef struct GevrSharedLoot {
    uint32_t id;
    int kind, item, build, fireMode, chamber, nativeRounds, nativeMissing;
    TacticalMagazine magazine;
    float position[3], orientation[3][3];
    uint8_t flat, magnet;
} GevrSharedLoot;

int gevrTacticalLootEnabled(void);
void gevrTacticalLootStageReset(void);
void gevrTacticalLootForgetSlot(int slot);
void gevrTacticalLootSendTo(int slot);
void gevrTacticalLootTick(void);
void gevrTacticalLootReceive(struct netbuf *packet, int sender);
uint32_t gevrTacticalLootPublish(const GevrSharedLoot *loot);
int gevrTacticalLootClaim(uint32_t id, int ctrl);
int gevrTacticalLootClaimProp(const struct PropRecord *prop, int ctrl);
uint32_t gevrTacticalLootPropId(const struct PropRecord *prop);
void gevrTacticalLootPublishGuardDrop(struct PropRecord *prop);
int gevrTacticalLootCanPublish(void);

/* The native game adapters own objects, magazines and hands. */
int gevrTacticalLootPutWorld(const GevrSharedLoot *loot);
int gevrTacticalLootSample(GevrSharedLoot *loot, int *moving);
void gevrTacticalLootMoveWorld(const GevrSharedLoot *loot);
void gevrTacticalLootEraseWorld(uint32_t id);
void gevrTacticalLootUnshareWorld(uint32_t id);
int gevrTacticalLootTakeLocal(const GevrSharedLoot *loot, int ctrl);
int gevrTacticalLootReadRig(uint32_t id, GevrSharedLoot *loot);
void gevrTacticalLootEraseRig(uint32_t id);
int gevrTacticalLootReserved(uint32_t id);
void gevrTacticalLootRetireRig(uint32_t id);

#endif
