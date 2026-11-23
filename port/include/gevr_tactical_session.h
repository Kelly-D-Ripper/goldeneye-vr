#ifndef GEVR_TACTICAL_SESSION_H
#define GEVR_TACTICAL_SESSION_H
#include "gevr_tactical_inventory.h"
#ifdef __cplusplus
extern "C" {
#endif
/* One independent mission. The engine owns the remaining loose-ammo count;
 * magazines moved to the rig are debited exactly once on first equip. */
enum TacticalGun {
    TACTICAL_GUN_AK, TACTICAL_GUN_USP, TACTICAL_GUN_MP5, TACTICAL_GUN_MP5K,
    TACTICAL_GUN_AK74N, TACTICAL_GUN_MAKAROV, TACTICAL_GUN_SKORPION, TACTICAL_GUN_SVD,
#define TACTICAL_EXPANDED(id, group, native, gun, prop, capacity, cadence, name) TACTICAL_GUN_##id,
#define TACTICAL_EXPANDED_BOUNDARY TACTICAL_GUN_RPG,
#include "gevr_tactical_expanded.inc.h"
#undef TACTICAL_EXPANDED
#undef TACTICAL_EXPANDED_BOUNDARY
    TACTICAL_GUN_COUNT
};
enum { TACTICAL_ORIGINAL_PRIMARY_FAMILIES = 16,
       TACTICAL_PRIMARY_FAMILY_COUNT = TACTICAL_ORIGINAL_PRIMARY_FAMILIES + TACTICAL_GUN_COUNT - TACTICAL_GUN_FN57,
       TACTICAL_SKORPION_MAG_TYPE = 6, TACTICAL_SVD_MAG_TYPE = 7,
       TACTICAL_EXPANDED_MAG_TYPE = 8 };

static inline int tacticalGunIsExpanded(int gun)
{
    return gun >= TACTICAL_GUN_UZI && gun < TACTICAL_GUN_COUNT && gun != TACTICAL_GUN_RPG;
}

static inline int tacticalGunIsAr(int gun)
{
    return gun == TACTICAL_GUN_M4A1 || gun == TACTICAL_GUN_HK416;
}

static inline int tacticalGunIsPistol(int gun)
{
    switch (gun) {
    case TACTICAL_GUN_USP:
    case TACTICAL_GUN_MAKAROV:
#define TACTICAL_PRIMARY(id)
#define TACTICAL_SECONDARY(id) case TACTICAL_GUN_##id:
#define TACTICAL_EXPANDED(id, group, native, model, prop, capacity, cadence, name) TACTICAL_##group(id)
#include "gevr_tactical_expanded.inc.h"
#undef TACTICAL_EXPANDED
#undef TACTICAL_SECONDARY
#undef TACTICAL_PRIMARY
        return 1;
    default:
        return 0;
    }
}

static inline int tacticalGunRigSlots(int gun)
{
    if (gun == TACTICAL_GUN_PKM) return 1;
    if (gun == TACTICAL_GUN_RPG) return 2;
    return tacticalGunIsPistol(gun) ? 2 : 3;
}

/* Enemy rigs and the player's wells use the same capacity and family keys. */
static inline unsigned int tacticalGunCapacity(int gun, int ppk)
{
    switch (gun) {
    case TACTICAL_GUN_USP: return ppk ? 7u : 12u;
    case TACTICAL_GUN_MAKAROV: return 8u;
    case TACTICAL_GUN_MP5K: case TACTICAL_GUN_SKORPION: return 20u;
    case TACTICAL_GUN_SVD: return 10u;
    case TACTICAL_GUN_RPG: return 1u;
#define TACTICAL_EXPANDED(id, group, native, model, prop, capacity, cadence, name) \
    case TACTICAL_GUN_##id: return capacity;
#include "gevr_tactical_expanded.inc.h"
#undef TACTICAL_EXPANDED
    default: return 30u;
    }
}

unsigned int tacticalGunMagazineType(int gun, unsigned int pistolType);

typedef struct TacticalStoredGun {
    TacticalMagazine magazine;
    unsigned char chambered;
} TacticalStoredGun;
typedef struct TacticalSession {
    TacticalInventory inv;
    TacticalInteraction interaction[TACTICAL_GUN_COUNT];
    unsigned int next_id;
    unsigned int pistol_mag_type; /* compatibility key for the selected pistol */
    unsigned char initialized[TACTICAL_GUN_COUNT];
    TacticalStoredGun stored[TACTICAL_GUN_COUNT];
    TacticalMagazine primary_rig_bank[TACTICAL_PRIMARY_FAMILY_COUNT][3];
    signed char active[2], rig_family;
    unsigned char physical_rig; /* the same pouches are used for all weapons */
    unsigned char rig_seeded[TACTICAL_PRIMARY_FAMILY_COUNT];
} TacticalSession;
void tacticalSessionInit(TacticalSession *session);
int tacticalSessionSetPistolMagazineType(TacticalSession *session, unsigned int type);
TacticalMagazine tacticalSessionNewMagazine(TacticalSession *session, int gun, unsigned int capacity, unsigned int rounds);
int tacticalSessionEquip(TacticalSession *session, int gun, unsigned int loaded,
                         unsigned int capacity, unsigned int *reserve, unsigned int *shown_loaded);
int tacticalSessionStow(TacticalSession *session, int gun);
int tacticalSessionFire(TacticalSession *session, int gun, unsigned int *shown_loaded);
int tacticalSessionEjectMagazine(TacticalSession *session, int gun, int well, TacticalMagazine *mag);
int tacticalSessionInteract(TacticalSession *session, int gun, const TacticalInteractionInput *in,
    TacticalMagazine *dropped, unsigned int *ejected, unsigned int *shown_loaded);
int tacticalSessionInteractAtWell(TacticalSession *session, int gun, TacticalInteraction *interaction,
    const TacticalInteractionInput *in, TacticalMagazine *dropped, unsigned int *ejected, unsigned int *shown_loaded);
unsigned int tacticalSessionLoaded(const TacticalSession *session, int gun);
/* Asset selection for the physical magazine in a hand, on a rig or on the ground. */
int tacticalMagazineGun(const TacticalMagazine *mag);
#ifdef __cplusplus
}
#endif
#endif
