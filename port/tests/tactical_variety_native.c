#include "gevr_tactical_session.h"
#include "gevr_tactical_fit.h"
#include <assert.h>
#include <stdio.h>

static void check_model_orientation(void)
{
    const float authoredLeft[3] = {-1.f, 0.f, 0.f};
    float model[3];
    gevrTacticalViewAxes(authoredLeft, model);
    assert(model[0] == 1.f && model[1] == 0.f && model[2] == 0.f);
    gevrTacticalPropAxes(authoredLeft, model);
    assert(model[0] == 0.f && model[1] == -1.f && model[2] == 0.f);

    float pose[4][4] = {{-2.f, 0.f, 0.f}, {0.f, 2.f, 0.f}, {0.f, 0.f, 2.f}, {8.f, 9.f, 10.f, 1.f}};
    gevrTacticalUnmirrorBasis(pose);
    gevrTacticalUnmirrorBasis(pose);
    assert(pose[0][0] == 2.f && pose[1][1] == 2.f && pose[2][2] == 2.f);
    assert(pose[3][0] == 8.f && pose[3][1] == 9.f && pose[3][2] == 10.f);

    for (int kind = 0; kind < TACTICAL_GUN_UZI; ++kind) {
        gevrTacticalEjectionPort(kind, 1, 1, model);
        assert(model[0] < 0.f);
    }
    gevrTacticalActionSocket(TACTICAL_GUN_MP5, 1, 1, model);
    assert(model[0] > 0.f);
    gevrTacticalActionSocket(TACTICAL_GUN_MP5K, 1, 1, model);
    assert(model[0] > 0.f);
}

static void check_expanded_feeds(void)
{
    for (int gun = TACTICAL_GUN_UZI; gun < TACTICAL_GUN_COUNT; ++gun) {
        TacticalSession s;
        tacticalSessionInit(&s);
        const unsigned capacity = gun == TACTICAL_GUN_PKM ? 100 : tacticalGunIsPistol(gun) ? 12 : 30;
        unsigned reserve = capacity * 3, shown, ejected;
        assert(tacticalSessionEquip(&s, gun, 1, capacity, &reserve, &shown) == TACTICAL_OK);
        const int well = tacticalGunIsPistol(gun) ? TACTICAL_RIGHT_PISTOL_WELL : TACTICAL_PRIMARY_WELL;
        const int rig = tacticalGunIsPistol(gun) ? TACTICAL_RIG_PISTOL_1 : TACTICAL_RIG_PRIMARY_1;
        assert(tacticalMagazineGun(&s.inv.slots[well]) == (tacticalGunIsAr(gun) ? TACTICAL_GUN_M4A1 : gun));
        assert(reserve == capacity * (3 - tacticalGunRigSlots(gun)));
        assert(tacticalSessionFire(&s, gun, &shown) == TACTICAL_OK && shown == 0);
        TacticalMagazine removed;
        assert(tacticalEjectMagazine(&s.inv, well, &removed) == TACTICAL_OK && removed.rounds == 0);

        for (int other = TACTICAL_GUN_UZI; other < TACTICAL_GUN_COUNT; ++other) {
            if (other == gun || (tacticalGunIsAr(gun) && tacticalGunIsAr(other))) continue;
            TacticalMagazine wrong = tacticalSessionNewMagazine(&s, other, 12, 7);
            assert(tacticalAddMagazine(&s.inv, TACTICAL_LEFT_HAND, wrong) == TACTICAL_OK);
            assert(tacticalMoveMagazine(&s.inv, TACTICAL_LEFT_HAND, well) == TACTICAL_INCOMPATIBLE);
            assert(tacticalDropMagazine(&s.inv, TACTICAL_LEFT_HAND, &wrong) == TACTICAL_OK);
        }
        assert(tacticalMoveMagazine(&s.inv, rig, TACTICAL_LEFT_HAND) == TACTICAL_OK);
        assert(tacticalMoveMagazine(&s.inv, TACTICAL_LEFT_HAND, well) == TACTICAL_OK);
        assert(tacticalSessionFire(&s, gun, &shown) == TACTICAL_EMPTY_CHAMBER);
        assert(tacticalRack(&s.inv, well, &ejected) == TACTICAL_OK && ejected == 0);
        assert(tacticalSessionFire(&s, gun, &shown) == TACTICAL_OK && shown == capacity - 1);
        assert(tacticalSessionStow(&s, gun) == TACTICAL_OK);
    }
}

static void check_shared_rifle_magazine(void)
{
    TacticalSession s;
    unsigned reserve = 60, loaded, ejected;
    tacticalSessionInit(&s);
    assert(tacticalSessionEquip(&s, TACTICAL_GUN_M4A1, 30, 30, &reserve, &loaded) == TACTICAL_OK);
    const TacticalMagazine spare = s.inv.slots[TACTICAL_RIG_PRIMARY_1];
    assert(spare.id && spare.rounds == 30);
    assert(tacticalSessionStow(&s, TACTICAL_GUN_M4A1) == TACTICAL_OK);
    assert(tacticalSessionEquip(&s, TACTICAL_GUN_HK416, 0, 30, &reserve, &loaded) == TACTICAL_OK);
    assert(s.inv.slots[TACTICAL_RIG_PRIMARY_1].id == spare.id);
    TacticalMagazine empty;
    assert(tacticalEjectMagazine(&s.inv, TACTICAL_PRIMARY_WELL, &empty) == TACTICAL_OK);
    assert(tacticalMoveMagazine(&s.inv, TACTICAL_RIG_PRIMARY_1, TACTICAL_LEFT_HAND) == TACTICAL_OK);
    assert(tacticalMoveMagazine(&s.inv, TACTICAL_LEFT_HAND, TACTICAL_PRIMARY_WELL) == TACTICAL_OK);
    assert(s.inv.slots[TACTICAL_PRIMARY_WELL].id == spare.id);
    assert(tacticalRack(&s.inv, TACTICAL_PRIMARY_WELL, &ejected) == TACTICAL_OK);
    assert(tacticalSessionFire(&s, TACTICAL_GUN_HK416, &loaded) == TACTICAL_OK && loaded == 29);
}

int main(void) {
 check_model_orientation();
 check_expanded_feeds();
 check_shared_rifle_magazine();
 TacticalSession s; unsigned int reserve=60,n=0; TacticalMagazine old;
 tacticalSessionInit(&s); assert(tacticalSessionSetPistolMagazineType(&s,4)==TACTICAL_OK);
 assert(tacticalSessionEquip(&s,TACTICAL_GUN_AK,30,30,&reserve,&n)==TACTICAL_OK);
 unsigned first=s.inv.slots[TACTICAL_RIG_PRIMARY_1].id;
 assert(first && s.inv.slots[TACTICAL_RIG_PRIMARY_1].type==1);
 assert(tacticalSessionStow(&s,TACTICAL_GUN_AK)==TACTICAL_OK);
 reserve=0;assert(tacticalSessionEquip(&s,TACTICAL_GUN_AK74N,30,30,&reserve,&n)==TACTICAL_OK);
 assert(s.inv.slots[TACTICAL_RIG_PRIMARY_1].id==first);
 assert(tacticalMoveMagazine(&s.inv,TACTICAL_PRIMARY_WELL,TACTICAL_LEFT_HAND)==TACTICAL_OK);
 assert(tacticalDropMagazine(&s.inv,TACTICAL_LEFT_HAND,&old)==TACTICAL_OK);
 assert(tacticalMoveMagazine(&s.inv,TACTICAL_RIG_PRIMARY_1,TACTICAL_LEFT_HAND)==TACTICAL_OK);
 assert(tacticalMoveMagazine(&s.inv,TACTICAL_LEFT_HAND,TACTICAL_PRIMARY_WELL)==TACTICAL_OK);
 assert(s.inv.slots[TACTICAL_PRIMARY_WELL].id==first);
 assert(tacticalSessionStow(&s,TACTICAL_GUN_AK74N)==TACTICAL_OK);
 reserve=14;assert(tacticalSessionEquip(&s,TACTICAL_GUN_USP,7,7,&reserve,&n)==TACTICAL_OK);
 assert(tacticalSessionStow(&s,TACTICAL_GUN_USP)==TACTICAL_OK);
 reserve=0;assert(tacticalSessionEquip(&s,TACTICAL_GUN_MAKAROV,8,8,&reserve,&n)==TACTICAL_OK);
 assert(s.inv.slots[TACTICAL_RIGHT_PISTOL_WELL].type==5);
 assert(tacticalMoveMagazine(&s.inv,TACTICAL_RIGHT_PISTOL_WELL,TACTICAL_LEFT_HAND)==TACTICAL_OK);
 assert(tacticalDropMagazine(&s.inv,TACTICAL_LEFT_HAND,&old)==TACTICAL_OK);
 assert(tacticalMoveMagazine(&s.inv,TACTICAL_RIG_PISTOL_1,TACTICAL_LEFT_HAND)==TACTICAL_OK);
 assert(tacticalMoveMagazine(&s.inv,TACTICAL_LEFT_HAND,TACTICAL_RIGHT_PISTOL_WELL)==TACTICAL_INCOMPATIBLE);
 assert(tacticalDropMagazine(&s.inv,TACTICAL_LEFT_HAND,&old)==TACTICAL_OK);
 TacticalMagazine enemy={9001,5,8,5,TACTICAL_MAG_PISTOL};
 assert(tacticalAddMagazine(&s.inv,TACTICAL_LEFT_HAND,enemy)==TACTICAL_OK);
 assert(tacticalMoveMagazine(&s.inv,TACTICAL_LEFT_HAND,TACTICAL_RIGHT_PISTOL_WELL)==TACTICAL_OK);
 assert(s.inv.slots[TACTICAL_RIGHT_PISTOL_WELL].id==9001);
 assert(s.inv.slots[TACTICAL_RIGHT_PISTOL_WELL].rounds==5);
 puts("AK magazines are shared; Makarov rejects PPK magazines and preserves a looted Makarov magazine");return 0;
}
