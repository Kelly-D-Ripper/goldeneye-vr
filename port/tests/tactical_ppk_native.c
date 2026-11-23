#include "gevr_tactical_session.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    TacticalSession s;
    TacticalMagazine old, wrong = {999, 2, 12, 12, TACTICAL_MAG_PISTOL};
    unsigned int reserve = 14, shown = 0, ejected = 0;
    tacticalSessionInit(&s);
    assert(tacticalSessionSetPistolMagazineType(&s, 4) == TACTICAL_OK);
    assert(tacticalSessionEquip(&s, TACTICAL_GUN_USP, 7, 7, &reserve, &shown) == TACTICAL_OK);
    assert(shown == 7 && reserve == 0 && s.inv.slots[TACTICAL_RIG_PISTOL_1].type == 4);
    assert(tacticalSessionSetPistolMagazineType(&s, 2) == TACTICAL_INVALID);
    assert(tacticalSessionFire(&s, TACTICAL_GUN_USP, &shown) == TACTICAL_OK && shown == 6);
    assert(tacticalMoveMagazine(&s.inv, TACTICAL_RIGHT_PISTOL_WELL, TACTICAL_LEFT_HAND) == TACTICAL_OK);
    assert(s.inv.slots[TACTICAL_LEFT_HAND].rounds == 5 && s.inv.chambered[TACTICAL_RIGHT_PISTOL_WELL]);
    assert(tacticalDropMagazine(&s.inv, TACTICAL_LEFT_HAND, &old) == TACTICAL_OK && old.rounds == 5);
    assert(tacticalAddMagazine(&s.inv, TACTICAL_LEFT_HAND, wrong) == TACTICAL_OK);
    assert(tacticalMoveMagazine(&s.inv, TACTICAL_LEFT_HAND, TACTICAL_RIGHT_PISTOL_WELL) == TACTICAL_INCOMPATIBLE);
    assert(tacticalDropMagazine(&s.inv, TACTICAL_LEFT_HAND, &wrong) == TACTICAL_OK);
    assert(tacticalMoveMagazine(&s.inv, TACTICAL_RIG_PISTOL_1, TACTICAL_LEFT_HAND) == TACTICAL_OK);
    assert(tacticalMoveMagazine(&s.inv, TACTICAL_LEFT_HAND, TACTICAL_RIGHT_PISTOL_WELL) == TACTICAL_OK);
    assert(tacticalSessionLoaded(&s, TACTICAL_GUN_USP) == 8); /* seven plus retained chamber */
    assert(tacticalRack(&s.inv, TACTICAL_RIGHT_PISTOL_WELL, &ejected) == TACTICAL_OK && ejected == 1);
    assert(tacticalSessionLoaded(&s, TACTICAL_GUN_USP) == 7);
    assert(tacticalCountRounds(&s.inv) + old.rounds + ejected == 20); /* one shot consumed */
    puts("PPK: 7-round magazines, chamber, partial reload and USP incompatibility passed");
    return 0;
}
