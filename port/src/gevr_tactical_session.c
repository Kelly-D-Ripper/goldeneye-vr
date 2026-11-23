#include "gevr_tactical_session.h"
#include <math.h>
#include <string.h>

static int validGun(int gun) { return gun >= 0 && gun < TACTICAL_GUN_COUNT; }
static int gunWell(int gun) { return tacticalGunIsPistol(gun) ? TACTICAL_RIGHT_PISTOL_WELL : TACTICAL_PRIMARY_WELL; }
static int category(int gun) { return tacticalGunIsPistol(gun) ? 1 : 0; }
static int family(int gun)
{
    switch (gun) {
    case TACTICAL_GUN_AK: case TACTICAL_GUN_AK74N: return 0;
    case TACTICAL_GUN_SKORPION: return 2;
    case TACTICAL_GUN_SVD: return 3;
    case TACTICAL_GUN_UZI: return 4;
    case TACTICAL_GUN_SR2M: return 5;
    case TACTICAL_GUN_MP9: return 6;
    case TACTICAL_GUN_EVO3: return 7;
    case TACTICAL_GUN_PM9: return 8;
    case TACTICAL_GUN_PKM: return 9;
    case TACTICAL_GUN_M4A1: case TACTICAL_GUN_HK416: return 10;
    case TACTICAL_GUN_ASVAL: return 11;
    case TACTICAL_GUN_P90: return 12;
    case TACTICAL_GUN_M32A1: return 13;
    case TACTICAL_GUN_SAIGA12K: return 14;
    case TACTICAL_GUN_RPG: return 15;
    case TACTICAL_GUN_MP7A1: case TACTICAL_GUN_MP7A2:
        return TACTICAL_ORIGINAL_PRIMARY_FAMILIES + TACTICAL_GUN_MP7A1 - TACTICAL_GUN_FN57;
    case TACTICAL_GUN_L85A2: return 10;
    default: return gun >= TACTICAL_GUN_FN57
        ? TACTICAL_ORIGINAL_PRIMARY_FAMILIES + gun - TACTICAL_GUN_FN57 : 1;
    }
}

unsigned int tacticalGunMagazineType(int gun, unsigned int pistolType)
{
    if (!validGun(gun)) return 0;
    if (tacticalGunIsAr(gun) || gun == TACTICAL_GUN_L85A2) gun = TACTICAL_GUN_M4A1;
    if (gun == TACTICAL_GUN_MP7A2) gun = TACTICAL_GUN_MP7A1;
    if (gun == TACTICAL_GUN_GLOCK18C || gun == TACTICAL_GUN_GLOCK19X) gun = TACTICAL_GUN_GLOCK17;
    if (gun >= TACTICAL_GUN_UZI) return TACTICAL_EXPANDED_MAG_TYPE + gun - TACTICAL_GUN_UZI;
    switch (gun) {
    case TACTICAL_GUN_AK: case TACTICAL_GUN_AK74N: return 1u;
    case TACTICAL_GUN_USP: return pistolType;
    case TACTICAL_GUN_MAKAROV: return 5u;
    case TACTICAL_GUN_SKORPION: return TACTICAL_SKORPION_MAG_TYPE;
    case TACTICAL_GUN_SVD: return TACTICAL_SVD_MAG_TYPE;
    default: return 3u;
    }
}

int tacticalMagazineGun(const TacticalMagazine *mag)
{
    if (!mag || !mag->id) return -1;
    if (mag->type >= TACTICAL_EXPANDED_MAG_TYPE
        && mag->type < TACTICAL_EXPANDED_MAG_TYPE + TACTICAL_GUN_COUNT - TACTICAL_GUN_UZI)
        return TACTICAL_GUN_UZI + (int)(mag->type - TACTICAL_EXPANDED_MAG_TYPE);
    switch (mag->type) {
    case 1: return TACTICAL_GUN_AK;
    case 2: case 4: return TACTICAL_GUN_USP;
    case 3: return mag->capacity <= 20 ? TACTICAL_GUN_MP5K : TACTICAL_GUN_MP5;
    case 5: return TACTICAL_GUN_MAKAROV;
    case TACTICAL_SKORPION_MAG_TYPE: return TACTICAL_GUN_SKORPION;
    case TACTICAL_SVD_MAG_TYPE: return TACTICAL_GUN_SVD;
    default: return -1;
    }
}

unsigned int tacticalSessionLoaded(const TacticalSession *s, int gun)
{
    int well;
    if (!s || !validGun(gun) || !s->initialized[gun]) return 0;
    well = gunWell(gun);
    if (s->active[category(gun)] != gun) return s->stored[gun].magazine.rounds + s->stored[gun].chambered;
    return s->inv.slots[well].rounds + s->inv.chambered[well];
}
void tacticalSessionInit(TacticalSession *s)
{
    if (!s) return;
    memset(s, 0, sizeof(*s));
    tacticalInventoryInit(&s->inv);
    s->pistol_mag_type = 2u;
    for (int gun = 0; gun < TACTICAL_GUN_COUNT; gun++)
        tacticalInteractionInit(&s->interaction[gun], gunWell(gun), TACTICAL_LEFT_HAND);
    s->next_id = 1;
    s->active[0] = s->active[1] = s->rig_family = -1;
}
int tacticalSessionSetPistolMagazineType(TacticalSession *s, unsigned int type)
{
    if (!s || !type || s->initialized[TACTICAL_GUN_USP] || s->active[1] >= 0) return TACTICAL_INVALID;
    s->pistol_mag_type = type;
    return TACTICAL_OK;
}
TacticalMagazine tacticalSessionNewMagazine(TacticalSession *s, int gun, unsigned int capacity, unsigned int rounds)
{
    if (!s || !validGun(gun) || !capacity || rounds > capacity) return (TacticalMagazine){0};
    return (TacticalMagazine){s->next_id++, tacticalGunMagazineType(gun, s->pistol_mag_type), capacity, rounds,
        tacticalGunIsPistol(gun) ? TACTICAL_MAG_PISTOL : TACTICAL_MAG_PRIMARY};
}
int tacticalSessionStow(TacticalSession *s, int gun)
{
    int well, cat;
    if (!s || !validGun(gun) || !s->initialized[gun]) return TACTICAL_INVALID;
    well = gunWell(gun); cat = category(gun);
    if (s->active[cat] != gun) return TACTICAL_OK;
    s->stored[gun].magazine = s->inv.slots[well];
    s->stored[gun].chambered = s->inv.chambered[well];
    memset(&s->inv.slots[well], 0, sizeof(s->inv.slots[well]));
    s->inv.chambered[well] = 0;
    s->active[cat] = -1;
    tacticalInteractionInit(&s->interaction[gun], well, TACTICAL_LEFT_HAND);
    return TACTICAL_OK;
}
static void switchPrimaryRig(TacticalSession *s, int next)
{
    if (s->rig_family == next) return;
    if (s->rig_family < 0) {
        /* Magazines looted before the first long gun are already in these slots. */
        s->rig_family = (signed char)next;
        return;
    }
    for (int k = 0; k < 3; k++) {
        s->primary_rig_bank[(int)s->rig_family][k] = s->inv.slots[TACTICAL_RIG_PRIMARY_1+k];
        s->inv.slots[TACTICAL_RIG_PRIMARY_1+k] = s->primary_rig_bank[next][k];
        memset(&s->primary_rig_bank[next][k], 0, sizeof(TacticalMagazine));
    }
    s->rig_family = (signed char)next;
}

int tacticalSessionEquip(TacticalSession *s, int gun, unsigned int loaded,
                         unsigned int capacity, unsigned int *reserve, unsigned int *shown)
{
    int well, rig, slots, cat, fam;
    unsigned int type;
    TacticalMagazine magazine;
    if (!s || !validGun(gun) || !capacity || loaded > capacity || !reserve || !shown) return TACTICAL_INVALID;
    well = gunWell(gun); cat = category(gun); fam = family(gun); type = tacticalGunMagazineType(gun, s->pistol_mag_type);
    if (s->active[cat] == gun) { *shown = tacticalSessionLoaded(s, gun); return TACTICAL_OK; }
    if (s->active[cat] >= 0) tacticalSessionStow(s, s->active[cat]);
    if (!s->physical_rig && !tacticalGunIsPistol(gun)) switchPrimaryRig(s, fam);
    if (tacticalSetWell(&s->inv, well, type) != TACTICAL_OK) return TACTICAL_OCCUPIED;
    if (s->initialized[gun]) {
        s->inv.slots[well] = s->stored[gun].magazine;
        s->inv.chambered[well] = s->stored[gun].chambered;
        memset(&s->stored[gun], 0, sizeof(s->stored[gun]));
    } else {
        rig = tacticalGunIsPistol(gun) ? TACTICAL_RIG_PISTOL_1 : TACTICAL_RIG_PRIMARY_1;
        slots = tacticalGunRigSlots(gun);
        if (gun != TACTICAL_GUN_RPG) {
            magazine = tacticalSessionNewMagazine(s, gun, capacity, loaded ? loaded - 1 : 0);
            if (tacticalAddMagazine(&s->inv, well, magazine) != TACTICAL_OK) return TACTICAL_INVALID;
        }
        s->inv.chambered[well] = loaded ? 1 : 0;
        if (tacticalGunIsPistol(gun) || !s->rig_seeded[fam]) {
            for (int k = 0; k < slots && *reserve; k++) {
                if (s->inv.slots[rig + k].id) continue;
                unsigned int count = *reserve < capacity ? *reserve : capacity;
                magazine = tacticalSessionNewMagazine(s, gun, capacity, count);
                if (tacticalAddMagazine(&s->inv, rig + k, magazine) != TACTICAL_OK) break;
                *reserve -= count;
            }
            if (!tacticalGunIsPistol(gun)) s->rig_seeded[fam] = 1;
        }
        s->initialized[gun] = 1;
    }
    s->active[cat] = (signed char)gun;
    if (s->inv.slots[TACTICAL_LEFT_HAND].id) {
        s->interaction[gun].state = 2;
        s->interaction[gun].require_regrip = 1;
    }
    *shown = tacticalSessionLoaded(s, gun);
    return TACTICAL_OK;
}
int tacticalSessionFire(TacticalSession *s, int gun, unsigned int *shown)
{
    int result;
    if (!s || !validGun(gun) || !s->initialized[gun] || s->active[category(gun)] != gun || !shown) return TACTICAL_INVALID;
    result = tacticalFire(&s->inv, gunWell(gun));
    *shown = tacticalSessionLoaded(s, gun);
    return result;
}
static void emptyCylinderChamber(TacticalInventory *inv, int well, TacticalMagazine *cylinder)
{
    if (inv->chambered[well]) {
        if (cylinder->rounds < cylinder->capacity) ++cylinder->rounds;
        inv->chambered[well] = 0;
    }
}

int tacticalSessionEjectMagazine(TacticalSession *s, int gun, int well, TacticalMagazine *mag)
{
    if (!s) return TACTICAL_INVALID;
    const int result = tacticalEjectMagazine(&s->inv, well, mag);
    if (result == TACTICAL_OK && gun == TACTICAL_GUN_M32A1)
        emptyCylinderChamber(&s->inv, well, mag);
    return result;
}

int tacticalSessionInteractAtWell(TacticalSession *s, int gun, TacticalInteraction *interaction,
    const TacticalInteractionInput *in, TacticalMagazine *dropped, unsigned int *ejected, unsigned int *shown)
{
    int event;
    if (!s || !validGun(gun) || !interaction || !shown
        || interaction->well < 0 || interaction->well >= TACTICAL_WELL_COUNT) return TACTICAL_EVENT_NONE;
    const int well = interaction->well;
    if (gun == TACTICAL_GUN_RPG && in) {
        /* A rocket occupies the chamber by itself. Insertion consumes that
         * physical item, leaving the launcher empty again after the shot. */
        const TacticalMagazine *held = &s->inv.slots[interaction->hand];
        TacticalInteractionInput round = *in;
        round.action_distance_cm = INFINITY;
        if (s->inv.chambered[well] || held->rounds != 1)
            round.magazine_distance_cm = INFINITY;
        event = tacticalInteractionTick(interaction, &s->inv, &round, dropped, ejected);
        if (event == TACTICAL_EVENT_MAG_SEATED) {
            memset(&s->inv.slots[well], 0, sizeof(TacticalMagazine));
            s->inv.chambered[well] = 1;
        }
    } else if (gun == TACTICAL_GUN_M32A1 && in) {
        TacticalInteractionInput cylinder = *in;
        cylinder.action_distance_cm = INFINITY;
        const unsigned installed = s->inv.slots[well].id;
        event = tacticalInteractionTick(interaction, &s->inv, &cylinder, dropped, ejected);
        if (event == TACTICAL_EVENT_MAG_TAKEN && installed && !s->inv.slots[well].id)
            emptyCylinderChamber(&s->inv, well, &s->inv.slots[interaction->hand]);
        if (event == TACTICAL_EVENT_MAG_SEATED && s->inv.slots[well].rounds) {
            --s->inv.slots[well].rounds;
            s->inv.chambered[well] = 1;
        }
    } else {
        event = tacticalInteractionTick(interaction, &s->inv, in, dropped, ejected);
    }
    *shown = s->inv.slots[well].rounds + s->inv.chambered[well];
    return event;
}

int tacticalSessionInteract(TacticalSession *s, int gun, const TacticalInteractionInput *in,
    TacticalMagazine *dropped, unsigned int *ejected, unsigned int *shown)
{
    if (!s || !validGun(gun) || !s->initialized[gun] || s->active[category(gun)] != gun) return TACTICAL_EVENT_NONE;
    return tacticalSessionInteractAtWell(s, gun, &s->interaction[gun], in, dropped, ejected, shown);
}
