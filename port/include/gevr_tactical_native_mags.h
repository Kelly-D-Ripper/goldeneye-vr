#ifndef GEVR_TACTICAL_NATIVE_MAGS_H
#define GEVR_TACTICAL_NATIVE_MAGS_H
#include "gevr_tactical_session.h"

/* The original items keep their own weapon models and stats. Pool the
 * compatible magazine families with their physical Tactical counterparts. */
static inline int gevrTacticalNativeMagazineGun(int item)
{
    switch (item) {
    case ITEM_M16: return TACTICAL_GUN_M4A1;
    case ITEM_UZI: return TACTICAL_GUN_UZI;
    case ITEM_FNP90: return TACTICAL_GUN_P90;
    case ITEM_MP5KSIL: return TACTICAL_GUN_MP5K;
    default: return -1;
    }
}
#endif
