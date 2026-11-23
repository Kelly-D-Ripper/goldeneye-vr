#ifndef _PROP_H_
#define _PROP_H_
#include <ultra64.h>

extern const u32 only_read_by_stageload[];

void proplvreset2(enum LEVELID stageId);
#ifdef GEVR
int gevrCampaignExtraGuardCapacity(int stage);
struct ChrRecord;
struct PadRecord;
struct patrol_path;
int gevrJunglePatrolSquad(const struct ChrRecord *chr);
int gevrCampaignGuardPost(const struct ChrRecord *chr, int *member);
bool gevrCampaignGuardIsIdleGroupMember(const struct ChrRecord *chr);
bool gevrCampaignGuardIsMarksman(const struct ChrRecord *chr);
struct patrol_path *gevrCampaignGuardRoute(const struct ChrRecord *chr);
struct PadRecord *gevrCampaignPatrolPad(struct patrol_path *path, s32 step);
s32 gevrCampaignSpawnPointClear(const struct PadRecord *pad);
bool gevrCampaignOpeningPointClear(const struct PadRecord *pad);
bool gevrCampaignReinforcementEntryClear(const struct PadRecord *pad, bool protectStart);
void gevrCampaignEncountersStageLoaded(int stage);
#endif

#endif
