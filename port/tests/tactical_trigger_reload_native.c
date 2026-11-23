#include "gevr_tactical_session.h"
#include <assert.h>
#include <stdio.h>

static void far(TacticalInteractionInput *in) {
    in->tracked=1; in->magazine_distance_cm=in->action_distance_cm=100;
    for (int n=0;n<5;n++) in->rig_distance_cm[n]=100;
}
int main(void) {
    TacticalSession s; TacticalInteractionInput in={0}; TacticalMagazine dropped={0}, ejectedMag={0};
    unsigned int reserve=14, shown=0, ejected=0, id;
    tacticalSessionInit(&s);
    assert(tacticalSessionSetPistolMagazineType(&s,4)==TACTICAL_OK);
    assert(tacticalSessionEquip(&s,TACTICAL_GUN_USP,7,7,&reserve,&shown)==TACTICAL_OK);
    assert(tacticalSessionFire(&s,TACTICAL_GUN_USP,&shown)==TACTICAL_OK && shown==6);
    id=s.inv.slots[TACTICAL_RIGHT_PISTOL_WELL].id;
    far(&in);in.grip=1;in.snap_inserted_magazine=1;in.magazine_distance_cm=2;
    assert(tacticalSessionInteract(&s,TACTICAL_GUN_USP,&in,&dropped,&ejected,&shown)==TACTICAL_EVENT_MAG_TAKEN);
    assert(s.inv.slots[TACTICAL_LEFT_HAND].id==id && s.inv.slots[TACTICAL_LEFT_HAND].rounds==5 && shown==1);
    in.magazine_distance_cm=50;
    assert(tacticalSessionInteract(&s,TACTICAL_GUN_USP,&in,&dropped,&ejected,&shown)==TACTICAL_EVENT_NONE);
    assert(s.inv.slots[TACTICAL_LEFT_HAND].id==id);
    in.grip=0; in.rig_distance_cm[3]=1; /* full slot means drop, never overwrite */
    assert(tacticalSessionInteract(&s,TACTICAL_GUN_USP,&in,&dropped,&ejected,&shown)==TACTICAL_EVENT_MAG_DROPPED);
    assert(dropped.id==id && dropped.rounds==5);
    far(&in); in.grip=1; in.rig_distance_cm[3]=2;
    assert(tacticalSessionInteract(&s,TACTICAL_GUN_USP,&in,&dropped,&ejected,&shown)==TACTICAL_EVENT_MAG_TAKEN);
    id=s.inv.slots[TACTICAL_LEFT_HAND].id;
    in.grip=0; in.magazine_distance_cm=2; in.rig_distance_cm[3]=100;
    assert(tacticalSessionInteract(&s,TACTICAL_GUN_USP,&in,&dropped,&ejected,&shown)==TACTICAL_EVENT_MAG_SEATED);
    assert(s.inv.slots[TACTICAL_RIGHT_PISTOL_WELL].id==id && shown==8);
    assert(tacticalEjectMagazine(&s.inv,TACTICAL_RIGHT_PISTOL_WELL,&ejectedMag)==TACTICAL_OK);
    assert(ejectedMag.id==id && ejectedMag.rounds==7 && s.inv.chambered[TACTICAL_RIGHT_PISTOL_WELL]);
    assert(tacticalEjectMagazine(&s.inv,TACTICAL_RIGHT_PISTOL_WELL,&ejectedMag)==TACTICAL_EMPTY);
    {
        TacticalSession x; TacticalInteractionInput hand={0}; TacticalMagazine temp={0};
        unsigned int ammo=14, result=0, expelled=0;
        tacticalSessionInit(&x);
        assert(tacticalSessionSetPistolMagazineType(&x,4)==TACTICAL_OK);
        assert(tacticalSessionEquip(&x,TACTICAL_GUN_USP,7,7,&ammo,&result)==TACTICAL_OK);
        far(&hand);hand.grip=1;hand.snap_inserted_magazine=1;hand.grab_radius_cm=20;hand.seat_radius_cm=15;
        hand.magazine_distance_cm=18;hand.action_distance_cm=24;
        assert(tacticalSessionInteract(&x,TACTICAL_GUN_USP,&hand,&temp,&expelled,&result)==TACTICAL_EVENT_MAG_TAKEN);
        id=x.inv.slots[TACTICAL_LEFT_HAND].id;
        /* Make room in a rig slot and release closer to it than the well. */
        x.inv.slots[TACTICAL_RIG_PISTOL_1].id=0;
        hand.grip=0;hand.magazine_distance_cm=12;hand.rig_distance_cm[3]=8;
        assert(tacticalSessionInteract(&x,TACTICAL_GUN_USP,&hand,&temp,&expelled,&result)==TACTICAL_EVENT_MAG_STORED);
        assert(x.inv.slots[TACTICAL_RIG_PISTOL_1].id==id && !x.inv.slots[TACTICAL_RIGHT_PISTOL_WELL].id);
        /* Nearby action beats a farther installed magazine. */
        assert(tacticalMoveMagazine(&x.inv,TACTICAL_RIG_PISTOL_1,TACTICAL_LEFT_HAND)==TACTICAL_OK);
        assert(tacticalMoveMagazine(&x.inv,TACTICAL_LEFT_HAND,TACTICAL_RIGHT_PISTOL_WELL)==TACTICAL_OK);
        far(&hand);hand.grip=1;hand.snap_inserted_magazine=1;hand.grab_radius_cm=20;
        hand.magazine_distance_cm=15;hand.action_distance_cm=5;
        assert(tacticalSessionInteract(&x,TACTICAL_GUN_USP,&hand,&temp,&expelled,&result)==TACTICAL_EVENT_NONE);
        assert(x.interaction[TACTICAL_GUN_USP].state==3 && x.inv.slots[TACTICAL_RIGHT_PISTOL_WELL].id==id);
    }
    {
        TacticalSession x; TacticalInteractionInput hand={0}; TacticalMagazine temp={0};
        unsigned int ammo=60, result=0, expelled=0;
        tacticalSessionInit(&x);
        assert(tacticalSessionEquip(&x,TACTICAL_GUN_AK,30,30,&ammo,&result)==TACTICAL_OK);
        id=x.inv.slots[TACTICAL_PRIMARY_WELL].id;
        far(&hand); hand.grip=1; hand.snap_inserted_magazine=1;
        hand.grab_radius_cm=20; hand.magazine_grab_radius_cm=13;
        /* The previous 20 cm mag zone captured a bolt grab at these actual
         * headset distances, even though the action was within reach. */
        hand.magazine_distance_cm=14; hand.action_distance_cm=15.5f;
        assert(tacticalSessionInteract(&x,TACTICAL_GUN_AK,&hand,&temp,&expelled,&result)==TACTICAL_EVENT_NONE);
        assert(x.interaction[TACTICAL_GUN_AK].state==3 && x.inv.slots[TACTICAL_PRIMARY_WELL].id==id);
        hand.grip=0; tacticalSessionInteract(&x,TACTICAL_GUN_AK,&hand,&temp,&expelled,&result);
        far(&hand); hand.grip=1; hand.magazine_distance_cm=12; hand.action_distance_cm=30;
        assert(tacticalSessionInteract(&x,TACTICAL_GUN_AK,&hand,&temp,&expelled,&result)==TACTICAL_EVENT_MAG_TAKEN);
        assert(x.inv.slots[TACTICAL_LEFT_HAND].id==id);
    }
    for (int gun = 0; gun < TACTICAL_GUN_COUNT; ++gun) {
        TacticalSession peek;
        TacticalInteractionInput hand = {0};
        unsigned int none = 0;
        tacticalSessionInit(&peek);
        assert(tacticalSessionEquip(&peek, gun, 2, 12, &none, &shown) == TACTICAL_OK);
        const int well = peek.interaction[gun].well;
        for (int hasRound = 1; hasRound >= 0; --hasRound) {
            far(&hand);
            hand.grip = 1;
            hand.action_distance_cm = 2;
            hand.action_grab_radius_cm = 14;
            const unsigned int before = tacticalSessionLoaded(&peek, gun);
            assert(tacticalSessionInteract(&peek, gun, &hand, &dropped, &ejected, &shown) == TACTICAL_EVENT_NONE);
            assert(peek.interaction[gun].state == 3);
            hand.action_pull_cm = 2.f;
            assert(tacticalSessionInteract(&peek, gun, &hand, &dropped, &ejected, &shown) == TACTICAL_EVENT_NONE);
            assert(shown == before && peek.inv.chambered[well] == hasRound);
            hand.grip = 0;
            assert(tacticalSessionInteract(&peek, gun, &hand, &dropped, &ejected, &shown) == TACTICAL_EVENT_NONE);
            assert(shown == before && ejected == 0);
            while (shown > 0) assert(tacticalSessionFire(&peek, gun, &shown) == TACTICAL_OK);
        }
    }
    puts("trigger reload: magazine transitions and non-consuming chamber checks on all guns passed");
    return 0;
}
