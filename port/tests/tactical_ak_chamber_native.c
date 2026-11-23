#include "gevr_tactical_session.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    TacticalSession s;
    unsigned reserve = 60, shown, ejected;
    tacticalSessionInit(&s);
    assert(tacticalSessionEquip(&s, TACTICAL_GUN_AK, 2, 30, &reserve, &shown) == TACTICAL_OK);
    assert(tacticalSessionFire(&s, TACTICAL_GUN_AK, &shown) == TACTICAL_OK && shown == 1);
    assert(tacticalMoveMagazine(&s.inv, TACTICAL_PRIMARY_WELL, TACTICAL_LEFT_HAND) == TACTICAL_OK);
    assert(tacticalMoveMagazine(&s.inv, TACTICAL_LEFT_HAND, TACTICAL_RIG_PRIMARY_3) == TACTICAL_OK);
    assert(tacticalMoveMagazine(&s.inv, TACTICAL_RIG_PRIMARY_1, TACTICAL_LEFT_HAND) == TACTICAL_OK);
    assert(tacticalMoveMagazine(&s.inv, TACTICAL_LEFT_HAND, TACTICAL_PRIMARY_WELL) == TACTICAL_OK);
    /* A tactical reload retains the chambered cartridge. */
    assert(tacticalSessionFire(&s, TACTICAL_GUN_AK, &shown) == TACTICAL_OK && shown == 30);
    for (int n = 0; n < 30; ++n) assert(tacticalSessionFire(&s, TACTICAL_GUN_AK, &shown) == TACTICAL_OK);
    assert(shown == 0);
    assert(tacticalMoveMagazine(&s.inv, TACTICAL_PRIMARY_WELL, TACTICAL_LEFT_HAND) == TACTICAL_OK);
    assert(tacticalMoveMagazine(&s.inv, TACTICAL_LEFT_HAND, TACTICAL_RIG_PRIMARY_1) == TACTICAL_OK);
    assert(tacticalMoveMagazine(&s.inv, TACTICAL_RIG_PRIMARY_2, TACTICAL_LEFT_HAND) == TACTICAL_OK);
    assert(tacticalMoveMagazine(&s.inv, TACTICAL_LEFT_HAND, TACTICAL_PRIMARY_WELL) == TACTICAL_OK);
    /* A new magazine does not chamber itself after the gun runs dry. */
    assert(tacticalSessionFire(&s, TACTICAL_GUN_AK, &shown) == TACTICAL_EMPTY_CHAMBER);
    assert(tacticalRack(&s.inv, TACTICAL_PRIMARY_WELL, &ejected) == TACTICAL_OK && ejected == 0);
    assert(tacticalSessionFire(&s, TACTICAL_GUN_AK, &shown) == TACTICAL_OK && shown == 29);
    puts("AK: chambered reload fires, empty reload requires rack");
    return 0;
}
