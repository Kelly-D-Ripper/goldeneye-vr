/* Exercise the production progression and Workshop against a staged catalogue. */
#include "gevr_tactical_progress.h"
#include "gevr_tactical_gear.h"
#include "system.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static int online, range;
int g_AppendCheatSinglePlayer;
bool netIsActive(void) { return online; }
int gevrTacticalRangeActive(void) { return range; }
int gevrCheatsBlockProgress(void) { return 1; }
int gevrTacticalCampaignRules(void) { return 1; }
const char *gevrTacticalGearItemName(int item) { (void)item; return NULL; }
s32 sysArgCheck(const char *arg) { (void)arg; return 1; }
void sysLogPrintf(s32 level, const char *fmt, ...) { (void)level; va_list args; va_start(args, fmt); vfprintf(stderr, fmt, args); va_end(args); fputc(10, stderr); }

static struct Carried {
    int key, mag;
    const GevrGearAssembly *assembly;
} slots[3];
static int slotCount;

void gevrTacticalProgressExtractCarried(void)
{
    for (int i = 0; i < slotCount; ++i) {
        static const char *const names[] = {"right hand", "back", "left hand"};
        gevrProgressExtractGun(slots[i].key, names[i], slots[i].assembly, slots[i].mag);
    }
}

static void carry(int first, int second, int third)
{
    const int keys[] = {first, second, third};
    slotCount = 0;
    for (int i = 0; i < 3 && keys[i] >= 0; ++i) {
        const GevrGearAssembly *a = gevrTacticalGearStockAssembly(keys[i]);
        assert(a && a->count);
        slots[slotCount++] = (struct Carried){keys[i], -2, a};
    }
}

static void clear(int stage)
{
    gevrProgressBeginMission();
    gevrProgressMissionComplete(stage, 0);
}

static int partById(const char *id)
{
    for (int i = 0; gevrTacticalGearPart(i); ++i)
        if (!strcmp(gevrTacticalGearPart(i)->id, id)) return i;
    fprintf(stderr, "Missing catalogue ID %s\n", id);
    assert(0);
    return -1;
}

static void kit(int key, const char *const ids[], int count, int allowed)
{
    for (int i = 0; i < count; ++i) assert(gevrProgressPartAllowed(key, partById(ids[i])) == allowed);
}

static int mountIndex(int key, const char *path)
{
    const GevrGearAssembly *a = gevrTacticalGearAssembly(key);
    const GevrGearMount *m = gevrTacticalGearFindMount(a, path);
    assert(m);
    return (int)(m - a->mounts);
}

static int mounted(const GevrGearAssembly *a, const char *path)
{
    const GevrGearMount *m = gevrTacticalGearFindMount(a, path);
    return m ? m->part : -1;
}

static int savedPart(const char *filename, const char *id)
{
    char line[256], target[80];
    snprintf(target, sizeof(target), "part\t%s\n", id);
    FILE *f = fopen(filename, "r");
    assert(f);
    int found = 0;
    while (fgets(line, sizeof(line), f)) if (!strcmp(target, line)) found = 1;
    fclose(f);
    return found;
}

int main(void)
{
    assert(gevrTacticalGearCatalogHash());
    const int m4 = gevrTacticalGearKey(TACTICAL_GUN_M4A1), hk = gevrTacticalGearKey(TACTICAL_GUN_HK416);
    const int pkm = gevrTacticalGearKey(TACTICAL_GUN_PKM), pm = gevrTacticalGearKey(TACTICAL_GUN_MAKAROV);
    const char *boxId = "646372518610c40fc20204e8", *d60Id = "59c1383d86f774290a37e0ca";
    const int box = partById(boxId), d60 = partById(d60Id), mag100 = partById("5c6592372e221600133e47d7");
    static const char *const rmr[] = {"5a32aa8bc4a2826c6e06d737", "5a33b2c9c4a282000c5a9511", "ff593a3c41ae99df438c9dd9"};
    static const char *const acog[] = {
        "5c05295e0db834001a66acbb", "5c05293e0db83400232fff80", "5c052a900db834001a66acbd",
        "3c5f880e56de5c5761aca7e2", "7fa1947522874c5b41cd50f9", "59db7e1086f77448be30ddf3",
        "5407c4fdbcfb5b6b488287d5", "59db7eed86f77461f8380365", "7c755f84914dd7e2103c20cb"
    };
    static const char *const white[] = {
        "67c26286da8783675c5967bf", "980d0feb0d371e0a9d0882d9", "67c26286da8783675c5967cc",
        "67c26286da8783675c5967c2", "67c26286da8783675c5967c3", "67c26286da8783675c5967ce",
        "67c26286da8783675c5967c5", "67c26286da8783675c5967c4", "67c26286da8783675c5967c7",
        "67c26286da8783675c5967c6", "67c26286da8783675c5967cf", "7e69050ef04e3b20b04e4ab8",
        "67c26286da8783675c5967c1", "67c26286da8783675c5967c0", "67c26286da8783675c5967cd",
        "67c26286da8783675c5967bd", "67c26286da8783675c5967ca", "5d133067d7ad1a33013f95b4",
        "67c26286da8783675c5967c8", "67c26286da8783675c5967be", "5fce83beea2144a1e79dd0da",
        "67c26286da8783675c5967d0"
    };
    const char *magPath = "mod_magazine";
    char msg[120];
    int conflict;

    assert(gevrProgressFolder() == -1);
    FILE *f = fopen("tactical-progress-2.txt", "w");
    assert(f);
    fputs("GEVRPROGRESS1\nmode\tall\n", f);
    fclose(f);
    assert(gevrProgressAllMissions(1));
    assert(gevrProgressFolder() == -1);
    gevrProgressSelectFolder(0);
    assert(gevrProgressAsk());
    gevrProgressAnswer(0);
    assert(gevrProgressLocks() && gevrProgressUnlockedGuns() == 1);
    assert(gevrProgressGunUnlocked("Walther PPK") && gevrProgressGunUnlocked("Suppressed PPK"));
    assert(!gevrProgressRequisition(pm));
    assert(!gevrProgressPartAllowed(m4, d60));
    kit(m4, rmr, 3, 0);
    kit(m4, acog, 9, 0);
    kit(m4, white, 22, 0);

    carry(m4, hk, m4);
    gevrProgressBeginMission();
    gevrTacticalProgressExtractCarried(); /* abandon a run before the successful debrief */
    assert(!gevrProgressGunUnlocked("M4A1") && gevrProgressTotalClears() == 0);
    online = 1;
    gevrProgressMissionComplete(0, 0);
    online = 0;
    range = 1;
    gevrProgressMissionComplete(0, 0);
    range = 0;
    g_AppendCheatSinglePlayer = 1;
    gevrProgressMissionComplete(0, 0);
    g_AppendCheatSinglePlayer = 0;
    assert(!gevrProgressTotalClears());

    /* A looted D60 can travel out, but cannot bypass mastery. */
    assert(gevrTacticalGearInstall(m4, mountIndex(m4, magPath), d60, &conflict));
    slots[0].assembly = gevrTacticalGearAssembly(m4);
    slots[0].mag = d60;
    gevrProgressMissionComplete(0, 0);
    gevrProgressMissionComplete(0, 0);
    int guns, parts;
    gevrProgressLastExtraction(&guns, &parts);
    assert(guns == 2 && parts > 0 && gevrProgressTotalClears() == 1 && gevrProgressReserve() == 1);
    assert(gevrProgressGunUnlocked("M4A1") && gevrProgressGunUnlocked("HK416A5"));
    assert(!gevrProgressGunClears(m4) && !gevrProgressGunClears(hk));
    assert(!savedPart("tactical-progress-1.txt", d60Id));
    kit(m4, rmr, 3, 0);
    assert(!gevrProgressRequisition(pm));
    gevrProgressBeginMission();
    assert(!gevrProgressAllMissions(2)); /* first lookup must not lose owned-at-launch */
    gevrProgressMissionComplete(0, 0);
    assert(gevrProgressGunClears(m4) == 1 && gevrProgressGunClears(hk) == 1);
    assert(gevrProgressRequisition(pm) && !gevrProgressRequisition(pm) && !gevrProgressReserve());
    kit(m4, rmr, 3, 1);
    kit(m4, acog, 9, 0);
    kit(m4, white, 22, 0);
    clear(0);
    kit(m4, acog, 9, 1);
    kit(m4, white, 22, 0);
    clear(0);
    kit(m4, white, 22, 1);
    assert(gevrProgressReserve() == 1 && !gevrProgressRequisition(pkm));

    /* Reopening an imported recipe must not overwrite the saved build. */
    const char *scopePath = "mod_reciever/mod_scope";
    gevrTacticalGearReset(m4);
    assert(gevrTacticalGearLoadNamedPreset(m4, "Whiteout"));
    const int scope = mounted(gevrTacticalGearAssembly(m4), scopePath);
    assert(scope >= 0 && gevrProgressPartAllowed(m4, scope));
    assert(gevrTacticalGearInstall(m4, mountIndex(m4, magPath), d60, &conflict));
    assert(gevrTacticalGearSavePreset(m4, "Mastery test") == GEVR_PRESET_SAVED);
    assert(gevrTacticalGearSanitize(m4, gevrProgressMountAllowed));
    assert(mounted(gevrTacticalGearAssembly(m4), scopePath) == scope);
    const int factory = mounted(gevrTacticalGearStockAssembly(m4), magPath);
    assert(factory >= 0 && mounted(gevrTacticalGearAssembly(m4), magPath) == factory);
    assert(!gevrTacticalGearSanitize(m4, gevrProgressMountAllowed));
    assert(gevrTacticalGearLoadNamedPreset(m4, "Mastery test"));
    assert(mounted(gevrTacticalGearAssembly(m4), magPath) == d60);
    gevrTacticalGearCommitIssued(0, 10, 0, gevrTacticalGearNamedBuild(m4, "Mastery test"), -1);
    assert(gevrTacticalGearDeployedBuild(0) >= 0);
    gevrTacticalGearSanitizeDeployed(gevrProgressMasteryAllowed);
    assert(gevrTacticalGearDeployedBuild(0) == -1);

    carry(m4, -1, -1);
    clear(1);
    assert(!gevrProgressPartAllowed(m4, d60));
    clear(2);
    assert(gevrProgressPartAllowed(m4, d60) && !gevrProgressPartAllowed(hk, d60));
    assert(!gevrProgressPartAllowed(m4, mag100));
    assert(gevrProgressRequisition(pkm));
    assert(!gevrProgressGunClears(pkm));
    assert(mounted(gevrTacticalGearStockAssembly(pkm), magPath) == box);
    assert(gevrTacticalGearPart(box)->capacity == 100 && gevrProgressPartAllowed(pkm, box));
    assert(!gevrProgressPartAllowed(pkm, d60) && !gevrProgressPartAllowed(pkm, factory));
    gevrProgressPartRequirement(pkm, box, msg, sizeof(msg));
    assert(strstr(msg, "factory 100-round"));
    const GevrGearMount *pkmMag = gevrTacticalGearFindMount(gevrTacticalGearStockAssembly(pkm), magPath);
    assert(pkmMag->socket->count == 1 && pkmMag->socket->allowed[0] == box);
    assert(!gevrTacticalGearSanitize(pkm, gevrProgressMountAllowed));
    carry(m4, pkm, -1);
    for (int i = 0; i < 3; ++i) clear(2);
    assert(gevrProgressGunClears(m4) == 8 && gevrProgressGunClears(hk) == 3 && gevrProgressGunClears(pkm) == 3);
    assert(gevrProgressPartAllowed(m4, mag100) && !gevrProgressPartAllowed(hk, mag100));
    assert(!savedPart("tactical-progress-1.txt", boxId) && !savedPart("tactical-progress-1.txt", d60Id));

    const unsigned reserve = gevrProgressReserve();
    gevrProgressSelectFolder(1);
    assert(!gevrProgressLocks() && gevrProgressPartAllowed(hk, d60));
    gevrProgressSelectFolder(0);
    assert(gevrProgressLocks() && gevrProgressReserve() == reserve && gevrProgressGunClears(m4) == 8);
    kit(hk, rmr, 3, 1);
    kit(m4, white, 22, 1);
    assert(gevrProgressPartAllowed(m4, mag100) && !gevrProgressPartAllowed(hk, d60));

    /* One fitted pickup earns the entire kit before its normal milestone. */
    gevrProgressSelectFolder(2);
    gevrProgressAnswer(0);
    gevrTacticalGearReset(m4);
    assert(gevrTacticalGearInstall(m4, mountIndex(m4, scopePath), partById(rmr[2]), &conflict));
    assert(gevrTacticalGearInstall(m4, mountIndex(m4, "mod_reciever/mod_scope/mod_scope"), partById(rmr[0]), &conflict));
    kit(m4, rmr, 3, 0);
    carry(m4, -1, -1);
    slots[0].assembly = gevrTacticalGearAssembly(m4);
    clear(0);
    kit(hk, rmr, 3, 1);
    assert(!gevrProgressGunClears(m4));
    kit(m4, acog, 9, 0);

    f = fopen("tactical-progress-4.txt", "w");
    assert(f);
    fprintf(f, "GEVRPROGRESS1\nmode\tearn\ngun\tSuppressed PPK\ngun\tM4A1\npart\t%s\nmissions\t6 0 0 0\n", d60Id);
    fclose(f);
    gevrProgressSelectFolder(3);
    assert(gevrProgressUnlockedGuns() == 2 && gevrProgressGunUnlocked("Walther PPK"));
    assert(gevrProgressReserve() == 2 && !gevrProgressPartAllowed(m4, d60));

    int high = 0;
    for (int i = 0; gevrTacticalGearPart(i); ++i) {
        if (gevrTacticalGearPart(i)->capacity < 60) continue;
        ++high;
        assert(!gevrProgressPartAllowed(m4, i));
        assert(gevrProgressPartAllowed(pkm, i) == (i == box));
    }
    assert(high == 15);
    puts("Passed: fitted pickup and milestone kits, reported slot rewards, Reserve, mastery, presets, save migration and PKM");
}
