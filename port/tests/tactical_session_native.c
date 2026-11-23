#include "gevr_tactical_session.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned int sessionRounds(const TacticalSession *s)
{
    unsigned int total = tacticalCountRounds(&s->inv);
    for (int i = 0; i < TACTICAL_GUN_COUNT; i++) total += s->stored[i].magazine.rounds + s->stored[i].chambered;
    for (int f = 0; f < TACTICAL_PRIMARY_FAMILY_COUNT; f++)
        for (int k = 0; k < 3; k++) total += s->primary_rig_bank[f][k].rounds;
    return total;
}
static void checkPhysicalRig(void)
{
    TacticalSession s;
    unsigned int loaded, reserve = 30;
    tacticalSessionInit(&s);
    s.physical_rig = 1;
    assert(tacticalSessionEquip(&s, TACTICAL_GUN_AK, 30, 30, &reserve, &loaded) == TACTICAL_OK);
    assert(reserve == 0);
    assert(tacticalSessionStow(&s, TACTICAL_GUN_AK) == TACTICAL_OK);

    /* Loot two partial PKM boxes and a pistol magazine before equipping
     * another weapon. The type, part, position and round count must survive. */
    TacticalMagazine belt = tacticalSessionNewMagazine(&s, TACTICAL_GUN_PKM, 100, 73);
    belt.gear_part = 68;
    assert(tacticalAddMagazine(&s.inv, TACTICAL_LEFT_HAND, belt) == TACTICAL_OK);
    assert(tacticalMoveMagazine(&s.inv, TACTICAL_LEFT_HAND, TACTICAL_RIG_PRIMARY_2) == TACTICAL_OK);
    belt = tacticalSessionNewMagazine(&s, TACTICAL_GUN_PKM, 100, 24);
    assert(tacticalAddMagazine(&s.inv, TACTICAL_LEFT_HAND, belt) == TACTICAL_OK);
    assert(tacticalMoveMagazine(&s.inv, TACTICAL_LEFT_HAND, TACTICAL_RIG_PRIMARY_3) == TACTICAL_OK);
    const TacticalMagazine pistol = tacticalSessionNewMagazine(&s, TACTICAL_GUN_FN57, 20, 13);
    assert(tacticalAddMagazine(&s.inv, TACTICAL_RIG_PISTOL_1, pistol) == TACTICAL_OK);

    TacticalMagazine pockets[5];
    memcpy(pockets, &s.inv.slots[TACTICAL_RIG_PRIMARY_1], sizeof(pockets));
    const unsigned int total = sessionRounds(&s);
    for (int gun = 0; gun < TACTICAL_GUN_COUNT; ++gun) {
        assert(tacticalSessionEquip(&s, gun, 0, tacticalGunCapacity(gun, 1), &reserve, &loaded) == TACTICAL_OK);
        assert(memcmp(pockets, &s.inv.slots[TACTICAL_RIG_PRIMARY_1], sizeof(pockets)) == 0);
        assert(sessionRounds(&s) == total);
    }
    assert(tacticalSessionEquip(&s, TACTICAL_GUN_PKM, 0, 100, &reserve, &loaded) == TACTICAL_OK);
    TacticalMagazine empty;
    assert(tacticalEjectMagazine(&s.inv, TACTICAL_PRIMARY_WELL, &empty) == TACTICAL_OK);
    assert(tacticalMoveMagazine(&s.inv, TACTICAL_RIG_PRIMARY_1, TACTICAL_LEFT_HAND) == TACTICAL_OK);
    assert(tacticalMoveMagazine(&s.inv, TACTICAL_LEFT_HAND, TACTICAL_PRIMARY_WELL) == TACTICAL_INCOMPATIBLE);
    assert(tacticalMoveMagazine(&s.inv, TACTICAL_LEFT_HAND, TACTICAL_RIG_PRIMARY_1) == TACTICAL_OK);
    assert(tacticalMoveMagazine(&s.inv, TACTICAL_RIG_PRIMARY_2, TACTICAL_LEFT_HAND) == TACTICAL_OK);
    assert(tacticalMoveMagazine(&s.inv, TACTICAL_LEFT_HAND, TACTICAL_PRIMARY_WELL) == TACTICAL_OK);
    assert(s.inv.slots[TACTICAL_PRIMARY_WELL].id == pockets[1].id);
    assert(s.inv.slots[TACTICAL_PRIMARY_WELL].rounds == 73);
    assert(s.inv.slots[TACTICAL_RIG_PRIMARY_3].id == pockets[2].id && sessionRounds(&s) == total);
}

int main(void)
{
    checkPhysicalRig();
    TacticalSession s;
    TacticalInteractionInput in = {0};
    TacticalMagazine drop = {0};
    unsigned int ammo = 80, shown = 0, ejected = 0;
    tacticalSessionInit(&s);
    assert(tacticalSessionEquip(&s, 1, 7, 12, &ammo, &shown) == TACTICAL_OK);
    assert(shown == 7 && ammo == 56 && tacticalCountRounds(&s.inv) == 31);
    assert(tacticalSessionFire(&s, 1, &shown) == TACTICAL_OK && shown == 6);
    assert(tacticalSessionFire(&s, 1, &shown) == TACTICAL_OK && shown == 5);
    /* Switching away and back must restore the same rounds and not reseed. */
    assert(tacticalSessionEquip(&s, 1, 0, 12, &ammo, &shown) == TACTICAL_OK);
    assert(shown == 5 && ammo == 56 && tacticalCountRounds(&s.inv) == 29);
    in.tracked = 1; in.grip = 1; in.magazine_distance_cm = 0; in.action_distance_cm = 99;
    for (int k = 0; k < 5; k++) in.rig_distance_cm[k] = 99;
    assert(tacticalSessionInteract(&s, 1, &in, &drop, &ejected, &shown) == TACTICAL_EVENT_NONE);
    in.magazine_pull_cm = 6;
    assert(tacticalSessionInteract(&s, 1, &in, &drop, &ejected, &shown) == TACTICAL_EVENT_MAG_TAKEN);
    assert(shown == 1); /* retained chamber */
    in.magazine_distance_cm = 99; in.rig_distance_cm[0] = 0; in.grip = 0;
    assert(tacticalSessionInteract(&s, 1, &in, &drop, &ejected, &shown) == TACTICAL_EVENT_MAG_DROPPED);
    assert(drop.rounds == 4 && tacticalCountRounds(&s.inv) + drop.rounds + ammo == 85);
    /* A physical fresh magazine must be carried to the well. */
    in.rig_distance_cm[0] = 99; in.rig_distance_cm[3] = 0; in.grip = 1;
    assert(tacticalSessionInteract(&s, 1, &in, &drop, &ejected, &shown) == TACTICAL_EVENT_MAG_TAKEN);
    in.rig_distance_cm[3] = 99; in.magazine_distance_cm = 0; in.grip = 0;
    assert(tacticalSessionInteract(&s, 1, &in, &drop, &ejected, &shown) == TACTICAL_EVENT_MAG_SEATED);
    assert(shown == 13); /* a fresh 12 plus the retained chamber */
    assert(tacticalSessionFire(&s, 1, &shown) == TACTICAL_OK && shown == 12);
    assert(tacticalCountRounds(&s.inv) + drop.rounds + ammo == 84);
    {
        TacticalSession x;
        unsigned int rifleReserve = 60, smgReserve = 60, pistolReserve = 24, empty = 0;
        unsigned int firstAkId, firstSmgId, sum;
        tacticalSessionInit(&x);
        assert(tacticalSessionEquip(&x, TACTICAL_GUN_AK, 30, 30, &rifleReserve, &shown) == TACTICAL_OK);
        firstAkId = x.inv.slots[TACTICAL_RIG_PRIMARY_1].id;
        assert(rifleReserve == 0 && shown == 30);
        assert(tacticalSessionFire(&x, TACTICAL_GUN_AK, &shown) == TACTICAL_OK && shown == 29);
        assert(tacticalSessionStow(&x, TACTICAL_GUN_AK) == TACTICAL_OK);
        assert(tacticalSessionEquip(&x, TACTICAL_GUN_USP, 7, 12, &pistolReserve, &shown) == TACTICAL_OK);
        assert(tacticalSessionFire(&x, TACTICAL_GUN_USP, &shown) == TACTICAL_OK && shown == 6);
        assert(tacticalSessionStow(&x, TACTICAL_GUN_USP) == TACTICAL_OK);
        assert(tacticalSessionEquip(&x, TACTICAL_GUN_MP5, 30, 30, &smgReserve, &shown) == TACTICAL_OK);
        firstSmgId = x.inv.slots[TACTICAL_RIG_PRIMARY_1].id;
        assert(firstSmgId != firstAkId && x.inv.slots[TACTICAL_RIG_PRIMARY_1].type == 3 && smgReserve == 0);
        assert(tacticalSessionFire(&x, TACTICAL_GUN_MP5, &shown) == TACTICAL_OK && shown == 29);
        sum = sessionRounds(&x);
        assert(sum == 208);
        assert(tacticalSessionStow(&x, TACTICAL_GUN_MP5) == TACTICAL_OK);
        assert(tacticalSessionEquip(&x, TACTICAL_GUN_MP5K, 20, 20, &empty, &shown) == TACTICAL_OK);
        assert(x.inv.slots[TACTICAL_RIG_PRIMARY_1].id == firstSmgId); /* same MP5 family */
        assert(tacticalMoveMagazine(&x.inv, TACTICAL_PRIMARY_WELL, TACTICAL_LEFT_HAND) == TACTICAL_OK);
        assert(tacticalDropMagazine(&x.inv, TACTICAL_LEFT_HAND, &drop) == TACTICAL_OK && drop.capacity == 20);
        {
            TacticalMagazine wrongFamily = {999, 1, 30, 5, TACTICAL_MAG_PRIMARY}, returned;
            assert(tacticalAddMagazine(&x.inv, TACTICAL_LEFT_HAND, wrongFamily) == TACTICAL_OK);
            assert(tacticalMoveMagazine(&x.inv, TACTICAL_LEFT_HAND, TACTICAL_PRIMARY_WELL) == TACTICAL_INCOMPATIBLE);
            assert(tacticalDropMagazine(&x.inv, TACTICAL_LEFT_HAND, &returned) == TACTICAL_OK && returned.id == 999);
        }
        assert(tacticalMoveMagazine(&x.inv, TACTICAL_RIG_PRIMARY_1, TACTICAL_LEFT_HAND) == TACTICAL_OK);
        assert(tacticalMoveMagazine(&x.inv, TACTICAL_LEFT_HAND, TACTICAL_PRIMARY_WELL) == TACTICAL_OK);
        assert(x.inv.slots[TACTICAL_PRIMARY_WELL].capacity == 30); /* full-size mag fits MP5K */
        assert(tacticalSessionStow(&x, TACTICAL_GUN_MP5K) == TACTICAL_OK);
        assert(tacticalSessionEquip(&x, TACTICAL_GUN_AK, 0, 30, &empty, &shown) == TACTICAL_OK && shown == 29);
        assert(x.inv.slots[TACTICAL_RIG_PRIMARY_1].id == firstAkId);
        assert(sessionRounds(&x) + drop.rounds == sum + 20);
        assert(tacticalSessionStow(&x, TACTICAL_GUN_AK) == TACTICAL_OK);
        assert(tacticalSessionEquip(&x, TACTICAL_GUN_MP5, 0, 30, &empty, &shown) == TACTICAL_OK && shown == 29);
        assert(x.inv.slots[TACTICAL_RIG_PRIMARY_1].id == 0); /* magazine remains in stowed MP5K */
        assert(sessionRounds(&x) + drop.rounds == sum + 20);
        assert(tacticalSessionEquip(&x, TACTICAL_GUN_USP, 0, 12, &empty, &shown) == TACTICAL_OK && shown == 6);
        assert(sessionRounds(&x) + drop.rounds == sum + 20);
    }
    {
        TacticalSession x;
        TacticalInteractionInput hand = {0};
        unsigned int ammo9 = 30, none = 0, heldId;
        tacticalSessionInit(&x);
        assert(tacticalSessionEquip(&x, TACTICAL_GUN_MP5, 30, 30, &ammo9, &shown) == TACTICAL_OK);
        hand.tracked = hand.grip = 1; hand.magazine_distance_cm = 0; hand.action_distance_cm = 99;
        for (int k = 0; k < 5; k++) hand.rig_distance_cm[k] = 99;
        assert(tacticalSessionInteract(&x, TACTICAL_GUN_MP5, &hand, &drop, &ejected, &shown) == TACTICAL_EVENT_NONE);
        hand.magazine_pull_cm = 6;
        assert(tacticalSessionInteract(&x, TACTICAL_GUN_MP5, &hand, &drop, &ejected, &shown) == TACTICAL_EVENT_MAG_TAKEN);
        heldId = x.inv.slots[TACTICAL_LEFT_HAND].id;
        assert(tacticalSessionStow(&x, TACTICAL_GUN_MP5) == TACTICAL_OK);
        assert(tacticalSessionEquip(&x, TACTICAL_GUN_MP5K, 20, 20, &none, &shown) == TACTICAL_OK);
        assert(x.inv.slots[TACTICAL_LEFT_HAND].id == heldId && x.interaction[TACTICAL_GUN_MP5K].require_regrip);
        hand.grip = 0; hand.magazine_distance_cm = 99; hand.rig_distance_cm[1] = 0;
        assert(tacticalSessionInteract(&x, TACTICAL_GUN_MP5K, &hand, &drop, &ejected, &shown) == TACTICAL_EVENT_NONE);
        assert(x.inv.slots[TACTICAL_LEFT_HAND].id == heldId);
        hand.grip = 1;
        assert(tacticalSessionInteract(&x, TACTICAL_GUN_MP5K, &hand, &drop, &ejected, &shown) == TACTICAL_EVENT_NONE);
        hand.grip = 0;
        assert(tacticalSessionInteract(&x, TACTICAL_GUN_MP5K, &hand, &drop, &ejected, &shown) == TACTICAL_EVENT_MAG_STORED);
        assert(x.inv.slots[TACTICAL_RIG_PRIMARY_2].id == heldId && sessionRounds(&x) == 80);
    }
    {
        TacticalSession x;
        unsigned int noReserves = 0;
        const TacticalMagazine looted = {0x80000011u, 1, 30, 23, TACTICAL_MAG_PRIMARY};
        tacticalSessionInit(&x);
        assert(tacticalAddMagazine(&x.inv, TACTICAL_LEFT_HAND, looted) == TACTICAL_OK);
        assert(tacticalMoveMagazine(&x.inv, TACTICAL_LEFT_HAND, TACTICAL_RIG_PRIMARY_1) == TACTICAL_OK);
        assert(tacticalSessionEquip(&x, TACTICAL_GUN_AK, 30, 30, &noReserves, &shown) == TACTICAL_OK);
        assert(x.inv.slots[TACTICAL_RIG_PRIMARY_1].id == looted.id);
        assert(sessionRounds(&x) == 53);
    }
    {
        TacticalSession x;
        unsigned int reserve = 60, none = 0;
        const TacticalMagazine mp5k = {999u, 3, 20, 17, TACTICAL_MAG_PRIMARY};
        tacticalSessionInit(&x);
        assert(tacticalSessionEquip(&x, TACTICAL_GUN_SKORPION, 20, 20, &reserve, &shown) == TACTICAL_OK);
        unsigned id = x.inv.slots[TACTICAL_RIG_PRIMARY_1].id;
        assert(reserve == 0 && shown == 20 && id);
        assert(tacticalMagazineGun(&x.inv.slots[TACTICAL_RIG_PRIMARY_1]) == TACTICAL_GUN_SKORPION);
        assert(tacticalSessionEquip(&x, TACTICAL_GUN_MP5K, 20, 20, &none, &shown) == TACTICAL_OK);
        assert(!x.inv.slots[TACTICAL_RIG_PRIMARY_1].id && sessionRounds(&x) == 100);
        assert(tacticalSessionEquip(&x, TACTICAL_GUN_SKORPION, 0, 20, &none, &shown) == TACTICAL_OK);
        assert(x.inv.slots[TACTICAL_RIG_PRIMARY_1].id == id && sessionRounds(&x) == 100);
        assert(tacticalEjectMagazine(&x.inv, TACTICAL_PRIMARY_WELL, &drop) == TACTICAL_OK);
        assert(tacticalAddMagazine(&x.inv, TACTICAL_LEFT_HAND, mp5k) == TACTICAL_OK);
        assert(tacticalMoveMagazine(&x.inv, TACTICAL_LEFT_HAND, TACTICAL_PRIMARY_WELL) == TACTICAL_INCOMPATIBLE);
        assert(tacticalDropMagazine(&x.inv, TACTICAL_LEFT_HAND, &drop) == TACTICAL_OK);
        assert(tacticalSessionFire(&x, TACTICAL_GUN_SKORPION, &shown) == TACTICAL_OK && shown == 0);
        assert(tacticalMoveMagazine(&x.inv, TACTICAL_RIG_PRIMARY_1, TACTICAL_LEFT_HAND) == TACTICAL_OK);
        assert(tacticalMoveMagazine(&x.inv, TACTICAL_LEFT_HAND, TACTICAL_PRIMARY_WELL) == TACTICAL_OK);
        assert(tacticalSessionFire(&x, TACTICAL_GUN_SKORPION, &shown) == TACTICAL_EMPTY_CHAMBER && shown == 20);
        assert(tacticalRack(&x.inv, TACTICAL_PRIMARY_WELL, &ejected) == TACTICAL_OK && ejected == 0);
        assert(tacticalSessionFire(&x, TACTICAL_GUN_SKORPION, &shown) == TACTICAL_OK && shown == 19);
    }
    {
        TacticalSession launcher;
        TacticalInteractionInput hand = {.tracked = 1, .grip = 1, .magazine_distance_cm = 99};
        const int gun = TACTICAL_GUN_RPG;
        unsigned int rockets = 2;
        tacticalSessionInit(&launcher);
        assert(tacticalSessionEquip(&launcher, gun, 1, 1, &rockets, &shown) == TACTICAL_OK);
        assert(shown == 1 && rockets == 0 && sessionRounds(&launcher) == 3);
        assert(!launcher.inv.slots[TACTICAL_PRIMARY_WELL].id);
        assert(tacticalSessionFire(&launcher, gun, &shown) == TACTICAL_OK && shown == 0);
        assert(tacticalSessionFire(&launcher, gun, &shown) == TACTICAL_EMPTY_CHAMBER);
        TacticalMagazine wrong = tacticalSessionNewMagazine(&launcher, TACTICAL_GUN_AK, 30, 1);
        assert(tacticalAddMagazine(&launcher.inv, TACTICAL_LEFT_HAND, wrong) == TACTICAL_OK);
        assert(tacticalMoveMagazine(&launcher.inv, TACTICAL_LEFT_HAND, TACTICAL_PRIMARY_WELL) == TACTICAL_INCOMPATIBLE);
        assert(tacticalDropMagazine(&launcher.inv, TACTICAL_LEFT_HAND, &drop) == TACTICAL_OK);
        for (int k = 1; k < 5; ++k) hand.rig_distance_cm[k] = 99;
        assert(tacticalSessionInteract(&launcher, gun, &hand, &drop, &ejected, &shown) == TACTICAL_EVENT_MAG_TAKEN);
        hand.grip = 0;
        hand.rig_distance_cm[0] = 99;
        hand.magazine_distance_cm = 0;
        assert(tacticalSessionInteract(&launcher, gun, &hand, &drop, &ejected, &shown) == TACTICAL_EVENT_MAG_SEATED);
        assert(shown == 1 && !launcher.inv.slots[TACTICAL_PRIMARY_WELL].id);
        assert(!launcher.inv.slots[TACTICAL_LEFT_HAND].id && sessionRounds(&launcher) == 2);
        assert(tacticalSessionStow(&launcher, gun) == TACTICAL_OK);
        assert(tacticalSessionEquip(&launcher, gun, 0, 1, &rockets, &shown) == TACTICAL_OK && shown == 1);
        assert(tacticalSessionFire(&launcher, gun, &shown) == TACTICAL_OK && shown == 0 && sessionRounds(&launcher) == 1);
    }
    puts("tactical session: shared physical rig on every gun, legacy banks, partial magazines and RPG reload passed");
    return 0;
}
