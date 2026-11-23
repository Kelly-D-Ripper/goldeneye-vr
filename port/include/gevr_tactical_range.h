#ifndef GEVR_TACTICAL_RANGE_H
#define GEVR_TACTICAL_RANGE_H
#include <ultra64.h>

struct PadRecord;
struct coord3d;
struct StandTile;
struct WeaponObjRecord;
enum { GEVR_RANGE_TARGETS = 4 };

void gevrTacticalRangeRequest(void);
void gevrTacticalRangeStageLoad(int stage);
int gevrTacticalRangeActive(void);
int gevrTacticalRangeReturning(void);
void gevrTacticalRangeFinishReturn(void);
void gevrTacticalRangeLeave(void);
int gevrTacticalRangeMoving(void);
void gevrTacticalRangeToggleMoving(void);
struct PadRecord *gevrTacticalRangeStartPad(void);
void gevrTacticalRangeTick(void);
Gfx *gevrTacticalRangeRender(Gfx *gdl);
void gevrTacticalRangeStockTables(void);
int gevrTacticalRangePlaceGun(struct WeaponObjRecord *record, int item,
    const struct coord3d *pos, struct StandTile *tile, float angle);
int gevrTacticalRangePlaceMagazine(int item, const struct coord3d *surface);
float gevrTacticalRangeSupport(const struct coord3d *at, float ceiling, float floor);

#endif
