#ifndef GEVR_TACTICAL_PROGRESS_H
#define GEVR_TACTICAL_PROGRESS_H
/*
 * Tactical campaign progression (user, 2026-11-23). Each save folder has its
 * own file, tactical-progress-<folder>.txt. The first time a folder reaches
 * the main menu, the player is asked once whether to unlock every mission and
 * item for that save. A progression save starts with only the PPK and earns
 * guns and parts. Stats are kept for every save.
 */
enum {
    GEVR_PROGRESS_UNSET,    /* not yet asked */
    GEVR_PROGRESS_EARN,     /* progression: unlock by playing */
    GEVR_PROGRESS_ALL       /* everything unlocked, missions too */
};
enum { GEVR_PROGRESS_NAME = 32, GEVR_PROGRESS_DIFFICULTIES = 4 };

typedef struct GevrProgressGun {
    char name[GEVR_PROGRESS_NAME];
    unsigned kills, headshots, shots, hits, clears;
} GevrProgressGun;

/* The folder the menus and the next mission use; loads its file once. */
void gevrProgressSelectFolder(int folder);
int gevrProgressFolder(void);
/* The current folder's mode. Outside the Tactical campaign every lock is off. */
int gevrProgressMode(void);
int gevrProgressLocks(void);
/* TRUE once, until the player answers the unlock question for this folder. */
int gevrProgressAsk(void);
void gevrProgressAnswer(int unlockAll);
/* file2.c: an "unlock all" save has every mission open (nothing is written to the save). */
int gevrProgressAllMissions(int folder);

/* Unlocks, by the gun's Workshop name and the part's catalogue id. */
int gevrProgressGunUnlocked(const char *name);
int gevrProgressUnlockedGuns(void);
unsigned gevrProgressGunClears(int build);
unsigned gevrProgressReserve(void);
unsigned gevrProgressReserveThreshold(int build);
unsigned gevrProgressTotalClears(void);
int gevrProgressRequisition(int build);

struct GevrGearAssembly;
struct GevrGearMount;
int gevrProgressPartAllowed(int build, int part);
int gevrProgressMountAllowed(int build, const struct GevrGearMount *mount);
int gevrProgressMasteryAllowed(int build, const struct GevrGearMount *mount);
void gevrProgressPartRequirement(int build, int part, char *text, unsigned size);
void gevrProgressBeginMission(void);
void gevrProgressExtractGun(int build, const char *slot, const struct GevrGearAssembly *assembly, int magazine);

/* Stats: the local player's own bullets in the Tactical campaign. */
void gevrProgressShot(int item);
void gevrProgressHit(int item, int killed, int headshot);
void gevrProgressMissionComplete(int mission, int difficulty);
void gevrProgressSave(void);

/* Unlock the carried kit during a successful solo debrief, before unloading the level. */
void gevrTacticalProgressExtractCarried(void);
void gevrProgressClearExtraction(void);
void gevrProgressLastExtraction(int *guns, int *parts);

/* Read back for the Progression screen. */
int gevrProgressGunCount(void);
const GevrProgressGun *gevrProgressGun(int index);
void gevrProgressTotals(unsigned *kills, unsigned *headshots, unsigned *shots, unsigned *hits);
unsigned gevrProgressMissions(int difficulty);

/* gevr_tactical_gear_menu.inc.h: a gun item's Workshop name, or NULL. */
const char *gevrTacticalGearItemName(int item);
#endif
