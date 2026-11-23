#ifndef GEVR_TACTICAL_FACILITY_H
#define GEVR_TACTICAL_FACILITY_H

#include "bondconstants.h"
#include "boss.h"
#include "player.h"
#include "system.h"

extern bool netIsActive(void);

/* Guard 46 starts between 33 and 34, down the hall beyond pad 94.
 * These are the three guards behind the running scientist. */
enum { GEVR_FACILITY_CORRIDOR_CENTER = 46 };

static inline bool gevrTacticalFacilityArmor(void)
{
    return sysArgCheck("--tactical-facility-armor") && bossGetStageNum() == LEVELID_FACILITY
        && !netIsActive() && getPlayerCount() == 1;
}

static inline bool gevrTacticalFacilityVest(s32 chrnum)
{
    switch (chrnum) {
    case 5: case 6: case 7:
    case 10: case 17: case 22: case 23:
    case 33: case 34: case GEVR_FACILITY_CORRIDOR_CENTER:
        return TRUE;
    default:
        return FALSE;
    }
}

#endif
