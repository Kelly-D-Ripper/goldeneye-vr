#ifndef GEVR_TACTICAL_GUARD_AI_H
#define GEVR_TACTICAL_GUARD_AI_H

struct ChrRecord;
struct coord3d;
struct StandTile;

void gevrTacticalGuardAiReset(void);
void gevrTacticalGuardReport(struct ChrRecord *guard, const struct coord3d *point, struct StandTile *floor);
void gevrTacticalGuardNoise(int player, float radius);
int gevrTacticalGuardCorpseEligible(struct ChrRecord *guard);
/* -1 leaves perception to the native guard, 0 is blocked, 1 is visible. */
int gevrTacticalGuardVisiblePlayer(struct ChrRecord *guard, int player);

int gevrTacticalGuardScriptSight(struct ChrRecord *guard);
int gevrTacticalGuardScriptHear(struct ChrRecord *guard, int recent);
int gevrTacticalGuardScriptBody(struct ChrRecord *guard);

#endif
