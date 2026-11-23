#ifndef GEVR_TACTICAL_NET_H
#define GEVR_TACTICAL_NET_H

#include "gevr_tactical_gear.h"

enum {
    GEVR_VIS_RIGHT, GEVR_VIS_LEFT, GEVR_VIS_SHOULDER, GEVR_VIS_HIP_LEFT, GEVR_VIS_HIP_RIGHT,
    GEVR_VIS_GUNS
};
enum {
    GEVR_VIS_RIG_MAGS = 5, GEVR_VIS_MAG_RIGHT = GEVR_VIS_RIG_MAGS, GEVR_VIS_MAG_LEFT, GEVR_VIS_MAGS
};
enum {
    GEVR_VIS_CHAMBER = 1, GEVR_VIS_SUPPRESSED = 2,
    GEVR_VIS_KNIFE = 1, GEVR_VIS_KATANA = 2, GEVR_VIS_LEFT_HANDED = 4
};
enum { GEVR_VIS_MOUNT_BYTES = (GEVR_GEAR_MOUNTS + 1) / 2 };

typedef struct GevrVisualWeapon {
    int item, build;
    TacticalMagazine magazine;
    uint16_t shots;
    uint8_t stock, action, manual, flags;  /* travel in tenths of a cm */
    uint8_t mounts[GEVR_VIS_MOUNT_BYTES];  /* each nibble: accessory mode and folded optic */
} GevrVisualWeapon;

typedef struct GevrTacticalVisual {
    GevrVisualWeapon weapons[GEVR_VIS_GUNS];
    TacticalMagazine magazines[GEVR_VIS_MAGS];
    uint8_t body;
} GevrTacticalVisual;

struct Model;
struct ChrRecord;
struct netbuf;

typedef struct GevrGuardEquipment {
    int vest, helmet, count;
    TacticalMagazine mags[3];
} GevrGuardEquipment;

void gevrTacticalGuardEquipment(struct ChrRecord *chr, GevrGuardEquipment *gear);
void gevrTacticalApplyGuardRig(struct ChrRecord *chr, const GevrGuardEquipment *gear, unsigned revision);
const GevrGuardEquipment *gevrTacticalRemoteGuardGear(const struct ChrRecord *chr);
struct PropRecord;

void gevrTacticalCollectVisual(GevrTacticalVisual *visual);
int gevrTacticalGuardIssuedBuild(const struct PropRecord *prop);
int gevrTacticalNativeRifle(int item);
void gevrTacticalNetWriteMagazine(struct netbuf *packet, const TacticalMagazine *mag);
int gevrTacticalNetReadMagazine(struct netbuf *packet, TacticalMagazine *mag);
void gevrTacticalWriteVisualBuild(struct netbuf *packet, int item, int build, int stock);
int gevrTacticalReadVisualBuild(struct netbuf *packet, GevrVisualWeapon *gun);
void gevrTacticalCollectMountVisual(const GevrGearAssembly *assembly, int hand,
    uint8_t mounts[GEVR_VIS_MOUNT_BYTES]);
const GevrVisualWeapon *gevrTacticalRemoteWeapon(const struct Model *model);
const GevrTacticalVisual *gevrTacticalRemoteBody(const struct ChrRecord *chr);

void gevrTacticalVisualTick(void);
void gevrTacticalVisualReset(void);
void gevrTacticalVisualForgetSlot(int slot);
void gevrTacticalVisualSendTo(int slot);
int gevrTacticalVisualReceive(int slot, int msg, struct netbuf *packet);

#endif
